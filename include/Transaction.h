#pragma once
// Transaction: an immutable record of one balance-changing event.
#include <ctime>
#include <string>

#include "Money.h"

enum class TransactionType { Deposit, Withdrawal, TransferOut, TransferIn, Reversal };

std::string toString(TransactionType type);
bool parseTransactionType(const std::string& text, TransactionType& out);

class Transaction {
public:
    Transaction(long long id, long long accountNumber, TransactionType type, money::Paise amount,
                std::time_t timestamp, money::Paise balanceAfter, std::string description,
                long long relatedId = 0);

    long long id() const noexcept { return id_; }
    long long accountNumber() const noexcept { return accountNumber_; }
    TransactionType type() const noexcept { return type_; }
    money::Paise amount() const noexcept { return amount_; }
    std::time_t timestamp() const noexcept { return timestamp_; }
    money::Paise balanceAfter() const noexcept { return balanceAfter_; }
    const std::string& description() const noexcept { return description_; }
    // Reversal -> id of the reversed transaction; TransferOut/In -> id of the other leg.
    long long relatedId() const noexcept { return relatedId_; }

    // Only plain deposits and withdrawals can be reversed (transfers are excluded).
    bool isReversible() const noexcept {
        return type_ == TransactionType::Deposit || type_ == TransactionType::Withdrawal;
    }
    std::string timestampString() const;  // "YYYY-MM-DD HH:MM:SS" (local time)

    // Human-readable reference such as "TXN00000042". It is DERIVED (not stored), so the
    // data-file format did not change. Both legs of a transfer share one reference because
    // it is built from the smaller of the two linked transaction ids.
    std::string reference() const;

private:
    long long id_;
    long long accountNumber_;
    TransactionType type_;
    money::Paise amount_;
    std::time_t timestamp_;
    money::Paise balanceAfter_;
    std::string description_;
    long long relatedId_;
};
