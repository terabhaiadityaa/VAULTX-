/**
 * Database.cpp
 * SQLite database operations with atomic transaction consistency and thread safety.
 *
 * First-Year CSBS Viva Concept:
 * - Integer Paise: All balances are stored as 64-bit integers (paise) to prevent IEEE-754 float errors.
 * - ACID Transactions: Deposits, withdrawals, and transfers use BEGIN IMMEDIATE and COMMIT/ROLLBACK.
 * - Concurrency Protection: Thread safety is guaranteed via std::mutex dbMutex.
 */

#include "database/Database.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <limits>

namespace {

bool executeSql(sqlite3* db, const char* sql) {
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        std::cerr << "SQLite error: "
                  << (errorMessage ? errorMessage : sqlite3_errmsg(db))
                  << std::endl;
        sqlite3_free(errorMessage);
        return false;
    }
    return true;
}

void rollback(sqlite3* db) {
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
}

} // namespace

Database::Database() : db(nullptr) {}

Database::~Database() {
    disconnect();
}

bool Database::connect(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(dbMutex);
    const int result = sqlite3_open_v2(
        dbPath.c_str(), &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );
    if (result != SQLITE_OK) {
        std::cerr << "Database connection failed: "
                  << (db ? sqlite3_errmsg(db) : "Unable to allocate SQLite handle")
                  << std::endl;
        if (db) sqlite3_close(db);
        db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db, 5000);
    if (!executeSql(db, "PRAGMA foreign_keys = ON;")) return false;
    std::cout << "SQLite database connected successfully!" << std::endl;
    return true;
}

void Database::disconnect() {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db != nullptr) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Database::execute(const std::string& sql) {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db == nullptr) {
        std::cerr << "Database is not connected." << std::endl;
        return false;
    }
    return executeSql(db, sql.c_str());
}

bool Database::initializeSchema(const std::string& schemaPath) {
    std::ifstream schemaFile(schemaPath);
    if (!schemaFile.is_open()) {
        std::cerr << "Could not open schema file: " << schemaPath << std::endl;
        return false;
    }
    std::stringstream buffer;
    buffer << schemaFile.rdbuf();
    const std::string sql = buffer.str();
    if (sql.empty()) {
        std::cerr << "Schema file is empty." << std::endl;
        return false;
    }
    if (!execute(sql)) {
        std::cerr << "Failed to initialize database tables." << std::endl;
        return false;
    }
    std::cout << "Database tables initialized successfully!" << std::endl;
    return true;
}

sqlite3* Database::getConnection() {
    return db;
}

std::mutex& Database::getMutex() {
    return dbMutex;
}

