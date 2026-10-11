// Runs every registered TEST. Optional argument: only run tests whose name contains it.
#include <exception>
#include <iostream>

#include "Security.h"
#include "test_framework.h"

int main(int argc, char** argv) {
    security::init();
    security::setHashCost(security::HashCost::Cheap);  // libsodium minimum cost: keeps the tests fast
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0, failedTests = 0;
    for (const TestCase& t : registry()) {
        if (!filter.empty() && t.name.find(filter) == std::string::npos) continue;
        ++ran;
        const int before = failureCount();
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++failureCount();
            std::cout << "    EXCEPTION: " << e.what() << "\n";
        }
        const bool failed = failureCount() != before;
        if (failed) ++failedTests;
        std::cout << (failed ? "[FAIL] " : "[ ok ] ") << t.name << "\n";
    }
    std::cout << "\n" << (ran - failedTests) << "/" << ran << " tests passed\n";
    return failedTests == 0 ? 0 : 1;
}
