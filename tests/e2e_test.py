#!/usr/bin/env python3
"""
VAULTX Banking Simulation Platform — Automated End-to-End Test Suite
"""

import urllib.request
import json
import time
import sys

BASE_URL = 'http://127.0.0.1:8080' if len(sys.argv) < 2 else sys.argv[1]

def get(path, headers=None):
    req = urllib.request.Request(f"{BASE_URL}{path}", headers=headers or {})
    with urllib.request.urlopen(req) as resp:
        return resp.status, json.loads(resp.read().decode('utf-8'))

def post(path, data, headers=None):
    body = json.dumps(data).encode('utf-8')
    h = {'Content-Type': 'application/json'}
    if headers:
        h.update(headers)
    req = urllib.request.Request(f"{BASE_URL}{path}", data=body, headers=h)
    with urllib.request.urlopen(req) as resp:
        return resp.status, json.loads(resp.read().decode('utf-8'))

def run_tests():
    print(f"[*] Running E2E Test Suite against: {BASE_URL}")

    # 1. Health check
    st, health = get('/api/health')
    assert st == 200 and health['status'] == 'success', f"Health check failed: {health}"
    print("  [✓] Health check OK")

    # 2. Register User 1
    ts = int(time.time() * 1000)
    email1 = f"e2e_alice_{ts}@vaultx.test"
    st, reg1 = post('/api/auth/register', {
        'fullName': 'Alice Robertson',
        'email': email1,
        'pin': '123456'
    })
    assert st == 201 and reg1['status'] == 'success'
    acc1 = reg1['accountNumber']
    print(f"  [✓] User 1 registered: {acc1} with initial balance ₹{reg1['initialBalance']}")

    # 3. Register User 2
    email2 = f"e2e_bob_{ts}@vaultx.test"
    st, reg2 = post('/api/auth/register', {
        'fullName': 'Bob Robertson',
        'email': email2,
        'pin': '654321'
    })
    assert st == 201 and reg2['status'] == 'success'
    acc2 = reg2['accountNumber']
    print(f"  [✓] User 2 registered: {acc2}")

    # 4. Login User 1
    st, log1 = post('/api/auth/login', {'email': email1, 'pin': '123456'})
    token = log1['sessionToken']
    auth_h = {'Authorization': f'Bearer {token}'}
    assert log1['balance'] == 10000.0
    print(f"  [✓] User 1 authenticated. Token: {token[:12]}...")

    # 5. Deposit 5,000
    st, dep = post('/api/deposit', {'amount': 5000.0}, auth_h)
    assert dep['balance'] == 15000.0
    print(f"  [✓] Deposit ₹5,000 OK. Balance: ₹{dep['balance']}")

    # 6. Withdraw 2,000
    st, wth = post('/api/withdraw', {'amount': 2000.0}, auth_h)
    assert wth['balance'] == 13000.0
    print(f"  [✓] Withdrawal ₹2,000 OK. Balance: ₹{wth['balance']}")

    # 7. Transfer 3,000 to User 2
    st, trf = post('/api/transfer', {'targetAccount': acc2, 'amount': 3000.0}, auth_h)
    assert trf['balance'] == 10000.0
    print(f"  [✓] Transfer ₹3,000 to User 2 OK. Remaining Balance: ₹{trf['balance']}")

    # 8. Check Transaction Ledger
    st, txs = get('/api/transactions', auth_h)
    assert txs['count'] >= 4
    print(f"  [✓] Ledger verified: {txs['count']} transactions recorded.")

    print("\n[SUCCESS] All E2E Integration Tests Passed Perfectly!\n")

if __name__ == '__main__':
    run_tests()
