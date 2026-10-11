#pragma once
// AccountLinkedList: hand-written singly linked list; the primary in-memory
// account collection.
//
// OWNERSHIP: the list owns every Node (created with new, released with delete
// in clear()/destructor/remove()). Each Node owns its account through a
// std::unique_ptr, so accounts are freed automatically with the node.
// The list is not copyable (copying raw node chains would double-delete).
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "BankAccount.h"

class AccountLinkedList {
public:
    AccountLinkedList() = default;
    ~AccountLinkedList();
    AccountLinkedList(const AccountLinkedList&) = delete;
    AccountLinkedList& operator=(const AccountLinkedList&) = delete;

    // Appends at the tail. Throws Conflict if the account number already exists.
    void insertBack(std::unique_ptr<BankAccount> account);

    BankAccount* find(long long accountNumber);                // mutable access
    const BankAccount* find(long long accountNumber) const;    // read-only access

    // Case-insensitive substring search on the holder name.
    std::vector<const BankAccount*> findByName(const std::string& fragment) const;

    bool remove(long long accountNumber);  // true if something was deleted
    void clear();

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return head_ == nullptr; }

    // Traverses every node in order.
    void forEach(const std::function<void(const BankAccount&)>& visit) const;
    std::vector<const BankAccount*> toVector() const;  // snapshot of pointers (for sorting)

private:
    struct Node {
        std::unique_ptr<BankAccount> account;
        Node* next = nullptr;
        explicit Node(std::unique_ptr<BankAccount> a) : account(std::move(a)) {}
    };
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;
};
