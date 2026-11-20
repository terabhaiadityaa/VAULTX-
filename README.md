# NOIR — ATM Transaction Simulation Platform

[![CI & Docker Build Verification](https://github.com/Chandreshh404/ATM-Transaction-Simulation/actions/workflows/ci.yml/badge.svg)](https://github.com/Chandreshh404/ATM-Transaction-Simulation/actions)

> **SIMULATION ENVIRONMENT — NO REAL MONEY**  
> An educational ATM and banking simulation platform built with a high-performance C++17 backend, SQLite atomic ledger, and a modern, responsive fintech web frontend.

---

## 🌐 Live Website

- **Public HTTPS URL:** [https://d8e5c07318c2267b-106-222-218-80.serveousercontent.com](https://d8e5c07318c2267b-106-222-218-80.serveousercontent.com)
- **Local Server:** `http://localhost:8080`

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
- **Production Containerization:** Multi-stage Dockerfile and deployment configurations for Render, Fly.io, and Docker Compose.

---

## 🛠️ Architecture & Technology Stack

- **Backend:** C++17
- **HTTP Server:** `cpp-httplib` (embedded header-only HTTP/HTTPS server)
- **JSON Parser:** `nlohmann/json` (v3.11.3)
- **Database:** SQLite3 with atomic transactions (`BEGIN IMMEDIATE TRANSACTION`) and WAL mode
- **Security:** OpenSSL 3.x PBKDF2-HMAC-SHA256 for 6-digit PIN hashing; high-entropy token-based session management
- **Frontend:** HTML5, CSS3, Vanilla JavaScript (ES6+) with zero heavy framework bloat
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

## 🧪 Testing

Run automated C++ unit tests:

```bash
ninja -C build
ctest --test-dir build --output-on-failure
```

Run comprehensive end-to-end API test suite:

```bash
python3 tests/e2e_test.py
```

---

## 🚀 Local Build & Run

```bash
# Configure build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build executable
ninja -C build

# Run application
./build/backend/atm_backend
```
Open `http://localhost:8080` in your web browser.
