#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <array>
#include <cstddef>
#include <mutex>
#include <string>

#include "database/Database.h"

struct AuthResult {
    bool success = false;
    bool invalidInput = false;
    std::string message;
    int userId = -1;
    std::string accountNumber;
    std::string mobileNumber;
};

class AuthService {
private:
    Database& database;

    std::string generateAccountNumber();

    static constexpr int MAX_FAILED_ATTEMPTS = 5;
    static constexpr int LOCKOUT_DURATION_MINUTES = 15;
    static constexpr size_t LOCK_STRIPES = 64;

    // Striped per-identity mutexes prevent concurrent login TOCTOU races without holding
    // the global Database mutex across PBKDF2 computation. Lock order: stripe -> dbMutex.
    mutable std::array<std::mutex, LOCK_STRIPES> identityStripes;

public:
    explicit AuthService(Database& database);

    AuthResult registerUser(
        const std::string& fullName,
        const std::string& countryCode,
        const std::string& mobileNumber,
        const std::string& email,
        const std::string& pin,
        const std::string& confirmPin = "",
        bool enforceConfirmPin = false
    );

    AuthResult loginUser(
        const std::string& countryCode,
        const std::string& mobileNumber,
        const std::string& pin
    );

    AuthResult linkMobileToLegacyUser(
        const std::string& accountOrEmail,
        const std::string& pin,
        const std::string& countryCode,
        const std::string& mobileNumber
    );
};

#endif
