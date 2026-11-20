
#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <cstddef>

struct Session {
    int userId;
    std::chrono::steady_clock::time_point lastActivity;
};

class SessionManager {
private:
    std::unordered_map<std::string, Session> sessions;
    std::mutex sessionsMutex;

    std::chrono::seconds sessionTimeout;

    std::string generateToken();

public:
    explicit SessionManager(
        std::chrono::seconds timeout = std::chrono::minutes(15)
    );

    std::string createSession(int userId);

    bool validateSession(
        const std::string& token,
        int& userId
    );

    bool invalidateSession(const std::string& token);

    void removeExpiredSessions();

    size_t getActiveSessionCount();
};

#endif
