
#include "security/PinHasher.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#include <sstream>
#include <iomanip>
#include <vector>
#include <cctype>

bool PinHasher::isValidPin(const std::string& pin) {
    if (pin.length() != 6) {
        return false;
    }

    for (unsigned char ch : pin) {
        if (!std::isdigit(ch)) {
            return false;
        }
    }

    return true;
}

std::string PinHasher::toHex(
    const unsigned char* data,
    int length
) {
    std::ostringstream result;

    result << std::hex << std::setfill('0');

    for (int i = 0; i < length; i++) {
        result << std::setw(2)
               << static_cast<int>(data[i]);
    }

    return result.str();
}

bool PinHasher::fromHex(
    const std::string& hex,
    unsigned char* output,
    int outputSize
) {
    if (hex.length() != static_cast<size_t>(outputSize * 2)) {
        return false;
    }

    for (int i = 0; i < outputSize; i++) {
        try {
            std::string byteString = hex.substr(i * 2, 2);

            size_t processed = 0;

            int value = std::stoi(
                byteString,
                &processed,
                16
            );

            if (processed != 2 || value < 0 || value > 255) {
                return false;
            }

            output[i] = static_cast<unsigned char>(value);
        }
        catch (...) {
            return false;
        }
    }

    return true;
}

std::string PinHasher::hashPin(const std::string& pin) {

    if (!isValidPin(pin)) {
        return "";
    }

    unsigned char salt[SALT_SIZE];
    unsigned char hash[HASH_SIZE];

    // Generate a cryptographically secure random salt.
    if (RAND_bytes(salt, SALT_SIZE) != 1) {
        return "";
    }

    // Generate PBKDF2-HMAC-SHA256 hash.
    if (PKCS5_PBKDF2_HMAC(
            pin.c_str(),
            static_cast<int>(pin.length()),
            salt,
            SALT_SIZE,
            ITERATIONS,
            EVP_sha256(),
            HASH_SIZE,
            hash
        ) != 1) {
        return "";
    }

    // Store algorithm, iterations, salt and hash.
    return "pbkdf2_sha256$"
           + std::to_string(ITERATIONS) + "$"
           + toHex(salt, SALT_SIZE) + "$"
           + toHex(hash, HASH_SIZE);
}

bool PinHasher::verifyPin(
    const std::string& pin,
    const std::string& storedHash
) {

    if (!isValidPin(pin)) {
        return false;
    }

    std::stringstream parser(storedHash);

    std::string algorithm;
    std::string iterationString;
    std::string saltHex;
    std::string hashHex;
    std::string extra;

    std::getline(parser, algorithm, '$');
    std::getline(parser, iterationString, '$');
    std::getline(parser, saltHex, '$');
    std::getline(parser, hashHex, '$');

    // Reject malformed or unexpected formats.
    if (algorithm != "pbkdf2_sha256" || (parser >> extra)) {
        return false;
    }

    int iterations = 0;

    try {
        size_t processed = 0;

        iterations = std::stoi(iterationString, &processed);

        if (processed != iterationString.length()) {
            return false;
        }
    }
    catch (...) {
        return false;
    }

    // Reject unreasonable iteration counts.
    if (iterations < 100000 || iterations > 2000000) {
        return false;
    }

    unsigned char salt[SALT_SIZE];
    unsigned char originalHash[HASH_SIZE];
    unsigned char calculatedHash[HASH_SIZE];

    if (!fromHex(saltHex, salt, SALT_SIZE)) {
        return false;
    }

    if (!fromHex(hashHex, originalHash, HASH_SIZE)) {
        return false;
    }

    if (PKCS5_PBKDF2_HMAC(
            pin.c_str(),
            static_cast<int>(pin.length()),
            salt,
            SALT_SIZE,
            iterations,
            EVP_sha256(),
            HASH_SIZE,
            calculatedHash
        ) != 1) {
        return false;
    }

    // Constant-time comparison prevents timing-based leaks.
    return CRYPTO_memcmp(
        originalHash,
        calculatedHash,
        HASH_SIZE
    ) == 0;
}
