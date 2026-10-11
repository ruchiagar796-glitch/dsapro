#include <filesystem>

#include "Bank.h"
#include "test_framework.h"

namespace fs = std::filesystem;

TEST(save_and_reload_restores_everything) {
    TempDir dir;
    {
        Bank bank(dir.str());
        const long long a = bank.createAccount("Asha | Verma\\Jr", AccountType::Savings, rs(1000)).accountNumber();
        const long long b = bank.createAccount("Bhanu", AccountType::Current, rs(6000)).accountNumber();
        bank.deposit(a, rs(100) + 25);
        bank.transfer(b, a, rs(1000));
        bank.issueToken("Waiting Person", "Loan | query");
    }
    Bank again(dir.str());  // simulates restarting the application
    CHECK_EQ(again.allAccounts().size(), static_cast<std::size_t>(2));
    const BankAccount* a = again.findAccount(1001);
    CHECK(a != nullptr);
    CHECK_EQ(a->holderName(), std::string("Asha | Verma\\Jr"));  // delimiters survive escaping
    CHECK_EQ(a->balance(), rs(2100) + 25);
    CHECK_EQ(again.findAccount(1002)->balance(), rs(5000));
    CHECK(again.findAccount(1002)->type() == AccountType::Current);
    CHECK_EQ(again.summary().transactionCount, static_cast<std::size_t>(5));
    CHECK_EQ(again.queueSnapshot().size(), static_cast<std::size_t>(1));
    CHECK_EQ(again.queueSnapshot()[0].purpose, std::string("Loan | query"));
}

TEST(ids_remain_unique_after_restart) {
    TempDir dir;
    long long lastTx = 0;
    {
        Bank bank(dir.str());
        bank.createAccount("One", AccountType::Savings, rs(500));
        const long long two = bank.createAccount("Two", AccountType::Savings, rs(500)).accountNumber();
        bank.withdraw(two, rs(500));
        bank.closeAccount(two);  // number 1002 is now retired
        lastTx = bank.recentTransactions(1)[0].id();
        bank.issueToken("T", "");
        bank.serveNextCustomer();
    }
    Bank again(dir.str());
    const long long n = again.createAccount("Three", AccountType::Savings, rs(500)).accountNumber();
    CHECK_EQ(n, 1003LL);  // 1002 was closed and is NOT reused
    CHECK(again.recentTransactions(1)[0].id() > lastTx);
    CHECK_EQ(again.issueToken("T2", "").number, 2LL);  // token numbers continue
}

TEST(reversal_state_survives_restart_and_blocks_duplicates) {
    TempDir dir;
    long long depositId = 0;
    {
        Bank bank(dir.str());
        const long long a = bank.createAccount("Rev", AccountType::Savings, rs(1000)).accountNumber();
        depositId = bank.deposit(a, rs(200)).id();
        bank.reverseLastTransaction(depositId);
    }
    Bank again(dir.str());
    CHECK_EQ(again.findAccount(1001)->balance(), rs(1000));
    CHECK_THROWS_CODE(again.reverseLastTransaction(depositId), ErrorCode::Conflict);
    // the initial deposit (not yet reversed) is rebuilt on the stack
    CHECK(again.peekReversible().has_value());
    CHECK(again.peekReversible()->description() == "Initial deposit");
}

TEST(stack_order_is_rebuilt_after_restart) {
    TempDir dir;
    {
        Bank bank(dir.str());
        const long long a = bank.createAccount("Order", AccountType::Savings, rs(1000)).accountNumber();
        bank.deposit(a, rs(10));
        bank.withdraw(a, rs(20));
    }
    Bank again(dir.str());
    CHECK(again.peekReversible()->type() == TransactionType::Withdrawal);  // newest on top
    CHECK_EQ(again.reversibleCount(), static_cast<std::size_t>(3));
}

