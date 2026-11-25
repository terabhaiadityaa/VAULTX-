/**
 * Database.cpp
 * Supabase PostgreSQL database operations via libpqxx with atomic transaction
 * consistency, row-level locking (FOR UPDATE), and thread safety.
 *
 * First-Year CSBS Viva Concept:
 * - Integer Paise: All balances are stored as 64-bit BIGINT integers (paise).
 * - ACID Transactions: Deposits, withdrawals, transfers, and registrations use
 *   PostgreSQL transactions (pqxx::work) with SELECT ... FOR UPDATE row locks.
 * - Deadlock Prevention: P2P transfers lock accounts in consistent account_number order.
 */

#include "database/Database.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

std::string fieldAsString(const pqxx::field& f) {
    return f.is_null() ? std::string("") : f.as<std::string>();
}

} // namespace

Database::Database() = default;

Database::~Database() {
    disconnect();
}

bool Database::ensureConnected(bool forceVerify) {
    const auto now = std::chrono::steady_clock::now();
    if (conn && conn->is_open()) {
        if (!forceVerify && lastVerifiedAt != std::chrono::steady_clock::time_point{} &&
            (now - lastVerifiedAt) < std::chrono::seconds(30)) {
            return true;
        }
        try {
            pqxx::nontransaction nt(*conn);
            nt.exec("SELECT 1;");
            lastVerifiedAt = std::chrono::steady_clock::now();
            return true;
        } catch (const pqxx::broken_connection&) {
            conn.reset();
        } catch (const std::exception&) {
            conn.reset();
        }
    }

    if (connectionString.empty()) {
        return false;
    }

    try {
        conn = std::make_unique<pqxx::connection>(connectionString);
        if (!conn || !conn->is_open()) {
            conn.reset();
            return false;
        }
        pqxx::nontransaction nt(*conn);
        nt.exec("SET TIME ZONE 'UTC';");
        lastVerifiedAt = std::chrono::steady_clock::now();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "PostgreSQL connection error: " << e.what() << std::endl;
        conn.reset();
        return false;
    }
}

template <typename Fn>
auto Database::runWithReconnect(Fn&& fn) -> decltype(fn()) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        try {
            return fn();
        } catch (const pqxx::broken_connection&) {
            conn.reset();
            lastVerifiedAt = std::chrono::steady_clock::time_point{};
            if (attempt == 1 || !ensureConnected(true)) {
                throw;
            }
        }
    }
    return fn();
}

bool Database::connect(const std::string& connUrl) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (connUrl.empty()) {
        std::cerr << "PostgreSQL connection string is empty." << std::endl;
        return false;
    }

    connectionString = connUrl;
    if (!ensureConnected(true)) {
        return false;
    }

    std::cout << "PostgreSQL (Supabase) database connected successfully!" << std::endl;
    return true;
}

void Database::disconnect() {
    std::lock_guard<std::mutex> lock(dbMutex);
    conn.reset();
    lastVerifiedAt = std::chrono::steady_clock::time_point{};
}

bool Database::isConnected() {
    std::lock_guard<std::mutex> lock(dbMutex);
    return ensureConnected(true);
}

bool Database::verifySchema() {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) {
        std::cerr << "Database is not connected during schema verification." << std::endl;
        return false;
    }

    try {
        return runWithReconnect([&]() {
            pqxx::nontransaction nt(*conn);
            pqxx::result res = nt.exec(
                "SELECT count(*) FROM information_schema.tables "
                "WHERE table_schema = 'public' "
                "AND table_name IN ('users', 'accounts', 'transactions', 'login_attempts');"
            );
            lastVerifiedAt = std::chrono::steady_clock::now();
            const int tableCount = res.empty() ? 0 : res[0][0].as<int>();
            if (tableCount != 4) {
                std::cerr << "Missing required PostgreSQL tables in public schema (found "
                          << tableCount << "/4)." << std::endl;
                return false;
            }
            std::cout << "PostgreSQL schema verified (4/4 production tables present)." << std::endl;
            return true;
        });
    } catch (const std::exception& e) {
        std::cerr << "PostgreSQL schema verification failed: " << e.what() << std::endl;
        return false;
    }
}

std::mutex& Database::getMutex() {
    return dbMutex;
}

