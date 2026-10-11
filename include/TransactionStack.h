#pragma once
// TransactionStack: hand-written LIFO stack (linked nodes) of reversible
// transactions. The stack owns its nodes; copying is disabled.
#include <cstddef>
#include <vector>

#include "Transaction.h"

class TransactionStack {
public:
    TransactionStack() = default;
    ~TransactionStack();
    TransactionStack(const TransactionStack&) = delete;
    TransactionStack& operator=(const TransactionStack&) = delete;

    void push(const Transaction& tx);
    Transaction pop();                 // throws BankError(NotFound) when empty
    const Transaction& top() const;    // throws BankError(NotFound) when empty
    bool empty() const noexcept { return top_ == nullptr; }
    std::size_t size() const noexcept { return size_; }
    void clear();
    std::vector<Transaction> toVector() const;  // most recent first (for display)

private:
    struct Node {
        Transaction data;
        Node* below;
        Node(const Transaction& d, Node* b) : data(d), below(b) {}
    };
    Node* top_ = nullptr;
    std::size_t size_ = 0;
};
