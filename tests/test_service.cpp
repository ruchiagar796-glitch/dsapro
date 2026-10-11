#include "BankService.h"
#include "test_framework.h"

using nlohmann::json;

namespace {
struct Fixture {
    Bank bank;
    BankService svc{bank};
    std::string alice = registerTestCustomer(bank, "Alice Sharma", "alice@example.com");
    std::string bob = registerTestCustomer(bank, "Bob Verma", "bob@example.com");
    long long aliceAcc() { return bank.users().findCustomerById(alice)->accountNumber; }
    long long bobAcc() { return bank.users().findCustomerById(bob)->accountNumber; }
};
}  // namespace

TEST(new_customers_start_with_zero_balance_and_unique_ids) {
    Fixture f;
    CHECK(f.alice != f.bob);
    CHECK(f.aliceAcc() != f.bobAcc());
    CHECK_EQ(f.bank.findAccount(f.aliceAcc())->balance(), 0LL);
    CHECK_EQ(f.bank.summary().transactionCount, static_cast<std::size_t>(0));  // no fake opening deposit
    CHECK_EQ(f.alice, std::string("CUS000001"));
}

TEST(service_validates_amounts) {
    Fixture f;
    CHECK_EQ(f.svc.deposit(f.alice, json::object()).status, 400);                         // missing amount
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", "abc"}}).status, 400);
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", "-5"}}).status, 400);
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", 0}}).status, 400);
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", 1e30}}).status, 400);                     // overflow-sized number
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", "1.999"}}).status, 400);                  // 3 decimals
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", "1e3"}}).status, 400);
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", "99999999999"}}).status, 422);            // over per-transaction limit
    CHECK_EQ(f.svc.deposit(f.alice, json::array()).status, 400);
    CHECK_EQ(f.svc.withdraw(f.alice, {{"amount", "5000"}}).status, 422);                  // insufficient funds
    CHECK_EQ(f.bank.findAccount(f.aliceAcc())->balance(), 0LL);                           // nothing above changed anything
    CHECK_EQ(f.bank.summary().transactionCount, static_cast<std::size_t>(0));
}

TEST(deposit_withdraw_and_history_are_correct) {
    Fixture f;
    auto d = f.svc.deposit(f.alice, {{"amount", "1000.50"}});
    CHECK_EQ(d.status, 201);
    CHECK_EQ(d.body["data"]["account"]["balance"].get<std::string>(), std::string("1000.50"));
    auto w = f.svc.withdraw(f.alice, {{"amount", 250}});
    CHECK_EQ(w.status, 201);
    CHECK_EQ(w.body["data"]["account"]["balance"].get<std::string>(), std::string("750.50"));
    CHECK_EQ(f.svc.withdraw(f.alice, {{"amount", "750.51"}}).status, 422);                // one paisa too much
    auto h = f.svc.customerTransactions(f.alice, {});
    CHECK_EQ(h.body["data"]["total"].get<int>(), 2);
    CHECK_EQ(h.body["data"]["items"][0]["type"].get<std::string>(), std::string("withdrawal"));  // newest first
    CHECK_EQ(h.body["data"]["items"][0]["direction"].get<std::string>(), std::string("debit"));
    CHECK_EQ(h.body["data"]["items"][1]["direction"].get<std::string>(), std::string("credit"));
    auto o = f.svc.customerOverview(f.alice);
    CHECK_EQ(o.body["data"]["summary"]["totalDeposits"].get<std::string>(), std::string("1000.50"));
    CHECK_EQ(o.body["data"]["summary"]["totalWithdrawals"].get<std::string>(), std::string("250.00"));
}

TEST(transfer_moves_money_and_shares_one_reference) {
    Fixture f;
    f.svc.deposit(f.alice, {{"amount", 500}});
    auto t = f.svc.transfer(f.alice, {{"toAccount", f.bobAcc()}, {"amount", "125.25"}});
    CHECK_EQ(t.status, 201);
    CHECK_EQ(f.bank.findAccount(f.aliceAcc())->balance(), rs(374) + 75);
    CHECK_EQ(f.bank.findAccount(f.bobAcc())->balance(), rs(125) + 25);
    auto a = f.svc.customerTransactions(f.alice, {{"type", "transfer"}});
    auto b = f.svc.customerTransactions(f.bob, {{"type", "transfer"}});
    CHECK_EQ(a.body["data"]["total"].get<int>(), 1);
    CHECK_EQ(b.body["data"]["total"].get<int>(), 1);
    CHECK_EQ(a.body["data"]["items"][0]["reference"].get<std::string>(), b.body["data"]["items"][0]["reference"].get<std::string>());
    CHECK_EQ(a.body["data"]["items"][0]["direction"].get<std::string>(), std::string("debit"));
    CHECK_EQ(b.body["data"]["items"][0]["direction"].get<std::string>(), std::string("credit"));
}