bool Database::accountExists(const std::string& accountNumber) {
    if (accountNumber.empty()) return false;
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return false;

    try {
        return runWithReconnect([&]() {
            pqxx::nontransaction nt(*conn);
            pqxx::result res = nt.exec_params(
                "SELECT 1 FROM public.accounts WHERE account_number = $1 LIMIT 1;",
                accountNumber
            );
            lastVerifiedAt = std::chrono::steady_clock::now();
            return !res.empty();
        });
    } catch (const std::exception& e) {
        std::cerr << "accountExists error: " << e.what() << std::endl;
        return false;
    }
}

RegistrationResult Database::createCardAccount(
    const std::string& fullName,
    const std::string& email,
    const std::string& pinHash,
    const std::string& accountNumber,
    std::int64_t initialBalancePaise,
    const std::string& depositDescription
) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) {
        return {false, false, false, "Database is not connected.", -1};
    }

    try {
        return runWithReconnect([&]() -> RegistrationResult {
            pqxx::work txn(*conn);

            pqxx::result userRes = txn.exec_params(
                "INSERT INTO public.users (full_name, email, pin_hash) "
                "VALUES ($1, $2, $3) RETURNING id;",
                fullName, email, pinHash
            );
            if (userRes.empty()) {
                return {false, false, false, "User registration failed.", -1};
            }
            const int userId = static_cast<int>(userRes[0][0].as<std::int64_t>());

            pqxx::result accRes = txn.exec_params(
                "INSERT INTO public.accounts (user_id, account_number, balance_paise, status) "
                "VALUES ($1, $2, $3, 'active') RETURNING id;",
                static_cast<std::int64_t>(userId), accountNumber, initialBalancePaise
            );
            if (accRes.empty()) {
                return {false, false, false, "Account creation failed.", -1};
            }
            const std::int64_t accountId = accRes[0][0].as<std::int64_t>();

            if (initialBalancePaise > 0) {
                txn.exec_params(
                    "INSERT INTO public.transactions "
                    "(account_id, transaction_type, amount_paise, description) "
                    "VALUES ($1, 'deposit', $2, $3);",
                    accountId, initialBalancePaise, depositDescription
                );
            }

            txn.commit();
            lastVerifiedAt = std::chrono::steady_clock::now();
            return {true, false, false, "User registered successfully.", userId};
        });
    } catch (const pqxx::unique_violation& e) {
        const std::string err = e.what();
        if (err.find("users_email_key") != std::string::npos ||
            err.find("email") != std::string::npos) {
            return {false, true, false, "This email is already registered.", -1};
        }
        if (err.find("accounts_account_number_key") != std::string::npos ||
            err.find("account_number") != std::string::npos) {
            return {false, false, true, "Account number collision; retrying.", -1};
        }
        return {false, false, false, "Registration conflict occurred.", -1};
    } catch (const std::exception& e) {
        std::cerr << "createCardAccount error: " << e.what() << std::endl;
        return {false, false, false, "Registration could not be completed.", -1};
    }
}

std::int64_t Database::getLoginLockUntil(const std::string& identifier) {
    if (identifier.empty()) return 0;
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return 0;

    try {
        return runWithReconnect([&]() -> std::int64_t {
            pqxx::nontransaction nt(*conn);
            pqxx::result res = nt.exec_params(
                "SELECT locked_until FROM public.login_attempts WHERE email = $1;",
                identifier
            );
            lastVerifiedAt = std::chrono::steady_clock::now();
            if (res.empty() || res[0][0].is_null()) return 0;
            return res[0][0].as<std::int64_t>();
        });
    } catch (const std::exception& e) {
        std::cerr << "getLoginLockUntil error: " << e.what() << std::endl;
        return 0;
    }
}

void Database::clearLoginAttempts(const std::string& identifier) {
    if (identifier.empty()) return;
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return;

    try {
        runWithReconnect([&]() {
            pqxx::nontransaction nt(*conn);
            nt.exec_params(
                "DELETE FROM public.login_attempts WHERE email = $1;",
                identifier
            );
            lastVerifiedAt = std::chrono::steady_clock::now();
        });
    } catch (const std::exception& e) {
        std::cerr << "clearLoginAttempts error: " << e.what() << std::endl;
    }
}

