#pragma once
// A tiny test framework (no external dependency).
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); CHECK_THROWS_CODE(expr, ErrorCode::X); }
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "BankError.h"
#include "Money.h"

struct TestCase { std::string name; std::function<void()> fn; };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
inline int& failureCount() { static int n = 0; return n; }

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

#define TEST(name)                                   \
    static void test_##name();                       \
    static Registrar registrar_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            ++failureCount();                                                               \
            std::cout << "    FAILED " << __FILE__ << ":" << __LINE__ << "  " #cond << "\n"; \
        }                                                                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                                      \
    do {                                                                                    \
        const auto va = (a);                                                                \
        const auto vb = (b);                                                                \
        if (!(va == vb)) {                                                                  \
            ++failureCount();                                                               \
            std::ostringstream os;                                                          \
            os << "    FAILED " << __FILE__ << ":" << __LINE__ << "  " #a " == " #b          \
               << "  (got " << va << " vs " << vb << ")\n";                                 \
            std::cout << os.str();                                                          \
        }                                                                                   \
    } while (0)

#define CHECK_THROWS_CODE(expr, expectedCode)                                               \
    do {                                                                                    \
        bool threw = false;                                                                 \
        try { (void)(expr); }                                                               \
        catch (const BankError& e) {                                                        \
            threw = true;                                                                   \
            if (e.code() != (expectedCode)) {                                               \
                ++failureCount();                                                           \
                std::cout << "    FAILED " << __FILE__ << ":" << __LINE__                   \
                          << "  wrong error code: " << errorCodeName(e.code()) << " (" << e.what() << ")\n"; \
            }                                                                               \
        }                                                                                   \
        if (!threw) {                                                                       \
            ++failureCount();                                                               \
            std::cout << "    FAILED " << __FILE__ << ":" << __LINE__ << "  expected BankError from " #expr "\n"; \
        }                                                                                   \
    } while (0)

// ---- helpers shared by the tests ----
#include <chrono>
#include <filesystem>
#include <fstream>

// Creates a fresh empty folder in the system temp directory and removes it afterwards.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               ("bms_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "_" + std::to_string(counter++));
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    std::string str() const { return path.string(); }
};

inline std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
inline void writeFile(const std::filesystem::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}
inline money::Paise rs(long long rupees) { return rupees * 100; }

// ---- helpers for customer/auth tests ----
#include "Bank.h"
inline std::string registerTestCustomer(Bank& bank, const std::string& name, const std::string& email) {
    return bank.registerCustomer(name, email, "9876543210", "$argon2id$test-hash").id;
}
