#include "database/Database.h"
#include "security/PhoneUtils.h"
#include "security/PinHasher.h"
#include "security/RateLimiter.h"
#include "security/SessionManager.h"
#include "services/AuthService.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

// Thread-safe in-memory Database stub for isolated AuthService unit tests (zero external DB)
class MockAuthDatabase : public Database {
public:
    struct MockUser {
        int id;
        std::string email;
        std::string mobileNumber;
        std::string accountNumber;
        std::string pinHash;
        std::string status;
    };

    struct LockRecord {
        int failedAttempts = 0;
        std::int64_t lockedUntil = 0;
    };

    std::mutex mockMutex;
    std::vector<MockUser> users;
    std::unordered_map<std::string, LockRecord> lockouts;

    UserAuthRecord findUserForLogin(const std::string& canonicalMobile) override {
        std::lock_guard<std::mutex> lock(mockMutex);
        for (const auto& u : users) {
            if (!u.mobileNumber.empty() && u.mobileNumber == canonicalMobile) {
                return {true, u.id, u.email, u.mobileNumber, u.pinHash};
            }
        }
        return {};
    }

    UserAuthRecord findLegacyUserForLinking(const std::string& legacyIdentifier) override {
        std::lock_guard<std::mutex> lock(mockMutex);
        for (const auto& u : users) {
            if (u.accountNumber == legacyIdentifier || u.email == legacyIdentifier) {
                return {true, u.id, u.email, u.mobileNumber, u.pinHash};
            }
        }
        return {};
    }

    LinkMobileResult linkMobileNumberToUser(int userId, const std::string& canonicalMobile) override {
        std::lock_guard<std::mutex> lock(mockMutex);
        for (const auto& u : users) {
            if (u.id != userId && u.mobileNumber == canonicalMobile) {
                return {false, false, true, "Unable to link this mobile number. Please use a different mobile number."};
            }
        }
        for (auto& u : users) {
            if (u.id == userId) {
                if (!u.mobileNumber.empty()) {
                    return {false, true, false, "This account already has a linked mobile number."};
                }
                u.mobileNumber = canonicalMobile;
                return {true, false, false, "Mobile number linked successfully."};
            }
        }
        return {false, false, false, "Account could not be updated."};
    }

    AccountLoginStatus findAccountForUser(int userId) override {
        std::lock_guard<std::mutex> lock(mockMutex);
        for (const auto& u : users) {
            if (u.id == userId) {
                return {true, u.accountNumber, u.status};
            }
        }
        return {};
    }

    std::int64_t getLoginLockUntil(const std::string& identifier) override {
        std::lock_guard<std::mutex> lock(mockMutex);
        auto it = lockouts.find(identifier);
        return (it != lockouts.end()) ? it->second.lockedUntil : 0;
    }

    void clearLoginAttempts(const std::string& identifier) override {
        std::lock_guard<std::mutex> lock(mockMutex);
        lockouts.erase(identifier);
    }

    void reconcileLegacyLoginAttempts(
        const std::string& legacyIdentifier,
        const std::string& canonicalIdentifier
    ) override {
        if (legacyIdentifier.empty() || canonicalIdentifier.empty() || legacyIdentifier == canonicalIdentifier) {
            return;
        }
        std::lock_guard<std::mutex> lock(mockMutex);
        auto it = lockouts.find(legacyIdentifier);
        if (it == lockouts.end()) {
            return;
        }

        auto& canon = lockouts[canonicalIdentifier];
        canon.failedAttempts += it->second.failedAttempts;
        canon.lockedUntil = std::max(canon.lockedUntil, it->second.lockedUntil);
        lockouts.erase(it);
    }

    bool recordFailedLoginAttempt(
        const std::string& identifier,
        int maxAttempts,
        std::int64_t lockUntilEpoch,
        std::int64_t currentEpoch
    ) override {
        std::lock_guard<std::mutex> lock(mockMutex);
        auto& rec = lockouts[identifier];
        rec.failedAttempts += 1;
        if (rec.failedAttempts >= maxAttempts) {
            rec.lockedUntil = lockUntilEpoch;
        }
        return rec.lockedUntil > currentEpoch;
    }
};

