
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

std::string AuthService::generateAccountNumber() {
    unsigned char randomBytes[12];

    if (RAND_bytes(randomBytes, sizeof(randomBytes)) != 1) {
        return "";
    }

    std::string accountNumber;
    accountNumber += std::to_string(1 + (randomBytes[0] % 9));

    for (int i = 1; i < 12; i++) {
        accountNumber += std::to_string(randomBytes[i] % 10);
    }

    return accountNumber;
}


// USER REGISTRATION
AuthResult AuthService::registerUser(
    const std::string& fullName,
    const std::string& email,
    const std::string& pin
) {
    // Protect the complete database operation.
    std::lock_guard<std::mutex> lock(database.getMutex());

    sqlite3* db = database.getConnection();

    if (db == nullptr) {
        return {false, "Database is not connected.", -1, ""};
    }

    auto trim = [](std::string value) {
        size_t start = value.find_first_not_of(" \t\n\r");

        if (start == std::string::npos) {
            return std::string("");
        }

        size_t end = value.find_last_not_of(" \t\n\r");

        return value.substr(start, end - start + 1);
    };

    std::string cleanName = trim(fullName);
    std::string cleanEmail = trim(email);

    std::transform(
        cleanEmail.begin(),
        cleanEmail.end(),
        cleanEmail.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );

    if (cleanName.length() < 2 || cleanName.length() > 100) {
        return {false, "Name must contain 2 to 100 characters.", -1, ""};
    }

    if (cleanEmail.empty() || cleanEmail.find('@') == std::string::npos) {
        // Auto-generate virtual simulated account identifier if none provided
        cleanEmail = "sim_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(std::rand() % 100000) + "@noir.atm";
    } else {
        size_t atPosition = cleanEmail.find('@');
        if (atPosition == 0 ||
            cleanEmail.find('@', atPosition + 1) != std::string::npos ||
            cleanEmail.find('.', atPosition) == std::string::npos) {
            return {false, "Please enter a valid email address.", -1, ""};
        }
    }

    std::string hashedPin = PinHasher::hashPin(pin);

    if (hashedPin.empty()) {
        return {false, "PIN must contain exactly 6 digits.", -1, ""};
    }

    for (int attempt = 0; attempt < 5; attempt++) {

        std::string accountNumber = generateAccountNumber();

        if (accountNumber.empty()) {
            return {false, "Account number generation failed.", -1, ""};
        }

        if (sqlite3_exec(
                db,
                "BEGIN IMMEDIATE TRANSACTION;",
                nullptr,
                nullptr,
                nullptr
            ) != SQLITE_OK) {

            return {false, "Could not start registration transaction.", -1, ""};
        }

        sqlite3_stmt* userStatement = nullptr;

        const char* userSQL =
            "INSERT INTO users (full_name, email, pin_hash) "
            "VALUES (?, ?, ?);";

        if (sqlite3_prepare_v2(
                db,
                userSQL,
                -1,
                &userStatement,
                nullptr
            ) != SQLITE_OK) {

            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);

            return {false, "Could not prepare user registration.", -1, ""};
        }

        sqlite3_bind_text(
            userStatement, 1, cleanName.c_str(), -1, SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            userStatement, 2, cleanEmail.c_str(), -1, SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            userStatement, 3, hashedPin.c_str(), -1, SQLITE_TRANSIENT
        );

        int userResult = sqlite3_step(userStatement);

        sqlite3_finalize(userStatement);

        if (userResult != SQLITE_DONE) {

            std::string error = sqlite3_errmsg(db);

            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);

            if (error.find("UNIQUE constraint failed: users.email")
                != std::string::npos) {

                return {
                    false,
                    "This email is already registered.",
                    -1,
                    ""
                };
            }

            return {false, "User registration failed.", -1, ""};
        }

        int userId = static_cast<int>(
            sqlite3_last_insert_rowid(db)
        );

        sqlite3_stmt* accountStatement = nullptr;

        const char* accountSQL =
            "INSERT INTO accounts "
            "(user_id, account_number, balance_paise) "
            "VALUES (?, ?, 1000000);";

        if (sqlite3_prepare_v2(
                db,
                accountSQL,
                -1,
                &accountStatement,
                nullptr
            ) != SQLITE_OK) {

            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);

            return {false, "Could not prepare account creation.", -1, ""};
        }

        sqlite3_bind_int(accountStatement, 1, userId);

        sqlite3_bind_text(
            accountStatement,
            2,
            accountNumber.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        int accountResult = sqlite3_step(accountStatement);

        sqlite3_finalize(accountStatement);

        if (accountResult != SQLITE_DONE) {

            std::string error = sqlite3_errmsg(db);

            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);

            if (error.find("UNIQUE constraint failed: accounts.account_number")
                != std::string::npos) {

                continue;
            }

            return {false, "Account creation failed.", -1, ""};
        }

        int accountId = static_cast<int>(sqlite3_last_insert_rowid(db));
        sqlite3_stmt* welcomeTx = nullptr;
        const char* welcomeTxSql =
            "INSERT INTO transactions (account_id, transaction_type, amount_paise, description) "
            "VALUES (?, 'deposit', 1000000, 'Welcome simulation balance');";
        if (sqlite3_prepare_v2(db, welcomeTxSql, -1, &welcomeTx, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(welcomeTx, 1, accountId);
            sqlite3_step(welcomeTx);
            sqlite3_finalize(welcomeTx);
        }

        if (sqlite3_exec(
                db,
                "COMMIT;",
                nullptr,
                nullptr,
                nullptr
            ) != SQLITE_OK) {

            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);

            return {false, "Registration could not be completed.", -1, ""};
        }

        return {
            true,
            "User registered successfully.",
            userId,
            accountNumber
        };
    }

    return {false, "Could not generate a unique account number.", -1, ""};
}


