#include <vector>

#include "AccountLinkedList.h"
#include "CustomerServiceQueue.h"
#include "SortingAlgorithms.h"
#include "TransactionStack.h"
#include "test_framework.h"

namespace {
std::unique_ptr<BankAccount> acct(long long n, const char* name = "Name") {
    return BankAccount::create(AccountType::Savings, n, name, 0, 0);
}
Transaction tx(long long id) { return Transaction(id, 1, TransactionType::Deposit, 100, 0, 100, "t"); }
}  // namespace

TEST(linked_list_empty_behaviour) {
    AccountLinkedList list;
    CHECK(list.empty());
    CHECK_EQ(list.size(), static_cast<std::size_t>(0));
    CHECK(list.find(1) == nullptr);
    CHECK(!list.remove(1));
    CHECK(list.toVector().empty());
}

TEST(linked_list_insert_find_count_traverse) {
    AccountLinkedList list;
    for (long long i = 1; i <= 5; ++i) list.insertBack(acct(i));
    CHECK_EQ(list.size(), static_cast<std::size_t>(5));
    CHECK(list.find(3) != nullptr);
    CHECK(list.find(6) == nullptr);
    std::vector<long long> seen;
    list.forEach([&](const BankAccount& a) { seen.push_back(a.accountNumber()); });
    CHECK((seen == std::vector<long long>{1, 2, 3, 4, 5}));
}

TEST(linked_list_delete_head_middle_tail) {
    AccountLinkedList list;
    for (long long i = 1; i <= 5; ++i) list.insertBack(acct(i));
    CHECK(list.remove(1));  // head
    CHECK(list.remove(3));  // middle
    CHECK(list.remove(5));  // tail
    CHECK(!list.remove(3));
    CHECK_EQ(list.size(), static_cast<std::size_t>(2));
    list.insertBack(acct(9));  // tail pointer must still be valid after removing the old tail
    std::vector<long long> seen;
    list.forEach([&](const BankAccount& a) { seen.push_back(a.accountNumber()); });
    CHECK((seen == std::vector<long long>{2, 4, 9}));
    CHECK(list.remove(2)); CHECK(list.remove(4)); CHECK(list.remove(9));
    CHECK(list.empty());
    list.insertBack(acct(1));  // list is reusable after becoming empty
    CHECK_EQ(list.size(), static_cast<std::size_t>(1));
}

TEST(linked_list_search_by_name_and_clear) {
    AccountLinkedList list;
    list.insertBack(acct(1, "Anita Sharma"));
    list.insertBack(acct(2, "Sharmila"));
    list.insertBack(acct(3, "Bob"));
    CHECK_EQ(list.findByName("SHARM").size(), static_cast<std::size_t>(2));
    list.clear();
    CHECK(list.empty());
    CHECK_THROWS_CODE(list.insertBack(nullptr), ErrorCode::Internal);
}

TEST(linked_list_handles_many_nodes_without_stack_overflow) {
    AccountLinkedList list;
    for (long long i = 1; i <= 2000; ++i) list.insertBack(acct(i));
    CHECK_EQ(list.size(), static_cast<std::size_t>(2000));
}  // destructor frees all nodes iteratively

TEST(stack_is_lifo) {
    TransactionStack s;
    CHECK(s.empty());
    CHECK_THROWS_CODE(s.pop(), ErrorCode::NotFound);
    CHECK_THROWS_CODE(s.top(), ErrorCode::NotFound);
    s.push(tx(1)); s.push(tx(2)); s.push(tx(3));
    CHECK_EQ(s.size(), static_cast<std::size_t>(3));
    CHECK_EQ(s.top().id(), 3LL);
    CHECK_EQ(s.pop().id(), 3LL);
    CHECK_EQ(s.pop().id(), 2LL);
    CHECK_EQ(s.pop().id(), 1LL);
    CHECK(s.empty());
}

TEST(queue_is_fifo_and_numbers_increase) {
    CustomerServiceQueue q;
    CHECK(q.empty());
    CHECK_THROWS_CODE(q.dequeue(), ErrorCode::NotFound);
    q.issueToken("A", "", 0); q.issueToken("B", "", 0); q.issueToken("C", "", 0);
    CHECK_EQ(q.size(), static_cast<std::size_t>(3));
    CHECK_EQ(q.front().number, 1LL);
    CHECK_EQ(q.dequeue().customerName, std::string("A"));
    CHECK_EQ(q.dequeue().customerName, std::string("B"));
    q.issueToken("D", "", 0);  // enqueue after partial dequeue
    CHECK_EQ(q.dequeue().customerName, std::string("C"));
    CHECK_EQ(q.dequeue().number, 4LL);
    CHECK(q.empty());
    q.issueToken("E", "", 0);  // tail pointer reset correctly after emptying
    CHECK_EQ(q.dequeue().number, 5LL);
}

TEST(merge_sort_sorts_stably_and_handles_edge_cases) {
    std::vector<int> empty;
    sorting::mergeSort(empty, [](int a, int b) { return a < b; });
    CHECK(empty.empty());
    std::vector<int> one{7};
    sorting::mergeSort(one, [](int a, int b) { return a < b; });
    CHECK_EQ(one[0], 7);
    std::vector<int> v{5, 2, 9, 1, 5, 6, 0, -3, 2};
    sorting::mergeSort(v, [](int a, int b) { return a < b; });
    CHECK((v == std::vector<int>{-3, 0, 1, 2, 2, 5, 5, 6, 9}));

    // stability: equal keys keep their original order
    std::vector<std::pair<int, char>> p{{2, 'a'}, {1, 'b'}, {2, 'c'}, {1, 'd'}};
    sorting::mergeSort(p, [](const auto& x, const auto& y) { return x.first < y.first; });
    CHECK_EQ(p[0].second, 'b'); CHECK_EQ(p[1].second, 'd');
    CHECK_EQ(p[2].second, 'a'); CHECK_EQ(p[3].second, 'c');
}
