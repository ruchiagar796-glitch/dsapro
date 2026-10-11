#include <filesystem>

#include "Bank.h"
#include "test_framework.h"

namespace fs = std::filesystem;

TEST(registration_persists_and_survives_restart) {
    TempDir dir;
    std::string id;
    long long acc = 0;
    {
        Bank bank(dir.str(), 5000);
        const Customer& c = bank.registerCustomer("Asha | Verma", "asha@example.com", "9876543210", "$argon2id$h|ash\\x");
        id = c.id;
        acc = c.accountNumber;
        CHECK_EQ(acc, 5000LL);
    }
    Bank again(dir.str(), 5000);
    const Customer* c = again.users().findCustomerByEmail("asha@example.com");
    CHECK(c != nullptr);
    CHECK_EQ(c->id, id);
    CHECK_EQ(c->fullName, std::string("Asha | Verma"));                    // delimiters survive escaping
    CHECK_EQ(c->passwordHash, std::string("$argon2id$h|ash\\x"));
    CHECK_EQ(c->accountNumber, acc);
    CHECK_EQ(again.findAccount(acc)->balance(), 0LL);
    CHECK(again.users().findCustomerByAccount(acc) == c);
    const Customer& d = again.registerCustomer("Next Person", "next@example.com", "9876543210", "h");
    CHECK_EQ(d.id, std::string("CUS000002"));                               // counters continue
    CHECK_EQ(d.accountNumber, 5001LL);
}

TEST(duplicate_registration_is_rejected_atomically) {
    TempDir dir;
    Bank bank(dir.str());
    bank.registerCustomer("One Person", "one@example.com", "9876543210", "h");
    CHECK_THROWS_CODE(bank.registerCustomer("Other Person", "one@example.com", "9999999999", "h"), ErrorCode::Conflict);
    CHECK_EQ(bank.allAccounts().size(), static_cast<std::size_t>(1));       // no orphan account was left behind
    CHECK_EQ(bank.users().customerCount(), static_cast<std::size_t>(1));
    Bank again(dir.str());
    CHECK_EQ(again.allAccounts().size(), static_cast<std::size_t>(1));
}

TEST(failed_save_does_not_leave_half_a_registration) {
    TempDir dir;
    Bank bank(dir.str());
    bank.registerCustomer("Safe Person", "safe@example.com", "9876543210", "h");
    fs::create_directory(dir.path / "customers.txt.tmp");                   // makes the next save fail
    CHECK_THROWS_CODE(bank.registerCustomer("Doomed Person", "doomed@example.com", "9876543210", "h"), ErrorCode::StorageFailure);
    CHECK(bank.users().findCustomerByEmail("doomed@example.com") == nullptr);
    CHECK_EQ(bank.allAccounts().size(), static_cast<std::size_t>(1));
    fs::remove(dir.path / "customers.txt.tmp");
    bank.registerCustomer("Doomed Person", "doomed@example.com", "9876543210", "h");   // works afterwards
    Bank again(dir.str());
    CHECK_EQ(again.users().customerCount(), static_cast<std::size_t>(2));
}

TEST(admins_and_account_status_persist) {
    TempDir dir;
    long long acc = 0;
    {
        Bank bank(dir.str());
        bank.createAdmin("root.admin", "$argon2id$adminhash");
        CHECK_THROWS_CODE(bank.createAdmin("root.admin", "x"), ErrorCode::Conflict);
        acc = bank.registerCustomer("Status Test", "status@example.com", "9876543210", "h").accountNumber;
        bank.setAccountActive(acc, false);
    }
    Bank again(dir.str());
    CHECK(again.users().hasAdmin());
    CHECK_EQ(again.users().findAdminByUsername("root.admin")->passwordHash, std::string("$argon2id$adminhash"));
    CHECK(!again.findAccount(acc)->active());
    CHECK_THROWS_CODE(again.deposit(acc, rs(10)), ErrorCode::Forbidden);
    again.setAccountActive(acc, true);
    again.deposit(acc, rs(10));
    CHECK_THROWS_CODE(again.setAccountActive(777, true), ErrorCode::NotFound);
}

TEST(old_five_field_account_files_still_load_as_active) {
    TempDir dir;
    writeFile(dir.path / "accounts.txt", "1001|Legacy User|Savings|123400|1700000000\n");
    Bank bank(dir.str());
    CHECK(bank.findAccount(1001)->active());
    CHECK_EQ(bank.findAccount(1001)->balance(), 123400LL);
    bank.deposit(1001, rs(1));                                               // and it rewrites in the new format
    CHECK(readFile(dir.path / "accounts.txt").find("|active") != std::string::npos);
}

