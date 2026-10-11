#pragma once
// Security helpers built on libsodium (a widely reviewed crypto library). We do NOT write our
// own cryptography.
//
//  * hashPassword / verifyPassword : Argon2id, the current recommended password-hashing
//    algorithm. The result is ONE self-describing string that contains the algorithm, its
//    cost settings, a random salt and the hash, so verifying needs nothing else.
//  * randomToken : cryptographically secure random bytes as hex, used for session ids and
//    CSRF tokens (NOT std::rand / mt19937, which are predictable).
//  * constantTimeEquals : compares secrets without leaking, through timing, where they differ.
#include <string>

namespace security {

// Must be called once at program start (initialises libsodium). Throws on failure.
void init();

enum class HashCost { Standard, Cheap };
// Cheap is ONLY for automated tests (libsodium's minimum cost) so thousands of hashes stay fast.
void setHashCost(HashCost cost);

std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& hash, const std::string& password);
// A valid hash of a random password. Verifying against it takes as long as a real check, so
// "unknown user" and "wrong password" cannot be told apart by response time.
const std::string& decoyHash();

std::string randomToken(std::size_t bytes = 32);  // returns 2*bytes hex characters
bool constantTimeEquals(const std::string& a, const std::string& b);

}  // namespace security