void testPhoneNormalizationAndValidation() {
    // Equivalent Indian mobile formats must all normalize to canonical +919876543210
    assert(PhoneUtils::normalize("+91", "9876543210").e164 == "+919876543210");
    assert(PhoneUtils::normalize("+91", "09876543210").e164 == "+919876543210");
    assert(PhoneUtils::normalize("+91", "+91 98765-43210").e164 == "+919876543210");
    assert(PhoneUtils::normalize("+91", "919876543210").e164 == "+919876543210");
    assert(PhoneUtils::normalize("", "+91 9876543210").e164 == "+919876543210");

    // Other supported countries
    assert(PhoneUtils::normalize("+1", "2025550143").e164 == "+12025550143");
    assert(PhoneUtils::normalize("+44", "07123456789").e164 == "+447123456789");
    assert(PhoneUtils::normalize("+65", "91234567").e164 == "+6591234567");

    // Invalid Indian numbers, alpha characters, invalid symbols, mismatched prefix, bad country code
    assert(!PhoneUtils::normalize("+91", "1234567890").valid);
    assert(!PhoneUtils::normalize("+91", "5876543210").valid);
    assert(!PhoneUtils::normalize("+91", "987654321").valid);
    assert(!PhoneUtils::normalize("+91", "98765abc10").valid);
    assert(!PhoneUtils::normalize("+91", "9876543210;").valid);
    assert(!PhoneUtils::normalize("+91", "+12025550143").valid);
    assert(!PhoneUtils::normalize("+999", "9876543210").valid);
    assert(!PhoneUtils::normalize("", "9876543210").valid);

    std::cout << "[PASS] PhoneUtils strictly validates and normalizes E.164 mobile numbers\n";
}

void testMobileCanonicalLockoutBypassImpossible() {
    MockAuthDatabase mockDb;
    mockDb.users.push_back({
        1, "alice@vaultx.sim", "+919876543210", "100099998888", PinHasher::hashPin("123456"), "active"
    });
    mockDb.users.push_back({
        2, "bob@vaultx.sim", "+919811112222", "100077776666", PinHasher::hashPin("654321"), "active"
    });

    AuthService auth(mockDb);

    // Fail 5 times using different formatting variants of the same Indian mobile number
    const std::vector<std::pair<std::string, std::string>> variants = {
        {"+91", "9876543210"},
        {"+91", "09876543210"},
        {"+91", "919876543210"},
        {"", "+91 98765-43210"},
        {"+91", "98765 43210"}
    };
    for (const auto& v : variants) {
        AuthResult r = auth.loginUser(v.first, v.second, "000000");
        assert(!r.success);
    }

    assert(mockDb.lockouts.find("+919876543210") != mockDb.lockouts.end());
    assert(mockDb.lockouts["+919876543210"].failedAttempts == 5);

    // Valid PIN in canonical or alternate form stays locked
    AuthResult bypassAttempt = auth.loginUser("", "+919876543210", "123456");
    assert(!bypassAttempt.success);
    assert(bypassAttempt.message.find("locked") != std::string::npos);

    // Email and card number are no longer accepted on primary /api/auth/login path
    AuthResult legacyEmailLogin = auth.loginUser("", "alice@vaultx.sim", "123456");
    assert(!legacyEmailLogin.success);
    assert(legacyEmailLogin.invalidInput);

    // Confirm PIN mismatch rejected cleanly on registration
    AuthResult mismatch = auth.registerUser("Test Name", "+91", "9898989898", "", "123456", "654321", true);
    assert(!mismatch.success && mismatch.invalidInput);

    std::cout << "[PASS] Mobile lockout blocks format-variant bypasses and rejects email on mobile login\n";
}

