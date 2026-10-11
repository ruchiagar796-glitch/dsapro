#include "AuthService.h"
#include "Security.h"
#include "test_framework.h"

using nlohmann::json;

namespace {
struct Auth {
    Bank bank;
    BankService svc{bank};
    SessionManager sessions;
    AuthService auth{svc, sessions};
    json reg(const std::string& email, const std::string& pw = "Str0ngPass1") {
        return {{"fullName", "Test Person"}, {"email", email}, {"phone", "9876543210"},
                {"password", pw}, {"confirmPassword", pw}};
    }
};
}  // namespace

TEST(registration_validates_every_field) {
    Auth a;
    auto bad = a.auth.registerCustomer({{"fullName", "X"}, {"email", "nope"}, {"phone", "1"}, {"password", "weak"}, {"confirmPassword", "other"}}, "1.1.1.1");
    CHECK_EQ(bad.result.status, 400);
    const json& f = bad.result.body["error"]["fields"];
    CHECK(f.contains("fullName")); CHECK(f.contains("email")); CHECK(f.contains("phone")); CHECK(f.contains("password"));
    auto mismatch = a.auth.registerCustomer(a.reg("m@example.com"), "1.1.1.1");
    CHECK_EQ(mismatch.result.status, 201);
    json j = a.reg("n@example.com"); j["confirmPassword"] = "Different1Pass";
    auto mm = a.auth.registerCustomer(j, "1.1.1.1");
    CHECK_EQ(mm.result.status, 400);
    CHECK(mm.result.body["error"]["fields"].contains("confirmPassword"));
    CHECK_EQ(a.auth.registerCustomer(json::array(), "1.1.1.1").result.status, 400);
    CHECK_EQ(a.bank.users().customerCount(), static_cast<std::size_t>(1));      // only the valid one
}

TEST(registration_creates_hashed_customer_and_rejects_duplicates) {
    Auth a;
    auto r = a.auth.registerCustomer(a.reg("Dup@Example.com"), "1.1.1.1");
    CHECK_EQ(r.result.status, 201);
    CHECK_EQ(r.result.body["data"]["customerId"].get<std::string>(), std::string("CUS000001"));
    CHECK(r.sessionId.empty());                                                 // registering does not log you in
    const Customer* c = a.bank.users().findCustomerByEmail("dup@example.com");  // e-mail is normalised
    CHECK(c != nullptr);
    CHECK(c->passwordHash.find("Str0ngPass1") == std::string::npos);
    CHECK(c->passwordHash.rfind("$argon2id$", 0) == 0);
    CHECK_EQ(a.bank.findAccount(c->accountNumber)->balance(), 0LL);
    auto dup = a.auth.registerCustomer(a.reg("dup@example.com"), "1.1.1.1");
    CHECK_EQ(dup.result.status, 400);
    CHECK(dup.result.body["error"]["fields"].contains("email"));
    CHECK_EQ(a.bank.users().customerCount(), static_cast<std::size_t>(1));
}

TEST(login_errors_do_not_reveal_whether_an_email_exists) {
    Auth a;
    a.auth.registerCustomer(a.reg("real@example.com"), "1.1.1.1");
    auto wrongPw = a.auth.loginCustomer({{"email", "real@example.com"}, {"password", "Wrong1Password"}}, "2.2.2.2");
    auto noUser = a.auth.loginCustomer({{"email", "ghost@example.com"}, {"password", "Wrong1Password"}}, "2.2.2.3");
    CHECK_EQ(wrongPw.result.status, 401);
    CHECK_EQ(noUser.result.status, 401);
    CHECK_EQ(wrongPw.result.body.dump(), noUser.result.body.dump());           // byte-for-byte identical answers
    CHECK(wrongPw.sessionId.empty());
    CHECK_EQ(a.auth.loginCustomer(json::object(), "2.2.2.4").result.status, 401);
    auto ok = a.auth.loginCustomer({{"email", " REAL@example.com "}, {"password", "Str0ngPass1"}}, "2.2.2.2");
    CHECK_EQ(ok.result.status, 200);
    CHECK_EQ(ok.sessionId.size(), static_cast<std::size_t>(64));
    CHECK(a.sessions.validate(ok.sessionId).has_value());
    CHECK(ok.result.body.dump().find(ok.sessionId) == std::string::npos);      // the cookie secret is not in the JSON
    auto second = a.auth.loginCustomer({{"email", "real@example.com"}, {"password", "Str0ngPass1"}}, "2.2.2.2");
    CHECK(second.sessionId != ok.sessionId);                                   // every login = new session id
}

