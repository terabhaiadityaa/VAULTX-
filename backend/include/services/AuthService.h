
#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <string>

#include "database/Database.h"

struct AuthResult {
    bool success;
    std::string message;
    int userId;
    std::string accountNumber;
};

class AuthService {
private:
    Database& database;

    std::string generateAccountNumber();

    static constexpr int MAX_FAILED_ATTEMPTS = 5;
    static constexpr int LOCKOUT_DURATION_MINUTES = 15;

public:
    explicit AuthService(Database& database);

    AuthResult registerUser(
        const std::string& fullName,
        const std::string& email,
        const std::string& pin
    );

    AuthResult loginUser(
        const std::string& email,
        const std::string& pin
    );
};

#endif
