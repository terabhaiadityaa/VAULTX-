#!/usr/bin/env python3
"""
VAULTX Banking Simulation Platform — Automated End-to-End Test Suite (Mobile Auth)
"""

import json
import sys
import time
import urllib.request

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
    print("  [OK] Health check")

    # Unique 10-digit Indian test mobile numbers starting with '98' (never collides on ephemeral DB)
    tail = int(time.time() * 1000) % 10000000
    mobile1 = f"98{tail:07d}1"[:10]
    mobile2 = f"98{tail:07d}2"[:10]

    # 2. Register User 1
    st, reg1 = post('/api/auth/register', {
        'fullName': 'Alice Robertson',
        'countryCode': '+91',
        'mobileNumber': mobile1,
        'pin': '123456',
        'confirmPin': '123456'
    })
    assert st == 201 and reg1['status'] == 'success'
    assert reg1['mobileNumber'] == f"+91{mobile1}"
    print(f"  [OK] User 1 registered: {reg1['mobileNumber']} -> card {reg1['accountNumber']}")

    # 3. Register User 2
    st, reg2 = post('/api/auth/register', {
        'fullName': 'Bob Robertson',
        'countryCode': '+91',
        'mobileNumber': mobile2,
        'pin': '654321',
        'confirmPin': '654321'
    })
    assert st == 201 and reg2['status'] == 'success'
    acc2 = reg2['accountNumber']
    print(f"  [OK] User 2 registered: {reg2['mobileNumber']} -> card {acc2}")

    # 4. Login User 1 with canonical E.164 mobile number + PIN (and verify phone_number alias for User 2)
    st, log1 = post('/api/auth/login', {'mobile_number': f"+91{mobile1}", 'pin': '123456'})
    token = log1['sessionToken']
    auth_h = {'Authorization': f'Bearer {token}'}
    assert st == 200 and log1['balance'] == 10000.0
    st2, log2 = post('/api/auth/login', {'phone_number': f"+91{mobile2}", 'pin': '654321'})
    assert st2 == 200 and log2['accountNumber'] == acc2
    print("  [OK] Users authenticated with mobile_number / phone_number + PIN")

    # 5. Deposit 5,000
    st, dep = post('/api/deposit', {'amount': 5000.0}, auth_h)
    assert dep['balance'] == 15000.0
    print(f"  [OK] Deposit INR 5,000 -> balance INR {dep['balance']}")

    # 6. Withdraw 2,000
    st, wth = post('/api/withdraw', {'amount': 2000.0}, auth_h)
    assert wth['balance'] == 13000.0
    print(f"  [OK] Withdrawal INR 2,000 -> balance INR {wth['balance']}")

    # 7. Transfer 3,000 to User 2
    st, trf = post('/api/transfer', {'targetAccount': acc2, 'amount': 3000.0}, auth_h)
    assert trf['balance'] == 10000.0
    print(f"  [OK] Transfer INR 3,000 to User 2 -> balance INR {trf['balance']}")

    # 8. Check Transaction Ledger
    st, txs = get('/api/transactions', auth_h)
    assert txs['count'] >= 4
    print(f"  [OK] Ledger verified: {txs['count']} transactions recorded")

    print("\n[SUCCESS] All E2E Integration Tests Passed Perfectly!\n")


if __name__ == '__main__':
    run_tests()