TEST(brute_force_attempts_are_locked_out) {
    Auth a;
    a.auth.registerCustomer(a.reg("victim@example.com"), "1.1.1.1");
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(a.auth.loginCustomer({{"email", "victim@example.com"}, {"password", "Guess" + std::to_string(i) + "abc"}}, "9.9.9.9").result.status, 401);
    auto locked = a.auth.loginCustomer({{"email", "victim@example.com"}, {"password", "Str0ngPass1"}}, "9.9.9.9");
    CHECK_EQ(locked.result.status, 429);                                       // even the RIGHT password is refused now
    CHECK(locked.sessionId.empty());
    CHECK(locked.result.body["error"]["retryAfterSeconds"].get<int>() > 0);
    // The lock is per ip+account, so the real owner on another network can still sign in.
    CHECK_EQ(a.auth.loginCustomer({{"email", "victim@example.com"}, {"password", "Str0ngPass1"}}, "8.8.8.8").result.status, 200);
}

TEST(registrations_per_ip_are_limited) {
    Auth a;
    int created = 0, limited = 0;
    for (int i = 0; i < 12; ++i) {
        auto r = a.auth.registerCustomer(a.reg("user" + std::to_string(i) + "@example.com"), "7.7.7.7");
        if (r.result.status == 201) ++created;
        if (r.result.status == 429) ++limited;
    }
    CHECK_EQ(created, 10);
    CHECK_EQ(limited, 2);
}

TEST(admin_bootstrap_is_safe_and_login_is_separate) {
    Auth a;
    CHECK_THROWS_CODE(a.auth.bootstrapAdmin("admin", "short1"), ErrorCode::InvalidInput);        // weak
    CHECK_THROWS_CODE(a.auth.bootstrapAdmin("admin", "alllettersnodigit"), ErrorCode::InvalidInput);
    CHECK(!a.svc.hasAdmin());
    CHECK(a.auth.bootstrapAdmin("Admin", "Adm1nPassw0rd!"));
    CHECK(!a.auth.bootstrapAdmin("admin", "AnotherPassw0rd!"));                                  // never overwrites
    CHECK(a.bank.users().findAdminByUsername("admin")->passwordHash.find("Adm1nPassw0rd") == std::string::npos);
    auto ok = a.auth.loginAdmin({{"username", "admin"}, {"password", "Adm1nPassw0rd!"}}, "1.1.1.1");
    CHECK_EQ(ok.result.status, 200);
    CHECK_EQ(ok.result.body["data"]["role"].get<std::string>(), std::string("admin"));
    CHECK(a.sessions.validate(ok.sessionId)->role == Role::Admin);
    // Admin credentials do not work on the customer login and vice versa.
    CHECK_EQ(a.auth.loginCustomer({{"email", "admin"}, {"password", "Adm1nPassw0rd!"}}, "1.1.1.1").result.status, 401);
    a.auth.registerCustomer(a.reg("cust@example.com"), "1.1.1.1");
    CHECK_EQ(a.auth.loginAdmin({{"username", "cust@example.com"}, {"password", "Str0ngPass1"}}, "1.1.1.1").result.status, 401);
}

TEST(password_change_checks_current_password_and_signs_out_other_devices) {
    Auth a;
    a.auth.registerCustomer(a.reg("pw@example.com"), "1.1.1.1");
    auto one = a.auth.loginCustomer({{"email", "pw@example.com"}, {"password", "Str0ngPass1"}}, "3.3.3.3");
    auto two = a.auth.loginCustomer({{"email", "pw@example.com"}, {"password", "Str0ngPass1"}}, "4.4.4.4");
    const Session s1 = *a.sessions.validate(one.sessionId);
    auto wrong = a.auth.changePassword(s1, {{"currentPassword", "nope"}, {"newPassword", "N3wStrongPass"}, {"confirmPassword", "N3wStrongPass"}});
    CHECK_EQ(wrong.status, 400);
    CHECK(wrong.body["error"]["fields"].contains("currentPassword"));
    CHECK_EQ(a.auth.changePassword(s1, {{"currentPassword", "Str0ngPass1"}, {"newPassword", "weak"}, {"confirmPassword", "weak"}}).status, 400);
    CHECK_EQ(a.auth.changePassword(s1, {{"currentPassword", "Str0ngPass1"}, {"newPassword", "Str0ngPass1"}, {"confirmPassword", "Str0ngPass1"}}).status, 400);
    CHECK_EQ(a.auth.changePassword(s1, {{"currentPassword", "Str0ngPass1"}, {"newPassword", "N3wStrongPass"}, {"confirmPassword", "x"}}).status, 400);
    auto ok = a.auth.changePassword(s1, {{"currentPassword", "Str0ngPass1"}, {"newPassword", "N3wStrongPass"}, {"confirmPassword", "N3wStrongPass"}});
    CHECK_EQ(ok.status, 200);
    CHECK(a.sessions.validate(one.sessionId).has_value());                     // this device stays signed in
    CHECK(!a.sessions.validate(two.sessionId).has_value());                    // the other one is signed out
    CHECK_EQ(a.auth.loginCustomer({{"email", "pw@example.com"}, {"password", "Str0ngPass1"}}, "5.5.5.5").result.status, 401);
    CHECK_EQ(a.auth.loginCustomer({{"email", "pw@example.com"}, {"password", "N3wStrongPass"}}, "5.5.5.5").result.status, 200);
}
