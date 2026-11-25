/**
 * RateLimiter.h
 * Thread-safe, memory-bounded sliding-window rate limiter with trusted-proxy IP resolution.
 */

#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

enum class TrustedProxyMode {
    NONE,                // Default: ignore proxy headers, use socket remote_addr
    RENDER_RIGHTMOST_XFF,// Render ingress: take rightmost IP in X-Forwarded-For
    CLOUDFLARE_STRICT    // Explicit Cloudflare proxy: take CF-Connecting-IP
};

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
    size_t maxEntries;
    size_t callCounter = 0;

    void cleanupLocked(std::chrono::steady_clock::time_point now);
    void evictOldestLocked();

public:
    // Default: 120 requests per 60 seconds, capped at 10,000 tracked IP records
    explicit RateLimiter(
        int maxRequests = 120,
        std::chrono::seconds window = std::chrono::seconds(60),
        size_t maxEntries = 10000
    );

    // Returns true if the request is permitted, false if rate limit is exceeded
    bool allow(const std::string& clientIp);

    // Thread-safe cleanup of expired records to prevent unbounded memory growth
    void cleanup();

    // Thread-safe count of currently tracked IP entries
    size_t getTrackedCount();

    // Parses TRUST_PROXY env value into TrustedProxyMode (defaults to NONE)
    static TrustedProxyMode parseTrustedProxyMode(const char* envVal);

    // Validates strict IPv4/IPv6 syntax using inet_pton()
    static bool isValidIpAddress(const std::string& ip);

    // Resolves client IP safely according to the configured TrustedProxyMode
    static std::string resolveClientIp(
        const std::string& remoteAddr,
        const std::string& cfConnectingIp,
        const std::string& xForwardedFor,
        TrustedProxyMode mode
    );
};

#endif // RATE_LIMITER_H
