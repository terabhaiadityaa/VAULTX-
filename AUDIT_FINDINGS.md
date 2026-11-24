# AUDIT FINDINGS: NOIR ATM Simulation Platform

> **Date:** 2026-11-24  
> **Auditor:** Senior Software Architect & Security Reviewer  
> **Target:** First-Year B.Tech CSBS Viva Evaluation (Clarity, Security, Explainability)

---

## SECTION A: Code Complexity and Duplication

### Finding A-1: Repeated Monetary Amount Validation Logic
- **Location:** `backend/src/main.cpp`:
  - Lines 455–472 (`/api/deposit`)
  - Lines 531–549 (`/api/withdraw`)
  - Lines 642–659 (`/api/transfer`)
- **Problem:** All three routes repeat the identical conversion sequence: extracting `long double amountRupees`, verifying `std::isfinite()`, checking `amountRupees > 0`, validating that fractional paise do not exceed 2 decimal places (`std::fabs(paiseValue - amountRupees * 100.0L) > 0.000001L`), and casting to `int64_t`. Duplicated validation across 60+ lines makes maintenance error-prone and hard for a student to explain.
- **Simpler Replacement:** Extract a clean, single helper function in `main.cpp`:
  ```cpp
  bool parseRupeesToPaise(const json& req, std::int64_t& outPaise, std::string& err)
  ```
  Every route calls this one function, reducing lines and ensuring identical, bulletproof validation.

### Finding A-2: Redundant CORS Header Definitions
- **Location:** `backend/src/main.cpp`:
  - Lines 33 (`sendJson` function sets `Access-Control-Allow-Origin: *`)
  - Lines 149 (`server.set_pre_routing_handler` sets `Access-Control-Allow-Origin: *`)
  - Lines 159 (`server.set_post_routing_handler` sets `Access-Control-Allow-Origin: *`)
- **Problem:** Triplicate CORS header manipulation causes clutter, conflicts, and violates the rule against wildcard origins on authenticated endpoints.
- **Simpler Replacement:** Consolidate all CORS and security headers into the pre-routing and post-routing handlers. Remove redundant manual header calls from `sendJson`.

### Finding A-3: Overly Verbose Spacing and Formatting in AuthService
- **Location:** `backend/src/services/AuthService.cpp` (Lines 42–695)
- **Problem:** `AuthService.cpp` is 696 lines long, but nearly half the lines are double/triple blank lines between individual variable declarations and statement parameters. This artificial inflation makes the file look daunting to a first-year student.
- **Simpler Replacement:** Reformat into standard, clean, beginner-friendly C++ blocks with concise explanatory comments explaining the 7 logical steps of authentication.

### Finding A-4: Redundant Account Lookup Query in Route Handlers
- **Location:** `backend/src/database/Database.cpp` (`getAccountNumberForUser`, lines 102–122) and `main.cpp` (lines 474, 558, 661).
- **Problem:** `getAccountNumberForUser` queries the `accounts` table solely for `account_number`, while `getAccountDetailsForUser` queries both `users` and `accounts` for full details. Routes often make multiple separate DB round-trips for the same user.
- **Simpler Replacement:** Keep the public interface intact for compatibility, but streamline `getAccountNumberForUser` to be short, clean, and directly query SQLite with a prepared statement.

### Finding A-5: Frontend Redundant Token Storage in LocalStorage
- **Location:** `frontend/js/app.js` (Lines 178, 188, 198)
- **Problem:** Storing financial session tokens in `localStorage` persists them indefinitely even after the browser tab is closed. In an ATM environment, sessions must live only in `sessionStorage` or memory so ejecting the card or closing the tab terminates access.
- **Simpler Replacement:** Use `sessionStorage` exclusively for session token persistence.

---

## SECTION B: Unnecessary Files, Dependencies, and Assets

### Candidate B-1: Empty Scaffold Directories
- **Paths:**
  - `backend/include/routes/` (0 files)
  - `backend/include/utils/` (0 files)
  - `backend/src/routes/` (0 files)
  - `docs/` (0 files)
- **Proof:** `find backend/include/routes backend/include/utils backend/src/routes docs -type f` returns 0 results. Zero references exist in `CMakeLists.txt` or code.
- **Decision:** Remove the empty directories to keep the project clean, uncluttered, and easy to explain.

