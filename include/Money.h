#pragma once
// Money is stored as a 64-bit integer number of paise (1 rupee = 100 paise).
// Integers are exact, so there is no floating-point rounding error.
#include <cstdint>
#include <string>

namespace money {

using Paise = std::int64_t;

constexpr Paise kMaxTransactionPaise = 1'000'000'000LL;     // Rs 1,00,00,000 per operation
constexpr Paise kMaxBalancePaise = 10'000'000'000'000LL;    // Rs 100 billion per account

// Parses text such as "1500", "1500.5" or "1500.50" (rupees) into paise.
// Rejects signs, exponents, commas, more than 2 decimals and huge values.
bool parseRupees(const std::string& text, Paise& out, std::string& error);

// Formats paise as rupees with two decimals, e.g. 150050 -> "1500.50".
std::string format(Paise paise);

}  // namespace money
