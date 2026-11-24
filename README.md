# VAULTX — Secure Access. Smarter Banking.

> **SIMULATION ENVIRONMENT — NO REAL MONEY**  
> An educational ATM and banking simulation platform built with a high-performance C++17 backend, SQLite atomic ledger, and a modern, responsive fintech web frontend. Designed for a 1st-year B.Tech CSBS student practical (viva) examination.

---

## 📖 Architectural Documentation & Viva Guide

For complete technical specifications, security audit findings, before vs. after refactor metrics, and an in-depth **Viva Examination Guide (with 10 questions and plain-English answers)**, see:
- 📄 **[REFACTOR_REPORT.md](REFACTOR_REPORT.md)**

---

## 🌟 Key Features

- **Simulated ATM Card & Account Authentication:** 6-digit PIN hashing with OpenSSL PBKDF2-HMAC-SHA256 and cryptographic salts.
- **Instant Welcome Simulation Balance:** Every newly created account receives ₹10,000.00 demo balance instantly upon registration.
- **3D Virtual ATM Debit Card:** Interactive debit card showing cardholder name, contactless symbol, EMV chip, and masked account number.
- **Real-Time Account Balance Inquiry:** Instant balance updates tracking both whole Rupees and integer Paise precision without floating-point drift.
- **Cash Withdrawal:** Fast cash presets (₹500, ₹1,000, ₹2,000, ₹5,000, ₹10,000) and custom withdrawal in multiples of ₹100 with automated denomination note breakdown.
- **Cash Deposit:** Immediate deposit into active accounts with real-time balance credit.
- **Atomic Fund Transfers:** Thread-safe, atomic account-to-account transfers with self-transfer prevention, balance validation, and double-entry ledger records.
- **Transaction Passbook & Filter:** Filter transactions by All, Credits, and Debits with directional indicators.
- **Thermal ATM Receipt Printing:** Printable transaction receipt modal with terminal ID, timestamp, and transaction reference numbers.
- **Security & Reliability:** In-memory sliding-window rate limiting, HTTP security headers (`nosniff`, `DENY`), client payload limits (64KB), and atomic SQLite transactions (`BEGIN IMMEDIATE TRANSACTION`).

---

## 🛠️ Architecture & Technology Stack

- **Backend:** C++17 (`std::int64_t` paise monetary precision, `std::mutex` concurrency control)
- **HTTP Server:** `cpp-httplib` (embedded header-only HTTP server)
- **JSON Parser:** `nlohmann/json` (modern C++ JSON library)
- **Database:** SQLite3 with atomic transactions and WAL (Write-Ahead Logging) mode
- **Security:** OpenSSL 3.x PBKDF2-HMAC-SHA256 for PIN hashing; 256-bit CSPRNG hex session tokens
- **Frontend:** Vanilla HTML5, CSS3, ES6+ JavaScript (zero external UI dependencies)
- **Build System:** CMake (>= 3.16) and Ninja

---

## 📡 API Reference

| Method | Endpoint | Description | Auth Required |
|---|---|---|---|
| `GET` | `/api/health` | Service health status | No |
| `GET` | `/api/health/db` | Database connection status | No |
| `POST` | `/api/auth/register` | Register new user + allocate account + ₹10,000 bonus | No |
| `POST` | `/api/auth/login` | Authenticate user & generate session token | No |
| `POST` | `/api/auth/logout` | Invalidate active session | Yes |
| `GET` | `/api/auth/validate` | Verify active session | Yes |
| `GET` | `/api/account` | Retrieve account details and balance | Yes |
| `POST` | `/api/deposit` | Deposit simulated funds into account | Yes |
| `POST` | `/api/withdraw` | Withdraw cash (multiples of ₹100) | Yes |
| `POST` | `/api/transfer` | Atomic transfer between accounts | Yes |
| `GET` | `/api/transactions` | Retrieve account transaction history | Yes |

---

## 🚀 Quick Start (Local Setup)

### 1. Build Backend
```bash
# Configure build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build executable
ninja -C build
```

### 2. Run Application
```bash
# Start backend server on port 8080
./build/backend/atm_backend
```
Then open `http://localhost:8080` in any web browser.

---

## 🧪 Testing

### Automated C++ Unit Tests
```bash
ctest --test-dir build --output-on-failure
```

### Automated End-to-End Test Suite (22/22 Tests)
```bash
python3 tests/comprehensive_test.py
```
