
#ifndef PIN_HASHER_H
#define PIN_HASHER_H

#include <string>

class PinHasher {
public:
    static std::string hashPin(const std::string& pin);

    static bool verifyPin(
        const std::string& pin,
        const std::string& storedHash
    );

private:
    static constexpr int SALT_SIZE = 16;
    static constexpr int HASH_SIZE = 32;
    static constexpr int ITERATIONS = 600000;

    static std::string toHex(
        const unsigned char* data,
        int length
    );

    static bool fromHex(
        const std::string& hex,
        unsigned char* output,
        int outputSize
    );

    static bool isValidPin(const std::string& pin);
};

#endif
