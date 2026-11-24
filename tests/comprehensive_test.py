#!/usr/bin/env python3
"""
comprehensive_test.py
Full End-to-End Functional, Security Regression, and Persistence Verification.
"""

import urllib.request
import urllib.error
import json
import time
import sys
import sqlite3

BASE_URL = 'http://127.0.0.1:8080'

# Clear any prior lockouts so test run starts with clean slate
try:
    conn = sqlite3.connect("database/atm.db")
    conn.execute("DELETE FROM login_attempts;")
    conn.commit()
    conn.close()
except Exception as e:
    print(f"Warning: could not clear login_attempts: {e}")

def request(method, path, data=None, headers=None):
    url = f"{BASE_URL}{path}"
    h = headers.copy() if headers else {}
    body = None
    if data is not None:
        body = json.dumps(data).encode('utf-8')
        h['Content-Type'] = 'application/json'
    req = urllib.request.Request(url, data=body, headers=h, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            resp_body = resp.read().decode('utf-8')
            return resp.status, json.loads(resp_body) if resp_body else {}
    except urllib.error.HTTPError as e:
        err_body = e.read().decode('utf-8')
        try:
            return e.code, json.loads(err_body)
        except Exception:
            return e.code, {"raw": err_body}

results = []

def record(name, passed, details=""):
    status = "PASS" if passed else "FAIL"
    results.append({"name": name, "status": status, "details": details})
    print(f"[{status}] {name} - {details}")

print("\n" + "="*70)
print("NOIR ATM SIMULATION — COMPREHENSIVE END-TO-END AUDIT TEST RUNNER")
print("="*70 + "\n")

# 1. Functional: Login with valid credentials & invalid credentials
code, res = request('POST', '/api/auth/login', {'accountNumber': '100020003000', 'pin': '123456'})
record("F1.1: Valid Login", code == 200 and 'sessionToken' in res, f"Token: {res.get('sessionToken', '')[:10]}...")
token = res.get('sessionToken')
auth_header = {'Authorization': f'Bearer {token}'}

code, res = request('POST', '/api/auth/login', {'accountNumber': '100020003000', 'pin': '999999'})
record("F1.2: Invalid Login (Generic Error)", code == 401 and res.get('message') == 'Invalid credentials.', f"Msg: {res.get('message')}")

# 2. Functional: Balance Inquiry
code, res = request('GET', '/api/account', headers=auth_header)
initial_balance = res.get('account', {}).get('balance', 0)
record("F2: Balance Inquiry", code == 200 and initial_balance > 0, f"Current balance: ₹{initial_balance}")

# 3. Functional: Cash Deposit (valid, zero, negative, decimal, huge, non-numeric)
code, res = request('POST', '/api/deposit', {'amount': 1500.0}, auth_header)
record("F3.1: Valid Deposit", code == 200 and res.get('balance') == initial_balance + 1500.0, f"New balance: ₹{res.get('balance')}")
curr_balance = res.get('balance')

code, res = request('POST', '/api/deposit', {'amount': 0}, auth_header)
record("F3.2: Zero Deposit Rejection", code == 400, f"Rejected with 400: {res.get('message')}")

code, res = request('POST', '/api/deposit', {'amount': -100}, auth_header)
record("F3.3: Negative Deposit Rejection", code == 400, f"Rejected with 400: {res.get('message')}")

code, res = request('POST', '/api/deposit', {'amount': 100.555}, auth_header)
record("F3.4: Over 2 Decimal Places Deposit Rejection", code == 400, f"Rejected with 400: {res.get('message')}")

code, res = request('POST', '/api/deposit', {'amount': 99999999999}, auth_header)
record("F3.5: Excessive Amount Deposit Rejection", code == 400, f"Rejected with 400: {res.get('message')}")

code, res = request('POST', '/api/deposit', {'amount': "not_a_number"}, auth_header)
record("F3.6: Non-numeric Deposit Rejection", code == 400, f"Rejected with 400: {res.get('message')}")

# 4. Functional: Cash Withdrawal (valid, equal to balance, greater than balance, zero, negative, not multiple of 100)
code, res = request('POST', '/api/withdraw', {'amount': 1000.0}, auth_header)
record("F4.1: Valid Withdrawal (Multiple of ₹100)", code == 200 and res.get('balance') == curr_balance - 1000.0, f"New balance: ₹{res.get('balance')}")
curr_balance = res.get('balance')

code, res = request('POST', '/api/withdraw', {'amount': 150.0}, auth_header)
record("F4.2: Non-multiple of ₹100 Withdrawal Rejection", code == 400, f"Rejected with 400: {res.get('message')}")

code, res = request('POST', '/api/withdraw', {'amount': curr_balance + 500000}, auth_header)
record("F4.3: Exceeding Balance Withdrawal Rejection", code == 400, f"Rejected with 400: {res.get('message')}")

code, res = request('POST', '/api/withdraw', {'amount': 0}, auth_header)
record("F4.4: Zero Withdrawal Rejection", code == 400, f"Rejected with 400: {res.get('message')}")

code, res = request('POST', '/api/withdraw', {'amount': -500}, auth_header)
record("F4.5: Negative Withdrawal Rejection", code == 400, f"Rejected with 400: {res.get('message')}")

# 5. Functional: Fund Transfer & Self-Transfer Prevention
code, res = request('POST', '/api/transfer', {'targetAccount': '100020004000', 'amount': 500.0}, auth_header)
record("F5.1: Valid P2P Transfer", code == 200 and res.get('balance') == curr_balance - 500.0, f"New balance: ₹{res.get('balance')}")
curr_balance = res.get('balance')

code, res = request('POST', '/api/transfer', {'targetAccount': '100020003000', 'amount': 500.0}, auth_header)
record("F5.2: Self-Transfer Prevention", code == 400, f"Rejected with 400: {res.get('message')}")

# 6. Functional: Transaction History Ledger
code, res = request('GET', '/api/transactions', headers=auth_header)
record("F6: Transaction History Retrieval", code == 200 and len(res.get('transactions', [])) >= 3, f"Total txs retrieved: {len(res.get('transactions', []))}")

# 7. Functional: Logout & Session Invalidation
code, res = request('POST', '/api/auth/logout', headers=auth_header)
record("F7.1: Logout Session Invalidation", code == 200, "Session token destroyed on server")

code, res = request('GET', '/api/account', headers=auth_header)
record("F7.2: Post-Logout Protected Route Access Denied", code == 401, f"Rejected with 401: {res.get('message')}")

# 8. Security Regression: SQL Injection in Login
code, res = request('POST', '/api/auth/login', {'accountNumber': "' OR 1=1 --", 'pin': "123456"})
record("S1: SQL Injection Attack Rejection", code == 401, f"Attack blocked cleanly: {res.get('message')}")

# 9. Security Regression: Security Headers
req = urllib.request.Request(f"{BASE_URL}/api/health")
with urllib.request.urlopen(req) as resp:
    nosniff = resp.headers.get('X-Content-Type-Options')
    frame = resp.headers.get('X-Frame-Options')
    referrer = resp.headers.get('Referrer-Policy')
    has_headers = (nosniff == 'nosniff' and frame == 'DENY' and referrer == 'strict-origin-when-cross-origin')
    record("S2: Security Headers Verification", has_headers, f"Nosniff={nosniff}, Frame={frame}, Referrer={referrer}")

# 10. Security Regression: Rate Limiting
rate_limited = False
for i in range(25):
    c, r = request('POST', '/api/auth/login', {'accountNumber': '999999999999', 'pin': '000000'})
    if c == 429:
        rate_limited = True
        break
record("S3: Brute-Force Rate Limiting (HTTP 429)", rate_limited, "Rate limiter throttled excessive requests")

# Summary
print("\n" + "="*70)
total = len(results)
passed_count = sum(1 for r in results if r['status'] == 'PASS')
print(f"TEST SUMMARY: {passed_count}/{total} PASSED ({passed_count/total*100:.1f}%)")
print("="*70 + "\n")

if passed_count != total:
    sys.exit(1)
