#include "CustomerServiceQueue.h"

#include "BankError.h"

CustomerServiceQueue::~CustomerServiceQueue() { clear(); }

void CustomerServiceQueue::clear() {
    while (head_ != nullptr) {
        Node* next = head_->next;
        delete head_;
        head_ = next;
    }
    tail_ = nullptr;
    size_ = 0;
}

void CustomerServiceQueue::enqueue(const QueueToken& token) {
    Node* node = new Node(token);
    if (tail_ == nullptr) head_ = tail_ = node;
    else { tail_->next = node; tail_ = node; }
    ++size_;
}

QueueToken CustomerServiceQueue::issueToken(const std::string& name, const std::string& purpose,
                                            std::time_t now) {
    QueueToken t;
    t.number = nextToken_++;
    t.customerName = name;
    t.purpose = purpose;
    t.issuedAt = now;
    enqueue(t);
    return t;
}

QueueToken CustomerServiceQueue::dequeue() {
    if (head_ == nullptr) throw BankError(ErrorCode::NotFound, "No customers are waiting");
    Node* node = head_;
    QueueToken result = node->data;
    head_ = node->next;
    if (head_ == nullptr) tail_ = nullptr;  // queue became empty
    delete node;
    --size_;
    return result;
}

const QueueToken& CustomerServiceQueue::front() const {
    if (head_ == nullptr) throw BankError(ErrorCode::NotFound, "No customers are waiting");
    return head_->data;
}

std::vector<QueueToken> CustomerServiceQueue::toVector() const {
    std::vector<QueueToken> out;
    for (const Node* cur = head_; cur != nullptr; cur = cur->next) out.push_back(cur->data);
    return out;
}
