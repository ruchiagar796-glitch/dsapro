#pragma once
// CustomerServiceQueue: hand-written FIFO queue (linked nodes, head + tail).
// Token numbers only ever increase (they are never reused, even after a restart).
#include <cstddef>
#include <ctime>
#include <string>
#include <vector>

struct QueueToken {
    long long number = 0;
    std::string customerName;
    std::string purpose;
    std::time_t issuedAt = 0;
};

class CustomerServiceQueue {
public:
    CustomerServiceQueue() = default;
    ~CustomerServiceQueue();
    CustomerServiceQueue(const CustomerServiceQueue&) = delete;
    CustomerServiceQueue& operator=(const CustomerServiceQueue&) = delete;

    QueueToken issueToken(const std::string& customerName, const std::string& purpose,
                          std::time_t now);          // generates the next number and enqueues
    void enqueue(const QueueToken& token);           // used when loading from file
    QueueToken dequeue();                            // throws BankError(NotFound) when empty
    const QueueToken& front() const;                 // throws BankError(NotFound) when empty
    bool empty() const noexcept { return head_ == nullptr; }
    std::size_t size() const noexcept { return size_; }
    void clear();

    long long nextTokenNumber() const noexcept { return nextToken_; }
    void setNextTokenNumber(long long n) noexcept { nextToken_ = n; }
    std::vector<QueueToken> toVector() const;        // front -> back

private:
    struct Node {
        QueueToken data;
        Node* next = nullptr;
        explicit Node(const QueueToken& d) : data(d) {}
    };
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;
    long long nextToken_ = 1;
};