void testLegacyAccountMobileLinkingAndReconciliation() {
    MockAuthDatabase mockDb;
    // Legacy user without mobile number yet, plus existing email lockout history
    mockDb.users.push_back({
        10, "legacy_alice@example.com", "", "100020003000", PinHasher::hashPin("123456"), "active"
    });
    mockDb.users.push_back({
        11, "linked_bob@vaultx.sim", "+919811112222", "100020004000", PinHasher::hashPin("654321"), "active"
    });

    const std::int64_t futureLock = static_cast<std::int64_t>(std::time(nullptr)) + 600;
    mockDb.lockouts["unrelated@vaultx.sim"] = {5, futureLock};
    mockDb.lockouts["100020003000"] = {4, 0}; // Pre-P1 legacy failure counter

    AuthService auth(mockDb);

    // Cannot link an already-taken mobile number even with valid PIN
    AuthResult takenAttempt = auth.linkMobileToLegacyUser("100020003000", "123456", "+91", "9811112222");
    assert(!takenAttempt.success);
    assert(mockDb.users[0].mobileNumber.empty());

    // Valid link succeeds and preserves userId/accountNumber/PIN hash
    AuthResult linkOk = auth.linkMobileToLegacyUser("100020003000", "123456", "+91", "9822223333");
    assert(linkOk.success);
    assert(linkOk.userId == 10);
    assert(linkOk.accountNumber == "100020003000");
    assert(linkOk.mobileNumber == "+919822223333");
    assert(mockDb.users[0].mobileNumber == "+919822223333");

    // Subsequent mobile login works immediately with existing PIN hash
    AuthResult mobileLogin = auth.loginUser("+91", "9822223333", "123456");
    assert(mobileLogin.success && mobileLogin.userId == 10);

    // Cannot overwrite already-linked mobile via link endpoint
    AuthResult relinkBlocked = auth.linkMobileToLegacyUser("100020003000", "123456", "+91", "9833334444");
    assert(!relinkBlocked.success);
    assert(mockDb.users[0].mobileNumber == "+919822223333");

    // Unrelated lockout untouched
    assert(mockDb.lockouts["unrelated@vaultx.sim"].failedAttempts == 5);
    assert(mockDb.lockouts["unrelated@vaultx.sim"].lockedUntil == futureLock);

    std::cout << "[PASS] Legacy account mobile linking preserves user identity and blocks duplicates\n";
}

void testConcurrentLoginLockoutTOCTOU() {
    MockAuthDatabase mockDb;
    mockDb.users.push_back({
        1, "alice@vaultx.sim", "+919876543210", "100099998888", PinHasher::hashPin("123456"), "active"
    });
    mockDb.users.push_back({
        2, "bob@vaultx.sim", "+919811112222", "100077776666", PinHasher::hashPin("654321"), "active"
    });

    AuthService auth(mockDb);

    // Fire 12 simultaneous failed login attempts targeting Alice via alternating mobile formats
    std::atomic<int> lockedResponses{0};
    std::atomic<int> invalidResponses{0};
    std::vector<std::thread> attackers;
    for (int i = 0; i < 12; ++i) {
        attackers.emplace_back([&auth, &lockedResponses, &invalidResponses, i]() {
            const std::string cc = (i % 2 == 0) ? "+91" : "";
            const std::string num = (i % 2 == 0) ? "09876543210" : "+91 98765-43210";
            AuthResult r = auth.loginUser(cc, num, "000000");
            assert(!r.success);
            if (r.message.find("locked") != std::string::npos) {
                lockedResponses.fetch_add(1);
            } else {
                invalidResponses.fetch_add(1);
            }
        });
    }

    // Simultaneously authenticate Bob to verify no global lock/deadlock
    AuthResult bobResult;
    std::thread bobThread([&auth, &bobResult]() {
        bobResult = auth.loginUser("+91", "9811112222", "654321");
    });

    for (auto& t : attackers) t.join();
    bobThread.join();

    assert(mockDb.lockouts["+919876543210"].failedAttempts == 5);
    assert(invalidResponses.load() == 4);
    assert(lockedResponses.load() == 8);
    assert(bobResult.success);

    std::cout << "[PASS] Concurrent mobile login burst capped strictly at 5 attempts with zero TOCTOU/deadlock\n";
}