void Database::reconcileLegacyLoginAttempts(
    const std::string& legacyIdentifier,
    const std::string& canonicalEmail
) {
    if (legacyIdentifier.empty() || canonicalEmail.empty() || legacyIdentifier == canonicalEmail) {
        return;
    }
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return;

    try {
        runWithReconnect([&]() {
            pqxx::nontransaction nt(*conn);
            nt.exec_params(
                "WITH leg AS ("
                "  DELETE FROM public.login_attempts WHERE email = $1 "
                "  RETURNING COALESCE(failed_attempts, 0) AS failed_attempts, "
                "            COALESCE(locked_until, 0) AS locked_until"
                ") "
                "INSERT INTO public.login_attempts (email, failed_attempts, locked_until, updated_at) "
                "SELECT $2, leg.failed_attempts, leg.locked_until, CURRENT_TIMESTAMP FROM leg "
                "ON CONFLICT (email) DO UPDATE SET "
                "failed_attempts = public.login_attempts.failed_attempts + EXCLUDED.failed_attempts, "
                "locked_until = GREATEST(public.login_attempts.locked_until, EXCLUDED.locked_until), "
                "updated_at = CURRENT_TIMESTAMP;",
                legacyIdentifier, canonicalEmail
            );
            lastVerifiedAt = std::chrono::steady_clock::now();
        });
    } catch (const std::exception& e) {
        std::cerr << "reconcileLegacyLoginAttempts error: " << e.what() << std::endl;
    }
}

bool Database::recordFailedLoginAttempt(
    const std::string& identifier,
    int maxAttempts,
    std::int64_t lockUntilEpoch,
    std::int64_t currentEpoch
) {
    if (identifier.empty()) return false;
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return false;

    try {
        return runWithReconnect([&]() {
            pqxx::work txn(*conn);
            pqxx::result res = txn.exec_params(
                "INSERT INTO public.login_attempts (email, failed_attempts, locked_until, updated_at) "
                "VALUES ($1, 1, 0, CURRENT_TIMESTAMP) "
                "ON CONFLICT (email) DO UPDATE SET "
                "failed_attempts = public.login_attempts.failed_attempts + 1, "
                "locked_until = CASE "
                "  WHEN public.login_attempts.failed_attempts + 1 >= $2 THEN $3 "
                "  ELSE public.login_attempts.locked_until "
                "END, "
                "updated_at = CURRENT_TIMESTAMP "
                "RETURNING locked_until;",
                identifier, maxAttempts, lockUntilEpoch
            );
            txn.commit();
            lastVerifiedAt = std::chrono::steady_clock::now();

            if (!res.empty() && !res[0][0].is_null()) {
                return res[0][0].as<std::int64_t>() > currentEpoch;
            }
            return false;
        });
    } catch (const std::exception& e) {
        std::cerr << "recordFailedLoginAttempt error: " << e.what() << std::endl;
        return false;
    }
}

UserAuthRecord Database::findUserForLogin(const std::string& identifier) {
    UserAuthRecord record;
    if (identifier.empty()) return record;
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return record;

    try {
        return runWithReconnect([&]() {
            UserAuthRecord out;
            pqxx::nontransaction nt(*conn);
            pqxx::result res = nt.exec_params(
                "SELECT u.id, u.email, u.pin_hash "
                "FROM public.users u "
                "LEFT JOIN public.accounts a ON a.user_id = u.id "
                "WHERE a.account_number = $1 OR u.email = $1 "
                "LIMIT 1;",
                identifier
            );
            lastVerifiedAt = std::chrono::steady_clock::now();
            if (!res.empty()) {
                out.found = true;
                out.userId = static_cast<int>(res[0][0].as<std::int64_t>());
                out.email = fieldAsString(res[0][1]);
                out.pinHash = fieldAsString(res[0][2]);
            }
            return out;
        });
    } catch (const std::exception& e) {
        std::cerr << "findUserForLogin error: " << e.what() << std::endl;
    }
    return record;
}

AccountLoginStatus Database::findAccountForUser(int userId) {
    AccountLoginStatus status;
    if (userId <= 0) return status;
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return status;

    try {
        return runWithReconnect([&]() {
            AccountLoginStatus out;
            pqxx::nontransaction nt(*conn);
            pqxx::result res = nt.exec_params(
                "SELECT account_number, status "
                "FROM public.accounts "
                "WHERE user_id = $1 "
                "LIMIT 1;",
                static_cast<std::int64_t>(userId)
            );
            lastVerifiedAt = std::chrono::steady_clock::now();
            if (!res.empty()) {
                out.found = true;
                out.accountNumber = fieldAsString(res[0][0]);
                out.status = fieldAsString(res[0][1]);
            }
            return out;
        });
    } catch (const std::exception& e) {
        std::cerr << "findAccountForUser error: " << e.what() << std::endl;
    }
    return status;
}