// USER LOGIN WITH FAILED-ATTEMPT TRACKING
AuthResult AuthService::loginUser(
    const std::string& email,
    const std::string& pin
) {
    // Protect the complete database operation.
    std::lock_guard<std::mutex> lock(database.getMutex());

    sqlite3* db = database.getConnection();

    if (db == nullptr) {
        return {false, "Database is not connected.", -1, ""};
    }

    std::string cleanEmail = email;

    std::transform(
        cleanEmail.begin(),
        cleanEmail.end(),
        cleanEmail.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );

    size_t start = cleanEmail.find_first_not_of(" \t\n\r");

    if (start == std::string::npos) {
        return {false, "Invalid email or PIN.", -1, ""};
    }

    size_t end = cleanEmail.find_last_not_of(" \t\n\r");

    cleanEmail = cleanEmail.substr(start, end - start + 1);

    // If identifier is predominantly digits, strip out formatting spaces and hyphens
    if (std::all_of(cleanEmail.begin(), cleanEmail.end(), [](unsigned char c) {
            return std::isdigit(c) || c == ' ' || c == '-';
        })) {
        cleanEmail.erase(
            std::remove_if(cleanEmail.begin(), cleanEmail.end(), [](unsigned char c) {
                return c == ' ' || c == '-';
            }),
            cleanEmail.end()
        );
    }

    const std::time_t currentTime = std::time(nullptr);


    // STEP 1: Check whether this email is locked.
    sqlite3_stmt* lockStatement = nullptr;

    const char* lockSQL =
        "SELECT locked_until FROM login_attempts WHERE email = ?;";

    if (sqlite3_prepare_v2(
            db,
            lockSQL,
            -1,
            &lockStatement,
            nullptr
        ) != SQLITE_OK) {

        return {false, "Login operation failed.", -1, ""};
    }

    sqlite3_bind_text(
        lockStatement,
        1,
        cleanEmail.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    int lockResult = sqlite3_step(lockStatement);

    if (lockResult == SQLITE_ROW) {

        std::time_t lockedUntil = static_cast<std::time_t>(
            sqlite3_column_int64(lockStatement, 0)
        );

        sqlite3_finalize(lockStatement);

        if (lockedUntil > currentTime) {
            return {
                false,
                "Account temporarily locked. Try again later.",
                -1,
                ""
            };
        }

        if (lockedUntil > 0 && lockedUntil <= currentTime) {

            sqlite3_stmt* resetStatement = nullptr;

            const char* resetSQL =
                "DELETE FROM login_attempts WHERE email = ?;";

            if (sqlite3_prepare_v2(
                    db,
                    resetSQL,
                    -1,
                    &resetStatement,
                    nullptr
                ) != SQLITE_OK) {

                return {false, "Login operation failed.", -1, ""};
            }

            sqlite3_bind_text(
                resetStatement,
                1,
                cleanEmail.c_str(),
                -1,
                SQLITE_TRANSIENT
            );

            int resetResult = sqlite3_step(resetStatement);

            sqlite3_finalize(resetStatement);

            if (resetResult != SQLITE_DONE) {
                return {false, "Login operation failed.", -1, ""};
            }
        }

    } else {

        sqlite3_finalize(lockStatement);

        if (lockResult != SQLITE_DONE) {
            return {false, "Login operation failed.", -1, ""};
        }
    }


    // STEP 2: Retrieve the stored PIN hash (by Account Number or Email).
    sqlite3_stmt* userStatement = nullptr;

    const char* userSQL =
        "SELECT u.id, u.pin_hash "
        "FROM users u "
        "LEFT JOIN accounts a ON a.user_id = u.id "
        "WHERE a.account_number = ? OR u.email = ? "
        "LIMIT 1;";

    if (sqlite3_prepare_v2(
            db,
            userSQL,
            -1,
            &userStatement,
            nullptr
        ) != SQLITE_OK) {

        return {false, "Login operation failed.", -1, ""};
    }

    sqlite3_bind_text(
        userStatement,
        1,
        cleanEmail.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        userStatement,
        2,
        cleanEmail.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    int userResult = sqlite3_step(userStatement);

    int userId = -1;
    std::string storedHash;

    if (userResult == SQLITE_ROW) {

        userId = sqlite3_column_int(userStatement, 0);

        const unsigned char* hashText =
            sqlite3_column_text(userStatement, 1);

        if (hashText != nullptr) {
            storedHash = reinterpret_cast<const char*>(hashText);
        }

    } else if (userResult != SQLITE_DONE) {

        sqlite3_finalize(userStatement);

        return {false, "Login operation failed.", -1, ""};
    }

    sqlite3_finalize(userStatement);


    // STEP 3: Verify the PIN.
    bool validCredentials =
        userId > 0 &&
        !storedHash.empty() &&
        PinHasher::verifyPin(pin, storedHash);


    // STEP 4: Record an incorrect login attempt.
    if (!validCredentials) {

        std::time_t newLockTime =
            currentTime + (LOCKOUT_DURATION_MINUTES * 60);

        sqlite3_stmt* attemptStatement = nullptr;

        const char* attemptSQL =
            "INSERT INTO login_attempts "
            "(email, failed_attempts, locked_until) "
            "VALUES (?, 1, 0) "
            "ON CONFLICT(email) DO UPDATE SET "
            "failed_attempts = login_attempts.failed_attempts + 1, "
            "locked_until = CASE "
            "WHEN login_attempts.failed_attempts + 1 >= ? "
            "THEN ? "
            "ELSE login_attempts.locked_until "
            "END, "
            "updated_at = CURRENT_TIMESTAMP;";

        if (sqlite3_prepare_v2(
                db,
                attemptSQL,
                -1,
                &attemptStatement,
                nullptr
            ) != SQLITE_OK) {

            return {false, "Login operation failed.", -1, ""};
        }

        sqlite3_bind_text(
            attemptStatement,
            1,
            cleanEmail.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_int(
            attemptStatement,
            2,
            MAX_FAILED_ATTEMPTS
        );

        sqlite3_bind_int64(
            attemptStatement,
            3,
            static_cast<sqlite3_int64>(newLockTime)
        );

        int attemptResult = sqlite3_step(attemptStatement);

        sqlite3_finalize(attemptStatement);

        if (attemptResult != SQLITE_DONE) {
            return {false, "Login operation failed.", -1, ""};
        }


        // Check whether the fifth failed attempt caused a lockout.
        sqlite3_stmt* checkStatement = nullptr;

        const char* checkSQL =
            "SELECT locked_until FROM login_attempts WHERE email = ?;";

        if (sqlite3_prepare_v2(
                db,
                checkSQL,
                -1,
                &checkStatement,
                nullptr
            ) != SQLITE_OK) {

            return {false, "Login operation failed.", -1, ""};
        }

        sqlite3_bind_text(
            checkStatement,
            1,
            cleanEmail.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        int checkResult = sqlite3_step(checkStatement);

        bool locked = false;

        if (checkResult == SQLITE_ROW) {
            locked =
                sqlite3_column_int64(checkStatement, 0) > currentTime;
        }

        sqlite3_finalize(checkStatement);

        if (checkResult != SQLITE_ROW) {
            return {false, "Login operation failed.", -1, ""};
        }

        if (locked) {
            return {
                false,
                "Account temporarily locked for 15 minutes.",
                -1,
                ""
            };
        }

        return {false, "Invalid email or PIN.", -1, ""};
    }


    // STEP 5: Retrieve account number and account status.
    sqlite3_stmt* accountStatement = nullptr;

    const char* accountSQL =
        "SELECT account_number, status "
        "FROM accounts WHERE user_id = ? LIMIT 1;";

    if (sqlite3_prepare_v2(
            db,
            accountSQL,
            -1,
            &accountStatement,
            nullptr
        ) != SQLITE_OK) {

        return {false, "Could not retrieve account details.", -1, ""};
    }

    sqlite3_bind_int(accountStatement, 1, userId);

    int accountResult = sqlite3_step(accountStatement);

    if (accountResult != SQLITE_ROW) {

        sqlite3_finalize(accountStatement);

        return {
            false,
            "No ATM account found for this user.",
            -1,
            ""
        };
    }

    const unsigned char* accountText =
        sqlite3_column_text(accountStatement, 0);

    const unsigned char* statusText =
        sqlite3_column_text(accountStatement, 1);

    if (accountText == nullptr || statusText == nullptr) {

        sqlite3_finalize(accountStatement);

        return {
            false,
            "Could not retrieve account details.",
            -1,
            ""
        };
    }

    std::string accountNumber(
        reinterpret_cast<const char*>(accountText)
    );

    std::string accountStatus(
        reinterpret_cast<const char*>(statusText)
    );

    sqlite3_finalize(accountStatement);


    // STEP 6: Reject blocked or closed accounts.
    if (accountStatus != "active") {
        return {
            false,
            "Account is not active.",
            -1,
            ""
        };
    }


    // STEP 7: Clear failed attempts after successful login.
    sqlite3_stmt* clearStatement = nullptr;

    const char* clearSQL =
        "DELETE FROM login_attempts WHERE email = ?;";

    if (sqlite3_prepare_v2(
            db,
            clearSQL,
            -1,
            &clearStatement,
            nullptr
        ) != SQLITE_OK) {

        return {false, "Login operation failed.", -1, ""};
    }

    sqlite3_bind_text(
        clearStatement,
        1,
        cleanEmail.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    int clearResult = sqlite3_step(clearStatement);

    sqlite3_finalize(clearStatement);

    if (clearResult != SQLITE_DONE) {
        return {false, "Login operation failed.", -1, ""};
    }

    return {
        true,
        "Login successful.",
        userId,
        accountNumber
    };
}
