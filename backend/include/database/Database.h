#ifndef DATABASE_H
#define DATABASE_H

#include <pqxx/pqxx>
#include <chrono>
#include <cstdint>
#include <memory>
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
    std::int64_t accountId = 0;
    std::string fullName;
    std::string email;
    std::string accountNumber;
    std::int64_t balancePaise = 0;
    std::string status;
    std::string createdAt;
};

struct TransactionItem {
    std::int64_t id = 0;
    std::string type;
    std::string direction; // "credit" or "debit"
    std::int64_t amountPaise = 0;
    std::string relatedAccount;
    std::string description;
    std::string createdAt;
};

struct RegistrationResult {
    bool success = false;
    bool duplicateEmail = false;
    bool duplicateAccount = false;
    std::string message;
    int userId = -1;
};

struct UserAuthRecord {
    bool found = false;
    int userId = -1;
    std::string email;
    std::string pinHash;
};

struct AccountLoginStatus {
    bool found = false;
    std::string accountNumber;
    std::string status;
};

class Database {
private:
    std::string connectionString;
    std::unique_ptr<pqxx::connection> conn;
    std::chrono::steady_clock::time_point lastVerifiedAt{};
    std::mutex dbMutex;

    // Must be called with dbMutex locked. Reconnects cleanly if Supabase dropped an idle socket.
    bool ensureConnected(bool forceVerify = false);

    // Runs an operation under dbMutex and retries once on a transient dropped PostgreSQL socket.
    template <typename Fn>
    auto runWithReconnect(Fn&& fn) -> decltype(fn());

public:
    Database();
    virtual ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool connect(const std::string& connUrl);
    void disconnect();
    bool isConnected();

    // Non-destructively verifies that required production tables exist in public schema.
    bool verifySchema();

    std::mutex& getMutex();

    // Checks whether an account number already exists.
    bool accountExists(const std::string& accountNumber);

    // Atomically creates a user, active account, and welcome deposit ledger entry.
    RegistrationResult createCardAccount(
        const std::string& fullName,
        const std::string& email,
        const std::string& pinHash,
        const std::string& accountNumber,
        std::int64_t initialBalancePaise,
        const std::string& depositDescription
    );

    // Brute-force login attempt helpers (virtual to allow isolated unit testing).
    virtual std::int64_t getLoginLockUntil(const std::string& identifier);
    virtual void clearLoginAttempts(const std::string& identifier);
    virtual void reconcileLegacyLoginAttempts(
        const std::string& legacyIdentifier,
        const std::string& canonicalEmail
    );
    virtual bool recordFailedLoginAttempt(
        const std::string& identifier,
        int maxAttempts,
        std::int64_t lockUntilEpoch,
        std::int64_t currentEpoch
    );
    virtual UserAuthRecord findUserForLogin(const std::string& identifier);
    virtual AccountLoginStatus findAccountForUser(int userId);

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
        std::int64_t accountId,
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
