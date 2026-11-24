/**
 * RateLimiter.cpp
 * Implementation of sliding window in-memory rate limiter.
 */

#include "security/RateLimiter.h"

RateLimiter::RateLimiter(int maxRequests, std::chrono::seconds window)
    : maxRequests(maxRequests), windowDuration(window) {
    if (this->maxRequests <= 0) this->maxRequests = 60;
    if (this->windowDuration <= std::chrono::seconds::zero()) {
        this->windowDuration = std::chrono::seconds(60);
    }
}

bool RateLimiter::allow(const std::string& clientIp) {
    if (clientIp.empty()) return true;

    std::lock_guard<std::mutex> lock(limiterMutex);
    auto now = std::chrono::steady_clock::now();

    auto it = records.find(clientIp);
    if (it == records.end()) {
        // First request from this IP
        records[clientIp] = {1, now};
        return true;
    }

    // Check if the current time window has elapsed
    if (now - it->second.windowStart >= windowDuration) {
        // Reset window
        it->second.count = 1;
        it->second.windowStart = now;
        return true;
    }

    // Still in same window: check count
    if (it->second.count < maxRequests) {
        it->second.count++;
        return true;
    }

    // Rate limit exceeded
    return false;
}

void RateLimiter::cleanup() {
    std::lock_guard<std::mutex> lock(limiterMutex);
    auto now = std::chrono::steady_clock::now();

    for (auto it = records.begin(); it != records.end();) {
        if (now - it->second.windowStart >= windowDuration * 2) {
            it = records.erase(it);
        } else {
            ++it;
        }
    }
}