std::string Database::getAccountNumberForUser(int userId) {
    if (userId <= 0) return "";
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return "";

    try {
        return runWithReconnect([&]() -> std::string {
            pqxx::nontransaction nt(*conn);
            pqxx::result res = nt.exec_params(
                "SELECT account_number FROM public.accounts "
                "WHERE user_id = $1 AND status = 'active' LIMIT 1;",
                static_cast<std::int64_t>(userId)
            );
            lastVerifiedAt = std::chrono::steady_clock::now();
            if (!res.empty()) {
                return fieldAsString(res[0][0]);
            }
            return "";
        });
    } catch (const std::exception& e) {
        std::cerr << "getAccountNumberForUser error: " << e.what() << std::endl;
    }
    return "";
}

TransferResult Database::transferFunds(
    const std::string& senderAcc,
    const std::string& receiverAcc,
    std::int64_t amountPaise
) {
    if (senderAcc.empty() || receiverAcc.empty())
        return {false, true, "Sender and receiver accounts are required.", 0};
    if (senderAcc == receiverAcc)
        return {false, true, "You cannot transfer money to your own account.", 0};
    if (amountPaise <= 0)
        return {false, true, "Transfer amount must be greater than zero.", 0};

    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected())
        return {false, false, "Database is not connected.", 0};

    try {
        return runWithReconnect([&]() -> TransferResult {
            pqxx::work txn(*conn);

            struct AccountInfo {
                bool found = false;
                std::int64_t id = 0;
                std::int64_t balance = 0;
                std::string status;
            };

            auto loadAndLock = [&txn](const std::string& number) -> AccountInfo {
                AccountInfo info;
                pqxx::result res = txn.exec_params(
                    "SELECT id, balance_paise, status FROM public.accounts "
                    "WHERE account_number = $1 FOR UPDATE;",
                    number
                );
                if (!res.empty()) {
                    info.found = true;
                    info.id = res[0][0].as<std::int64_t>();
                    info.balance = res[0][1].as<std::int64_t>();
                    info.status = fieldAsString(res[0][2]);
                }
                return info;
            };

            // Acquire row locks in consistent account-number order to prevent deadlocks
            const std::string firstAcc = std::min(senderAcc, receiverAcc);
            const std::string secondAcc = std::max(senderAcc, receiverAcc);
            AccountInfo firstInfo = loadAndLock(firstAcc);
            AccountInfo secondInfo = loadAndLock(secondAcc);

            const AccountInfo& sender = (firstAcc == senderAcc) ? firstInfo : secondInfo;
            const AccountInfo& receiver = (firstAcc == receiverAcc) ? firstInfo : secondInfo;

            if (!sender.found)
                return {false, true, "Sender account was not found.", 0};
            if (sender.status != "active")
                return {false, true, "Sender account is blocked or closed.", sender.balance};
            if (!receiver.found)
                return {false, true, "Receiver account was not found.", sender.balance};
            if (receiver.status != "active")
                return {false, true, "Receiver account is blocked or closed.", sender.balance};
            if (sender.balance < amountPaise)
                return {false, true, "Insufficient balance.", sender.balance};
            if (receiver.balance > std::numeric_limits<std::int64_t>::max() - amountPaise)
                return {false, true, "Receiver balance limit would be exceeded.", sender.balance};

            pqxx::result debitRes = txn.exec_params(
                "UPDATE public.accounts SET balance_paise = balance_paise - $1 "
                "WHERE id = $2 AND status = 'active' AND balance_paise >= $1 "
                "RETURNING balance_paise;",
                amountPaise, sender.id
            );
            if (debitRes.empty()) {
                return {false, true, "Sender debit failed; transfer was cancelled.", sender.balance};
            }
            const std::int64_t newSenderBalance = debitRes[0][0].as<std::int64_t>();

            pqxx::result creditRes = txn.exec_params(
                "UPDATE public.accounts SET balance_paise = balance_paise + $1 "
                "WHERE id = $2 AND status = 'active';",
                amountPaise, receiver.id
            );
            if (creditRes.affected_rows() != 1) {
                return {false, false, "Receiver credit failed; transfer was rolled back.", sender.balance};
            }

            const std::string outDescription = "Transfer to account " + receiverAcc;
            const std::string inDescription = "Transfer from account " + senderAcc;

            txn.exec_params(
                "INSERT INTO public.transactions "
                "(account_id, transaction_type, amount_paise, related_account_id, description) "
                "VALUES ($1, 'transfer_out', $2, $3, $4);",
                sender.id, amountPaise, receiver.id, outDescription
            );
            txn.exec_params(
                "INSERT INTO public.transactions "
                "(account_id, transaction_type, amount_paise, related_account_id, description) "
                "VALUES ($1, 'transfer_in', $2, $3, $4);",
                receiver.id, amountPaise, sender.id, inDescription
            );

            txn.commit();
            lastVerifiedAt = std::chrono::steady_clock::now();
            return {true, false, "Transfer completed successfully.", newSenderBalance};
        });
    } catch (const std::exception& e) {
        std::cerr << "transferFunds error: " << e.what() << std::endl;
        return {false, false, "Could not complete transfer; transaction was rolled back.", 0};
    }
}

