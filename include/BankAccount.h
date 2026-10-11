#pragma once
// BankAccount: abstract base class (encapsulation + polymorphism).
// The balance is private; it can only change through validated member functions.
// SavingsAccount and CurrentAccount differ in REAL rules (minimum opening
// deposit and per-withdrawal limit), which is why inheritance is justified here.
#include <ctime>
#include <memory>
#include <string>

#include "BankError.h"
#include "Money.h"

enum class AccountType { Savings, Current };

std::string toString(AccountType type);
bool parseAccountType(const std::string& text, AccountType& out);  // case-insensitive

class BankAccount {
public:
    virtual ~BankAccount() = default;
    BankAccount(const BankAccount&) = delete;             // accounts are unique objects
    BankAccount& operator=(const BankAccount&) = delete;

    // Factory: creates the right derived class for the type.
    static std::unique_ptr<BankAccount> create(AccountType type, long long accountNumber,
                                               const std::string& holderName,
                                               std::time_t createdAt,
                                               money::Paise openingBalance = 0);
    // Returns the trimmed name or throws BankError(InvalidInput).
    static std::string validateName(const std::string& rawName);

    long long accountNumber() const noexcept { return accountNumber_; }
    const std::string& holderName() const noexcept { return holderName_; }
    AccountType type() const noexcept { return type_; }
    money::Paise balance() const noexcept { return balance_; }
    std::time_t createdAt() const noexcept { return createdAt_; }

    // Account status. A deactivated account can be viewed but cannot move money.
    bool active() const noexcept { return active_; }
    void setActive(bool value) noexcept { active_ = value; }
    // Keeps the holder name in sync when the customer edits their profile.
    void setHolderName(const std::string& rawName) { holderName_ = validateName(rawName); }

    // Rules that differ per account type (runtime polymorphism).
    virtual std::string typeName() const = 0;
    virtual money::Paise minimumOpeningDeposit() const = 0;
    virtual money::Paise maxWithdrawalPerTransaction() const = 0;

    virtual std::string describe() const;

    // Validation only (never changes anything) - used to check before mutating.
    void validateDeposit(money::Paise amount) const;
    void validateWithdrawal(money::Paise amount) const;

    void deposit(money::Paise amount);
    void withdraw(money::Paise amount);

    // Used by transaction reversal. Same safety: never negative, never overflow.
    void reverseDeposit(money::Paise amount);
    void reverseWithdrawal(money::Paise amount);

protected:
    BankAccount(AccountType type, long long accountNumber, const std::string& holderName,
                std::time_t createdAt, money::Paise openingBalance);

private:
    long long accountNumber_;
    std::string holderName_;
    AccountType type_;
    money::Paise balance_;
    std::time_t createdAt_;
    bool active_ = true;
};

class SavingsAccount final : public BankAccount {
public:
    SavingsAccount(long long no, const std::string& name, std::time_t created, money::Paise opening)
        : BankAccount(AccountType::Savings, no, name, created, opening) {}
    std::string typeName() const override { return "Savings"; }
    money::Paise minimumOpeningDeposit() const override { return 500 * 100; }          // Rs 500
    money::Paise maxWithdrawalPerTransaction() const override { return 50000 * 100; }  // Rs 50,000
};

class CurrentAccount final : public BankAccount {
public:
    CurrentAccount(long long no, const std::string& name, std::time_t created, money::Paise opening)
        : BankAccount(AccountType::Current, no, name, created, opening) {}
    std::string typeName() const override { return "Current"; }
    money::Paise minimumOpeningDeposit() const override { return 5000 * 100; }           // Rs 5,000
    money::Paise maxWithdrawalPerTransaction() const override { return 500000 * 100; }   // Rs 5,00,000
};
