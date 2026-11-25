/**
 * PhoneUtils.cpp
 * Strict server-side mobile number validation and E.164 normalization.
 */

#include "security/PhoneUtils.h"

#include <cctype>
#include <vector>

namespace {

struct CountryRule {
    const char* code;      // With '+', e.g. "+91"
    const char* digits;    // Without '+', e.g. "91"
    size_t minLen;
    size_t maxLen;
    bool stripLeadingZero;
    const char* firstDigitsAllowed; // nullptr = 1-9, else explicit allowed leading chars
};

const std::vector<CountryRule>& getCountryRules() {
    static const std::vector<CountryRule> rules = {
        {"+91",  "91",  10, 10, true,  "6789"}, // India
        {"+1",   "1",   10, 10, false, "23456789"}, // US / Canada
        {"+44",  "44",  10, 10, true,  "123456789"}, // UK
        {"+61",  "61",   9,  9, true,  "23456789"}, // Australia
        {"+65",  "65",   8,  8, false, "3689"}, // Singapore
        {"+971", "971",  9,  9, true,  "23456789"}, // UAE
        {"+49",  "49",  10, 11, true,  "123456789"}, // Germany
        {"+33",  "33",   9,  9, true,  "123456789"}, // France
        {"+81",  "81",  10, 10, true,  "123456789"}  // Japan
    };
    return rules;
}

std::string trimCopy(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

const CountryRule* findRuleByCode(const std::string& code) {
    for (const auto& rule : getCountryRules()) {
        if (code == rule.code) {
            return &rule;
        }
    }
    return nullptr;
}

bool containsChar(const char* allowed, char c) {
    if (!allowed) return c >= '1' && c <= '9';
    for (const char* p = allowed; *p != '\0'; ++p) {
        if (*p == c) return true;
    }
    return false;
}

} // namespace

bool PhoneUtils::isSupportedCountryCode(const std::string& countryCode) {
    return findRuleByCode(trimCopy(countryCode)) != nullptr;
}

PhoneParseResult PhoneUtils::normalize(
    const std::string& rawCountryCode,
    const std::string& rawNumber
) {
    PhoneParseResult res;

    if (rawCountryCode.find('\0') != std::string::npos ||
        rawNumber.find('\0') != std::string::npos) {
        res.errorMessage = "Invalid characters in mobile number.";
        return res;
    }

    std::string cc = trimCopy(rawCountryCode);
    std::string num = trimCopy(rawNumber);

    if (num.empty()) {
        res.errorMessage = "Mobile number is required.";
        return res;
    }

    // Reject anything other than digits, optional leading '+', spaces, '-', '(', ')'
    bool hasLeadingPlus = false;
    std::string digits;
    digits.reserve(num.size());

    for (size_t i = 0; i < num.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(num[i]);
        if (ch == '+') {
            if (i != 0 || hasLeadingPlus) {
                res.errorMessage = "Invalid '+' placement in mobile number.";
                return res;
            }
            hasLeadingPlus = true;
        } else if (std::isdigit(ch)) {
            digits.push_back(static_cast<char>(ch));
        } else if (ch == ' ' || ch == '-' || ch == '(' || ch == ')') {
            continue;
        } else {
            res.errorMessage = "Mobile number may contain only digits and formatting separators.";
            return res;
        }
    }

    if (digits.empty()) {
        res.errorMessage = "Mobile number must contain digits.";
        return res;
    }

    const CountryRule* rule = nullptr;

    if (!cc.empty()) {
        // Normalize country code format if caller passed e.g. "91" instead of "+91"
        if (cc.front() != '+') {
            for (unsigned char ch : cc) {
                if (!std::isdigit(ch)) {
                    res.errorMessage = "Invalid country code.";
                    return res;
                }
            }
            cc = "+" + cc;
        }
        rule = findRuleByCode(cc);
        if (!rule) {
            res.errorMessage = "Unsupported or invalid country code.";
            return res;
        }

        const std::string ccDigits = rule->digits;
        if (hasLeadingPlus) {
            // If rawNumber includes '+', its country code prefix must match selected cc
            if (digits.rfind(ccDigits, 0) != 0) {
                res.errorMessage = "Mobile number country prefix does not match selected country code.";
                return res;
            }
            digits = digits.substr(ccDigits.size());
        } else {
            // Also collapse repeated country prefix without '+' when national length matches cleanly
            if (digits.size() > rule->maxLen && digits.rfind(ccDigits, 0) == 0) {
                const std::string stripped = digits.substr(ccDigits.size());
                if (stripped.size() >= rule->minLen && stripped.size() <= rule->maxLen + (rule->stripLeadingZero ? 1 : 0)) {
                    digits = stripped;
                }
            }
        }
    } else {
        // Combined input must start with '+' and match a supported country code prefix (longest first)
        if (!hasLeadingPlus) {
            res.errorMessage = "Country code is required for mobile number.";
            return res;
        }

        const CountryRule* bestMatch = nullptr;
        size_t bestLen = 0;
        for (const auto& candidate : getCountryRules()) {
            const std::string cDigits = candidate.digits;
            if (digits.rfind(cDigits, 0) == 0 && cDigits.size() > bestLen) {
                bestMatch = &candidate;
                bestLen = cDigits.size();
            }
        }

        if (!bestMatch) {
            res.errorMessage = "Unsupported or invalid country code.";
            return res;
        }
        rule = bestMatch;
        digits = digits.substr(bestLen);
    }

    // Strip a single national trunk leading '0' (e.g., 09876543210 -> 9876543210) where valid
    if (rule->stripLeadingZero && digits.size() == rule->maxLen + 1 && digits.front() == '0') {
        digits.erase(digits.begin());
    }

    if (digits.size() < rule->minLen || digits.size() > rule->maxLen) {
        if (std::string(rule->code) == "+91") {
            res.errorMessage = "Enter a valid 10-digit Indian mobile number.";
        } else {
            res.errorMessage = "Mobile number length is invalid for selected country code.";
        }
        return res;
    }

    if (!containsChar(rule->firstDigitsAllowed, digits.front())) {
        if (std::string(rule->code) == "+91") {
            res.errorMessage = "Indian mobile numbers must start with digits 6, 7, 8, or 9.";
        } else {
            res.errorMessage = "Mobile number format is invalid for selected country code.";
        }
        return res;
    }

    const std::string e164 = std::string(rule->code) + digits;
    if (e164.size() < 9 || e164.size() > 16) { // '+' + 8..15 E.164 digits
        res.errorMessage = "Mobile number violates E.164 length bounds.";
        return res;
    }

    res.valid = true;
    res.countryCode = rule->code;
    res.nationalNumber = digits;
    res.e164 = e164;
    return res;
}