AccountDetails Database::getAccountDetailsForUser(int userId) {
    AccountDetails details;
    if (userId <= 0) return details;
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return details;

    try {
        return runWithReconnect([&]() {
            AccountDetails out;
            pqxx::nontransaction nt(*conn);
            pqxx::result res = nt.exec_params(
                "SELECT u.id, u.full_name, u.email, a.id, a.account_number, a.balance_paise, a.status, "
                "       TO_CHAR(a.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD HH24:MI:SS') "
                "FROM public.users u "
                "JOIN public.accounts a ON u.id = a.user_id "
                "WHERE u.id = $1 AND a.status = 'active' LIMIT 1;",
                static_cast<std::int64_t>(userId)
            );
            lastVerifiedAt = std::chrono::steady_clock::now();
            if (!res.empty()) {
                out.found = true;
                out.userId = static_cast<int>(res[0][0].as<std::int64_t>());
                out.fullName = fieldAsString(res[0][1]);
                out.email = fieldAsString(res[0][2]);
                out.accountId = res[0][3].as<std::int64_t>();
                out.accountNumber = fieldAsString(res[0][4]);
                out.balancePaise = res[0][5].as<std::int64_t>();
                out.status = fieldAsString(res[0][6]);
                out.createdAt = fieldAsString(res[0][7]);
            }
            return out;
        });
    } catch (const std::exception& e) {
        std::cerr << "getAccountDetailsForUser error: " << e.what() << std::endl;
    }
    return details;
}

OperationResult Database::depositFunds(const std::string& accountNumber, std::int64_t amountPaise) {
    if (accountNumber.empty())
        return {false, true, "Account number is required.", 0};
    if (amountPaise <= 0)
        return {false, true, "Deposit amount must be greater than zero.", 0};

    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected())
        return {false, false, "Database is not connected.", 0};

    try {
        return runWithReconnect([&]() -> OperationResult {
            pqxx::work txn(*conn);
            pqxx::result selRes = txn.exec_params(
                "SELECT id, balance_paise, status FROM public.accounts "
                "WHERE account_number = $1 FOR UPDATE;",
                accountNumber
            );
            if (selRes.empty()) {
                return {false, true, "Account not found.", 0};
            }

            const std::int64_t accountId = selRes[0][0].as<std::int64_t>();
            const std::int64_t currentBalance = selRes[0][1].as<std::int64_t>();
            const std::string status = fieldAsString(selRes[0][2]);

            if (status != "active")
                return {false, true, "Account is not active.", currentBalance};
            if (currentBalance > std::numeric_limits<std::int64_t>::max() - amountPaise)
                return {false, true, "Balance limit would be exceeded.", currentBalance};

            pqxx::result updRes = txn.exec_params(
                "UPDATE public.accounts SET balance_paise = balance_paise + $1 "
                "WHERE id = $2 AND status = 'active' RETURNING balance_paise;",
                amountPaise, accountId
            );
            if (updRes.empty()) {
                return {false, false, "Deposit could not be applied.", currentBalance};
            }
            const std::int64_t newBalance = updRes[0][0].as<std::int64_t>();

            txn.exec_params(
                "INSERT INTO public.transactions "
                "(account_id, transaction_type, amount_paise, description) "
                "VALUES ($1, 'deposit', $2, 'Cash deposit');",
                accountId, amountPaise
            );

            txn.commit();
            lastVerifiedAt = std::chrono::steady_clock::now();
            return {true, false, "Cash deposit completed successfully.", newBalance};
        });
    } catch (const std::exception& e) {
        std::cerr << "depositFunds error: " << e.what() << std::endl;
        return {false, false, "Could not complete deposit transaction.", 0};
    }
}

