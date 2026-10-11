#include "AccountLinkedList.h"

#include <cctype>

static std::string lower(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

AccountLinkedList::~AccountLinkedList() { clear(); }

void AccountLinkedList::clear() {
    Node* cur = head_;
    while (cur != nullptr) {  // iterative: no recursion, so long lists cannot overflow the stack
        Node* next = cur->next;
        delete cur;
        cur = next;
    }
    head_ = tail_ = nullptr;
    size_ = 0;
}

void AccountLinkedList::insertBack(std::unique_ptr<BankAccount> account) {
    if (!account) throw BankError(ErrorCode::Internal, "Cannot insert a null account");
    if (find(account->accountNumber()) != nullptr)  // O(n) duplicate check
        throw BankError(ErrorCode::Conflict,
                        "Account number " + std::to_string(account->accountNumber()) + " already exists");
    Node* node = new Node(std::move(account));
    if (tail_ == nullptr) head_ = tail_ = node;  // empty list
    else { tail_->next = node; tail_ = node; }
    ++size_;
}

BankAccount* AccountLinkedList::find(long long no) {
    for (Node* cur = head_; cur != nullptr; cur = cur->next)
        if (cur->account->accountNumber() == no) return cur->account.get();
    return nullptr;
}

const BankAccount* AccountLinkedList::find(long long no) const {
    for (const Node* cur = head_; cur != nullptr; cur = cur->next)
        if (cur->account->accountNumber() == no) return cur->account.get();
    return nullptr;
}

std::vector<const BankAccount*> AccountLinkedList::findByName(const std::string& fragment) const {
    std::vector<const BankAccount*> out;
    const std::string needle = lower(fragment);
    for (const Node* cur = head_; cur != nullptr; cur = cur->next)
        if (lower(cur->account->holderName()).find(needle) != std::string::npos)
            out.push_back(cur->account.get());
    return out;
}

bool AccountLinkedList::remove(long long no) {
    Node* prev = nullptr;
    for (Node* cur = head_; cur != nullptr; prev = cur, cur = cur->next) {
        if (cur->account->accountNumber() != no) continue;
        if (prev == nullptr) head_ = cur->next;       // deleting the head
        else prev->next = cur->next;
        if (cur == tail_) tail_ = prev;               // deleting the tail
        delete cur;
        --size_;
        return true;
    }
    return false;
}

void AccountLinkedList::forEach(const std::function<void(const BankAccount&)>& visit) const {
    for (const Node* cur = head_; cur != nullptr; cur = cur->next) visit(*cur->account);
}

std::vector<const BankAccount*> AccountLinkedList::toVector() const {
    std::vector<const BankAccount*> out;
    out.reserve(size_);
    for (const Node* cur = head_; cur != nullptr; cur = cur->next) out.push_back(cur->account.get());
    return out;
}
