/**
 * AuthService.cpp
 * Manages user registration, ATM card creation, and secure login with lockout policy.
 *
 * First-Year CSBS Viva Concept:
 * - Registration creates both a User (credentials) and an Account (funds) in ONE atomic transaction.
 * - Login tracks failed attempts to prevent brute-force attacks (locks for 15 minutes after 5 failures).
 * - PINs are verified via constant-time comparison against salted PBKDF2 hashes.
 */

#include "services/AuthService.h"
#include "security/PinHasher.h"

#include <openssl/rand.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <string>

AuthService::AuthService(Database& database)
    : database(database) {
}

// Generates a cryptographically random 12-digit simulated ATM card / account number.
std::string AuthService::generateAccountNumber() {
    unsigned char randomBytes[12];
    if (RAND_bytes(randomBytes, sizeof(randomBytes)) != 1) {
        return "";
    }

    // First digit is 1-9 to avoid leading zero
    std::string accountNumber;
    accountNumber += std::to_string(1 + (randomBytes[0] % 9));

    // Next 11 digits are 0-9
    for (int i = 1; i < 12; i++) {
        accountNumber += std::to_string(randomBytes[i] % 10);
    }

    return accountNumber;
}

// Issues a new simulated ATM Debit Card with initial promotional balance
AuthResult AuthService::registerUser(
    const std::string& fullName,
    const std::string& email,
    const std::string& pin
) {
    // Helper: trim whitespace
    auto trim = [](std::string value) {
        size_t start = value.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return std::string("");
        size_t end = value.find_last_not_of(" \t\n\r");
        return value.substr(start, end - start + 1);
    };

    std::string cleanName = trim(fullName);
    std::string cleanEmail = trim(email);

    std::transform(cleanEmail.begin(), cleanEmail.end(), cleanEmail.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (cleanName.length() < 2 || cleanName.length() > 100) {
        return {false, "Name must contain 2 to 100 characters.", -1, ""};
    }

    // Auto-generate virtual simulated account identifier if none provided
    if (cleanEmail.empty() || cleanEmail.find('@') == std::string::npos) {
        unsigned char randBuf[4];
        unsigned int suffix = 0;
        if (RAND_bytes(randBuf, sizeof(randBuf)) == 1) {
            suffix = (static_cast<unsigned int>(randBuf[0]) << 16)
                   | (static_cast<unsigned int>(randBuf[1]) << 8)
                   | static_cast<unsigned int>(randBuf[2]);
        } else {
            suffix = static_cast<unsigned int>(std::rand() % 100000);
        }
        cleanEmail = "sim_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(suffix) + "@vaultx.sim";
    } else {
        size_t atPos = cleanEmail.find('@');
        if (atPos == 0 || cleanEmail.find('@', atPos + 1) != std::string::npos ||
            cleanEmail.find('.', atPos) == std::string::npos) {
            return {false, "Please enter a valid email address.", -1, ""};
        }
    }

    // Hash the 6-digit PIN securely before storing
    std::string hashedPin = PinHasher::hashPin(pin);
    if (hashedPin.empty()) {
        return {false, "PIN must contain exactly 6 digits.", -1, ""};
    }

    // Try up to 5 times to generate a unique account number
    for (int attempt = 0; attempt < 5; attempt++) {
        std::string accountNumber = generateAccountNumber();
        if (accountNumber.empty()) {
            return {false, "Account number generation failed.", -1, ""};
        }

        const RegistrationResult regRes = database.createCardAccount(
            cleanName,
            cleanEmail,
            hashedPin,
            accountNumber,
            1000000, // ₹10,000 promotional balance (1,000,000 paise)
            "Welcome simulation balance"
        );

        if (regRes.success) {
            return {true, regRes.message, regRes.userId, accountNumber};
        }
        if (regRes.duplicateAccount) {
            continue; // Retry with a new account number
        }
        return {false, regRes.message, -1, ""};
    }

    return {false, "Could not generate a unique account number.", -1, ""};
}

// Authenticates user with Account Number (or Email) and 6-digit PIN
AuthResult AuthService::loginUser(
    const std::string& emailOrAccount,
    const std::string& pin
) {
    std::string cleanIdentifier = emailOrAccount;
    std::transform(cleanIdentifier.begin(), cleanIdentifier.end(), cleanIdentifier.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    // Trim whitespace
    size_t start = cleanIdentifier.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return {false, "Invalid credentials.", -1, ""};
    }
    size_t end = cleanIdentifier.find_last_not_of(" \t\n\r");
    cleanIdentifier = cleanIdentifier.substr(start, end - start + 1);

    // If identifier is predominantly digits, strip out formatting spaces and hyphens
    if (std::all_of(cleanIdentifier.begin(), cleanIdentifier.end(), [](unsigned char c) {
            return std::isdigit(c) || c == ' ' || c == '-';
        })) {
        cleanIdentifier.erase(
            std::remove_if(cleanIdentifier.begin(), cleanIdentifier.end(), [](unsigned char c) {
                return c == ' ' || c == '-';
            }),
            cleanIdentifier.end()
        );
    }

    const std::int64_t currentTime = static_cast<std::int64_t>(std::time(nullptr));

    // STEP 1: Check whether this identifier is currently locked out
    const std::int64_t lockedUntil = database.getLoginLockUntil(cleanIdentifier);
    if (lockedUntil > currentTime) {
        return {false, "Account temporarily locked. Try again later.", -1, ""};
    }
    if (lockedUntil > 0 && lockedUntil <= currentTime) {
        database.clearLoginAttempts(cleanIdentifier);
    }

    // STEP 2: Retrieve the user ID and stored PIN hash
    const UserAuthRecord userRecord = database.findUserForLogin(cleanIdentifier);

    // STEP 3: Verify PIN with constant-time memory comparison
    const bool validCredentials = (
        userRecord.found &&
        userRecord.userId > 0 &&
        !userRecord.pinHash.empty() &&
        PinHasher::verifyPin(pin, userRecord.pinHash)
    );

    // STEP 4: Handle failed attempt (brute-force protection)
    if (!validCredentials) {
        const std::int64_t newLockTime = currentTime + (LOCKOUT_DURATION_MINUTES * 60);
        const bool locked = database.recordFailedLoginAttempt(
            cleanIdentifier,
            MAX_FAILED_ATTEMPTS,
            newLockTime,
            currentTime
        );

        if (locked) {
            return {false, "Account temporarily locked for 15 minutes.", -1, ""};
        }

        // Generic error message prevents username harvesting
        return {false, "Invalid credentials.", -1, ""};
    }

    // STEP 5: Retrieve active account details
    const AccountLoginStatus accStatus = database.findAccountForUser(userRecord.userId);
    if (!accStatus.found) {
        return {false, "No ATM account found for this user.", -1, ""};
    }

    // STEP 6: Reject blocked or closed accounts
    if (accStatus.status != "active") {
        return {false, "Account is not active.", -1, ""};
    }

    // STEP 7: Successful login — reset failed attempts counter
    database.clearLoginAttempts(cleanIdentifier);

    return {true, "Login successful.", userRecord.userId, accStatus.accountNumber};
}
