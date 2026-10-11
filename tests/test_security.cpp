#include "RateLimiter.h"
#include "Security.h"
#include "SessionManager.h"
#include "Validation.h"
#include "test_framework.h"

TEST(password_hashing_is_salted_and_verifiable) {
    const std::string h1 = security::hashPassword("Correct1Horse");
    const std::string h2 = security::hashPassword("Correct1Horse");
    CHECK(h1 != h2);                                           // random salt: same password, different hash
    CHECK(h1.rfind("$argon2id$", 0) == 0);                     // Argon2id format
    CHECK(h1.find("Correct1Horse") == std::string::npos);      // plaintext is nowhere in the hash
    CHECK(security::verifyPassword(h1, "Correct1Horse"));
    CHECK(security::verifyPassword(h2, "Correct1Horse"));
    CHECK(!security::verifyPassword(h1, "correct1horse"));
    CHECK(!security::verifyPassword(h1, ""));
    CHECK(!security::verifyPassword("", "Correct1Horse"));
    CHECK(!security::verifyPassword("not-a-hash", "Correct1Horse"));
    CHECK(!security::verifyPassword(security::decoyHash(), "anything1"));
}

TEST(random_tokens_are_long_unique_hex) {
    const std::string a = security::randomToken(32), b = security::randomToken(32);
    CHECK_EQ(a.size(), static_cast<std::size_t>(64));
    CHECK(a != b);
    for (char c : a) CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    CHECK(security::constantTimeEquals(a, a));
    CHECK(!security::constantTimeEquals(a, b));
    CHECK(!security::constantTimeEquals(a, a.substr(1)));
}

TEST(sessions_expire_and_can_be_destroyed) {
    std::time_t now = 1000000;
    SessionManager sm(600, 3600, [&] { return now; });
    const std::string id = sm.create(Role::Customer, "CUS000001");
    auto s = sm.validate(id);
    CHECK(s.has_value());
    CHECK(s->role == Role::Customer);
    CHECK_EQ(s->subjectId, std::string("CUS000001"));
    CHECK(!sm.validate("deadbeef").has_value());
    CHECK(!sm.validate(std::string(64, 'a')).has_value());
    now += 500; CHECK(sm.validate(id).has_value());          // activity refreshes the idle timer
    now += 500; CHECK(sm.validate(id).has_value());
    now += 601; CHECK(!sm.validate(id).has_value());         // idle too long
    const std::string id2 = sm.create(Role::Admin, "ADM0001");
    for (int i = 0; i < 7; ++i) { now += 500; CHECK(sm.validate(id2).has_value()); }   // keeps refreshing (3500 s)...
    now += 200;                                                                         // 3700 s > 1 h
    CHECK(!sm.validate(id2).has_value());                    // ...but the absolute limit (1h) still applies
    const std::string id3 = sm.create(Role::Customer, "CUS000002");
    sm.destroy(id3);
    CHECK(!sm.validate(id3).has_value());
}

TEST(destroy_all_for_keeps_only_the_named_session) {
    SessionManager sm;
    const std::string a = sm.create(Role::Customer, "CUS000001");
    const std::string b = sm.create(Role::Customer, "CUS000001");
    const std::string other = sm.create(Role::Customer, "CUS000002");
    sm.destroyAllFor(Role::Customer, "CUS000001", a);
    CHECK(sm.validate(a).has_value());
    CHECK(!sm.validate(b).has_value());
    CHECK(sm.validate(other).has_value());
    CHECK(sm.validate(a)->csrfToken != sm.validate(other)->csrfToken);
}

TEST(rate_limiter_blocks_then_recovers) {
    std::time_t now = 5000;
    RateLimiter rl(3, 60, [&] { return now; });
    int wait = 0;
    CHECK(!rl.isBlocked("k"));
    rl.recordFailure("k"); rl.recordFailure("k");
    CHECK(!rl.isBlocked("k"));
    rl.recordFailure("k");
    CHECK(rl.isBlocked("k", &wait));
    CHECK(wait >= 1 && wait <= 60);
    CHECK(!rl.isBlocked("other"));
    now += 61;
    CHECK(!rl.isBlocked("k"));                    // window passed
    rl.recordFailure("k"); rl.recordFailure("k"); rl.recordFailure("k");
    CHECK(rl.isBlocked("k"));
    rl.reset("k");
    CHECK(!rl.isBlocked("k"));
}

TEST(validation_accepts_good_and_rejects_bad_input) {
    CHECK_EQ(validation::email("  Ruchi.Agar+x@Example.COM "), std::string("ruchi.agar+x@example.com"));
    for (const char* bad : {"", "plain", "a@b", "a@@b.com", "@b.com", "a b@c.com", "a@b..com", ".a@b.com", "a@b.c", "a@-b.com", "a@b.c0m"})
        CHECK_THROWS_CODE(validation::email(bad), ErrorCode::InvalidInput);
    CHECK_EQ(validation::phone("+91 98765-43210"), std::string("+919876543210"));
    CHECK_EQ(validation::phone("9876543210"), std::string("9876543210"));
    for (const char* bad : {"", "12345", "98765abc10", "98765 4321 0987 6543", "9876+543210"})
        CHECK_THROWS_CODE(validation::phone(bad), ErrorCode::InvalidInput);
    CHECK_EQ(validation::fullName("  Mary-Ann O'Neil Jr. "), std::string("Mary-Ann O'Neil Jr."));
    for (const char* bad : {"", "A", "<script>alert(1)</script>", "Bob; DROP TABLE", "12345", "a|b", "Name\nTwo"})
        CHECK_THROWS_CODE(validation::fullName(bad), ErrorCode::InvalidInput);
    validation::password("Str0ngPass", "x@y.com");
    for (const char* bad : {"short1", "allletters", "12345678901", "password123", "x@y.com", "Has\tTab1abc"})
        CHECK_THROWS_CODE(validation::password(bad, "x@y.com"), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(validation::password("Abc12345", "", 10), ErrorCode::InvalidInput);
    CHECK_EQ(validation::username(" Admin.One "), std::string("admin.one"));
    CHECK_THROWS_CODE(validation::username("a"), ErrorCode::InvalidInput);
    CHECK_THROWS_CODE(validation::username("bad name"), ErrorCode::InvalidInput);
}
