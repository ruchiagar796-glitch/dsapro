#pragma once
// Validation: server-side checks for what people type in forms. The browser also checks these
// (for friendly messages) but the SERVER is the only check that counts, because anyone can
// bypass the browser. Every function returns a cleaned value or throws BankError(InvalidInput).
#include <string>

namespace validation {

std::string fullName(const std::string& raw);   // 2-60 chars: letters, spaces, . ' -
std::string email(const std::string& raw);      // returns the lower-case form
std::string phone(const std::string& raw);      // digits with optional leading '+', 10-15 digits
std::string username(const std::string& raw);   // admin user name: 3-32 of [a-z0-9._-]
// Rules: at least minLength characters, at most 128, a letter AND a digit, not the e-mail/user
// name itself and not a very common password.
void password(const std::string& password, const std::string& identity, std::size_t minLength = 8);

}  // namespace validation