TEST(invalid_transfers_change_nothing) {
    Fixture f;
    f.svc.deposit(f.alice, {{"amount", 100}});
    const auto before = f.bank.summary().transactionCount;
    CHECK_EQ(f.svc.transfer(f.alice, {{"toAccount", 424242}, {"amount", 10}}).status, 404);          // nobody there
    CHECK_EQ(f.svc.transfer(f.alice, {{"toAccount", f.aliceAcc()}, {"amount", 10}}).status, 400);    // to self
    CHECK_EQ(f.svc.transfer(f.alice, {{"toAccount", f.bobAcc()}, {"amount", -1}}).status, 400);
    CHECK_EQ(f.svc.transfer(f.alice, {{"toAccount", f.bobAcc()}, {"amount", 0}}).status, 400);
    CHECK_EQ(f.svc.transfer(f.alice, {{"toAccount", f.bobAcc()}, {"amount", 100.01}}).status, 422);  // insufficient
    CHECK_EQ(f.svc.transfer(f.alice, {{"toAccount", "12abc"}, {"amount", 1}}).status, 400);
    CHECK_EQ(f.svc.transfer(f.alice, {{"amount", 1}}).status, 400);                                  // no recipient
    CHECK_EQ(f.bank.findAccount(f.aliceAcc())->balance(), rs(100));
    CHECK_EQ(f.bank.findAccount(f.bobAcc())->balance(), 0LL);
    CHECK_EQ(f.bank.summary().transactionCount, before);
}

TEST(repeated_submission_is_applied_only_once) {
    Fixture f;
    const json req = {{"amount", 100}, {"idempotencyKey", "key-0001-abcdefgh"}};
    auto first = f.svc.deposit(f.alice, req);
    auto second = f.svc.deposit(f.alice, req);          // double click / retry
    CHECK_EQ(first.status, 201);
    CHECK_EQ(second.status, 201);
    CHECK(second.body.contains("replayed"));
    CHECK_EQ(f.bank.findAccount(f.aliceAcc())->balance(), rs(100));    // credited once, not twice
    CHECK_EQ(f.svc.customerTransactions(f.alice, {}).body["data"]["total"].get<int>(), 1);
    // same key but a different amount is a different request -> refused
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", 999}, {"idempotencyKey", "key-0001-abcdefgh"}}).status, 409);
    // another customer using the same key string is unaffected (keys are per customer)
    CHECK_EQ(f.svc.deposit(f.bob, req).status, 201);
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", 1}, {"idempotencyKey", "bad key!"}}).status, 400);
    // Transfers too.
    const json tr = {{"toAccount", f.bobAcc()}, {"amount", 10}, {"idempotencyKey", "xfer-0001-abcdefgh"}};
    CHECK_EQ(f.svc.transfer(f.alice, tr).status, 201);
    CHECK_EQ(f.svc.transfer(f.alice, tr).status, 201);
    CHECK_EQ(f.bank.findAccount(f.aliceAcc())->balance(), rs(90));
}

TEST(customers_only_ever_see_their_own_data) {
    Fixture f;
    f.svc.deposit(f.alice, {{"amount", 111}});
    f.svc.deposit(f.bob, {{"amount", 222}});
    const std::string aliceHistory = f.svc.customerTransactions(f.alice, {}).body.dump();
    CHECK(aliceHistory.find("222.00") == std::string::npos);
    CHECK_EQ(f.svc.customerTransactions(f.alice, {}).body["data"]["total"].get<int>(), 1);
    CHECK_EQ(f.svc.customerOverview(f.bob).body["data"]["account"]["balance"].get<std::string>(), std::string("222.00"));
    // The service has no parameter through which a customer could name someone else's account:
    // extra fields in the body are ignored and an unknown customer id is refused.
    auto sneaky = f.svc.deposit(f.alice, {{"amount", 5}, {"accountNumber", f.bobAcc()}, {"customerId", f.bob}});
    CHECK_EQ(sneaky.status, 201);
    CHECK_EQ(f.bank.findAccount(f.bobAcc())->balance(), rs(222));   // Bob untouched
    CHECK_EQ(f.bank.findAccount(f.aliceAcc())->balance(), rs(116));
    CHECK_EQ(f.svc.customerOverview("CUS999999").status, 401);
    // Transaction filters never widen the scope.
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"account", std::to_string(f.bobAcc())}}).body["data"]["total"].get<int>(), 2);
}

