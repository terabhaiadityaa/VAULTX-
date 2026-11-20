
#include <iostream>
#include <string>

#include "security/PinHasher.h"

int main() {

    std::cout << "\n===== PIN HASHING TEST =====\n\n";

    std::string pin = "123456";

    // Test 1: Generate hash
    std::string hashedPin = PinHasher::hashPin(pin);

    if (hashedPin.empty()) {
        std::cout << "[FAIL] PIN hashing failed.\n";
        return 1;
    }

    std::cout << "[PASS] PIN hashed successfully.\n";

    // Test 2: Verify correct PIN
    if (PinHasher::verifyPin(pin, hashedPin)) {
        std::cout << "[PASS] Correct PIN accepted.\n";
    } else {
        std::cout << "[FAIL] Correct PIN rejected.\n";
        return 1;
    }

    // Test 3: Reject incorrect PIN
    if (!PinHasher::verifyPin("654321", hashedPin)) {
        std::cout << "[PASS] Incorrect PIN rejected.\n";
    } else {
        std::cout << "[FAIL] Incorrect PIN accepted.\n";
        return 1;
    }

    // Test 4: Reject invalid PIN length
    if (PinHasher::hashPin("1234").empty()) {
        std::cout << "[PASS] Invalid PIN length rejected.\n";
    } else {
        std::cout << "[FAIL] Invalid PIN length accepted.\n";
        return 1;
    }

    // Test 5: Verify random salt
    std::string secondHash = PinHasher::hashPin(pin);

    if (!secondHash.empty() && secondHash != hashedPin) {
        std::cout << "[PASS] Random salt generated.\n";
    } else {
        std::cout << "[FAIL] Random salt test failed.\n";
        return 1;
    }

    std::cout << "\n===== ALL TESTS PASSED =====\n";

    return 0;
}
