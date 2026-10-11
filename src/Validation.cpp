#include "Validation.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "BankError.h"

namespace validation {

namespace {
[[noreturn]] void bad(const std::string& m) { throw BankError(ErrorCode::InvalidInput, m); }

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool isAlpha(unsigned char c) { return std::isalpha(c) != 0 || c >= 0x80; }  // >=0x80: UTF-8 letters
}  // namespace

std::string fullName(const std::string& raw) {
    const std::string s = trim(raw);
    if (s.size() < 2) bad("Full name must be at least 2 characters");
    if (s.size() > 60) bad("Full name must be at most 60 characters");
    int letters = 0;
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (isAlpha(c)) ++letters;
        else if (c == ' ' || c == '.' || c == '\'' || c == '-') continue;
        else bad("Full name may contain only letters, spaces, dots, apostrophes and hyphens");
    }
    if (letters < 2) bad("Full name must contain at least 2 letters");
    return s;
}

std::string email(const std::string& raw) {
    const std::string s = lower(trim(raw));
    if (s.empty()) bad("Email address is required");
    if (s.size() > 254) bad("Email address is too long");
    const auto at = s.find('@');
    if (at == std::string::npos || at != s.rfind('@') || at == 0 || at > 64) bad("Enter a valid email address");
    const std::string local = s.substr(0, at), domain = s.substr(at + 1);
    for (char ch : local) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-')) bad("Enter a valid email address");
    }
    if (local.front() == '.' || local.back() == '.' || local.find("..") != std::string::npos) bad("Enter a valid email address");
    if (domain.size() < 4 || domain.find('.') == std::string::npos || domain.front() == '.' || domain.back() == '.' ||
        domain.front() == '-' || domain.find("..") != std::string::npos)
        bad("Enter a valid email address");
    for (char ch : domain) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || c == '.' || c == '-')) bad("Enter a valid email address");
    }
    const std::string tld = domain.substr(domain.rfind('.') + 1);
    if (tld.size() < 2 || !std::all_of(tld.begin(), tld.end(), [](unsigned char c) { return std::isalpha(c) != 0; }))
        bad("Enter a valid email address");
    return s;
}

std::string phone(const std::string& raw) {
    std::string out;
    for (char ch : trim(raw)) {
        if (ch == ' ' || ch == '-') continue;
        out += ch;
    }
    std::size_t digits = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(out[i]);
        if (std::isdigit(c)) ++digits;
        else if (!(c == '+' && i == 0)) bad("Phone number may contain only digits, spaces, hyphens and a leading +");
    }
    if (digits < 10 || digits > 15) bad("Phone number must have 10 to 15 digits");
    return out;
}

std::string username(const std::string& raw) {
    const std::string s = lower(trim(raw));
    if (s.size() < 3 || s.size() > 32) bad("Username must be 3 to 32 characters");
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) bad("Username may contain only letters, digits, dot, underscore and hyphen");
    }
    return s;
}

void password(const std::string& pw, const std::string& identity, std::size_t minLength) {
    if (pw.size() < minLength) bad("Password must be at least " + std::to_string(minLength) + " characters");
    if (pw.size() > 128) bad("Password must be at most 128 characters");
    bool letter = false, digit = false;
    for (char ch : pw) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f) bad("Password contains invalid characters");
        if (std::isalpha(c)) letter = true;
        if (std::isdigit(c)) digit = true;
    }
    if (!letter || !digit) bad("Password must contain at least one letter and one number");
    const std::string lp = lower(pw);
    if (!identity.empty() && lp == lower(identity)) bad("Password must not be the same as your email or username");
    static const std::array<const char*, 8> common = {"password1", "password123", "12345678a", "qwerty123",
                                                      "admin12345", "welcome123", "iloveyou1", "abc123456"};
    for (const char* c : common)
        if (lp == c) bad("That password is too common. Choose something harder to guess");
}

}  // namespace validation
