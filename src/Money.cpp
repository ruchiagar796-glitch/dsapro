#include "Money.h"

#include <cctype>

namespace money {

static bool allDigits(const std::string& s) {
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool parseRupees(const std::string& input, Paise& out, std::string& error) {
    const auto b = input.find_first_not_of(" \t");
    if (b == std::string::npos) {
        error = "Amount is required";
        return false;
    }
    const auto e = input.find_last_not_of(" \t");
    const std::string s = input.substr(b, e - b + 1);

    if (s[0] == '-') {
        error = "Amount cannot be negative";
        return false;
    }
    const auto dot = s.find('.');
    const std::string whole = (dot == std::string::npos) ? s : s.substr(0, dot);
    const std::string frac = (dot == std::string::npos) ? "" : s.substr(dot + 1);

    const bool badShape = whole.empty() || !allDigits(whole) || !allDigits(frac) ||
                          frac.size() > 2 || (dot != std::string::npos && frac.empty());
    if (badShape) {
        error = "Amount is not valid. Use digits with at most 2 decimals, e.g. 1500.50";
        return false;
    }
    if (whole.size() > 12) {
        error = "Amount is too large";
        return false;
    }
    Paise value = 0;
    for (char c : whole) value = value * 10 + (c - '0');
    value *= 100;
    if (!frac.empty()) {
        Paise f = 0;
        for (char c : frac) f = f * 10 + (c - '0');
        if (frac.size() == 1) f *= 10;
        value += f;
    }
    out = value;
    return true;
}

std::string format(Paise p) {
    const bool neg = p < 0;
    const Paise a = neg ? -p : p;
    const Paise cents = a % 100;
    return std::string(neg ? "-" : "") + std::to_string(a / 100) + "." +
           (cents < 10 ? "0" : "") + std::to_string(cents);
}

}  // namespace money
