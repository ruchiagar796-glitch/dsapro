#include "TransactionStack.h"

#include "BankError.h"

TransactionStack::~TransactionStack() { clear(); }

void TransactionStack::clear() {
    while (top_ != nullptr) {
        Node* below = top_->below;
        delete top_;
        top_ = below;
    }
    size_ = 0;
}

void TransactionStack::push(const Transaction& tx) {
    top_ = new Node(tx, top_);
    ++size_;
}

Transaction TransactionStack::pop() {
    if (top_ == nullptr) throw BankError(ErrorCode::NotFound, "There is no reversible transaction");
    Node* node = top_;
    Transaction result = node->data;
    top_ = node->below;
    delete node;
    --size_;
    return result;
}

const Transaction& TransactionStack::top() const {
    if (top_ == nullptr) throw BankError(ErrorCode::NotFound, "There is no reversible transaction");
    return top_->data;
}

std::vector<Transaction> TransactionStack::toVector() const {
    std::vector<Transaction> out;
    for (const Node* cur = top_; cur != nullptr; cur = cur->below) out.push_back(cur->data);
    return out;
}
