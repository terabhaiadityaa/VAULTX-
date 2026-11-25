#include "database/Database.h"
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

    UserAuthRecord findUserForLogin(const std::string& identifier) override {
        std::lock_guard<std::mutex> lock(mockMutex);
        for (const auto& u : users) {
            if (u.accountNumber == identifier || u.email == identifier) {
                return {true, u.id, u.email, u.pinHash};
            }
        }
        return {};
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
        const std::string& canonicalEmail
    ) override {
        if (legacyIdentifier.empty() || canonicalEmail.empty() || legacyIdentifier == canonicalEmail) {
            return;
        }
        std::lock_guard<std::mutex> lock(mockMutex);
        auto it = lockouts.find(legacyIdentifier);
        if (it == lockouts.end()) {
            return;
        }

        auto& canon = lockouts[canonicalEmail];
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

void testCanonicalLockoutBypassImpossible() {
    MockAuthDatabase mockDb;
    mockDb.users.push_back({
        1, "alice@vaultx.sim", "100099998888", PinHasher::hashPin("123456"), "active"
    });
    mockDb.users.push_back({
        2, "bob@vaultx.sim", "100077776666", PinHasher::hashPin("654321"), "active"
    });

    AuthService auth(mockDb);

    // 1. Fail 5 times using Alice's account number
    for (int i = 0; i < 5; ++i) {
        AuthResult r = auth.loginUser("100099998888", "000000");
        assert(!r.success);
    }

    // Lockout must be recorded under canonical email, NOT account number
    assert(mockDb.lockouts.find("alice@vaultx.sim") != mockDb.lockouts.end());
    assert(mockDb.lockouts.find("100099998888") == mockDb.lockouts.end());

    // 2. Try logging in via Alice's email WITH the correct PIN -> must stay locked
    AuthResult bypassAttempt1 = auth.loginUser("alice@vaultx.sim", "123456");
    assert(!bypassAttempt1.success);
    assert(bypassAttempt1.message.find("locked") != std::string::npos);

    // 3. Reverse direction: fail 5 times using Bob's email, then try Bob's card number
    for (int i = 0; i < 5; ++i) {
        AuthResult r = auth.loginUser("bob@vaultx.sim", "000000");
        assert(!r.success);
    }
    AuthResult bypassAttempt2 = auth.loginUser("100077776666", "654321");
    assert(!bypassAttempt2.success);
    assert(bypassAttempt2.message.find("locked") != std::string::npos);

    std::cout << "[PASS] Canonical lockout blocks email/account-number bypass in both directions\n";
}

void testLegacyLockoutReconciliation() {
    MockAuthDatabase mockDb;
    mockDb.users.push_back({
        1, "alice@vaultx.sim", "100099998888", PinHasher::hashPin("123456"), "active"
    });
    mockDb.users.push_back({
        2, "bob@vaultx.sim", "100077776666", PinHasher::hashPin("654321"), "active"
    });

    // Unrelated lockout record must never be modified or deleted
    const std::int64_t futureLock = static_cast<std::int64_t>(std::time(nullptr)) + 600;
    mockDb.lockouts["unrelated@vaultx.sim"] = {5, futureLock};

    // Case A: 4 legacy failures stored under account number -> 1 more failure via account number
    // reconciles to 4 and then increments to 5 (locks immediately, no double counting)
    mockDb.lockouts["100099998888"] = {4, 0};

    AuthService auth(mockDb);
    AuthResult r5 = auth.loginUser("100099998888", "000000");
    assert(!r5.success);
    assert(r5.message.find("15 minutes") != std::string::npos);
    assert(mockDb.lockouts.find("100099998888") == mockDb.lockouts.end());
    assert(mockDb.lockouts["alice@vaultx.sim"].failedAttempts == 5);
    assert(mockDb.lockouts["alice@vaultx.sim"].lockedUntil > 0);

    // Subsequent login via email must stay locked without double counting
    AuthResult stillLocked = auth.loginUser("alice@vaultx.sim", "123456");
    assert(!stillLocked.success);
    assert(mockDb.lockouts["alice@vaultx.sim"].failedAttempts == 5);

    // Case B: Active legacy lockout under Bob's card number preserves expiration timestamp
    mockDb.lockouts["100077776666"] = {5, futureLock};
    AuthResult bobBlocked = auth.loginUser("100077776666", "654321");
    assert(!bobBlocked.success);
    assert(bobBlocked.message.find("locked") != std::string::npos);
    assert(mockDb.lockouts.find("100077776666") == mockDb.lockouts.end());
    assert(mockDb.lockouts["bob@vaultx.sim"].lockedUntil == futureLock);

    // Confirm unrelated record is untouched
    assert(mockDb.lockouts.find("unrelated@vaultx.sim") != mockDb.lockouts.end());
    assert(mockDb.lockouts["unrelated@vaultx.sim"].failedAttempts == 5);
    assert(mockDb.lockouts["unrelated@vaultx.sim"].lockedUntil == futureLock);

    std::cout << "[PASS] Legacy lockout reconciliation preserves expirations without double counting\n";
}

void testConcurrentLoginLockoutTOCTOU() {
    MockAuthDatabase mockDb;
    mockDb.users.push_back({
        1, "alice@vaultx.sim", "100099998888", PinHasher::hashPin("123456"), "active"
    });
    mockDb.users.push_back({
        2, "bob@vaultx.sim", "100077776666", PinHasher::hashPin("654321"), "active"
    });

    AuthService auth(mockDb);

    // Fire 12 simultaneous failed login attempts targeting Alice via alternating identifiers
    std::atomic<int> lockedResponses{0};
    std::atomic<int> invalidResponses{0};
    std::vector<std::thread> attackers;
    for (int i = 0; i < 12; ++i) {
        attackers.emplace_back([&auth, &lockedResponses, &invalidResponses, i]() {
            const std::string id = (i % 2 == 0) ? "100099998888" : "alice@vaultx.sim";
            AuthResult r = auth.loginUser(id, "000000");
            assert(!r.success);
            if (r.message.find("locked") != std::string::npos) {
                lockedResponses.fetch_add(1);
            } else {
                invalidResponses.fetch_add(1);
            }
        });
    }

    // Simultaneously authenticate Bob (different stripe) to verify no global lock/deadlock
    AuthResult bobResult{false, "", -1, ""};
    std::thread bobThread([&auth, &bobResult]() {
        bobResult = auth.loginUser("100077776666", "654321");
    });

    for (auto& t : attackers) t.join();
    bobThread.join();

    // Striped locking ensures failedAttempts never overshoots 5 due to TOCTOU races
    assert(mockDb.lockouts["alice@vaultx.sim"].failedAttempts == 5);
    assert(invalidResponses.load() == 4);
    assert(lockedResponses.load() == 8); // 5th triggers 15m lock + 6..12 rejected before PIN check
    assert(bobResult.success);

    std::cout << "[PASS] Concurrent login burst capped strictly at 5 attempts with zero TOCTOU/deadlock\n";
}

void testStrictIpValidationAndProxyHeaders() {
    // Valid IPv4 & IPv6 checks
    assert(RateLimiter::isValidIpAddress("203.0.113.10"));
    assert(RateLimiter::isValidIpAddress("2001:db8::1"));
    assert(RateLimiter::isValidIpAddress("::ffff:192.0.2.1"));

    // Invalid / malformed / oversized IP strings rejected
    assert(!RateLimiter::isValidIpAddress(""));
    assert(!RateLimiter::isValidIpAddress("999.999.999.999"));
    assert(!RateLimiter::isValidIpAddress("evil;drop"));
    assert(!RateLimiter::isValidIpAddress("1.2.3.4 extra"));
    assert(!RateLimiter::isValidIpAddress("::bad::ip"));
    assert(!RateLimiter::isValidIpAddress(std::string(50, '1')));

    // Default (NONE): ignore spoofed CF-Connecting-IP and X-Forwarded-For
    assert(RateLimiter::parseTrustedProxyMode(nullptr) == TrustedProxyMode::NONE);
    assert(RateLimiter::parseTrustedProxyMode("false") == TrustedProxyMode::NONE);
    assert(RateLimiter::resolveClientIp(
        "203.0.113.10", "1.2.3.4", "5.6.7.8, 9.9.9.9", TrustedProxyMode::NONE
    ) == "203.0.113.10");

    // Render rightmost XFF mode: valid rightmost IPv4/IPv6 accepted; invalid falls back safely
    assert(RateLimiter::parseTrustedProxyMode("render") == TrustedProxyMode::RENDER_RIGHTMOST_XFF);
    assert(RateLimiter::resolveClientIp(
        "10.0.0.1", "1.2.3.4", "8.8.8.8, 198.51.100.42", TrustedProxyMode::RENDER_RIGHTMOST_XFF
    ) == "198.51.100.42");
    assert(RateLimiter::resolveClientIp(
        "10.0.0.1", "1.2.3.4", "8.8.8.8, 2001:db8::99", TrustedProxyMode::RENDER_RIGHTMOST_XFF
    ) == "2001:db8::99");
    assert(RateLimiter::resolveClientIp(
        "10.0.0.1", "1.2.3.4", "1.2.3.4, ", TrustedProxyMode::RENDER_RIGHTMOST_XFF
    ) == "10.0.0.1");
    assert(RateLimiter::resolveClientIp(
        "10.0.0.1", "1.2.3.4", "1.2.3.4, invalid-ip", TrustedProxyMode::RENDER_RIGHTMOST_XFF
    ) == "10.0.0.1");

    // Cloudflare strict mode rejects invalid CF-Connecting-IP and falls back to remoteAddr
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
    testCanonicalLockoutBypassImpossible();
    testLegacyLockoutReconciliation();
    testConcurrentLoginLockoutTOCTOU();
    testStrictIpValidationAndProxyHeaders();
    testRateLimiterBoundsAndCleanup();
    testSessionMemoryBoundsAndCleanup();
    std::cout << "\nALL P1 SECURITY HARDENING TESTS PASSED!\n";
    return 0;
}
