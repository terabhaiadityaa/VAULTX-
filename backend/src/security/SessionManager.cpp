/**
 * SessionManager.cpp
 * In-memory thread-safe, memory-bounded session token management with sliding expiration.
 */

#include "security/SessionManager.h"

#include <openssl/rand.h>

#include <array>
#include <iomanip>
#include <sstream>

SessionManager::SessionManager(std::chrono::seconds timeout, size_t maxSessions)
    : sessionTimeout(timeout), maxSessions(maxSessions) {
    if (sessionTimeout <= std::chrono::seconds::zero()) {
        sessionTimeout = std::chrono::minutes(15);
    }
    if (this->maxSessions == 0) {
        this->maxSessions = 1;
    }
}

std::string SessionManager::generateToken() {
    std::array<unsigned char, 32> randomBytes{};

    if (RAND_bytes(
            randomBytes.data(),
            static_cast<int>(randomBytes.size())
        ) != 1) {
        return "";
    }

    std::ostringstream token;
    token << std::hex << std::setfill('0');

    for (unsigned char byte : randomBytes) {
        token << std::setw(2)
              << static_cast<int>(byte);
    }

    return token.str();
}

void SessionManager::removeExpiredSessionsLocked(std::chrono::steady_clock::time_point now) {
    for (auto it = sessions.begin(); it != sessions.end();) {
        if (now - it->second.lastActivity >= sessionTimeout) {
            it = sessions.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionManager::evictOldestLocked() {
    if (sessions.empty()) return;
    auto oldest = sessions.begin();
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->second.lastActivity < oldest->second.lastActivity) {
            oldest = it;
        }
    }
    sessions.erase(oldest);
}

std::string SessionManager::createSession(int userId) {
    if (userId <= 0) {
        return "";
    }

    std::lock_guard<std::mutex> lock(sessionsMutex);
    const auto now = std::chrono::steady_clock::now();

    // Proactively prune expired sessions before inserting
    removeExpiredSessionsLocked(now);
    if (sessions.size() >= maxSessions) {
        evictOldestLocked();
    }

    std::string token;

    do {
        token = generateToken();

        if (token.empty()) {
            return "";
        }
    } while (sessions.find(token) != sessions.end());

    sessions[token] = {userId, now};

    return token;
}

bool SessionManager::validateSession(
    const std::string& token,
    int& userId
) {
    if (token.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(sessionsMutex);

    auto it = sessions.find(token);

    if (it == sessions.end()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();

    if (now - it->second.lastActivity >= sessionTimeout) {
        sessions.erase(it);
        return false;
    }

    it->second.lastActivity = now;
    userId = it->second.userId;

    return true;
}

bool SessionManager::invalidateSession(
    const std::string& token
) {
    std::lock_guard<std::mutex> lock(sessionsMutex);

    return sessions.erase(token) > 0;
}

void SessionManager::removeExpiredSessions() {
    std::lock_guard<std::mutex> lock(sessionsMutex);
    removeExpiredSessionsLocked(std::chrono::steady_clock::now());
}

size_t SessionManager::getActiveSessionCount() {
    std::lock_guard<std::mutex> lock(sessionsMutex);

    auto now = std::chrono::steady_clock::now();
    size_t count = 0;

    for (const auto& entry : sessions) {
        if (now - entry.second.lastActivity < sessionTimeout) {
            count++;
        }
    }

    return count;
}

size_t SessionManager::getStoredSessionCount() {
    std::lock_guard<std::mutex> lock(sessionsMutex);
    return sessions.size();
}