TEST(deactivated_accounts_cannot_move_money) {
    Fixture f;
    f.svc.deposit(f.alice, {{"amount", 100}});
    CHECK_EQ(f.svc.setAccountStatus(std::to_string(f.aliceAcc()), {{"status", "deactivated"}}).status, 200);
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", 1}}).status, 403);
    CHECK_EQ(f.svc.withdraw(f.alice, {{"amount", 1}}).status, 403);
    CHECK_EQ(f.svc.transfer(f.alice, {{"toAccount", f.bobAcc()}, {"amount", 1}}).status, 403);
    CHECK_EQ(f.svc.transfer(f.bob, {{"toAccount", f.aliceAcc()}, {"amount", 1}}).status, 409);   // cannot receive
    CHECK_EQ(f.svc.customerOverview(f.alice).status, 200);                                        // may still look
    CHECK_EQ(f.svc.setAccountStatus(std::to_string(f.aliceAcc()), {{"status", "active"}}).status, 200);
    CHECK_EQ(f.svc.deposit(f.alice, {{"amount", 1}}).status, 201);
    CHECK_EQ(f.svc.setAccountStatus(std::to_string(f.aliceAcc()), {{"status", "frozen"}}).status, 400);
    CHECK_EQ(f.svc.setAccountStatus("99999", {{"status", "active"}}).status, 404);
}

TEST(recipient_lookup_masks_the_name) {
    Fixture f;
    auto r = f.svc.lookupRecipient(f.alice, std::to_string(f.bobAcc()));
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body["data"]["maskedName"].get<std::string>(), std::string("B** V****"));
    CHECK(r.body.dump().find("Verma") == std::string::npos);
    CHECK_EQ(f.svc.lookupRecipient(f.alice, "424242").status, 404);
    CHECK_EQ(f.svc.lookupRecipient(f.alice, std::to_string(f.aliceAcc())).status, 400);
    CHECK_EQ(f.svc.lookupRecipient(f.alice, "abc").status, 400);
}

TEST(profile_update_only_changes_name_and_phone) {
    Fixture f;
    auto r = f.svc.updateProfile(f.alice, {{"fullName", "Alice B. Sharma"}, {"phone", "+91 98765 43210"},
                                           {"email", "evil@example.com"}, {"role", "admin"}, {"accountNumber", 1}});
    CHECK_EQ(r.status, 200);
    const Customer* c = f.bank.users().findCustomerById(f.alice);
    CHECK_EQ(c->fullName, std::string("Alice B. Sharma"));
    CHECK_EQ(c->phone, std::string("+919876543210"));
    CHECK_EQ(c->email, std::string("alice@example.com"));                       // unchanged
    CHECK_EQ(f.bank.findAccount(c->accountNumber)->holderName(), std::string("Alice B. Sharma"));
    CHECK_EQ(f.svc.updateProfile(f.alice, {{"fullName", "<script>"}, {"phone", "9876543210"}}).status, 400);
    CHECK_EQ(f.svc.updateProfile(f.alice, {{"fullName", "Al"}, {"phone", "123"}}).status, 400);
}

TEST(admin_views_never_contain_password_hashes) {
    Fixture f;
    f.svc.deposit(f.alice, {{"amount", 50}});
    const std::string everything =
        f.svc.adminCustomers({}).body.dump() + f.svc.adminCustomerDetail(f.alice).body.dump() +
        f.svc.adminAccounts({}).body.dump() + f.svc.adminTransactions({}).body.dump() +
        f.svc.adminOverview({}).body.dump() + f.svc.customerProfile(f.alice).body.dump() +
        f.svc.customerOverview(f.alice).body.dump();
    CHECK(everything.find("argon2") == std::string::npos);
    CHECK(everything.find("passwordHash") == std::string::npos);
    CHECK(everything.find("test-hash") == std::string::npos);
}

