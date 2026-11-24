# PROJECT MAP: NOIR ATM Simulation Platform

> **Audit Date:** 2026-11-24  
> **Target Audience:** First-Year B.Tech CSBS Student (Viva / Practical Examination Ready)  
> **Project Scope:** Educational ATM & Banking Simulation Platform (C++17 + SQLite + Vanilla JS Frontend)

---

## 1. Directory and File Tree

```text
ATM-Transaction-Simulation/
├── CMakeLists.txt                         # Top-level CMake configuration (C++17, OpenSSL, subdirectories)
├── Dockerfile                             # Multi-stage Linux build & runtime container definition
├── docker-compose.yml                     # Local container orchestration specification
├── fly.toml                               # Fly.io cloud deployment descriptor
├── render.yaml                            # Render.com cloud deployment specification
├── README.md                              # Project overview and execution manual
├── DEPLOYMENT.md                          # Live hosting and tunnel documentation
├── PROJECT_PLAN.md                        # Architecture blueprint and requirement checklist
│
├── database/
│   ├── schema.sql                         # DDL definition: tables, constraints, foreign keys, indexes
│   └── atm.db                             # SQLite binary database file
│
├── backend/
│   ├── CMakeLists.txt                     # Backend executable build rules, includes, and linked libraries
│   ├── external/
│   │   ├── httplib/
│   │   │   └── httplib.h                  # Header-only C++ HTTP/HTTPS server library (yhirose)
│   │   └── nlohmann/
│   │       └── json.hpp                   # Header-only C++ modern JSON serialization library
│   ├── include/
│   │   ├── database/
│   │   │   └── Database.h                 # Database manager class header (thread-safe operations)
│   │   ├── security/
│   │   │   ├── PinHasher.h                # PBKDF2-HMAC-SHA256 PIN hashing interface
│   │   │   └── SessionManager.h           # In-memory session token lifecycle management header
│   │   ├── services/
│   │   │   └── AuthService.h              # Registration, login, lockout, card issuance header
│   │   ├── routes/                        # Empty directory (routes currently embedded in main.cpp)
│   │   └── utils/                         # Empty directory
│   └── src/
│       ├── database/
│       │   └── Database.cpp               # Atomic SQL operations: transfer, deposit, withdraw, history
│       ├── security/
│       │   ├── PinHasher.cpp              # OpenSSL PKCS5_PBKDF2_HMAC implementation
│       │   └── SessionManager.cpp         # Token generation, session validation, expiry tracking
│       ├── services/
│       │   └── AuthService.cpp            # Authentication logic, lockout policy, card creation
│       ├── routes/                        # Empty directory
│       └── main.cpp                       # Server entry point, route definitions, demo seeder, static server
│
├── frontend/
│   ├── index.html                         # SPA interface: Login, registration, virtual 3D card, modals
│   ├── css/
│   │   └── style.css                      # Modern CSS: theme tokens, responsive grids, ATM animations
│   └── js/
│       └── app.js                         # Vanilla JS controller: API calls, validation, receipt printer
│
└── tests/
    ├── CMakeLists.txt                     # Test build specifications
    ├── e2e_test.py                        # Python end-to-end integration test runner
    ├── network_timeout_proxy.py           # Network simulation testing utility
    ├── pin_hasher_test.cpp                # C++ unit tests for PBKDF2 PIN hashing
    └── session_manager_test.cpp           # C++ unit tests for Session token expiration
```

---

## 2. Tech Stack Summary

| Layer | Technology | Details / Role |
| :--- | :--- | :--- |
| **Backend Language** | C++17 | Compiled with `-std=c++17`, strict type safety, standard algorithms |
| **HTTP Server** | `cpp-httplib` (v0.14+) | Header-only embedded HTTP server (`httplib::Server`) with static file mounting |
| **JSON Serialization** | `nlohmann/json` (v3.11+) | Header-only JSON parsing, validation, and object formatting |
| **Database** | SQLite3 (`sqlite3.h`) | Embedded relational database with thread-safe mutex and WAL/busy timeouts |
| **Data Querying** | Raw SQL with Prepared Statements | `sqlite3_prepare_v2`, `sqlite3_bind_*`, parameterized queries for zero SQLi |
| **Cryptography** | OpenSSL `libcrypto` | `PKCS5_PBKDF2_HMAC` (SHA256, 600,000 iterations), `RAND_bytes`, `CRYPTO_memcmp` |
| **Frontend Stack** | HTML5, CSS3, Vanilla JavaScript | Native Fetch API, CSS custom variables, Flexbox/Grid, zero external JS frameworks |
| **Build System** | CMake 3.20+ & Ninja / Make | Portable native cross-platform build automation |

