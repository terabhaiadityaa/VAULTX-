#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

struct Session {
    int userId;
    std::chrono::steady_clock::time_point lastActivity;
};

class SessionManager {
private:
    std::unordered_map<std::string, Session> sessions;
    std::mutex sessionsMutex;

    std::chrono::seconds sessionTimeout;
    size_t maxSessions;

    std::string generateToken();
    void removeExpiredSessionsLocked(std::chrono::steady_clock::time_point now);
    void evictOldestLocked();

public:
    explicit SessionManager(
        std::chrono::seconds timeout = std::chrono::minutes(15),
        size_t maxSessions = 10000
    );

    std::string createSession(int userId);

    bool validateSession(
        const std::string& token,
        int& userId
    );

    bool invalidateSession(const std::string& token);

    void removeExpiredSessions();

    size_t getActiveSessionCount();

    // Thread-safe count of entries currently stored in the internal map (for cleanup verification)
    size_t getStoredSessionCount();
};

#endif