TEST(corrupt_customer_and_admin_files_are_rejected_untouched) {
    struct Case { const char* accounts; const char* file; const char* content; };
    const char* oneAccount = "1001|A B|Savings|0|0|active\n";
    const Case cases[] = {
        {oneAccount, "customers.txt", "CUS000001|A B|a@b.com|9876543210|hash|1001\n"},                          // 6 fields
        {oneAccount, "customers.txt", "CUS000001|A B|no-at-sign|9876543210|hash|1001|0\n"},
        {oneAccount, "customers.txt", "CUS000001|A B|a@b.com|9876543210|hash|9999|0\n"},                        // missing account
        {oneAccount, "customers.txt", "CUS000001|A B|a@b.com|1|h|1001|0\nCUS000002|C D|a@b.com|1|h|1001|0\n"},  // dup email
        {oneAccount, "customers.txt", "CUS000001|A B|a@b.com|1|h|1001|0\nCUS000001|C D|c@b.com|1|h|1001|0\n"},  // dup id
        {oneAccount, "admins.txt", "ADM0001|root\n"},
        {oneAccount, "admins.txt", "ADM0001|root|h|0\nADM0002|root|h|0\n"},                                    // dup username
        {"1001|A B|Savings|0|0|frozen\n", "metadata.txt", ""},                                                 // unknown status
    };
    for (const Case& c : cases) {
        TempDir dir;
        writeFile(dir.path / "accounts.txt", c.accounts);
        writeFile(dir.path / c.file, c.content);
        CHECK_THROWS_CODE(Bank(dir.str()), ErrorCode::DataCorrupt);
        CHECK_EQ(readFile(dir.path / c.file), std::string(c.content));
    }
}

TEST(crash_after_commit_marker_rolls_forward) {
    TempDir dir;
    long long acc = 0;
    {
        Bank bank(dir.str());
        acc = bank.registerCustomer("Crash Test", "crash@example.com", "9876543210", "h").accountNumber;
        bank.deposit(acc, rs(100));
    }
    // Simulate a crash in the middle of the NEXT save: a complete new accounts file (balance 250)
    // waits as ".tmp", the COMMIT marker exists, and the real file still holds the old data.
    const std::string old = readFile(dir.path / "accounts.txt");
    CHECK(old.find("|10000|") != std::string::npos);
    std::string neu = old;
    neu.replace(neu.find("|10000|"), 7, "|25000|");
    writeFile(dir.path / "accounts.txt.tmp", neu);
    writeFile(dir.path / "COMMIT", "commit\n");
    Bank after(dir.str());
    CHECK_EQ(after.findAccount(acc)->balance(), rs(250));                    // the committed save was completed
    CHECK(!fs::exists(dir.path / "COMMIT"));
    CHECK(!fs::exists(dir.path / "accounts.txt.tmp"));
}

TEST(crash_before_commit_marker_keeps_old_data) {
    TempDir dir;
    long long acc = 0;
    {
        Bank bank(dir.str());
        acc = bank.registerCustomer("Crash Test", "crash@example.com", "9876543210", "h").accountNumber;
        bank.deposit(acc, rs(100));
    }
    writeFile(dir.path / "accounts.txt.tmp", "garbage that was only half written");
    writeFile(dir.path / "transactions.txt.tmp", "1|");
    Bank after(dir.str());
    CHECK_EQ(after.findAccount(acc)->balance(), rs(100));                    // old consistent state
    CHECK(!fs::exists(dir.path / "accounts.txt.tmp"));
    CHECK(!fs::exists(dir.path / "transactions.txt.tmp"));
}

TEST(registered_accounts_cannot_be_closed_and_numbering_is_configurable) {
    Bank bank("", 900000);
    const Customer& c = bank.registerCustomer("Keep Me", "keep@example.com", "9876543210", "h");
    CHECK_EQ(c.accountNumber, 900000LL);
    CHECK_THROWS_CODE(bank.closeAccount(c.accountNumber), ErrorCode::Conflict);
}

TEST(transfers_between_customers_survive_restart_consistently) {
    TempDir dir;
    long long a = 0, b = 0;
    {
        Bank bank(dir.str());
        a = bank.registerCustomer("Payer One", "p1@example.com", "9876543210", "h").accountNumber;
        b = bank.registerCustomer("Payee Two", "p2@example.com", "9876543210", "h").accountNumber;
        bank.deposit(a, rs(1000));
        bank.transfer(a, b, rs(300) + 50);
    }
    Bank again(dir.str());
    CHECK_EQ(again.findAccount(a)->balance(), rs(699) + 50);
    CHECK_EQ(again.findAccount(b)->balance(), rs(300) + 50);
    const auto t = again.transactionsFor(a);
    CHECK_EQ(t[0].reference(), again.transactionsFor(b)[0].reference());     // shared reference
    CHECK(t[0].reference().rfind("TXN", 0) == 0);
}