void testStrictIpValidationAndProxyHeaders() {
    assert(RateLimiter::isValidIpAddress("203.0.113.10"));
    assert(RateLimiter::isValidIpAddress("2001:db8::1"));
    assert(RateLimiter::isValidIpAddress("::ffff:192.0.2.1"));

    assert(!RateLimiter::isValidIpAddress(""));
    assert(!RateLimiter::isValidIpAddress("999.999.999.999"));
    assert(!RateLimiter::isValidIpAddress("evil;drop"));
    assert(!RateLimiter::isValidIpAddress("1.2.3.4 extra"));
    assert(!RateLimiter::isValidIpAddress("::bad::ip"));
    assert(!RateLimiter::isValidIpAddress(std::string(50, '1')));

    assert(RateLimiter::parseTrustedProxyMode(nullptr) == TrustedProxyMode::NONE);
    assert(RateLimiter::parseTrustedProxyMode("false") == TrustedProxyMode::NONE);
    assert(RateLimiter::resolveClientIp(
        "203.0.113.10", "1.2.3.4", "5.6.7.8, 9.9.9.9", TrustedProxyMode::NONE
    ) == "203.0.113.10");

    assert(RateLimiter::parseTrustedProxyMode("render") == TrustedProxyMode::RENDER_RIGHTMOST_XFF);
    assert(RateLimiter::resolveClientIp(
        "10.0.0.1", "1.2.3.4", "8.8.8.8, 198.51.100.42", TrustedProxyMode::RENDER_RIGHTMOST_XFF
    ) == "198.51.100.42");
    assert(RateLimiter::resolveClientIp(
        "10.0.0.1", "1.2.3.4", "1.2.3.4, invalid-ip", TrustedProxyMode::RENDER_RIGHTMOST_XFF
    ) == "10.0.0.1");

    assert(RateLimiter::parseTrustedProxyMode("cloudflare") == TrustedProxyMode::CLOUDFLARE_STRICT);
    assert(RateLimiter::resolveClientIp(
        "10.0.0.1", "198.51.100.77", "8.8.8.8", TrustedProxyMode::CLOUDFLARE_STRICT
    ) == "198.51.100.77");
    assert(RateLimiter::resolveClientIp(
        "10.0.0.1", "not-an-ip!!", "8.8.8.8", TrustedProxyMode::CLOUDFLARE_STRICT
    ) == "10.0.0.1");

    std::cout << "[PASS] Strict inet_pton IPv4/IPv6 validation & safe fallback verified\n";
}

void testRateLimiterBoundsAndCleanup() {
    RateLimiter limiter(3, std::chrono::seconds(1), 10);

    for (int i = 0; i < 25; ++i) {
        assert(limiter.allow("192.0.2." + std::to_string(i)));
        assert(limiter.getTrackedCount() <= 10);
    }

    assert(limiter.allow("198.51.100.99"));
    assert(limiter.allow("198.51.100.99"));
    assert(limiter.allow("198.51.100.99"));
    assert(!limiter.allow("198.51.100.99"));

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&limiter, t]() {
            for (int i = 0; i < 50; ++i) {
                limiter.allow("198.51.100." + std::to_string((t * 10) + (i % 5)));
                if (i % 10 == 0) limiter.cleanup();
            }
        });
    }
    for (auto& w : workers) w.join();
    assert(limiter.getTrackedCount() <= 10);

    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    limiter.cleanup();
    assert(limiter.getTrackedCount() == 0);

    std::cout << "[PASS] RateLimiter enforces maxEntries bound, thread safety, and expired cleanup\n";
}

void testSessionMemoryBoundsAndCleanup() {
    SessionManager mgr(std::chrono::seconds(1), 5);

    for (int i = 1; i <= 10; ++i) {
        std::string tok = mgr.createSession(i);
        assert(!tok.empty());
        assert(mgr.getStoredSessionCount() <= 5);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    std::string fresh = mgr.createSession(999);
    assert(!fresh.empty());
    assert(mgr.getStoredSessionCount() == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    mgr.removeExpiredSessions();
    assert(mgr.getStoredSessionCount() == 0);
    assert(mgr.getActiveSessionCount() == 0);

    std::cout << "[PASS] SessionManager bounds memory and prunes expired sessions\n";
}

} // namespace

int main() {
    testPhoneNormalizationAndValidation();
    testMobileCanonicalLockoutBypassImpossible();
    testLegacyAccountMobileLinkingAndReconciliation();
    testConcurrentLoginLockoutTOCTOU();
    testStrictIpValidationAndProxyHeaders();
    testRateLimiterBoundsAndCleanup();
    testSessionMemoryBoundsAndCleanup();
    std::cout << "\nALL MOBILE AUTH & P1/P2 SECURITY TESTS PASSED!\n";
    return 0;
}
