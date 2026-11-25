/**
 * main.cpp
 * HTTP REST Server entry point for the VAULTX Banking Simulation Platform.
 *
 * First-Year CSBS Viva Concept:
 * - Embedded Server: Uses cpp-httplib to serve static HTML/CSS/JS and JSON REST API endpoints on port 8080.
 * - Architecture: Controller handles HTTP requests, parses JSON, and delegates business logic to Database/AuthService.
 * - Security: Pre-routing rate limiting (60s sliding window), security headers, payload size limit, Bearer auth.
 */

#include <iostream>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <limits>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <thread>
#include <vector>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "database/Database.h"
#include "services/AuthService.h"
#include "security/SessionManager.h"
#include "security/PinHasher.h"
#include "security/RateLimiter.h"

using json = nlohmann::json;

namespace {

std::atomic<bool> gShutdownRequested{false};
volatile std::sig_atomic_t gSignalReceived = 0;

void handleShutdownSignal(int sig) {
    gSignalReceived = sig;
}

std::string extractBearerToken(const httplib::Request& req) {
    std::string token = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    if (token.rfind(prefix, 0) == 0) {
        return token.substr(prefix.length());
    }
    return "";
}

std::string trimString(const std::string& input) {
    size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

bool isEnvFlagTrue(const char* val) {
    if (!val) return false;
    std::string cleaned = trimString(val);
    if (cleaned.empty()) return false;
    std::transform(cleaned.begin(), cleaned.end(), cleaned.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return (cleaned == "true" || cleaned == "1" || cleaned == "yes");
}

std::vector<std::string> parseAllowedOrigins(const char* envVal) {
    std::vector<std::string> origins;
    if (envVal) {
        std::stringstream ss(envVal);
        std::string item;
        while (std::getline(ss, item, ',')) {
            std::string trimmed = trimString(item);
            while (trimmed.size() > 1 && trimmed.back() == '/') {
                trimmed.pop_back();
            }
            if (!trimmed.empty()) {
                origins.push_back(trimmed);
            }
        }
    }
    if (origins.empty()) {
        origins.emplace_back("https://vaultx-five-rosy.vercel.app");
    }
    return origins;
}

bool applyCorsHeaders(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& allowedOrigins
) {
    res.set_header("Vary", "Origin");
    if (!req.has_header("Origin")) {
        return true; // Same-origin / non-browser client
    }

    const std::string origin = trimString(req.get_header_value("Origin"));
    for (const auto& allowed : allowedOrigins) {
        if (allowed == "*" || origin == allowed) {
            res.set_header("Access-Control-Allow-Origin", allowed == "*" ? "*" : origin);
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            return true;
        }
    }
    return false;
}

void applySecurityHeaders(httplib::Response& res) {
    res.set_header(
        "Content-Security-Policy",
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
        "img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; "
        "base-uri 'self'; form-action 'self'"
    );
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
    res.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
}

std::string getClientIp(const httplib::Request& req, TrustedProxyMode proxyMode) {
    const std::string cfIp = req.has_header("CF-Connecting-IP") ? req.get_header_value("CF-Connecting-IP") : "";
    const std::string xff = req.has_header("X-Forwarded-For") ? req.get_header_value("X-Forwarded-For") : "";
    return RateLimiter::resolveClientIp(req.remote_addr, cfIp, xff, proxyMode);
}

void sendJson(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_header("Content-Type", "application/json");
    res.set_header("Cache-Control", "no-store, no-cache, must-revalidate");
    applySecurityHeaders(res);
    res.set_content(body.dump(4), "application/json");
}

bool parseRupeesToPaise(
    const json& request,
    std::int64_t& outPaise,
    std::string& errorMessage,
    long double maxRupees = 10000000.0L
) {
    if (!request.is_object() || !request.contains("amount") || !request["amount"].is_number()) {
        errorMessage = "Amount is required and must be a number.";
        return false;
    }

    const long double amountRupees = request["amount"].get<long double>();
    if (!std::isfinite(amountRupees) || amountRupees <= 0.0L) {
        errorMessage = "Amount must be a positive number.";
        return false;
    }

    if (amountRupees > maxRupees) {
        errorMessage = "Amount exceeds maximum allowable transaction limit.";
        return false;
    }

    const long double paiseValue = std::round(amountRupees * 100.0L);
    if (std::fabs(paiseValue - amountRupees * 100.0L) > 0.000001L) {
        errorMessage = "Amount cannot have more than two decimal places.";
        return false;
    }

    outPaise = static_cast<std::int64_t>(paiseValue);
    return true;
}

void seedDemoAccounts(Database& database) {
    struct DemoCard {
        std::string name;
        std::string email;
        std::string pin;
        std::string accountNum;
        std::int64_t balancePaise;
        std::string tier;
    };

    std::vector<DemoCard> demoCards = {
        {"Aditya Sharma", "aditya@vaultx.sim", "123456", "100020003000", 7500000, "Platinum VIP"},
        {"Priya Patel",   "priya@vaultx.sim",  "654321", "100020004000", 4250000, "Gold Premium"},
        {"Rahul Verma",   "rahul@vaultx.sim",  "112233", "100020005000", 15000000, "Business Executive"},
        {"Simran Kaur",   "simran@vaultx.sim", "998877", "100020006000", 1800000, "Student Advantage"}
    };

    for (const auto& card : demoCards) {
        // Non-destructive: skip if this demo account already exists in PostgreSQL
        if (database.accountExists(card.accountNum)) {
            continue;
        }

        std::string hashedPin = PinHasher::hashPin(card.pin);
        if (hashedPin.empty()) continue;

        std::string desc = "Initial simulated balance allocation (" + card.tier + ")";
        database.createCardAccount(
            card.name,
            card.email,
            hashedPin,
            card.accountNum,
            card.balancePaise,
            desc
        );
    }
}

} // namespace

int main() {
    // Read PostgreSQL connection URI from environment variable (never hardcoded)
    const char* envDbUrl = std::getenv("DATABASE_URL");
    if (!envDbUrl || std::string(envDbUrl).empty()) {
        envDbUrl = std::getenv("SUPABASE_DB_URL");
    }
    if (!envDbUrl || std::string(envDbUrl).empty()) {
        std::cerr << "[ERROR] DATABASE_URL environment variable is not set." << std::endl;
        std::cerr << "        Set DATABASE_URL to your Supabase PostgreSQL URI and restart." << std::endl;
        return 1;
    }

    Database database;
    if (!database.connect(envDbUrl)) {
        std::cerr << "Could not connect to PostgreSQL (Supabase) database." << std::endl;
        return 1;
    }

    // Non-destructively verify production schema exists (never drops/truncates/overwrites)
    if (!database.verifySchema()) {
        std::cerr << "PostgreSQL schema verification failed." << std::endl;
        return 1;
    }

    // Demo accounts are DISABLED by default in production (ENABLE_DEMO_ACCOUNTS must be explicitly true)
    const bool demoAccountsEnabled = isEnvFlagTrue(std::getenv("ENABLE_DEMO_ACCOUNTS"));
    if (demoAccountsEnabled) {
        seedDemoAccounts(database);
    } else {
        std::cout << "[INFO] Demo accounts disabled by default (ENABLE_DEMO_ACCOUNTS=false)." << std::endl;
    }

    const TrustedProxyMode proxyMode = RateLimiter::parseTrustedProxyMode(std::getenv("TRUST_PROXY"));
    const std::vector<std::string> allowedOrigins = parseAllowedOrigins(std::getenv("ALLOWED_ORIGIN"));

    AuthService authService(database);
    SessionManager sessionManager;
    RateLimiter generalLimiter(120, std::chrono::seconds(60));
    RateLimiter authLimiter(30, std::chrono::seconds(60));
    std::atomic<std::uint64_t> apiRequestCounter{0};

    httplib::Server server;

    // Security: HTTP timeouts & 64 KB payload limit to mitigate Slowloris & DoS exhaustion
    server.set_read_timeout(10, 0);
    server.set_write_timeout(10, 0);
    server.set_keep_alive_timeout(15);
    server.set_keep_alive_max_count(50);
    server.set_payload_max_length(64 * 1024);

    // Global CORS preflight, Security Headers, IP Rate Limiting, and Periodic Cleanup
    server.set_pre_routing_handler([&generalLimiter, &authLimiter, &sessionManager, &apiRequestCounter, proxyMode, &allowedOrigins](const httplib::Request& req, httplib::Response& res) {
        if (req.method == "OPTIONS") {
            applySecurityHeaders(res);
            if (!applyCorsHeaders(req, res, allowedOrigins)) {
                res.status = 403;
                return httplib::Server::HandlerResponse::Handled;
            }
            res.status = 204;
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Max-Age", "86400");
            return httplib::Server::HandlerResponse::Handled;
        }

        // Apply sliding-window rate limiting to API endpoints and periodic memory sweeps
        if (req.path.rfind("/api/", 0) == 0) {
            if ((apiRequestCounter.fetch_add(1, std::memory_order_relaxed) % 100) == 0) {
                sessionManager.removeExpiredSessions();
                generalLimiter.cleanup();
                authLimiter.cleanup();
            }

            std::string clientIp = getClientIp(req, proxyMode);
            bool allowed = true;
            if (req.path == "/api/auth/login" || req.path == "/api/auth/register") {
                allowed = authLimiter.allow(clientIp);
            } else {
                allowed = generalLimiter.allow(clientIp);
            }

            if (!allowed) {
                applyCorsHeaders(req, res, allowedOrigins);
                sendJson(res, 429, {
                    {"status", "error"},
                    {"message", "Too many requests. Please wait a moment and try again."}
                });
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        return httplib::Server::HandlerResponse::Unhandled;
    });

    server.set_post_routing_handler([&allowedOrigins](const httplib::Request& req, httplib::Response& res) {
        applySecurityHeaders(res);
        applyCorsHeaders(req, res, allowedOrigins);
    });

    // Mount static frontend directory
    std::string frontendDir = "./frontend";
    if (const char* envFrontend = std::getenv("FRONTEND_DIR")) {
        frontendDir = envFrontend;
    } else if (!std::filesystem::exists(frontendDir) && std::filesystem::exists("../frontend")) {
        frontendDir = "../frontend";
    }

    if (std::filesystem::exists(frontendDir)) {
        server.set_mount_point("/", frontendDir);
        std::cout << "[INFO] Serving static frontend from: " << frontendDir << std::endl;
    } else {
        std::cerr << "[WARN] Frontend directory not found at: " << frontendDir << std::endl;
    }

    // Health check
    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, {
            {"status", "success"},
            {"message", "VAULTX Banking Simulation Backend is operational"},
            {"language", "C++17"},
            {"simulation", true},
            {"currency", "INR"}
        });
    });

    // Database health check
    server.Get("/api/health/db", [&database](const httplib::Request&, httplib::Response& res) {
        if (database.isConnected()) {
            sendJson(res, 200, {
                {"status", "success"},
                {"database", "PostgreSQL (Supabase)"},
                {"message", "PostgreSQL (Supabase) database is connected and operational"}
            });
        } else {
            sendJson(res, 503, {
                {"status", "error"},
                {"database", "PostgreSQL (Supabase)"},
                {"message", "PostgreSQL database connection unavailable"}
            });
        }
    });

    // Pre-configured simulated demo accounts (disabled by default unless ENABLE_DEMO_ACCOUNTS=true)
    server.Get("/api/demo-accounts", [demoAccountsEnabled](const httplib::Request&, httplib::Response& res) {
        if (!demoAccountsEnabled) {
            sendJson(res, 404, {
                {"status", "error"},
                {"message", "Demo accounts are disabled in this environment."}
            });
            return;
        }

        sendJson(res, 200, {
            {"status", "success"},
            {"accounts", {
                {
                    {"accountNumber", "100020003000"},
                    {"name", "Aditya Sharma"},
                    {"tier", "Platinum VIP"},
                    {"balance", 75000.0}
                },
                {
                    {"accountNumber", "100020004000"},
                    {"name", "Priya Patel"},
                    {"tier", "Gold Premium"},
                    {"balance", 42500.0}
                },
                {
                    {"accountNumber", "100020005000"},
                    {"name", "Rahul Verma"},
                    {"tier", "Business Executive"},
                    {"balance", 150000.0}
                },
                {
                    {"accountNumber", "100020006000"},
                    {"name", "Simran Kaur"},
                    {"tier", "Student Advantage"},
                    {"balance", 18000.0}
                }
            }}
        });
    });

    // User registration (Card issuance)
    server.Post("/api/auth/register", [&authService](const httplib::Request& req, httplib::Response& res) {
        try {
            json request = json::parse(req.body);

            if (!request.is_object() ||
                !request.contains("fullName") || !request.contains("pin") ||
                !request["fullName"].is_string() || !request["pin"].is_string()) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "Full name and 6-digit PIN are required."}
                });
                return;
            }