TEST(missing_and_empty_files_are_fine) {
    TempDir dir;
    {
        Bank bank(dir.str());  // directory exists but has no files
        CHECK_EQ(bank.allAccounts().size(), static_cast<std::size_t>(0));
    }
    for (const char* f : {"accounts.txt", "transactions.txt", "metadata.txt", "reversed_transactions.txt", "customer_queue.txt"})
        writeFile(dir.path / f, "");
    Bank bank(dir.str());
    CHECK_EQ(bank.allAccounts().size(), static_cast<std::size_t>(0));
    CHECK_EQ(bank.createAccount("First", AccountType::Savings, rs(500)).accountNumber(), 1001LL);
}

TEST(data_directory_is_created_when_missing) {
    TempDir dir;
    const fs::path nested = dir.path / "does" / "not" / "exist";
    Bank bank(nested.string());
    bank.createAccount("Maker", AccountType::Savings, rs(500));
    CHECK(fs::exists(nested / "accounts.txt"));
    CHECK(fs::exists(nested / "transactions.txt"));
    CHECK(fs::exists(nested / "metadata.txt"));
    CHECK(!fs::exists(nested / "accounts.txt.tmp"));  // temp files are cleaned up by the rename
}

TEST(corrupted_files_are_rejected_and_left_untouched) {
    struct Case { const char* file; const char* content; };
    const Case cases[] = {
        {"accounts.txt", "1001|Name|Savings|notanumber|0\n"},
        {"accounts.txt", "1001|Name|Savings\n"},
        {"accounts.txt", "1001|Name|Gold|100|0\n"},
        {"accounts.txt", "1001|Name|Savings|-5|0\n"},
        {"accounts.txt", "1001|A|Savings|100|0\n1001|B|Savings|100|0\n"},   // duplicate number
        {"accounts.txt", "1001|Bad\\xName|Savings|100|0\n"},                // unknown escape
        {"transactions.txt", "1|1001|deposit|100|0|100|0\n"},               // 7 fields
        {"transactions.txt", "1|1001|teleport|100|0|100|0|x\n"},
        {"transactions.txt", "1|1001|deposit|0|0|100|0|x\n"},               // zero amount
        {"transactions.txt", "1|1|deposit|100|0|100|0|a\n1|1|deposit|100|0|100|0|b\n"},  // duplicate id
        {"reversed_transactions.txt", "77\n"},                              // unknown tx id
        {"metadata.txt", "next_account_number=abc\n"},
        {"metadata.txt", "garbage line\n"},
        {"customer_queue.txt", "1|Name\n"},
    };
    for (const Case& c : cases) {
        TempDir dir;
        writeFile(dir.path / c.file, c.content);
        CHECK_THROWS_CODE(Bank(dir.str()), ErrorCode::DataCorrupt);
        CHECK_EQ(readFile(dir.path / c.file), std::string(c.content));  // never silently modified
    }
}

TEST(failed_save_rolls_back_memory) {
    TempDir dir;
    Bank bank(dir.str());
    const long long a = bank.createAccount("Safe", AccountType::Savings, rs(1000)).accountNumber();
    // Block the next save: a DIRECTORY named like a temp file cannot be opened for writing.
    fs::create_directory(dir.path / "transactions.txt.tmp");
    CHECK_THROWS_CODE(bank.deposit(a, rs(500)), ErrorCode::StorageFailure);
    CHECK_EQ(bank.findAccount(a)->balance(), rs(1000));  // memory restored from last good files
    fs::remove(dir.path / "transactions.txt.tmp");
    bank.deposit(a, rs(500));                             // works again afterwards
    CHECK_EQ(bank.findAccount(a)->balance(), rs(1500));
    Bank again(dir.str());
    CHECK_EQ(again.findAccount(a)->balance(), rs(1500));
}

TEST(windows_line_endings_are_accepted) {
    TempDir dir;
    writeFile(dir.path / "accounts.txt", "# comment\r\n1005|Crlf User|Savings|50000|1700000000\r\n");
    Bank bank(dir.str());
    CHECK_EQ(bank.findAccount(1005)->balance(), 50000LL);
    CHECK_EQ(bank.createAccount("Next", AccountType::Savings, rs(500)).accountNumber(), 1006LL);  // counter derived from data
}
