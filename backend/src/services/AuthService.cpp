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

#include <sqlite3.h>
#include <openssl/rand.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <mutex>

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
    // Thread safety: synchronize all database access
    std::lock_guard<std::mutex> lock(database.getMutex());
    sqlite3* db = database.getConnection();
    if (!db) {
        return {false, "Database is not connected.", -1, ""};
    }

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
        cleanEmail = "sim_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(std::rand() % 100000) + "@vaultx.sim";
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

        // Begin atomic database transaction: insert user + account + initial deposit
        if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            return {false, "Could not start registration transaction.", -1, ""};
        }

        // 1. Insert User
        sqlite3_stmt* userStmt = nullptr;
        const char* userSQL = "INSERT INTO users (full_name, email, pin_hash) VALUES (?, ?, ?);";
        if (sqlite3_prepare_v2(db, userSQL, -1, &userStmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return {false, "Could not prepare user registration.", -1, ""};
        }

        sqlite3_bind_text(userStmt, 1, cleanName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(userStmt, 2, cleanEmail.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(userStmt, 3, hashedPin.c_str(), -1, SQLITE_TRANSIENT);

        int userResult = sqlite3_step(userStmt);
        sqlite3_finalize(userStmt);

        if (userResult != SQLITE_DONE) {
            std::string error = sqlite3_errmsg(db);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            if (error.find("UNIQUE constraint failed: users.email") != std::string::npos) {
                return {false, "This email is already registered.", -1, ""};
            }
            return {false, "User registration failed.", -1, ""};
        }

        int userId = static_cast<int>(sqlite3_last_insert_rowid(db));

        // 2. Insert Account with ₹10,000 promotional balance (1,000,000 paise)
        sqlite3_stmt* accStmt = nullptr;
        const char* accSQL = "INSERT INTO accounts (user_id, account_number, balance_paise) VALUES (?, ?, 1000000);";
        if (sqlite3_prepare_v2(db, accSQL, -1, &accStmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return {false, "Could not prepare account creation.", -1, ""};
        }

        sqlite3_bind_int(accStmt, 1, userId);
        sqlite3_bind_text(accStmt, 2, accountNumber.c_str(), -1, SQLITE_TRANSIENT);

        int accResult = sqlite3_step(accStmt);
        sqlite3_finalize(accStmt);

        if (accResult != SQLITE_DONE) {
            std::string error = sqlite3_errmsg(db);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            if (error.find("UNIQUE constraint failed: accounts.account_number") != std::string::npos) {
                continue; // Retry with a new account number
            }
            return {false, "Account creation failed.", -1, ""};
        }

        int accountId = static_cast<int>(sqlite3_last_insert_rowid(db));

        // 3. Log Initial Welcome Deposit Transaction
        sqlite3_stmt* txStmt = nullptr;
        const char* txSQL = "INSERT INTO transactions (account_id, transaction_type, amount_paise, description) VALUES (?, 'deposit', 1000000, 'Welcome simulation balance');";
        if (sqlite3_prepare_v2(db, txSQL, -1, &txStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(txStmt, 1, accountId);
            sqlite3_step(txStmt);
            sqlite3_finalize(txStmt);
        }

        // Commit all changes atomically
        if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return {false, "Registration could not be completed.", -1, ""};
        }

        return {true, "User registered successfully.", userId, accountNumber};
    }

    return {false, "Could not generate a unique account number.", -1, ""};
}

// Authenticates user with Account Number (or Email) and 6-digit PIN
AuthResult AuthService::loginUser(
    const std::string& emailOrAccount,
    const std::string& pin
) {
    std::lock_guard<std::mutex> lock(database.getMutex());
    sqlite3* db = database.getConnection();
    if (!db) {
        return {false, "Database is not connected.", -1, ""};
    }

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

    const std::time_t currentTime = std::time(nullptr);

    // STEP 1: Check whether this identifier is currently locked out
    sqlite3_stmt* lockStmt = nullptr;
    const char* lockSQL = "SELECT locked_until FROM login_attempts WHERE email = ?;";
    if (sqlite3_prepare_v2(db, lockSQL, -1, &lockStmt, nullptr) != SQLITE_OK) {
        return {false, "Login operation failed.", -1, ""};
    }

    sqlite3_bind_text(lockStmt, 1, cleanIdentifier.c_str(), -1, SQLITE_TRANSIENT);
    int lockResult = sqlite3_step(lockStmt);

    if (lockResult == SQLITE_ROW) {
        std::time_t lockedUntil = static_cast<std::time_t>(sqlite3_column_int64(lockStmt, 0));
        sqlite3_finalize(lockStmt);

        if (lockedUntil > currentTime) {
            return {false, "Account temporarily locked. Try again later.", -1, ""};
        }

        // Expired lockout: reset attempt counter
        if (lockedUntil > 0 && lockedUntil <= currentTime) {
            sqlite3_stmt* resetStmt = nullptr;
            const char* resetSQL = "DELETE FROM login_attempts WHERE email = ?;";
            if (sqlite3_prepare_v2(db, resetSQL, -1, &resetStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(resetStmt, 1, cleanIdentifier.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(resetStmt);
                sqlite3_finalize(resetStmt);
            }
        }
    } else {
        sqlite3_finalize(lockStmt);
    }

    // STEP 2: Retrieve the user ID and stored PIN hash
    sqlite3_stmt* userStmt = nullptr;
    const char* userSQL =
        "SELECT u.id, u.pin_hash "
        "FROM users u "
        "LEFT JOIN accounts a ON a.user_id = u.id "
        "WHERE a.account_number = ? OR u.email = ? "
        "LIMIT 1;";

    if (sqlite3_prepare_v2(db, userSQL, -1, &userStmt, nullptr) != SQLITE_OK) {
        return {false, "Login operation failed.", -1, ""};
    }

    sqlite3_bind_text(userStmt, 1, cleanIdentifier.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(userStmt, 2, cleanIdentifier.c_str(), -1, SQLITE_TRANSIENT);

    int userResult = sqlite3_step(userStmt);
    int userId = -1;
    std::string storedHash;

    if (userResult == SQLITE_ROW) {
        userId = sqlite3_column_int(userStmt, 0);
        const unsigned char* hashText = sqlite3_column_text(userStmt, 1);
        if (hashText) storedHash = reinterpret_cast<const char*>(hashText);
    }
    sqlite3_finalize(userStmt);

    // STEP 3: Verify PIN with constant-time memory comparison
    bool validCredentials = (userId > 0 && !storedHash.empty() && PinHasher::verifyPin(pin, storedHash));

    // STEP 4: Handle failed attempt (brute-force protection)
    if (!validCredentials) {
        std::time_t newLockTime = currentTime + (LOCKOUT_DURATION_MINUTES * 60);
        sqlite3_stmt* attemptStmt = nullptr;
        const char* attemptSQL =
            "INSERT INTO login_attempts (email, failed_attempts, locked_until) "
            "VALUES (?, 1, 0) "
            "ON CONFLICT(email) DO UPDATE SET "
            "failed_attempts = login_attempts.failed_attempts + 1, "
            "locked_until = CASE "
            "  WHEN login_attempts.failed_attempts + 1 >= ? THEN ? "
            "  ELSE login_attempts.locked_until "
            "END, "
            "updated_at = CURRENT_TIMESTAMP;";

        if (sqlite3_prepare_v2(db, attemptSQL, -1, &attemptStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(attemptStmt, 1, cleanIdentifier.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(attemptStmt, 2, MAX_FAILED_ATTEMPTS);
            sqlite3_bind_int64(attemptStmt, 3, static_cast<sqlite3_int64>(newLockTime));
            sqlite3_step(attemptStmt);
            sqlite3_finalize(attemptStmt);
        }

        // Check if this failure caused an account lockout
        sqlite3_stmt* checkStmt = nullptr;
        const char* checkSQL = "SELECT locked_until FROM login_attempts WHERE email = ?;";
        bool locked = false;
        if (sqlite3_prepare_v2(db, checkSQL, -1, &checkStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(checkStmt, 1, cleanIdentifier.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(checkStmt) == SQLITE_ROW) {
                locked = (sqlite3_column_int64(checkStmt, 0) > currentTime);
            }
            sqlite3_finalize(checkStmt);
        }

        if (locked) {
            return {false, "Account temporarily locked for 15 minutes.", -1, ""};
        }

        // Generic error message prevents username harvesting
        return {false, "Invalid credentials.", -1, ""};
    }

    // STEP 5: Retrieve active account details
    sqlite3_stmt* accStmt = nullptr;
    const char* accSQL = "SELECT account_number, status FROM accounts WHERE user_id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db, accSQL, -1, &accStmt, nullptr) != SQLITE_OK) {
        return {false, "Could not retrieve account details.", -1, ""};
    }

    sqlite3_bind_int(accStmt, 1, userId);
    int accResult = sqlite3_step(accStmt);

    if (accResult != SQLITE_ROW) {
        sqlite3_finalize(accStmt);
        return {false, "No ATM account found for this user.", -1, ""};
    }

    const unsigned char* accText = sqlite3_column_text(accStmt, 0);
    const unsigned char* statusText = sqlite3_column_text(accStmt, 1);
    std::string accountNumber = accText ? reinterpret_cast<const char*>(accText) : "";
    std::string accountStatus = statusText ? reinterpret_cast<const char*>(statusText) : "";
    sqlite3_finalize(accStmt);

    // STEP 6: Reject blocked or closed accounts
    if (accountStatus != "active") {
        return {false, "Account is not active.", -1, ""};
    }

    // STEP 7: Successful login — reset failed attempts counter
    sqlite3_stmt* clearStmt = nullptr;
    const char* clearSQL = "DELETE FROM login_attempts WHERE email = ?;";
    if (sqlite3_prepare_v2(db, clearSQL, -1, &clearStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(clearStmt, 1, cleanIdentifier.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(clearStmt);
        sqlite3_finalize(clearStmt);
    }

    return {true, "Login successful.", userId, accountNumber};
}
