/**
 * PhoneUtils.h
 * Strict server-side mobile number validation and E.164 normalization.
 */

#ifndef PHONE_UTILS_H
#define PHONE_UTILS_H

#include <string>

struct PhoneParseResult {
    bool valid = false;
    std::string e164;           // Canonical format, e.g. "+919876543210"
    std::string countryCode;    // Dial code, e.g. "+91"
    std::string nationalNumber; // Normalized national digits, e.g. "9876543210"
    std::string errorMessage;
};

class PhoneUtils {
public:
    // Validates and normalizes countryCode + rawNumber (or standalone E.164 when countryCode is empty)
    // into canonical E.164 format. Never trusts unvalidated frontend inputs.
    static PhoneParseResult normalize(
        const std::string& rawCountryCode,
        const std::string& rawNumber
    );

    // Formats canonical E.164 for UI/logs safely without altering canonical comparison value.
    static bool isSupportedCountryCode(const std::string& countryCode);
};

#endif // PHONE_UTILS_H