std::string Database::getAccountNumberForUser(int userId) {
    if (userId <= 0) return "";
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return "";

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT account_number FROM accounts "
        "WHERE user_id = ? AND status = 'active' LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return "";

    sqlite3_bind_int(statement, 1, userId);
    std::string accountNumber;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* value = sqlite3_column_text(statement, 0);
        if (value) accountNumber = reinterpret_cast<const char*>(value);
    }
    sqlite3_finalize(statement);
    return accountNumber;
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
    if (!db) return {false, false, "Database is not connected.", 0};

    if (!executeSql(db, "BEGIN IMMEDIATE TRANSACTION;"))
        return {false, false, "Could not start transfer transaction.", 0};

    auto fail = [this](const std::string& message, bool clientError,
                       std::int64_t balance = 0) {
        rollback(db);
        return TransferResult{false, clientError, message, balance};
    };

    struct AccountInfo { sqlite3_int64 id; std::int64_t balance; };
    auto loadAccount = [this](const std::string& number, AccountInfo& info,
                              std::string& status) -> bool {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT id, balance_paise, status FROM accounts WHERE account_number = ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(stmt, 1, number.c_str(), -1, SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        if (step == SQLITE_ROW) {
            info.id = sqlite3_column_int64(stmt, 0);
            info.balance = sqlite3_column_int64(stmt, 1);
            const auto* text = sqlite3_column_text(stmt, 2);
            status = text ? reinterpret_cast<const char*>(text) : "";
        }
        sqlite3_finalize(stmt);
        return step == SQLITE_ROW;
    };

    AccountInfo sender{}, receiver{};
    std::string senderStatus, receiverStatus;
    if (!loadAccount(senderAcc, sender, senderStatus))
        return fail("Sender account was not found.", true);
    if (senderStatus != "active")
        return fail("Sender account is blocked or closed.", true, sender.balance);
    if (!loadAccount(receiverAcc, receiver, receiverStatus))
        return fail("Receiver account was not found.", true, sender.balance);
    if (receiverStatus != "active")
        return fail("Receiver account is blocked or closed.", true, sender.balance);
    if (sender.balance < amountPaise)
        return fail("Insufficient balance.", true, sender.balance);

    // Guard against integer overflow before crediting the receiver.
    if (receiver.balance > std::numeric_limits<std::int64_t>::max() - amountPaise)
        return fail("Receiver balance limit would be exceeded.", true, sender.balance);

    sqlite3_stmt* updateSender = nullptr;
    const char* debitSql =
        "UPDATE accounts SET balance_paise = balance_paise - ? "
        "WHERE id = ? AND status = 'active' AND balance_paise >= ?;";
    if (sqlite3_prepare_v2(db, debitSql, -1, &updateSender, nullptr) != SQLITE_OK)
        return fail("Could not prepare sender debit.", false, sender.balance);
    sqlite3_bind_int64(updateSender, 1, amountPaise);
    sqlite3_bind_int64(updateSender, 2, sender.id);
    sqlite3_bind_int64(updateSender, 3, amountPaise);
    int step = sqlite3_step(updateSender);
    const int changedSender = sqlite3_changes(db);
    sqlite3_finalize(updateSender);
    if (step != SQLITE_DONE || changedSender != 1)
        return fail("Sender debit failed; transfer was cancelled.", true, sender.balance);

    sqlite3_stmt* updateReceiver = nullptr;
    const char* creditSql =
        "UPDATE accounts SET balance_paise = balance_paise + ? "
        "WHERE id = ? AND status = 'active';";
    if (sqlite3_prepare_v2(db, creditSql, -1, &updateReceiver, nullptr) != SQLITE_OK)
        return fail("Could not prepare receiver credit.", false, sender.balance);
    sqlite3_bind_int64(updateReceiver, 1, amountPaise);
    sqlite3_bind_int64(updateReceiver, 2, receiver.id);
    step = sqlite3_step(updateReceiver);
    const int changedReceiver = sqlite3_changes(db);
    sqlite3_finalize(updateReceiver);
    if (step != SQLITE_DONE || changedReceiver != 1)
        return fail("Receiver credit failed; transfer was rolled back.", false, sender.balance);

    auto insertLedgerEntry = [this, amountPaise](sqlite3_int64 accountId,
            sqlite3_int64 relatedId, const char* type, const std::string& description) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO transactions "
            "(account_id, transaction_type, amount_paise, related_account_id, description) "
            "VALUES (?, ?, ?, ?, ?);";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int64(stmt, 1, accountId);
        sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, amountPaise);
        sqlite3_bind_int64(stmt, 4, relatedId);
        sqlite3_bind_text(stmt, 5, description.c_str(), -1, SQLITE_TRANSIENT);
        const int result = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return result == SQLITE_DONE;
    };

    const std::string outDescription = "Transfer to account " + receiverAcc;
    const std::string inDescription = "Transfer from account " + senderAcc;
    if (!insertLedgerEntry(sender.id, receiver.id, "transfer_out", outDescription) ||
        !insertLedgerEntry(receiver.id, sender.id, "transfer_in", inDescription)) {
        return fail("Could not record transfer; all changes were rolled back.", false,
                    sender.balance);
    }

    if (!executeSql(db, "COMMIT;"))
        return fail("Could not commit transfer; transaction was rolled back.", false,
                    sender.balance);

    return {true, false, "Transfer completed successfully.",
            sender.balance - amountPaise};
}

AccountDetails Database::getAccountDetailsForUser(int userId) {
    AccountDetails details;
    if (userId <= 0) return details;
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return details;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT u.id, u.full_name, u.email, a.id, a.account_number, a.balance_paise, a.status, a.created_at "
        "FROM users u "
        "JOIN accounts a ON u.id = a.user_id "
        "WHERE u.id = ? AND a.status = 'active' LIMIT 1;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return details;

    sqlite3_bind_int(stmt, 1, userId);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        details.found = true;
        details.userId = sqlite3_column_int(stmt, 0);
        const auto* fn = sqlite3_column_text(stmt, 1);
        details.fullName = fn ? reinterpret_cast<const char*>(fn) : "";
        const auto* em = sqlite3_column_text(stmt, 2);
        details.email = em ? reinterpret_cast<const char*>(em) : "";
        details.accountId = sqlite3_column_int64(stmt, 3);
        const auto* an = sqlite3_column_text(stmt, 4);
        details.accountNumber = an ? reinterpret_cast<const char*>(an) : "";
        details.balancePaise = sqlite3_column_int64(stmt, 5);
        const auto* st = sqlite3_column_text(stmt, 6);
        details.status = st ? reinterpret_cast<const char*>(st) : "";
        const auto* ca = sqlite3_column_text(stmt, 7);
        details.createdAt = ca ? reinterpret_cast<const char*>(ca) : "";
    }
    sqlite3_finalize(stmt);
    return details;
}

