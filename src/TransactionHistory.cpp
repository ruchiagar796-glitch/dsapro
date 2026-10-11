#include "TransactionHistory.h"

void TransactionHistory::add(const Transaction& tx) {
    byId_[tx.id()] = records_.size();
    byAccount_[tx.accountNumber()].push_back(records_.size());
    records_.push_back(tx);
}

void TransactionHistory::clear() {
    records_.clear();
    byId_.clear();
    byAccount_.clear();
}

const Transaction* TransactionHistory::findById(long long id) const {
    const auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &records_[it->second];
}

std::vector<Transaction> TransactionHistory::recent(std::size_t limit) const {
    std::vector<Transaction> out;
    for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
        if (limit != 0 && out.size() >= limit) break;
        out.push_back(*it);
    }
    return out;
}

std::vector<Transaction> TransactionHistory::forAccount(long long acc, std::size_t limit) const {
    std::vector<Transaction> out;
    const auto it = byAccount_.find(acc);
    if (it == byAccount_.end()) return out;
    const std::vector<std::size_t>& positions = it->second;  // oldest -> newest
    for (auto p = positions.rbegin(); p != positions.rend(); ++p) {
        if (limit != 0 && out.size() >= limit) break;
        out.push_back(records_[*p]);
    }
    return out;
}