### Candidate B-2: Network Timeout Proxy Test Utility
- **Path:** `tests/network_timeout_proxy.py`
- **Proof:** Search for `network_timeout_proxy` across the entire codebase returns zero occurrences outside its own file. It was an ad-hoc proxy used during initial development.
- **Decision:** Move to `_to_delete_review` and confirm no automated scripts or CI workflows rely on it, then remove safely.

### Candidate B-3: Unused API Endpoint `/api/auth/validate`
- **Location:** `backend/src/main.cpp` (Lines 367–391)
- **Proof:** `grep -rn "validate" frontend/` returns zero matches. The frontend checks existing sessions by directly calling `/api/account` (which validates the token and loads user info in a single call).
- **Decision:** Retain the endpoint for backward compatibility with external API testers or scripts, but document it as an auxiliary health validation route.

---

## SECTION C: Comprehensive Security Audit (Items 1 to 19)

| # | Security Check Item | Status | Evidence / Location | Severity (if Fail) | Exploitability & Fix |
|---|:---|:---:|:---|:---:|:---|
| **1** | **SQL Injection** | **PASS** | Parameterized queries used everywhere (`sqlite3_prepare_v2`, `sqlite3_bind_*`). | — | No raw string interpolation exists. |
| **2** | **XSS (Cross-Site Scripting)** | **FAIL** | `frontend/js/app.js` lines 110 & 750 use `.innerHTML` with string concatenation. | **Medium** | An attacker could inject malicious markup if unescaped transaction descriptions reach the DOM. **Fix:** Replace `.innerHTML` in toasts and table rows with safe DOM methods (`document.createElement`, `.textContent`). |
| **3** | **CSRF** | **PASS** | Protected routes use custom `Authorization: Bearer <token>` header, not ambient browser cookies. | — | Browsers cannot send custom authorization headers cross-origin without CORS consent. Immune to standard CSRF. |
| **4** | **Authentication** | **PASS** | `backend/src/main.cpp` validates Bearer token via `SessionManager::validateSession` on every private route. | — | Unauthenticated requests return HTTP 401. |
| **5** | **Authorization & IDOR** | **PASS** | `deposit`, `withdraw`, `transfer`, `account`, and `transactions` derive user identity strictly from the verified session token. | — | Client cannot supply an arbitrary account ID to modify or view another user's funds. |
| **6** | **Password / PIN Storage** | **PASS** | `backend/src/security/PinHasher.cpp` uses PBKDF2-HMAC-SHA256, 600,000 iterations, 16-byte random salt, and constant-time `CRYPTO_memcmp`. | — | Resilient against GPU brute-force and rainbow table attacks. |
| **7** | **Session Security** | **FAIL** | `frontend/js/app.js` line 188 saves session token to `localStorage`. | **Low** | Token remains in browser storage indefinitely after tab close. **Fix:** Restrict token storage to `sessionStorage`. |
| **8** | **Backend Input Validation** | **PASS** | Validation on type, numerical range, finite check, positive balance, and decimal limit present on all endpoints. | — | Client-side bypass cannot send invalid values to backend. |
| **9** | **Business Rule Validation** | **PASS** | Withdrawal checks balance (`balance_paise >= ?`), multiples of ₹100, max ₹50,000; deposit max ₹1,00,00,000; self-transfer blocked. | — | Enforced on server inside atomic SQL transaction. |
| **10**| **Balance Manipulation** | **PASS** | Client never sends a balance value; server alone computes new balance in paise. | — | Impossible for client to alter its own balance. |
| **11**| **Environment Variables & Secrets** | **FAIL** | Missing `.env.example` template; no `.env` loading utility. | **Medium** | Prospective deployers or evaluators lack documented environment variables. **Fix:** Create `.env.example` with clear documentation of `PORT`, `HOST`, `DATABASE_PATH`, `FRONTEND_DIR`. |
| **12**| **Secure Database Access** | **PASS** | Parameterized queries, `PRAGMA foreign_keys = ON;`, `busy_timeout` set, thread-safe mutex lock. | — | SQLite handles concurrency safely without database locks. |
| **13**| **Error Handling & Information Leakage** | **PASS** | Generic error messages returned to clients (`"Invalid JSON request format."`, `"Invalid credentials."`). | — | No internal stack traces, DB file paths, or SQL syntax errors exposed. |
| **14**| **Rate Limiting & Brute-Force** | **FAIL** | Login has account lockout after 5 failures, BUT there is no IP-based rate limiting on money or login endpoints. | **High** | An automated script could flood `/api/deposit` or `/api/auth/login` with thousands of requests per second. **Fix:** Implement a lightweight in-memory sliding-window IP rate limiter in C++. |
| **15**| **Race Conditions & Atomicity** | **PASS** | `BEGIN IMMEDIATE TRANSACTION;`, conditional atomic updates (`balance_paise >= ?`), mutex lock `dbMutex`. | — | Simultaneous withdrawals cannot overdraw the account. |
| **16**| **Security Headers & CORS** | **FAIL** | CORS allows wildcard `*`; missing standard security headers (`X-Content-Type-Options`, `X-Frame-Options`, `CSP`); no request body size limit. | **High** | App can be embedded in an iframe (clickjacking) or subjected to MIME-sniffing. **Fix:** Add standard security headers, restrict CORS to same-origin / configured origin, and set `server.set_payload_max_length(1024 * 64)`. |
| **17**| **Dependency Vulnerabilities** | **PASS** | All dependencies are well-vetted: OpenSSL (system), SQLite3 (system), `httplib` (header), `nlohmann/json` (header). | — | Zero unpatched third-party npm or pip vulnerabilities in the active project. |
| **18**| **Sensitive Data in Frontend** | **PASS** | No hardcoded credentials or API keys in `index.html` or `app.js`. Console logs do not print PINs. | — | Clean client-side code. |
| **19**| **Debug Mode & Leftover Endpoints** | **PASS** | Production-ready endpoints; demo accounts do not leak PINs; simulation environment notice clearly displayed. | — | Complies with educational simulation rules. |