---

## 3. Entry Points, Configuration & Environment

- **Backend Entry Point:** `backend/src/main.cpp` -> `int main()`
  - Reads environment variables:
    - `DATABASE_PATH`: Path to SQLite file (defaults to `database/atm.db`)
    - `FRONTEND_DIR`: Path to static assets (defaults to `./frontend` or `../frontend`)
    - `HOST`: Server bind address (defaults to `0.0.0.0`)
    - `PORT`: Server bind port (defaults to `8080`)
- **Frontend Entry Point:** `frontend/index.html` (served at `/` via `server.set_mount_point("/", frontendDir)`)
- **Database Connection Setup:**
  - Initialized in `Database::connect(dbPath)` using `sqlite3_open_v2` with `SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX`.
  - Configures `PRAGMA foreign_keys = ON;` and `sqlite3_busy_timeout(db, 5000);`.
  - Automatically loads and executes `database/schema.sql` if tables do not exist.
  - Automatically executes `seedDemoAccounts()` to guarantee 4 simulated test debit cards are active.

---

## 4. API Route Catalog

| Method | Endpoint | Purpose | Authentication | Called By (Frontend) |
| :--- | :--- | :--- | :--- | :--- |
| `GET` | `/api/health` | Backend & platform status check | None (Public) | Keepalive & monitoring scripts |
| `GET` | `/api/health/db` | Database connection check | None (Public) | Health monitoring |
| `GET` | `/api/demo-accounts`| List simulated test accounts (names & numbers) | None (Public) | Demo visualizer |
| `POST` | `/api/auth/register`| Issue new simulated ATM card & initial demo balance | None (Public) | `registerForm` in `app.js` |
| `POST` | `/api/auth/login` | Authenticate 12-digit card/account number + PIN | None (Public) | `loginForm` in `app.js` |
| `POST` | `/api/auth/logout`| Invalidate session token | Bearer Token | `logoutBtn` in `app.js` |
| `GET` | `/api/auth/validate`| Check if existing token is valid | Bearer Token | `checkExistingSession()` |
| `GET` | `/api/account` | Retrieve account details & live balance | Bearer Token | `refreshAccountBalance()`, dashboard load |
| `POST` | `/api/deposit` | Atomically deposit cash into account | Bearer Token | `depositForm` in `app.js` |
| `POST` | `/api/withdraw` | Atomically withdraw cash (multiples of ₹100) | Bearer Token | `withdrawForm` in `app.js` |
| `POST` | `/api/transfer` | Atomically transfer funds to another account | Bearer Token | `transferForm` in `app.js` |
| `GET` | `/api/transactions`| Fetch recent ledger history (`?limit=50`) | Bearer Token | `fetchTransactions()` in `app.js` |

---

## 5. Frontend Pages and Scripts

- **`frontend/index.html`:** Single Page Application (SPA) containing:
  - Top header with mandatory Educational Simulation Banner.
  - Auth View: Card Number Login tab and New Card Issuance (Registration) tab.
  - Authenticated Dashboard: 3D interactive virtual debit card visualizer, live balance card with quick refresh.
  - Action Center: 4 operation tabs (Cash Withdrawal, Cash Deposit, Fund Transfer, Passbook Ledger).
  - Thermal ATM Receipt Modal: Formatted printable slip with `#TXN-XXXXXX` identifier.
  - Toast notification container for non-blocking UI alerts.