OperationResult Database::depositFunds(const std::string& accountNumber, std::int64_t amountPaise) {
    if (accountNumber.empty())
        return {false, true, "Account number is required.", 0};
    if (amountPaise <= 0)
        return {false, true, "Deposit amount must be greater than zero.", 0};

    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return {false, false, "Database is not connected.", 0};

    if (!executeSql(db, "BEGIN IMMEDIATE TRANSACTION;"))
        return {false, false, "Could not start deposit transaction.", 0};

    auto fail = [this](const std::string& message, bool clientError, std::int64_t balance = 0) {
        rollback(db);
        return OperationResult{false, clientError, message, balance};
    };

    sqlite3_stmt* selectStmt = nullptr;
    const char* selectSql = "SELECT id, balance_paise, status FROM accounts WHERE account_number = ?;";
    if (sqlite3_prepare_v2(db, selectSql, -1, &selectStmt, nullptr) != SQLITE_OK)
        return fail("Could not query account.", false);

    sqlite3_bind_text(selectStmt, 1, accountNumber.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_int64 accountId = 0;
    std::int64_t currentBalance = 0;
    std::string status;
    bool exists = false;
    if (sqlite3_step(selectStmt) == SQLITE_ROW) {
        exists = true;
        accountId = sqlite3_column_int64(selectStmt, 0);
        currentBalance = sqlite3_column_int64(selectStmt, 1);
        const auto* st = sqlite3_column_text(selectStmt, 2);
        status = st ? reinterpret_cast<const char*>(st) : "";
    }
    sqlite3_finalize(selectStmt);

    if (!exists) return fail("Account not found.", true);
    if (status != "active") return fail("Account is not active.", true, currentBalance);
    if (currentBalance > std::numeric_limits<std::int64_t>::max() - amountPaise)
        return fail("Balance limit would be exceeded.", true, currentBalance);

    sqlite3_stmt* updateStmt = nullptr;
    const char* updateSql = "UPDATE accounts SET balance_paise = balance_paise + ? WHERE id = ? AND status = 'active';";
    if (sqlite3_prepare_v2(db, updateSql, -1, &updateStmt, nullptr) != SQLITE_OK)
        return fail("Could not prepare deposit update.", false, currentBalance);

    sqlite3_bind_int64(updateStmt, 1, amountPaise);
    sqlite3_bind_int64(updateStmt, 2, accountId);
    int step = sqlite3_step(updateStmt);
    const int changed = sqlite3_changes(db);
    sqlite3_finalize(updateStmt);
    if (step != SQLITE_DONE || changed != 1)
        return fail("Deposit could not be applied.", false, currentBalance);

    sqlite3_stmt* txStmt = nullptr;
    const char* txSql = "INSERT INTO transactions (account_id, transaction_type, amount_paise, description) VALUES (?, 'deposit', ?, 'Cash deposit');";
    if (sqlite3_prepare_v2(db, txSql, -1, &txStmt, nullptr) != SQLITE_OK)
        return fail("Could not prepare transaction log.", false, currentBalance);

    sqlite3_bind_int64(txStmt, 1, accountId);
    sqlite3_bind_int64(txStmt, 2, amountPaise);
    int txStep = sqlite3_step(txStmt);
    sqlite3_finalize(txStmt);
    if (txStep != SQLITE_DONE)
        return fail("Could not log deposit transaction.", false, currentBalance);

    if (!executeSql(db, "COMMIT;"))
        return fail("Could not commit deposit transaction.", false, currentBalance);

    return {true, false, "Cash deposit completed successfully.", currentBalance + amountPaise};
}

OperationResult Database::withdrawFunds(const std::string& accountNumber, std::int64_t amountPaise) {
    if (accountNumber.empty())
        return {false, true, "Account number is required.", 0};
    if (amountPaise <= 0)
        return {false, true, "Withdrawal amount must be greater than zero.", 0};

    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return {false, false, "Database is not connected.", 0};

    if (!executeSql(db, "BEGIN IMMEDIATE TRANSACTION;"))
        return {false, false, "Could not start withdrawal transaction.", 0};

    auto fail = [this](const std::string& message, bool clientError, std::int64_t balance = 0) {
        rollback(db);
        return OperationResult{false, clientError, message, balance};
    };

    sqlite3_stmt* selectStmt = nullptr;
    const char* selectSql = "SELECT id, balance_paise, status FROM accounts WHERE account_number = ?;";
    if (sqlite3_prepare_v2(db, selectSql, -1, &selectStmt, nullptr) != SQLITE_OK)
        return fail("Could not query account.", false);

    sqlite3_bind_text(selectStmt, 1, accountNumber.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_int64 accountId = 0;
    std::int64_t currentBalance = 0;
    std::string status;
    bool exists = false;
    if (sqlite3_step(selectStmt) == SQLITE_ROW) {
        exists = true;
        accountId = sqlite3_column_int64(selectStmt, 0);
        currentBalance = sqlite3_column_int64(selectStmt, 1);
        const auto* st = sqlite3_column_text(selectStmt, 2);
        status = st ? reinterpret_cast<const char*>(st) : "";
    }
    sqlite3_finalize(selectStmt);

    if (!exists) return fail("Account not found.", true);
    if (status != "active") return fail("Account is not active.", true, currentBalance);
    if (currentBalance < amountPaise)
        return fail("Insufficient funds for withdrawal.", true, currentBalance);

    sqlite3_stmt* updateStmt = nullptr;
    const char* updateSql = "UPDATE accounts SET balance_paise = balance_paise - ? WHERE id = ? AND status = 'active' AND balance_paise >= ?;";
    if (sqlite3_prepare_v2(db, updateSql, -1, &updateStmt, nullptr) != SQLITE_OK)
        return fail("Could not prepare withdrawal update.", false, currentBalance);

    sqlite3_bind_int64(updateStmt, 1, amountPaise);
    sqlite3_bind_int64(updateStmt, 2, accountId);
    sqlite3_bind_int64(updateStmt, 3, amountPaise);
    int step = sqlite3_step(updateStmt);
    const int changed = sqlite3_changes(db);
    sqlite3_finalize(updateStmt);
    if (step != SQLITE_DONE || changed != 1)
        return fail("Withdrawal failed; insufficient balance or concurrent modification.", true, currentBalance);

    sqlite3_stmt* txStmt = nullptr;
    const char* txSql = "INSERT INTO transactions (account_id, transaction_type, amount_paise, description) VALUES (?, 'withdrawal', ?, 'Cash withdrawal');";
    if (sqlite3_prepare_v2(db, txSql, -1, &txStmt, nullptr) != SQLITE_OK)
        return fail("Could not prepare transaction log.", false, currentBalance);

    sqlite3_bind_int64(txStmt, 1, accountId);
    sqlite3_bind_int64(txStmt, 2, amountPaise);
    int txStep = sqlite3_step(txStmt);
    sqlite3_finalize(txStmt);
    if (txStep != SQLITE_DONE)
        return fail("Could not log withdrawal transaction.", false, currentBalance);

    if (!executeSql(db, "COMMIT;"))
        return fail("Could not commit withdrawal transaction.", false, currentBalance);

    return {true, false, "Cash withdrawal completed successfully.", currentBalance - amountPaise};
}

std::vector<TransactionItem> Database::getTransactionHistory(sqlite3_int64 accountId, int limit) {
    std::vector<TransactionItem> items;
    if (accountId <= 0) return items;
    if (limit <= 0) limit = 50;

    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db) return items;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT t.id, t.transaction_type, t.amount_paise, t.description, t.created_at, "
        "       rel.account_number "
        "FROM transactions t "
        "LEFT JOIN accounts rel ON t.related_account_id = rel.id "
        "WHERE t.account_id = ? "
        "ORDER BY t.created_at DESC, t.id DESC "
        "LIMIT ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return items;

    sqlite3_bind_int64(stmt, 1, accountId);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TransactionItem item;
        item.id = sqlite3_column_int64(stmt, 0);
        const auto* ty = sqlite3_column_text(stmt, 1);
        item.type = ty ? reinterpret_cast<const char*>(ty) : "";
        item.amountPaise = sqlite3_column_int64(stmt, 2);
        const auto* desc = sqlite3_column_text(stmt, 3);
        item.description = desc ? reinterpret_cast<const char*>(desc) : "";
        const auto* ca = sqlite3_column_text(stmt, 4);
        item.createdAt = ca ? reinterpret_cast<const char*>(ca) : "";
        const auto* rel = sqlite3_column_text(stmt, 5);
        item.relatedAccount = rel ? reinterpret_cast<const char*>(rel) : "";

        if (item.type == "deposit" || item.type == "transfer_in") {
            item.direction = "credit";
        } else {
            item.direction = "debit";
        }

        items.push_back(item);
    }
    sqlite3_finalize(stmt);
    return items;
}
