#include "Bank.h"
#include "test_framework.h"

namespace {
long long open(Bank& bank, const char* name, long long rupees, AccountType t = AccountType::Savings) {
    return bank.createAccount(name, t, rs(rupees)).accountNumber();
}
}  // namespace

TEST(deposit_success_updates_balance_and_history) {
    Bank bank;
    const long long a = open(bank, "Dep", 1000);
    const Transaction t = bank.deposit(a, rs(250) + 50);
    CHECK_EQ(bank.findAccount(a)->balance(), rs(1250) + 50);
    CHECK_EQ(t.balanceAfter(), rs(1250) + 50);
    CHECK_EQ(bank.transactionsFor(a).size(), static_cast<std::size_t>(2));
}

TEST(deposit_invalid_amounts_rejected_without_change) {
    Bank bank;
    const long long a = open(bank, "Dep", 1000);
    CHECK_THROWS_CODE(bank.deposit(a, 0), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(bank.deposit(a, -100), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(bank.deposit(a, money::kMaxTransactionPaise + 1), ErrorCode::LimitExceeded);
    CHECK_THROWS_CODE(bank.deposit(404, rs(10)), ErrorCode::NotFound);
    CHECK_EQ(bank.findAccount(a)->balance(), rs(1000));
    CHECK_EQ(bank.summary().transactionCount, static_cast<std::size_t>(1));  // only the initial deposit
}

TEST(balance_cap_prevents_overflow) {
    Bank bank;
    const long long a = open(bank, "Rich", 10'000'000, AccountType::Current);
    for (int i = 0; i < 9999; ++i) bank.deposit(a, rs(10'000'000));  // up to Rs 100 billion
    CHECK_THROWS_CODE(bank.deposit(a, rs(10'000'000)), ErrorCode::LimitExceeded);
}

TEST(withdraw_success_and_insufficient_balance) {
    Bank bank;
    const long long a = open(bank, "Wd", 1000);
    bank.withdraw(a, rs(400));
    CHECK_EQ(bank.findAccount(a)->balance(), rs(600));
    CHECK_THROWS_CODE(bank.withdraw(a, rs(600) + 1), ErrorCode::InsufficientFunds);
    CHECK_THROWS_CODE(bank.withdraw(a, 0), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(bank.withdraw(a, -5), ErrorCode::InvalidInput);
    CHECK_EQ(bank.findAccount(a)->balance(), rs(600));
    bank.withdraw(a, rs(600));  // exact balance is allowed
    CHECK_EQ(bank.findAccount(a)->balance(), 0LL);
}

TEST(transfer_success_moves_money_and_logs_two_legs) {
    Bank bank;
    const long long a = open(bank, "Src", 1000);
    const long long b = open(bank, "Dst", 600);
    const auto legs = bank.transfer(a, b, rs(300));
    CHECK_EQ(bank.findAccount(a)->balance(), rs(700));
    CHECK_EQ(bank.findAccount(b)->balance(), rs(900));
    CHECK_EQ(legs.first.relatedId(), legs.second.id());
    CHECK_EQ(legs.second.relatedId(), legs.first.id());
    CHECK(legs.first.type() == TransactionType::TransferOut);
    CHECK(legs.second.type() == TransactionType::TransferIn);
    CHECK_EQ(bank.summary().totalBalance, rs(1600));  // money neither created nor destroyed
}

TEST(transfer_invalid_cases_change_nothing) {
    Bank bank;
    const long long a = open(bank, "Src", 1000);
    const long long b = open(bank, "Dst", 600);
    CHECK_THROWS_CODE(bank.transfer(a, a, rs(10)), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(bank.transfer(a, 9999, rs(10)), ErrorCode::NotFound);
    CHECK_THROWS_CODE(bank.transfer(9999, b, rs(10)), ErrorCode::NotFound);
    CHECK_THROWS_CODE(bank.transfer(a, b, rs(1001)), ErrorCode::InsufficientFunds);
    CHECK_THROWS_CODE(bank.transfer(a, b, 0), ErrorCode::InvalidInput);
    CHECK_EQ(bank.findAccount(a)->balance(), rs(1000));
    CHECK_EQ(bank.findAccount(b)->balance(), rs(600));
    CHECK_EQ(bank.summary().transactionCount, static_cast<std::size_t>(2));
}

TEST(reverse_deposit_and_withdrawal_in_lifo_order) {
    Bank bank;
    const long long a = open(bank, "Rev", 1000);
    bank.deposit(a, rs(200));    // balance 1200
    bank.withdraw(a, rs(300));   // balance 900
    const Transaction r1 = bank.reverseLastTransaction();  // undoes the withdrawal
    CHECK_EQ(bank.findAccount(a)->balance(), rs(1200));
    CHECK(r1.type() == TransactionType::Reversal);
    bank.reverseLastTransaction();                           // undoes the deposit
    CHECK_EQ(bank.findAccount(a)->balance(), rs(1000));
}

TEST(duplicate_and_out_of_order_reversal_prevented) {
    Bank bank;
    const long long a = open(bank, "Rev", 1000);
    const Transaction d = bank.deposit(a, rs(200));
    const Transaction w = bank.withdraw(a, rs(100));
    CHECK_THROWS_CODE(bank.reverseLastTransaction(d.id()), ErrorCode::Conflict);  // not the top of the stack
    bank.reverseLastTransaction(w.id());
    CHECK_THROWS_CODE(bank.reverseLastTransaction(w.id()), ErrorCode::Conflict);  // already reversed
    CHECK_THROWS_CODE(bank.reverseLastTransaction(99999), ErrorCode::NotFound);
    CHECK_EQ(bank.findAccount(a)->balance(), rs(1200));
}

TEST(reversal_never_creates_negative_balance) {
    Bank bank;
    const long long a = open(bank, "Neg", 1000);
    const long long b = open(bank, "Other", 1000);
    bank.deposit(a, rs(500));      // top of stack for a... (b's initial deposit is below? order: a,b,dep)
    bank.transfer(a, b, rs(1500)); // drains a; transfers are not reversible
    CHECK_THROWS_CODE(bank.reverseLastTransaction(), ErrorCode::InsufficientFunds);
    CHECK_EQ(bank.findAccount(a)->balance(), 0LL);        // unchanged
    CHECK(bank.peekReversible().has_value());             // still on the stack
}

TEST(transfers_and_reversals_are_not_reversible) {
    Bank bank;
    const long long a = open(bank, "A", 1000);
    const long long b = open(bank, "B", 1000);
    const auto legs = bank.transfer(a, b, rs(100));
    CHECK_THROWS_CODE(bank.reverseLastTransaction(legs.first.id()), ErrorCode::InvalidInput);
    bank.deposit(a, rs(50));
    const Transaction rev = bank.reverseLastTransaction();
    CHECK_THROWS_CODE(bank.reverseLastTransaction(rev.id()), ErrorCode::InvalidInput);
}

TEST(nothing_to_reverse_on_empty_stack) {
    Bank bank;
    CHECK_THROWS_CODE(bank.reverseLastTransaction(), ErrorCode::NotFound);
}

TEST(history_recent_and_filter_by_account) {
    Bank bank;
    const long long a = open(bank, "A", 1000);
    const long long b = open(bank, "B", 1000);
    bank.deposit(a, rs(1));
    bank.deposit(b, rs(2));
    bank.deposit(a, rs(3));
    CHECK_EQ(bank.recentTransactions(2).size(), static_cast<std::size_t>(2));
    CHECK_EQ(bank.recentTransactions(2)[0].amount(), rs(3));  // newest first
    CHECK_EQ(bank.transactionsFor(a).size(), static_cast<std::size_t>(3));
    CHECK_EQ(bank.transactionsFor(b).size(), static_cast<std::size_t>(2));
    CHECK_EQ(bank.transactionsFor(555).size(), static_cast<std::size_t>(0));
}

TEST(customer_queue_through_bank) {
    Bank bank;
    CHECK_THROWS_CODE(bank.serveNextCustomer(), ErrorCode::NotFound);
    CHECK_THROWS_CODE(bank.issueToken("", "x"), ErrorCode::InvalidInput);
    const QueueToken t1 = bank.issueToken("First", "Loan");
    const QueueToken t2 = bank.issueToken("Second", "");
    CHECK_EQ(t1.number, 1LL);
    CHECK_EQ(t2.number, 2LL);
    CHECK_EQ(bank.serveNextCustomer().number, 1LL);  // FIFO
    CHECK_EQ(bank.queueSnapshot().size(), static_cast<std::size_t>(1));
    CHECK_EQ(bank.nextTokenNumber(), 3LL);
}
