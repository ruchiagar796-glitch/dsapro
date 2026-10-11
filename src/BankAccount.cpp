#include "BankAccount.h"

#include <algorithm>
#include <cctype>

std::string toString(AccountType type) {
    return type == AccountType::Savings ? "Savings" : "Current";
}

bool parseAccountType(const std::string& text, AccountType& out) {
    std::string t;
    for (char c : text) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t == "savings") { out = AccountType::Savings; return true; }
    if (t == "current") { out = AccountType::Current; return true; }
    return false;
}

std::string BankAccount::validateName(const std::string& raw) {
    const auto b = raw.find_first_not_of(" \t");
    if (b == std::string::npos)
        throw BankError(ErrorCode::InvalidInput, "Account holder name cannot be empty");
    const auto e = raw.find_last_not_of(" \t");
    std::string name = raw.substr(b, e - b + 1);
    if (name.size() > 60)
        throw BankError(ErrorCode::InvalidInput, "Account holder name must be at most 60 characters");
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f)
            throw BankError(ErrorCode::InvalidInput, "Account holder name contains invalid characters");
    }
    return name;
}

std::unique_ptr<BankAccount> BankAccount::create(AccountType type, long long no,
                                                 const std::string& name, std::time_t created,
                                                 money::Paise opening) {
    if (type == AccountType::Savings)
        return std::unique_ptr<BankAccount>(new SavingsAccount(no, name, created, opening));
    return std::unique_ptr<BankAccount>(new CurrentAccount(no, name, created, opening));
}

BankAccount::BankAccount(AccountType type, long long no, const std::string& name,
                         std::time_t created, money::Paise opening)
    : accountNumber_(no), holderName_(validateName(name)), type_(type), balance_(opening),
      createdAt_(created) {
    if (no <= 0) throw BankError(ErrorCode::InvalidInput, "Account number must be positive");
    if (opening < 0 || opening > money::kMaxBalancePaise)
        throw BankError(ErrorCode::InvalidInput, "Opening balance is out of range");
}

std::string BankAccount::describe() const {
    return "Account #" + std::to_string(accountNumber_) + " | " + typeName() + " | " +
           holderName_ + " | Balance: Rs " + money::format(balance_);
}

void BankAccount::validateDeposit(money::Paise amount) const {
    if (!active_)
        throw BankError(ErrorCode::Forbidden,
                        "This account is deactivated. Please contact the bank for help.");
    if (amount <= 0)
        throw BankError(ErrorCode::InvalidInput, "Deposit amount must be greater than zero");
    if (amount > money::kMaxTransactionPaise)
        throw BankError(ErrorCode::LimitExceeded,
                        "Amount exceeds the per-transaction limit of Rs " +
                            money::format(money::kMaxTransactionPaise));
    if (balance_ > money::kMaxBalancePaise - amount)
        throw BankError(ErrorCode::LimitExceeded, "Deposit would exceed the maximum account balance");
}

void BankAccount::validateWithdrawal(money::Paise amount) const {
    if (!active_)
        throw BankError(ErrorCode::Forbidden,
                        "This account is deactivated. Please contact the bank for help.");
    if (amount <= 0)
        throw BankError(ErrorCode::InvalidInput, "Withdrawal amount must be greater than zero");
    if (amount > maxWithdrawalPerTransaction())
        throw BankError(ErrorCode::LimitExceeded,
                        typeName() + " accounts allow at most Rs " +
                            money::format(maxWithdrawalPerTransaction()) + " per withdrawal or transfer");
    if (amount > balance_)
        throw BankError(ErrorCode::InsufficientFunds,
                        "Insufficient balance. Available: Rs " + money::format(balance_));
}

void BankAccount::deposit(money::Paise amount) {
    validateDeposit(amount);  // validate first, then change: a failure never alters the balance
    balance_ += amount;
}

void BankAccount::withdraw(money::Paise amount) {
    validateWithdrawal(amount);
    balance_ -= amount;
}

void BankAccount::reverseDeposit(money::Paise amount) {
    if (amount <= 0) throw BankError(ErrorCode::InvalidInput, "Invalid reversal amount");
    if (amount > balance_)
        throw BankError(ErrorCode::InsufficientFunds,
                        "Cannot reverse this deposit: current balance (Rs " + money::format(balance_) +
                            ") is lower than the deposited amount (Rs " + money::format(amount) + ")");
    balance_ -= amount;
}

void BankAccount::reverseWithdrawal(money::Paise amount) {
    if (amount <= 0) throw BankError(ErrorCode::InvalidInput, "Invalid reversal amount");
    if (balance_ > money::kMaxBalancePaise - amount)
        throw BankError(ErrorCode::LimitExceeded, "Reversal would exceed the maximum account balance");
    balance_ += amount;
}
