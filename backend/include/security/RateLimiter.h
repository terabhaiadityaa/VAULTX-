/**
 * RateLimiter.h
 * Lightweight, thread-safe in-memory rate limiter for IP request throttling.
 * Designed for first-year CSBS students: simple unordered_map + mutex + time window.
 */

#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

class RateLimiter {
private:
    struct ClientRecord {
        int count = 0;
        std::chrono::steady_clock::time_point windowStart;
    };

    std::unordered_map<std::string, ClientRecord> records;
    std::mutex limiterMutex;

    int maxRequests;
    std::chrono::seconds windowDuration;

public:
    // Default: 120 requests per 60 seconds
    explicit RateLimiter(
        int maxRequests = 120,
        std::chrono::seconds window = std::chrono::seconds(60)
    );

    // Returns true if the request is permitted, false if rate limit is exceeded
    bool allow(const std::string& clientIp);

    // Clean up expired records to prevent unbounded memory growth
    void cleanup();
};

#endif // RATE_LIMITER_H