TEST(admin_customer_search_filter_sort_and_overview) {
    Fixture f;
    registerTestCustomer(f.bank, "Zoya Khan", "zoya@example.com");
    f.svc.deposit(f.bob, {{"amount", 900}});
    f.svc.deposit(f.alice, {{"amount", 100}});
    f.svc.withdraw(f.alice, {{"amount", 30}});
    auto all = f.svc.adminCustomers({});
    CHECK_EQ(all.body["data"]["total"].get<int>(), 3);
    CHECK_EQ(f.svc.adminCustomers({{"search", "ZOYA"}}).body["data"]["total"].get<int>(), 1);          // case-insensitive
    CHECK_EQ(f.svc.adminCustomers({{"search", "bob@"}}).body["data"]["total"].get<int>(), 1);
    CHECK_EQ(f.svc.adminCustomers({{"search", std::to_string(f.aliceAcc())}}).body["data"]["total"].get<int>(), 1);
    CHECK_EQ(f.svc.adminCustomers({{"sort", "balance"}, {"order", "desc"}}).body["data"]["items"][0]["fullName"].get<std::string>(), std::string("Bob Verma"));
    CHECK_EQ(f.svc.adminCustomers({{"sort", "name"}}).body["data"]["items"][0]["fullName"].get<std::string>(), std::string("Alice Sharma"));
    CHECK_EQ(f.svc.adminCustomers({{"sort", "colour"}}).status, 400);
    f.svc.setAccountStatus(std::to_string(f.bobAcc()), {{"status", "deactivated"}});
    CHECK_EQ(f.svc.adminCustomers({{"status", "deactivated"}}).body["data"]["total"].get<int>(), 1);
    CHECK_EQ(f.svc.adminCustomers({{"status", "active"}}).body["data"]["total"].get<int>(), 2);
    auto ov = f.svc.adminOverview({});
    CHECK_EQ(ov.body["data"]["totalCustomers"].get<int>(), 3);
    CHECK_EQ(ov.body["data"]["totalTransactions"].get<int>(), 3);
    CHECK_EQ(ov.body["data"]["totalDeposits"].get<std::string>(), std::string("1000.00"));
    CHECK_EQ(ov.body["data"]["totalWithdrawals"].get<std::string>(), std::string("30.00"));
    CHECK_EQ(ov.body["data"]["deactivatedAccounts"].get<int>(), 1);
    CHECK_EQ(f.svc.adminCustomerDetail("CUS000999").status, 404);
    CHECK_EQ(f.svc.adminAccountDetail(std::to_string(f.aliceAcc())).body["data"]["transactionCount"].get<int>(), 2);
}

TEST(transaction_filters_by_type_and_date) {
    Fixture f;
    f.svc.deposit(f.alice, {{"amount", 100}});
    f.svc.withdraw(f.alice, {{"amount", 10}});
    f.svc.transfer(f.alice, {{"toAccount", f.bobAcc()}, {"amount", 5}});
    CHECK_EQ(f.svc.customerTransactions(f.alice, {}).body["data"]["total"].get<int>(), 3);
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"type", "deposit"}}).body["data"]["total"].get<int>(), 1);
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"type", "withdrawal"}}).body["data"]["total"].get<int>(), 1);
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"type", "transfer"}}).body["data"]["total"].get<int>(), 1);
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"type", "bogus"}}).status, 400);
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"from", "4102444800"}}).body["data"]["total"].get<int>(), 0);   // far future
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"to", "1"}}).body["data"]["total"].get<int>(), 0);             // before everything
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"from", "0"}, {"to", "4102444800"}}).body["data"]["total"].get<int>(), 3);
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"from", "10"}, {"to", "5"}}).status, 400);
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"limit", "2"}}).body["data"]["items"].size(), static_cast<std::size_t>(2));
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"limit", "2"}, {"offset", "2"}}).body["data"]["items"].size(), static_cast<std::size_t>(1));
    CHECK_EQ(f.svc.customerTransactions(f.alice, {{"limit", "5000"}}).status, 400);
}

TEST(error_shape_is_consistent) {
    Fixture f;
    auto err = f.svc.adminAccountDetail("9999");
    CHECK_EQ(err.status, 404);
    CHECK(err.body["success"] == false);
    CHECK_EQ(err.body["error"]["code"].get<std::string>(), std::string("NOT_FOUND"));
    CHECK_EQ(f.svc.adminAccountDetail("abc").status, 400);
    CHECK_EQ(f.svc.reverse(json::object()).status, 404);    // nothing to reverse yet
    f.svc.deposit(f.alice, {{"amount", 10}});
    CHECK_EQ(f.svc.reverse(json::object()).status, 201);
    CHECK_EQ(f.bank.findAccount(f.aliceAcc())->balance(), 0LL);
}