---

## SECTION D: Database and API Audit

1. **Database Connection Lifecycle:**
   - Database connection is established once at startup in `main.cpp` and held cleanly by `Database database;`.
   - Thread safety is guarded by `std::mutex dbMutex` for all operations.
   - Resource cleanup is handled via RAII in `Database::~Database()` which calls `disconnect()`.
2. **Atomic Ledger Consistency:**
   - Every deposit, withdrawal, and transfer executes as a single ACID transaction (`BEGIN IMMEDIATE` ... `COMMIT`).
   - Every transfer writes two balanced ledger rows: `transfer_out` for sender and `transfer_in` for receiver.
   - Rollback is executed on any step failure.
3. **API Redundancies:**
   - `/api/health` and `/api/health/db` can be unified so `/api/health` reports full system + database status.
   - `/api/auth/validate` is kept for external tooling.

---

## SECTION E: Refactor & Implementation Plan

| Step # | Action Description | Target Files | Risk Level | Safety Measure |
|:---|:---|:---|:---:|:---|
| **Step 1** | **Add Security Headers, CORS tightening & Payload Limit** | `backend/src/main.cpp` | **Low** | Verify all static assets and API routes continue to load cleanly. |
| **Step 2** | **Add In-Memory IP Rate Limiter for Endpoints** | `backend/include/security/RateLimiter.h`, `backend/src/security/RateLimiter.cpp`, `main.cpp` | **Medium** | Keep limits generous (e.g. 60 req/min for general, 15 req/min for login) so normal usage is never impacted. |
| **Step 3** | **Unify Monetary Amount Validation in Backend** | `backend/src/main.cpp` | **Low** | Run `tests/e2e_test.py` to verify decimal handling and boundaries. |
| **Step 4** | **Harden Frontend XSS & Session Storage** | `frontend/js/app.js` | **Low** | Use `textContent` and DOM nodes; switch from `localStorage` to `sessionStorage`. |
| **Step 5** | **Provide `.env.example` & Clean Configuration** | `.env.example`, `backend/src/main.cpp` | **Low** | Ensure fallback defaults work out-of-the-box without requiring `.env`. |
| **Step 6** | **Clean Unused Directories & Scaffold Files** | `backend/include/routes`, `backend/src/routes`, `docs` | **Low** | Stage in `_to_delete_review` first; test build before permanent deletion. |
| **Step 7** | **Code Formatting & Beginner-Friendly Viva Comments** | `AuthService.cpp`, `Database.cpp`, `main.cpp` | **Low** | Re-run full test suite and compare UI screenshots. |
