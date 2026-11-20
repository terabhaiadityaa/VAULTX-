#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct TransferResult {
    bool success = false;
    bool clientError = false;
    std::string message;
    std::int64_t senderBalancePaise = 0;
};

struct OperationResult {
    bool success = false;
    bool clientError = false;
    std::string message;
    std::int64_t balancePaise = 0;
};

struct AccountDetails {
    bool found = false;
    int userId = -1;
    sqlite3_int64 accountId = 0;
    std::string fullName;
    std::string email;
    std::string accountNumber;
    std::int64_t balancePaise = 0;
    std::string status;
    std::string createdAt;
};

struct TransactionItem {
    sqlite3_int64 id = 0;
    std::string type;
    std::string direction; // "credit" or "debit"
    std::int64_t amountPaise = 0;
    std::string relatedAccount;
    std::string description;
    std::string createdAt;
};

class Database {
private:
    sqlite3* db;
    std::mutex dbMutex;

public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool connect(const std::string& dbPath);
    void disconnect();

    bool execute(const std::string& sql);
    bool initializeSchema(const std::string& schemaPath);

    sqlite3* getConnection();
    std::mutex& getMutex();

    // Returns the active account number belonging to this authenticated user.
    std::string getAccountNumberForUser(int userId);

    // Returns full account details for an authenticated user.
    AccountDetails getAccountDetailsForUser(int userId);

    // Deposit funds atomically into an active account.
    OperationResult depositFunds(
        const std::string& accountNumber,
        std::int64_t amountPaise
    );

    // Withdraw funds atomically from an active account with balance check.
    OperationResult withdrawFunds(
        const std::string& accountNumber,
        std::int64_t amountPaise
    );

    // Retrieve recent transaction history for an account.
    std::vector<TransactionItem> getTransactionHistory(
        sqlite3_int64 accountId,
        int limit = 50
    );

    // amountPaise is an integer number of paise, never a floating-point rupee value.
    TransferResult transferFunds(
        const std::string& senderAcc,
        const std::string& receiverAcc,
        std::int64_t amountPaise
    );
};

#endif
