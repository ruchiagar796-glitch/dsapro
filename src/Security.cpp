#include "Security.h"

#include <sodium.h>

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "BankError.h"

namespace security {

namespace {
std::atomic<HashCost> g_cost{HashCost::Standard};
// Argon2id is deliberately memory-hungry (64 MiB per hash at the standard setting). Running
// hashes one at a time keeps the server's memory use predictable even if many people log in
// at once (important on a small 512 MB host). Rate limiting keeps the queue short.
std::mutex g_hashMutex;

unsigned long long opsLimit() {
    return g_cost == HashCost::Standard ? crypto_pwhash_OPSLIMIT_INTERACTIVE : crypto_pwhash_OPSLIMIT_MIN;
}
std::size_t memLimit() {
    return g_cost == HashCost::Standard ? crypto_pwhash_MEMLIMIT_INTERACTIVE : crypto_pwhash_MEMLIMIT_MIN;
}
}  // namespace

void init() {
    if (sodium_init() < 0) throw std::runtime_error("libsodium could not be initialised");
}

void setHashCost(HashCost cost) { g_cost = cost; }

std::string hashPassword(const std::string& password) {
    std::lock_guard<std::mutex> lock(g_hashMutex);
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str_alg(out, password.c_str(), password.size(), opsLimit(), memLimit(),
                              crypto_pwhash_ALG_ARGON2ID13) != 0)
        throw BankError(ErrorCode::Internal, "Password hashing failed (out of memory?)");
    return std::string(out);
}

bool verifyPassword(const std::string& hash, const std::string& password) {
    if (hash.empty() || hash.size() >= crypto_pwhash_STRBYTES) return false;
    std::lock_guard<std::mutex> lock(g_hashMutex);
    return crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.size()) == 0;
}

const std::string& decoyHash() {
    static const std::string h = hashPassword(randomToken(16));
    return h;
}

std::string randomToken(std::size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    randombytes_buf(buf.data(), buf.size());
    std::string hex(bytes * 2 + 1, '\0');
    sodium_bin2hex(&hex[0], hex.size(), buf.data(), buf.size());
    hex.resize(bytes * 2);
    return hex;
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
    // Different lengths are not secret here (tokens have a fixed length), so exit early on them.
    if (a.size() != b.size()) return false;
    return sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace security
