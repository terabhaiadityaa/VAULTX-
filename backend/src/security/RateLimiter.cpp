/**
 * RateLimiter.cpp
 * Thread-safe, memory-bounded sliding-window rate limiter with trusted-proxy resolution.
 */

#include "security/RateLimiter.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <algorithm>
#include <cctype>

namespace {

std::string trimCopy(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

RateLimiter::RateLimiter(int maxRequests, std::chrono::seconds window, size_t maxEntries)
    : maxRequests(maxRequests), windowDuration(window), maxEntries(maxEntries) {
    if (this->maxRequests <= 0) this->maxRequests = 60;
    if (this->windowDuration <= std::chrono::seconds::zero()) {
        this->windowDuration = std::chrono::seconds(60);
    }
    if (this->maxEntries == 0) {
        this->maxEntries = 1;
    }
}

void RateLimiter::cleanupLocked(std::chrono::steady_clock::time_point now) {
    for (auto it = records.begin(); it != records.end();) {
        if (now - it->second.windowStart >= windowDuration) {
            it = records.erase(it);
        } else {
            ++it;
        }
    }
}

void RateLimiter::evictOldestLocked() {
    if (records.empty()) return;
    auto oldest = records.begin();
    for (auto it = records.begin(); it != records.end(); ++it) {
        if (it->second.windowStart < oldest->second.windowStart) {
            oldest = it;
        }
    }
    records.erase(oldest);
}

bool RateLimiter::allow(const std::string& clientIp) {
    if (clientIp.empty()) return true;

    std::lock_guard<std::mutex> lock(limiterMutex);
    const auto now = std::chrono::steady_clock::now();

    callCounter++;
    if (callCounter >= 64 || records.size() >= maxEntries) {
        cleanupLocked(now);
        callCounter = 0;
    }

    auto it = records.find(clientIp);
    if (it == records.end()) {
        if (records.size() >= maxEntries) {
            evictOldestLocked();
        }
        records[clientIp] = {1, now};
        return true;
    }

    // Check if the current time window has elapsed
    if (now - it->second.windowStart >= windowDuration) {
        it->second.count = 1;
        it->second.windowStart = now;
        return true;
    }

    // Still in same window: check count
    if (it->second.count < maxRequests) {
        it->second.count++;
        return true;
    }

    return false;
}

void RateLimiter::cleanup() {
    std::lock_guard<std::mutex> lock(limiterMutex);
    cleanupLocked(std::chrono::steady_clock::now());
}

size_t RateLimiter::getTrackedCount() {
    std::lock_guard<std::mutex> lock(limiterMutex);
    return records.size();
}

TrustedProxyMode RateLimiter::parseTrustedProxyMode(const char* envVal) {
    if (!envVal) return TrustedProxyMode::NONE;
    const std::string mode = toLowerCopy(trimCopy(envVal));
    if (mode == "render" || mode == "rightmost_xff") {
        return TrustedProxyMode::RENDER_RIGHTMOST_XFF;
    }
    if (mode == "cloudflare") {
        return TrustedProxyMode::CLOUDFLARE_STRICT;
    }
    return TrustedProxyMode::NONE;
}

bool RateLimiter::isValidIpAddress(const std::string& ip) {
    // Max IPv6 string length (including IPv4-mapped form) is 45 characters; reject embedded NULs
    if (ip.empty() || ip.size() > 45 || ip.find('\0') != std::string::npos) {
        return false;
    }

    struct in_addr addr4 {};
    struct in6_addr addr6 {};
    return inet_pton(AF_INET, ip.c_str(), &addr4) == 1
        || inet_pton(AF_INET6, ip.c_str(), &addr6) == 1;
}

std::string RateLimiter::resolveClientIp(
    const std::string& remoteAddr,
    const std::string& cfConnectingIp,
    const std::string& xForwardedFor,
    TrustedProxyMode mode
) {
    if (mode == TrustedProxyMode::RENDER_RIGHTMOST_XFF) {
        const std::string xff = trimCopy(xForwardedFor);
        if (!xff.empty()) {
            const size_t lastComma = xff.rfind(',');
            const std::string rightmost = trimCopy(
                lastComma == std::string::npos ? xff : xff.substr(lastComma + 1)
            );
            if (isValidIpAddress(rightmost)) {
                return rightmost;
            }
        }
        return remoteAddr;
    }

    if (mode == TrustedProxyMode::CLOUDFLARE_STRICT) {
        const std::string cf = trimCopy(cfConnectingIp);
        if (isValidIpAddress(cf)) {
            return cf;
        }
        return remoteAddr;
    }

    // Default (NONE): ignore all client-spoofable forwarding headers
    return remoteAddr;
}
