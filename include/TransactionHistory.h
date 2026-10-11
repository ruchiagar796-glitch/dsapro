#pragma once
// TransactionHistory: keeps every transaction in the order it happened.
//
// DSA note: besides the ordered vector (append-only log) it keeps two hash-map
// indexes so that lookups do not have to scan the whole log:
//   byId_      : transaction id      -> position in the vector      O(1) average
//   byAccount_ : account number      -> positions of its records    O(k) for k records
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "Transaction.h"

class TransactionHistory {
public:
    void add(const Transaction& tx);
    void clear();
    std::size_t size() const noexcept { return records_.size(); }
    const std::vector<Transaction>& all() const noexcept { return records_; }

    const Transaction* findById(long long id) const;
    // Newest first. limit == 0 means "no limit".
    std::vector<Transaction> recent(std::size_t limit) const;
    std::vector<Transaction> forAccount(long long accountNumber, std::size_t limit = 0) const;

private:
    std::vector<Transaction> records_;
    std::unordered_map<long long, std::size_t> byId_;
    std::unordered_map<long long, std::vector<std::size_t>> byAccount_;
};