- **`frontend/js/app.js`:**
  - Dynamic API base URL detection.
  - Token storage in `sessionStorage` (with legacy fallback to `localStorage`).
  - Input masks: Automatic spacing on 12-digit card inputs (`1000 2000 3000`).
  - Cash dispenser visualizer: Computes physical note breakdowns (₹500, ₹200, ₹100).
  - Transaction filter pills: Toggle between All, Credits (+), and Debits (-).
  - Thermal receipt generator and window print handler.

---

## 6. Database Schema & Storage Rules

```sql
-- 1. USERS: User credentials and identity
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    full_name TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE,
    pin_hash TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 2. ACCOUNTS: Financial account and ledger balance
CREATE TABLE accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    account_number TEXT NOT NULL UNIQUE,       -- 12-digit simulated debit card number
    balance_paise INTEGER NOT NULL DEFAULT 0 CHECK (balance_paise >= 0), -- Integer Paise
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'blocked', 'closed')),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 3. TRANSACTIONS: Double-entry ledger audit trail
CREATE TABLE transactions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL REFERENCES accounts(id),
    transaction_type TEXT NOT NULL CHECK (transaction_type IN ('deposit', 'withdrawal', 'transfer_in', 'transfer_out')),
    amount_paise INTEGER NOT NULL CHECK (amount_paise > 0),
    related_account_id INTEGER REFERENCES accounts(id),
    description TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 4. LOGIN_ATTEMPTS: Brute-force & PIN guessing defense
CREATE TABLE login_attempts (
    email TEXT PRIMARY KEY,
    failed_attempts INTEGER NOT NULL DEFAULT 0,
    locked_until INTEGER NOT NULL DEFAULT 0,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

### Critical Financial Rule: Integer Paise Storage
- Floating-point representations (e.g. `float` or `double` storing `₹10.50`) suffer from IEEE-754 precision inaccuracies.
- **All monetary values are strictly stored as 64-bit signed integers (`std::int64_t`) representing Paise (1 Rupee = 100 Paise).**
- Conversions to Rupees occur solely at the presentation boundary (`balanceRupees = balancePaise / 100.0`).

---

## 7. Authentication & Session Lifecycle

1. **Card/Account Login:**
   - User inputs 12-digit account number (or virtual email) and 6-digit PIN.
   - Server checks `login_attempts` to ensure account is not locked.
   - Server queries user's `pin_hash` from SQLite.
   - `PinHasher::verifyPin()` computes PBKDF2-HMAC-SHA256 and compares using constant-time `CRYPTO_memcmp`.
   - On success: `login_attempts` is cleared, and `SessionManager` generates a 64-character cryptographic hex token.
   - On failure: `failed_attempts` increments. If attempts reach 5, account is locked for 15 minutes (`currentTime + 900`).
2. **Session Storage & Validation:**
   - Tokens are stored in an in-memory thread-safe map (`SessionManager`).
   - Every protected API endpoint inspects the `Authorization: Bearer <token>` header.
   - 15-minute sliding inactivity timeout: Each request updates `lastActivity`. Expired tokens are purged.

---

## 8. Dependencies & Libraries Catalog

| Dependency | Origin | Type | Purpose | Status |
| :--- | :--- | :--- | :--- | :--- |
| `OpenSSL::Crypto` | System library | Native C library | PBKDF2 hashing, CSPRNG random bytes, constant-time memory comparisons | Active & Required |
| `SQLite3::SQLite3` | System library | Native C library | Relational database engine | Active & Required |
| `cpp-httplib` | `backend/external/httplib/httplib.h` | Header-only C++ | Embedded HTTP server & routing | Active & Required |
| `nlohmann/json` | `backend/external/nlohmann/json.hpp` | Header-only C++ | JSON parsing and serialization | Active & Required |
| `python3` | System runtime | Standard library | Used only to execute automated integration tests (`tests/e2e_test.py`) | Active & Required |

> **Note on Parent Workspace Directory (`/Users/aditya/first project`):**  
> The parent folder contains leftover files (`backend/app`, `frontend/src`, `package.json`, `requirements.txt`) from an older, abandoned Python/React test from Nov 7. The active project is entirely contained within `ATM-Transaction-Simulation`.
