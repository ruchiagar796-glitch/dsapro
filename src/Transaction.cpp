#include "Transaction.h"

#include <utility>

std::string toString(TransactionType t) {
    switch (t) {
        case TransactionType::Deposit: return "deposit";
        case TransactionType::Withdrawal: return "withdrawal";
        case TransactionType::TransferOut: return "transfer_out";
        case TransactionType::TransferIn: return "transfer_in";
        default: return "reversal";
    }
}

bool parseTransactionType(const std::string& s, TransactionType& out) {
    if (s == "deposit") out = TransactionType::Deposit;
    else if (s == "withdrawal") out = TransactionType::Withdrawal;
    else if (s == "transfer_out") out = TransactionType::TransferOut;
    else if (s == "transfer_in") out = TransactionType::TransferIn;
    else if (s == "reversal") out = TransactionType::Reversal;
    else return false;
    return true;
}

Transaction::Transaction(long long id, long long accountNumber, TransactionType type,
                         money::Paise amount, std::time_t timestamp, money::Paise balanceAfter,
                         std::string description, long long relatedId)
    : id_(id), accountNumber_(accountNumber), type_(type), amount_(amount), timestamp_(timestamp),
      balanceAfter_(balanceAfter), description_(std::move(description)), relatedId_(relatedId) {}

std::string Transaction::reference() const {
    long long base = id_;
    const bool isTransfer = type_ == TransactionType::TransferOut || type_ == TransactionType::TransferIn;
    if (isTransfer && relatedId_ > 0 && relatedId_ < base) base = relatedId_;
    std::string digits = std::to_string(base);
    if (digits.size() < 8) digits.insert(0, 8 - digits.size(), '0');
    return "TXN" + digits;
}

std::string Transaction::timestampString() const {
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &timestamp_);
#else
    localtime_r(&timestamp_, &tmv);
#endif
    char buf[32];
    if (std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv) == 0) return "";
    return buf;
}
