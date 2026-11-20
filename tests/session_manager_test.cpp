
#include "security/SessionManager.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    // Use a 2-second timeout for testing.
    SessionManager manager(std::chrono::seconds(2));

    int userId = 0;

    // Test 1: Create a session.
    std::string token = manager.createSession(101);

    if (token.empty()) {
        std::cout << "FAIL: Session creation\n";
        return 1;
    }

    std::cout << "PASS: Session creation\n";

    // Test 2: Validate an active session.
    if (!manager.validateSession(token, userId) || userId != 101) {
        std::cout << "FAIL: Active session validation\n";
        return 1;
    }

    std::cout << "PASS: Active session validation\n";

    // Test 3: Confirm activity refreshes the timeout.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (!manager.validateSession(token, userId)) {
        std::cout << "FAIL: Session activity refresh\n";
        return 1;
    }

    std::cout << "PASS: Session activity refresh\n";

    // Test 4: Wait long enough for inactivity expiry.
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));

    if (manager.validateSession(token, userId)) {
        std::cout << "FAIL: Expired session was accepted\n";
        return 1;
    }

    std::cout << "PASS: Expired session rejected\n";

    // Test 5: Confirm expired session is removed.
    if (manager.getActiveSessionCount() != 0) {
        std::cout << "FAIL: Expired session count\n";
        return 1;
    }

    std::cout << "PASS: Expired session count\n";

    std::cout << "\nAll session tests passed.\n";

    return 0;
}