            std::string email = "";
            if (request.contains("email") && request["email"].is_string()) {
                email = request["email"].get<std::string>();
            }

            AuthResult result = authService.registerUser(
                request["fullName"].get<std::string>(),
                email,
                request["pin"].get<std::string>()
            );

            if (result.success) {
                sendJson(res, 201, {
                    {"status", "success"},
                    {"message", "New ATM Debit Card issued successfully."},
                    {"userId", result.userId},
                    {"accountNumber", result.accountNumber},
                    {"initialBalance", 10000.0}
                });
            } else {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", result.message}
                });
            }
        } catch (const json::exception&) {
            sendJson(res, 400, {
                {"status", "error"},
                {"message", "Invalid JSON request format."}
            });
        }
    });

    // ATM Card Login (Account Number + PIN)
    server.Post("/api/auth/login", [&authService, &sessionManager, &database](const httplib::Request& req, httplib::Response& res) {
        try {
            json request = json::parse(req.body);

            if (!request.is_object()) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "Invalid JSON request format."}
                });
                return;
            }

            std::string identifier = "";
            if (request.contains("accountNumber") && request["accountNumber"].is_string()) {
                identifier = request["accountNumber"].get<std::string>();
            } else if (request.contains("email") && request["email"].is_string()) {
                identifier = request["email"].get<std::string>();
            } else if (request.contains("identifier") && request["identifier"].is_string()) {
                identifier = request["identifier"].get<std::string>();
            }

            if (identifier.empty() || !request.contains("pin") || !request["pin"].is_string()) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "12-digit ATM Card/Account Number and 6-digit PIN are required."}
                });
                return;
            }

            // Strip spaces and dashes from account number
            identifier.erase(
                std::remove_if(identifier.begin(), identifier.end(), [](unsigned char c) {
                    return c == ' ' || c == '-';
                }),
                identifier.end()
            );

            std::string pin = request["pin"].get<std::string>();

            AuthResult result = authService.loginUser(identifier, pin);
            if (!result.success) {
                sendJson(res, 401, {
                    {"status", "error"},
                    {"message", result.message}
                });
                return;
            }

            std::string token = sessionManager.createSession(result.userId);
            if (token.empty()) {
                sendJson(res, 500, {
                    {"status", "error"},
                    {"message", "Could not establish secure login session."}
                });
                return;
            }

            AccountDetails acc = database.getAccountDetailsForUser(result.userId);

            sendJson(res, 200, {
                {"status", "success"},
                {"message", "Card authenticated successfully."},
                {"userId", result.userId},
                {"fullName", acc.fullName},
                {"email", acc.email},
                {"accountNumber", result.accountNumber},
                {"balancePaise", acc.balancePaise},
                {"balance", static_cast<double>(acc.balancePaise) / 100.0},
                {"sessionToken", token},
                {"expiresInSeconds", 900}
            });
        } catch (const json::exception&) {
            sendJson(res, 400, {
                {"status", "error"},
                {"message", "Invalid JSON request format."}
            });
        }
    });

    // User logout
    server.Post("/api/auth/logout", [&sessionManager](const httplib::Request& req, httplib::Response& res) {
        std::string token = extractBearerToken(req);
        if (token.empty() || !sessionManager.invalidateSession(token)) {
            sendJson(res, 401, {
                {"status", "error"},
                {"message", "Invalid or expired session."}
            });
            return;
        }

        sendJson(res, 200, {
            {"status", "success"},
            {"message", "Logged out successfully."}
        });
    });

    // Session validation
    server.Get("/api/auth/validate", [&sessionManager, &database](const httplib::Request& req, httplib::Response& res) {
        std::string token = extractBearerToken(req);
        int userId = -1;

        if (token.empty() || !sessionManager.validateSession(token, userId)) {
            sendJson(res, 401, {
                {"status", "error"},
                {"message", "Invalid or expired session."}
            });
            return;
        }

        AccountDetails acc = database.getAccountDetailsForUser(userId);

        sendJson(res, 200, {
            {"status", "success"},
            {"message", "Session is valid."},
            {"userId", userId},
            {"fullName", acc.fullName},
            {"email", acc.email},
            {"accountNumber", acc.accountNumber},
            {"balancePaise", acc.balancePaise},
            {"balance", static_cast<double>(acc.balancePaise) / 100.0}
        });
    });

    // Account details and balance inquiry
    server.Get("/api/account", [&sessionManager, &database](const httplib::Request& req, httplib::Response& res) {
        std::string token = extractBearerToken(req);
        int userId = -1;

        if (token.empty() || !sessionManager.validateSession(token, userId)) {
            sendJson(res, 401, {
                {"status", "error"},
                {"message", "Invalid or expired session. Please login again."}
            });
            return;
        }

        AccountDetails acc = database.getAccountDetailsForUser(userId);
        if (!acc.found) {
            sendJson(res, 404, {
                {"status", "error"},
                {"message", "Active account not found for this user."}
            });
            return;
        }

        sendJson(res, 200, {
            {"status", "success"},
            {"user", {
                {"userId", acc.userId},
                {"fullName", acc.fullName},
                {"email", acc.email}
            }},
            {"account", {
                {"accountId", acc.accountId},
                {"accountNumber", acc.accountNumber},
                {"balancePaise", acc.balancePaise},
                {"balance", static_cast<double>(acc.balancePaise) / 100.0},
                {"status", acc.status}
            }}
        });
    });

    // Cash Deposit
    server.Post("/api/deposit", [&sessionManager, &database](const httplib::Request& req, httplib::Response& res) {
        std::string token = extractBearerToken(req);
        int userId = -1;

        if (token.empty() || !sessionManager.validateSession(token, userId)) {
            sendJson(res, 401, {
                {"status", "error"},
                {"message", "Invalid or expired session."}
            });
            return;
        }

        try {
            json request = json::parse(req.body);
            std::int64_t amountPaise = 0;
            std::string err;

            if (!parseRupeesToPaise(request, amountPaise, err, 10000000.0L)) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", err}
                });
                return;
            }

            std::string accountNumber = database.getAccountNumberForUser(userId);
            if (accountNumber.empty()) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "No active account found for authenticated user."}
                });
                return;
            }

            OperationResult result = database.depositFunds(accountNumber, amountPaise);
            if (!result.success) {
                sendJson(res, result.clientError ? 400 : 500, {
                    {"status", "error"},
                    {"message", result.message}
                });
                return;
            }

            sendJson(res, 200, {
                {"status", "success"},
                {"message", result.message},
                {"accountNumber", accountNumber},
                {"depositedAmount", static_cast<double>(amountPaise) / 100.0},
                {"balancePaise", result.balancePaise},
                {"balance", static_cast<double>(result.balancePaise) / 100.0}
            });
        } catch (const json::exception&) {
            sendJson(res, 400, {
                {"status", "error"},
                {"message", "Invalid JSON request format."}
            });
        }
    });

    // Cash Withdrawal
    server.Post("/api/withdraw", [&sessionManager, &database](const httplib::Request& req, httplib::Response& res) {
        std::string token = extractBearerToken(req);
        int userId = -1;

        if (token.empty() || !sessionManager.validateSession(token, userId)) {
            sendJson(res, 401, {
                {"status", "error"},
                {"message", "Invalid or expired session."}
            });
            return;
        }

        try {
            json request = json::parse(req.body);
            std::int64_t amountPaise = 0;
            std::string err;

            if (!parseRupeesToPaise(request, amountPaise, err, 50000.0L)) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "ATM withdrawal amount must be between ₹100 and ₹50,000 per transaction."}
                });
                return;
            }

            if (amountPaise < 10000 || amountPaise % 10000 != 0) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "ATM cash withdrawal must be in multiples of ₹100 notes."}
                });
                return;
            }

            std::string accountNumber = database.getAccountNumberForUser(userId);
            if (accountNumber.empty()) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "No active account found for authenticated user."}
                });
                return;
            }

            OperationResult result = database.withdrawFunds(accountNumber, amountPaise);
            if (!result.success) {
                sendJson(res, result.clientError ? 400 : 500, {
                    {"status", "error"},
                    {"message", result.message}
                });
                return;
            }

            sendJson(res, 200, {
                {"status", "success"},
                {"message", result.message},
                {"accountNumber", accountNumber},
                {"withdrawnAmount", static_cast<double>(amountPaise) / 100.0},
                {"balancePaise", result.balancePaise},
                {"balance", static_cast<double>(result.balancePaise) / 100.0}
            });
        } catch (const json::exception&) {
            sendJson(res, 400, {
                {"status", "error"},
                {"message", "Invalid JSON request format."}
            });
        }
    });

    // Fund Transfer
    server.Post("/api/transfer", [&database, &sessionManager](const httplib::Request& req, httplib::Response& res) {
        std::string token = extractBearerToken(req);
        int userId = -1;

        if (token.empty() || !sessionManager.validateSession(token, userId)) {
            sendJson(res, 401, {
                {"status", "error"},
                {"message", "Invalid or expired session."}
            });
            return;
        }

        try {
            const json request = json::parse(req.body);

            std::string targetAccount;
            if (request.contains("targetAccount") && request["targetAccount"].is_string()) {
                targetAccount = request["targetAccount"].get<std::string>();
            } else if (request.contains("recipientAccountNumber") && request["recipientAccountNumber"].is_string()) {
                targetAccount = request["recipientAccountNumber"].get<std::string>();
            }

            if (targetAccount.empty() || !request.contains("amount") || !request["amount"].is_number()) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "Recipient account number and numeric amount are required."}
                });
                return;
            }

            // Strip spaces and dashes
            targetAccount.erase(
                std::remove_if(targetAccount.begin(), targetAccount.end(), [](unsigned char c) {
                    return c == ' ' || c == '-';
                }),
                targetAccount.end()
            );

            if (targetAccount.empty() || targetAccount.size() > 32 ||
                !std::all_of(targetAccount.begin(), targetAccount.end(), [](unsigned char c) { return std::isdigit(c); })) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "Enter a valid recipient account number (digits only)."}
                });
                return;
            }

            std::int64_t amountPaise = 0;
            std::string err;
            if (!parseRupeesToPaise(request, amountPaise, err, 10000000.0L)) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", err}
                });
                return;
            }

            const std::string senderAccount = database.getAccountNumberForUser(userId);
            if (senderAccount.empty()) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "No active sender account is available."}
                });
                return;
            }

            if (senderAccount == targetAccount) {
                sendJson(res, 400, {
                    {"status", "error"},
                    {"message", "Self-transfer is not allowed. Please enter a different recipient account."}
                });
                return;
            }

            const TransferResult result = database.transferFunds(senderAccount, targetAccount, amountPaise);
            if (!result.success) {
                sendJson(res, result.clientError ? 400 : 500, {
                    {"status", "error"},
                    {"message", result.message}
                });
                return;
            }

            sendJson(res, 200, {
                {"status", "success"},
                {"message", result.message},
                {"senderAccount", senderAccount},
                {"targetAccount", targetAccount},
                {"transferredAmount", static_cast<double>(amountPaise) / 100.0},
                {"balancePaise", result.senderBalancePaise},
                {"balance", static_cast<double>(result.senderBalancePaise) / 100.0}
            });
        } catch (const json::exception&) {
            sendJson(res, 400, {
                {"status", "error"},
                {"message", "Invalid JSON request format."}
            });
        }
    });

    // Transaction History
    server.Get("/api/transactions", [&sessionManager, &database](const httplib::Request& req, httplib::Response& res) {
        std::string token = extractBearerToken(req);
        int userId = -1;

        if (token.empty() || !sessionManager.validateSession(token, userId)) {
            sendJson(res, 401, {
                {"status", "error"},
                {"message", "Invalid or expired session."}
            });
            return;
        }

        AccountDetails acc = database.getAccountDetailsForUser(userId);
        if (!acc.found) {
            sendJson(res, 404, {
                {"status", "error"},
                {"message", "Account not found for authenticated user."}
            });
            return;
        }

        int limit = 50;
        if (req.has_param("limit")) {
            try {
                limit = std::stoi(req.get_param_value("limit"));
                if (limit <= 0 || limit > 100) limit = 50;
            } catch (...) {
                limit = 50;
            }
        }

        std::vector<TransactionItem> history = database.getTransactionHistory(acc.accountId, limit);

        json txArray = json::array();
        for (const auto& item : history) {
            txArray.push_back({
                {"id", item.id},
                {"type", item.type},
                {"direction", item.direction},
                {"amountPaise", item.amountPaise},
                {"amount", static_cast<double>(item.amountPaise) / 100.0},
                {"description", item.description},
                {"relatedAccount", item.relatedAccount},
                {"createdAt", item.createdAt}
            });
        }

        sendJson(res, 200, {
            {"status", "success"},
            {"accountNumber", acc.accountNumber},
            {"balancePaise", acc.balancePaise},
            {"balance", static_cast<double>(acc.balancePaise) / 100.0},
            {"count", static_cast<int>(history.size())},
            {"transactions", txArray}
        });
    });

    // Determine host and port
    const char* envHost = std::getenv("HOST");
    std::string host = envHost ? envHost : "0.0.0.0";

    int port = 8080;
    if (const char* envPort = std::getenv("PORT")) {
        try {
            port = std::stoi(envPort);
        } catch (...) {
            port = 8080;
        }
    }

    std::cout << "\n============================================\n";
    std::cout << " VAULTX — SECURE ACCESS. SMARTER BANKING.\n";
    std::cout << " (Account-Only ATM Card Authentication)\n";
    std::cout << "============================================\n";
    std::cout << "[INFO] Database: PostgreSQL (Supabase via libpqxx)\n";
    std::cout << "[INFO] Listening on: http://" << host << ":" << port << "\n";
    std::cout << "[INFO] Endpoints active:\n";
    std::cout << "  GET  /api/health\n";
    std::cout << "  GET  /api/demo-accounts\n";
    std::cout << "  POST /api/auth/register\n";
    std::cout << "  POST /api/auth/login\n";
    std::cout << "  POST /api/auth/logout\n";
    std::cout << "  GET  /api/account\n";
    std::cout << "  POST /api/deposit\n";
    std::cout << "  POST /api/withdraw\n";
    std::cout << "  POST /api/transfer\n";
    std::cout << "  GET  /api/transactions\n";
    std::cout << "============================================\n\n";

    std::signal(SIGINT, handleShutdownSignal);
    std::signal(SIGTERM, handleShutdownSignal);

    std::thread signalWatcher([&server]() {
        while (!gShutdownRequested.load(std::memory_order_relaxed)) {
            if (gSignalReceived != 0) {
                gShutdownRequested.store(true, std::memory_order_relaxed);
                server.stop();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });

    const bool listenOk = server.listen(host.c_str(), port);
    gShutdownRequested.store(true, std::memory_order_relaxed);
    if (signalWatcher.joinable()) {
        signalWatcher.join();
    }
    database.disconnect();

    if (!listenOk && gSignalReceived == 0) {
        std::cerr << "Failed to start server on " << host << ":" << port << std::endl;
        return 1;
    }

    return 0;
}