OperationResult Database::withdrawFunds(const std::string& accountNumber, std::int64_t amountPaise) {
    if (accountNumber.empty())
        return {false, true, "Account number is required.", 0};
    if (amountPaise <= 0)
        return {false, true, "Withdrawal amount must be greater than zero.", 0};

    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected())
        return {false, false, "Database is not connected.", 0};

    try {
        return runWithReconnect([&]() -> OperationResult {
            pqxx::work txn(*conn);
            pqxx::result selRes = txn.exec_params(
                "SELECT id, balance_paise, status FROM public.accounts "
                "WHERE account_number = $1 FOR UPDATE;",
                accountNumber
            );
            if (selRes.empty()) {
                return {false, true, "Account not found.", 0};
            }

            const std::int64_t accountId = selRes[0][0].as<std::int64_t>();
            const std::int64_t currentBalance = selRes[0][1].as<std::int64_t>();
            const std::string status = fieldAsString(selRes[0][2]);

            if (status != "active")
                return {false, true, "Account is not active.", currentBalance};
            if (currentBalance < amountPaise)
                return {false, true, "Insufficient funds for withdrawal.", currentBalance};

            pqxx::result updRes = txn.exec_params(
                "UPDATE public.accounts SET balance_paise = balance_paise - $1 "
                "WHERE id = $2 AND status = 'active' AND balance_paise >= $1 "
                "RETURNING balance_paise;",
                amountPaise, accountId
            );
            if (updRes.empty()) {
                return {false, true, "Withdrawal failed; insufficient balance or concurrent modification.", currentBalance};
            }
            const std::int64_t newBalance = updRes[0][0].as<std::int64_t>();

            txn.exec_params(
                "INSERT INTO public.transactions "
                "(account_id, transaction_type, amount_paise, description) "
                "VALUES ($1, 'withdrawal', $2, 'Cash withdrawal');",
                accountId, amountPaise
            );

            txn.commit();
            lastVerifiedAt = std::chrono::steady_clock::now();
            return {true, false, "Cash withdrawal completed successfully.", newBalance};
        });
    } catch (const std::exception& e) {
        std::cerr << "withdrawFunds error: " << e.what() << std::endl;
        return {false, false, "Could not complete withdrawal transaction.", 0};
    }
}

std::vector<TransactionItem> Database::getTransactionHistory(std::int64_t accountId, int limit) {
    std::vector<TransactionItem> items;
    if (accountId <= 0) return items;
    if (limit <= 0) limit = 50;

    std::lock_guard<std::mutex> lock(dbMutex);
    if (!ensureConnected()) return items;

    try {
        return runWithReconnect([&]() {
            std::vector<TransactionItem> out;
            pqxx::nontransaction nt(*conn);
            pqxx::result res = nt.exec_params(
                "SELECT t.id, t.transaction_type, t.amount_paise, t.description, "
                "       TO_CHAR(t.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD HH24:MI:SS'), "
                "       rel.account_number "
                "FROM public.transactions t "
                "LEFT JOIN public.accounts rel ON t.related_account_id = rel.id "
                "WHERE t.account_id = $1 "
                "ORDER BY t.created_at DESC, t.id DESC "
                "LIMIT $2;",
                accountId, limit
            );
            lastVerifiedAt = std::chrono::steady_clock::now();

            for (const auto& row : res) {
                TransactionItem item;
                item.id = row[0].as<std::int64_t>();
                item.type = fieldAsString(row[1]);
                item.amountPaise = row[2].as<std::int64_t>();
                item.description = fieldAsString(row[3]);
                item.createdAt = fieldAsString(row[4]);
                item.relatedAccount = fieldAsString(row[5]);

                if (item.type == "deposit" || item.type == "transfer_in") {
                    item.direction = "credit";
                } else {
                    item.direction = "debit";
                }
                out.push_back(std::move(item));
            }
            return out;
        });
    } catch (const std::exception& e) {
        std::cerr << "getTransactionHistory error: " << e.what() << std::endl;
    }

    return items;
}
