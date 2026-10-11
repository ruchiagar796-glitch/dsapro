#include "Bank.h"
#include "test_framework.h"

TEST(create_account_assigns_number_and_records_initial_deposit) {
    Bank bank;
    const BankAccount& a = bank.createAccount("  Asha Verma ", AccountType::Savings, rs(1000));
    CHECK_EQ(a.accountNumber(), 1001LL);
    CHECK_EQ(a.holderName(), std::string("Asha Verma"));  // trimmed
    CHECK_EQ(a.balance(), rs(1000));
    CHECK_EQ(bank.transactionsFor(1001).size(), static_cast<std::size_t>(1));
}

TEST(account_numbers_are_unique_and_never_reused) {
    Bank bank;
    const long long first = bank.createAccount("A One", AccountType::Savings, rs(500)).accountNumber();
    const long long second = bank.createAccount("B Two", AccountType::Savings, rs(500)).accountNumber();
    CHECK(first != second);
    bank.withdraw(second, rs(500));
    bank.closeAccount(second);
    const long long third = bank.createAccount("C Three", AccountType::Savings, rs(500)).accountNumber();
    CHECK(third != first);
    CHECK(third != second);  // closed number is not reused
}

TEST(duplicate_account_number_rejected_by_list) {
    AccountLinkedList list;
    list.insertBack(BankAccount::create(AccountType::Savings, 7, "X", 0, 0));
    CHECK_THROWS_CODE(list.insertBack(BankAccount::create(AccountType::Current, 7, "Y", 0, 0)), ErrorCode::Conflict);
    CHECK_EQ(list.size(), static_cast<std::size_t>(1));
}

TEST(create_account_validation) {
    Bank bank;
    CHECK_THROWS_CODE(bank.createAccount("", AccountType::Savings, rs(1000)), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(bank.createAccount("   ", AccountType::Savings, rs(1000)), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(bank.createAccount("Bad\nName", AccountType::Savings, rs(1000)), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(bank.createAccount("Low", AccountType::Savings, rs(499)), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(bank.createAccount("Low", AccountType::Current, rs(4999)), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(bank.createAccount("Big", AccountType::Savings, rs(10'000'001)), ErrorCode::LimitExceeded);
    CHECK_EQ(bank.allAccounts().size(), static_cast<std::size_t>(0));  // nothing was stored
    CHECK_EQ(bank.summary().transactionCount, static_cast<std::size_t>(0));
}

TEST(account_type_rules_differ_polymorphically) {
    Bank bank;
    const long long s = bank.createAccount("Sav", AccountType::Savings, rs(100000)).accountNumber();
    const long long c = bank.createAccount("Cur", AccountType::Current, rs(100000)).accountNumber();
    CHECK_THROWS_CODE(bank.withdraw(s, rs(50001)), ErrorCode::LimitExceeded);  // savings limit Rs 50,000
    bank.withdraw(c, rs(60000));                                               // current allows it
    CHECK_EQ(bank.findAccount(c)->balance(), rs(40000));
}

TEST(search_existing_and_nonexistent) {
    Bank bank;
    bank.createAccount("Ravi Kumar", AccountType::Savings, rs(500));
    bank.createAccount("Meera Rao", AccountType::Savings, rs(500));
    CHECK(bank.findAccount(1001) != nullptr);
    CHECK(bank.findAccount(9999) == nullptr);
    CHECK_EQ(bank.searchByName("kum").size(), static_cast<std::size_t>(1));  // case-insensitive substring
    CHECK_EQ(bank.searchByName("zzz").size(), static_cast<std::size_t>(0));
}

TEST(close_account_rules) {
    Bank bank;
    const long long n = bank.createAccount("Closer", AccountType::Savings, rs(800)).accountNumber();
    CHECK_THROWS_CODE(bank.closeAccount(n), ErrorCode::Conflict);  // non-zero balance
    CHECK(bank.findAccount(n) != nullptr);
    bank.withdraw(n, rs(800));
    bank.closeAccount(n);
    CHECK(bank.findAccount(n) == nullptr);
    CHECK_THROWS_CODE(bank.closeAccount(n), ErrorCode::NotFound);
    CHECK(!bank.peekReversible().has_value());  // its reversible entries were purged
}

TEST(sorted_view_by_name_and_balance_keeps_original_order) {
    Bank bank;
    bank.createAccount("charlie", AccountType::Savings, rs(900));
    bank.createAccount("Alice", AccountType::Savings, rs(700));
    bank.createAccount("bob", AccountType::Savings, rs(800));
    const auto byName = bank.sortedAccounts(SortKey::Name, true);
    CHECK_EQ(byName[0]->holderName(), std::string("Alice"));
    CHECK_EQ(byName[1]->holderName(), std::string("bob"));
    CHECK_EQ(byName[2]->holderName(), std::string("charlie"));
    const auto byBalDesc = bank.sortedAccounts(SortKey::Balance, false);
    CHECK_EQ(byBalDesc[0]->balance(), rs(900));
    CHECK_EQ(byBalDesc[2]->balance(), rs(700));
    const auto original = bank.allAccounts();  // insertion order untouched
    CHECK_EQ(original[0]->holderName(), std::string("charlie"));
    CHECK_EQ(original.size(), static_cast<std::size_t>(3));
}

TEST(money_parsing_and_formatting) {
    money::Paise p = 0;
    std::string err;
    CHECK(money::parseRupees("1500.5", p, err));
    CHECK_EQ(p, 150050LL);
    CHECK(money::parseRupees(" 10 ", p, err));
    CHECK_EQ(p, 1000LL);
    CHECK(!money::parseRupees("-5", p, err));
    CHECK(!money::parseRupees("1e5", p, err));
    CHECK(!money::parseRupees("1.234", p, err));
    CHECK(!money::parseRupees("abc", p, err));
    CHECK(!money::parseRupees("", p, err));
    CHECK(!money::parseRupees("1,000", p, err));
    CHECK(!money::parseRupees("9999999999999", p, err));  // 13 digits: overflow guard
    CHECK(!money::parseRupees("nan", p, err));
    CHECK_EQ(money::format(150050), std::string("1500.50"));
    CHECK_EQ(money::format(5), std::string("0.05"));
}
