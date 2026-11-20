
# ATM Transaction Simulation System

## Project Technology

- Backend: C++17
- Database: SQLite
- Frontend: HTML, CSS, Vanilla JavaScript
- HTTP Server: cpp-httplib
- JSON: nlohmann/json
- Build System: CMake and Ninja
- Compiler: GCC (MSYS2 UCRT64)

## Project Purpose

Build an educational ATM transaction simulation system.
This project does not process real money or connect to real banking networks.

## Main Features

1. User registration and login
2. PIN-based authentication
3. Account balance enquiry
4. Cash withdrawal
5. Cash deposit
6. Fund transfer between simulation accounts
7. Transaction history
8. Logout
9. Input validation and error handling

## Architecture

- Frontend communicates with backend through HTTP REST APIs.
- Backend handles business logic and authentication.
- SQLite stores users, accounts, and transactions.
- All database operations must use prepared statements.
- Transfers must use database transactions to prevent partial updates.

## Development Rules

- Use C++17 only for backend code.
- Do not introduce Python, Flask, MongoDB, or Firebase.
- Store monetary amounts as integer paise, not floating-point values.
- Never store PINs as plain text.
- Validate all input on the backend.
- Keep routes, services, database logic, and utilities separate.
- Use clear names and beginner-readable code.
- Do not generate unnecessary files.
- Explain which file each code block belongs to.
- Do not overwrite existing working code without inspecting it first.

## Development Phases

1. Project setup and backend health endpoint
2. SQLite database and authentication
3. Balance, deposit, and withdrawal
4. Fund transfers and transaction history
5. Frontend development and API integration
6. Testing, security checks, and documentation
