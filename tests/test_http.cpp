// End-to-end tests: a real ApiServer listens on a free local port and the tests talk to it with
// an HTTP client, exactly like a browser would (cookies, CSRF header, redirects).
#include <httplib.h>

#include <atomic>
#include <thread>

#include "ApiServer.h"
#include "Security.h"
#include "test_framework.h"

using nlohmann::json;

namespace {

struct Server {
    TempDir dir;
    Bank bank{dir.str()};
    BankService svc{bank};
    SessionManager sessions;
    AuthService auth{svc, sessions};
    ApiServer api;
    std::thread thread;
    int port = 0;

    static ApiConfig config() {
        ApiConfig c;
        c.frontendDir = FRONTEND_DIR;
        return c;
    }
    Server() : api(svc, auth, sessions, config()) {
        port = api.bindToFreePort("127.0.0.1");
        thread = std::thread([this] { api.serve(); });
        httplib::Client probe("127.0.0.1", port);
        for (int i = 0; i < 200 && !probe.Get("/healthz"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ~Server() { api.stop(); thread.join(); }
};

// A tiny "browser": remembers the session cookie and the CSRF token.
struct Browser {
    int port;
    std::string cookie, csrf;
    explicit Browser(int p) : port(p) {}

    httplib::Headers headers(bool withCsrf, httplib::Headers extra = {}) const {
        httplib::Headers h = extra;
        if (!cookie.empty()) h.emplace("Cookie", "bms_session=" + cookie);
        if (withCsrf && !csrf.empty()) h.emplace("X-CSRF-Token", csrf);
        return h;
    }
    httplib::Result get(const std::string& path) {
        httplib::Client c("127.0.0.1", port);
        return c.Get(path, headers(false));
    }
    httplib::Result post(const std::string& path, const json& body, bool withCsrf = true, httplib::Headers extra = {}) {
        httplib::Client c("127.0.0.1", port);
        return c.Post(path, headers(withCsrf, extra), body.dump(), "application/json");
    }
    httplib::Result put(const std::string& path, const json& body) {
        httplib::Client c("127.0.0.1", port);
        return c.Put(path, headers(true), body.dump(), "application/json");
    }
    // Logs in and stores cookie + csrf. Returns the HTTP status.
    int login(const std::string& path, const json& body) {
        auto r = post(path, body, false);
        if (!r) return -1;
        if (r->status == 200) {
            const std::string sc = r->get_header_value("Set-Cookie");
            const auto eq = sc.find('='), semi = sc.find(';');
            cookie = sc.substr(eq + 1, semi - eq - 1);
            csrf = json::parse(r->body)["data"]["csrfToken"].get<std::string>();
        }
        return r->status;
    }
};

json J(const httplib::Result& r) { return json::parse(r->body, nullptr, false); }

json registration(const std::string& email, const std::string& name = "Http Tester") {
    return {{"fullName", name}, {"email", email}, {"phone", "9876543210"},
            {"password", "Str0ngPass1"}, {"confirmPassword", "Str0ngPass1"}};
}

// Registers through the API and logs in. Each Server allows 10 registrations per IP.
Browser customer(Server& s, const std::string& email, const std::string& name = "Http Tester") {
    Browser b(s.port);
    auto r = b.post("/api/auth/register", registration(email, name), false);
    CHECK_EQ(r->status, 201);
    CHECK_EQ(b.login("/api/auth/login", {{"email", email}, {"password", "Str0ngPass1"}}), 200);
    return b;
}
Browser admin(Server& s) {
    s.auth.bootstrapAdmin("root", "Adm1nPassw0rd!");
    Browser b(s.port);
    CHECK_EQ(b.login("/api/admin/login", {{"username", "root"}, {"password", "Adm1nPassw0rd!"}}), 200);
    return b;
}
long long accountOf(Browser& b) { return J(b.get("/api/me/account"))["data"]["accountNumber"].get<long long>(); }

}  // namespace

TEST(http_pages_and_basic_protection) {
    Server s;
    Browser anon(s.port);
    CHECK_EQ(anon.get("/healthz")->status, 200);
    auto home = anon.get("/");
    CHECK_EQ(home->status, 200);
    CHECK(home->body.find("Customer Banking") != std::string::npos);
    CHECK(home->body.find("Admin Login") != std::string::npos);
    for (const char* p : {"/login", "/register", "/admin-login"}) CHECK_EQ(anon.get(p)->status, 200);
    auto d = anon.get("/dashboard");                       // signed-out visitors are redirected by the SERVER
    CHECK_EQ(d->status, 302);
    CHECK_EQ(d->get_header_value("Location"), std::string("/login"));
    CHECK_EQ(anon.get("/admin")->get_header_value("Location"), std::string("/admin-login"));
    auto nf = anon.get("/no/such/page");
    CHECK_EQ(nf->status, 404);
    CHECK(nf->body.find("Page not found") != std::string::npos);
    CHECK_EQ(anon.get("/api/nothing")->status, 404);
    CHECK_EQ(J(anon.get("/api/nothing"))["error"]["code"].get<std::string>(), std::string("NOT_FOUND"));
    CHECK_EQ(anon.get("/static/js/common.js")->status, 200);
    CHECK_EQ(anon.get("/static/css/app.css")->status, 200);
    // security headers
    CHECK(home->get_header_value("Content-Security-Policy").find("script-src 'self'") != std::string::npos);
    CHECK_EQ(home->get_header_value("X-Frame-Options"), std::string("DENY"));
    CHECK_EQ(home->get_header_value("X-Content-Type-Options"), std::string("nosniff"));
    // every API route needs a session
    for (const char* p : {"/api/me/overview", "/api/me/account", "/api/me/transactions", "/api/admin/overview", "/api/admin/customers"})
        CHECK_EQ(anon.get(p)->status, 401);
    CHECK_EQ(anon.post("/api/me/deposit", {{"amount", 5}})->status, 401);
    CHECK_EQ(J(anon.get("/api/auth/session"))["data"]["authenticated"].get<bool>(), false);
}

TEST(http_register_login_logout_cycle) {
    Server s;
    Browser b(s.port);
    auto weak = b.post("/api/auth/register", {{"fullName", "A"}, {"email", "x"}, {"phone", "1"}, {"password", "a"}, {"confirmPassword", "b"}}, false);
    CHECK_EQ(weak->status, 400);
    CHECK(J(weak)["error"]["fields"].contains("email"));
    auto reg = b.post("/api/auth/register", registration("cycle@example.com"), false);
    CHECK_EQ(reg->status, 201);
    CHECK_EQ(J(reg)["data"]["customerId"].get<std::string>(), std::string("CUS000001"));
    CHECK(reg->body.find("Str0ngPass1") == std::string::npos);
    CHECK_EQ(b.post("/api/auth/register", registration("cycle@example.com"), false)->status, 400);   // duplicate

    CHECK_EQ(b.login("/api/auth/login", {{"email", "cycle@example.com"}, {"password", "WrongPass1"}}), 401);
    auto bad = b.post("/api/auth/login", {{"email", "cycle@example.com"}, {"password", "WrongPass1"}}, false);
    CHECK(bad->get_header_value("Set-Cookie").empty());
    auto good = b.post("/api/auth/login", {{"email", "cycle@example.com"}, {"password", "Str0ngPass1"}}, false);
    CHECK_EQ(good->status, 200);
    const std::string sc = good->get_header_value("Set-Cookie");
    CHECK(sc.find("HttpOnly") != std::string::npos);
    CHECK(sc.find("SameSite=Strict") != std::string::npos);
    CHECK(sc.find("Path=/") != std::string::npos);
    CHECK_EQ(b.login("/api/auth/login", {{"email", "cycle@example.com"}, {"password", "Str0ngPass1"}}), 200);

    auto dash = b.get("/dashboard");
    CHECK_EQ(dash->status, 200);                                   // signed in: the page is served
    CHECK_EQ(b.get("/login")->status, 302);                        // and the login page skips ahead
    auto me = J(b.get("/api/me/overview"));
    CHECK_EQ(me["data"]["profile"]["email"].get<std::string>(), std::string("cycle@example.com"));
    CHECK_EQ(me["data"]["account"]["balance"].get<std::string>(), std::string("0.00"));
    CHECK(me.dump().find("argon2") == std::string::npos);          // no password hash in any response

    const std::string oldCookie = b.cookie;
    auto out = b.post("/api/auth/logout", json::object());
    CHECK_EQ(out->status, 200);
    CHECK(out->get_header_value("Set-Cookie").find("Max-Age=0") != std::string::npos);
    Browser replay(s.port);
    replay.cookie = oldCookie;                                     // someone who copied the old cookie
    CHECK_EQ(replay.get("/api/me/overview")->status, 401);         // the session is dead on the server
    CHECK_EQ(replay.get("/dashboard")->status, 302);
}

TEST(http_csrf_origin_and_content_type_checks) {
    Server s;
    Browser b = customer(s, "csrf@example.com");
    auto noToken = b.post("/api/me/deposit", {{"amount", 10}}, /*csrf=*/false);
    CHECK_EQ(noToken->status, 403);
    CHECK_EQ(J(noToken)["error"]["code"].get<std::string>(), std::string("CSRF_FAILED"));
    Browser forged = b;
    forged.csrf = std::string(48, 'a');
    CHECK_EQ(forged.post("/api/me/deposit", {{"amount", 10}})->status, 403);
    CHECK_EQ(J(b.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("0.00"));    // nothing happened
    CHECK_EQ(b.post("/api/me/deposit", {{"amount", 10}}, true, {{"Origin", "https://evil.example"}})->status, 403);
    CHECK_EQ(b.post("/api/me/deposit", {{"amount", 10}}, true, {{"Sec-Fetch-Site", "cross-site"}})->status, 403);
    CHECK_EQ(b.post("/api/me/deposit", {{"amount", 10}}, true, {{"Origin", "http://127.0.0.1:" + std::to_string(s.port)}})->status, 201);
    httplib::Client c("127.0.0.1", s.port);
    auto form = c.Post("/api/me/deposit", b.headers(true), "amount=10", "application/x-www-form-urlencoded");
    CHECK_EQ(form->status, 415);
    auto junk = c.Post("/api/me/deposit", b.headers(true), "{not json", "application/json");
    CHECK_EQ(junk->status, 400);
    CHECK_EQ(b.post("/api/auth/logout", json::object(), false)->status, 403);        // logout needs the token too
}

TEST(http_money_flows_and_balances) {
    Server s;
    Browser a = customer(s, "a@example.com", "Alice Sharma");
    Browser b = customer(s, "b@example.com", "Bob Verma");
    const long long accA = accountOf(a), accB = accountOf(b);
    CHECK(accA != accB);
    CHECK_EQ(a.post("/api/me/deposit", {{"amount", "1000.00"}})->status, 201);
    CHECK_EQ(a.post("/api/me/deposit", {{"amount", 0}})->status, 400);
    CHECK_EQ(a.post("/api/me/deposit", {{"amount", -50}})->status, 400);
    CHECK_EQ(a.post("/api/me/deposit", {{"amount", "abc"}})->status, 400);
    CHECK_EQ(a.post("/api/me/deposit", {{"amount", "99999999999"}})->status, 422);
    CHECK_EQ(a.post("/api/me/withdraw", {{"amount", "5000"}})->status, 422);
    auto w = a.post("/api/me/withdraw", {{"amount", "100.25"}});
    CHECK_EQ(w->status, 201);
    CHECK_EQ(J(w)["data"]["account"]["balance"].get<std::string>(), std::string("899.75"));
    CHECK_EQ(a.post("/api/me/transfer", {{"toAccount", 424242}, {"amount", 1}})->status, 404);
    CHECK_EQ(a.post("/api/me/transfer", {{"toAccount", accA}, {"amount", 1}})->status, 400);
    CHECK_EQ(a.post("/api/me/transfer", {{"toAccount", accB}, {"amount", "900"}})->status, 422);
    auto t = a.post("/api/me/transfer", {{"toAccount", accB}, {"amount", "99.75"}});
    CHECK_EQ(t->status, 201);
    CHECK_EQ(J(a.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("800.00"));
    CHECK_EQ(J(b.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("99.75"));
    auto rec = J(a.get("/api/me/recipient?account=" + std::to_string(accB)));
    CHECK_EQ(rec["data"]["maskedName"].get<std::string>(), std::string("B** V****"));
    auto ha = J(a.get("/api/me/transactions?type=transfer"));
    auto hb = J(b.get("/api/me/transactions"));
    CHECK_EQ(ha["data"]["total"].get<int>(), 1);
    CHECK_EQ(hb["data"]["total"].get<int>(), 1);
    CHECK_EQ(ha["data"]["items"][0]["reference"].get<std::string>(), hb["data"]["items"][0]["reference"].get<std::string>());
    CHECK_EQ(J(a.get("/api/me/transactions"))["data"]["total"].get<int>(), 3);
    CHECK_EQ(a.get("/api/me/transactions?type=hack")->status, 400);
}

TEST(http_customer_a_cannot_touch_customer_b) {
    Server s;
    Browser a = customer(s, "a@example.com", "Alice Sharma");
    Browser b = customer(s, "b@example.com", "Bob Verma");
    const long long accB = accountOf(b);
    b.post("/api/me/deposit", {{"amount", 777}});
    // Everything a customer reads comes from the session. Trying to name Bob changes nothing:
    const std::string hist = a.get("/api/me/transactions?account=" + std::to_string(accB) + "&customerId=CUS000002")->body;
    CHECK(hist.find("777") == std::string::npos);
    CHECK_EQ(J(a.get("/api/me/account?accountNumber=" + std::to_string(accB)))["data"]["holderName"].get<std::string>(), std::string("Alice Sharma"));
    // Fields in the body naming Bob (or claiming a role) are ignored.
    auto dep = a.post("/api/me/deposit", {{"amount", 5}, {"accountNumber", accB}, {"customerId", "CUS000002"}, {"role", "admin"}});
    CHECK_EQ(dep->status, 201);
    CHECK_EQ(J(b.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("777.00"));
    CHECK_EQ(J(a.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("5.00"));
    auto prof = a.put("/api/me/profile", {{"fullName", "Alice Sharma"}, {"phone", "9123456789"}, {"role", "admin"}, {"email", "x@y.com"}, {"customerId", "CUS000002"}});
    CHECK_EQ(prof->status, 200);
    CHECK_EQ(J(b.get("/api/me/profile"))["data"]["phone"].get<std::string>(), std::string("9876543210"));
    CHECK_EQ(J(a.get("/api/me/profile"))["data"]["email"].get<std::string>(), std::string("a@example.com"));
    // Customers have no door into admin endpoints, whatever they send.
    for (const char* p : {"/api/admin/overview", "/api/admin/customers", "/api/admin/customers/CUS000002", "/api/admin/accounts",
                          "/api/admin/transactions", "/api/admin/queue", "/api/admin/reversal"})
        CHECK_EQ(a.get(p)->status, 403);
    CHECK_EQ(a.post("/api/admin/accounts/" + std::to_string(accB) + "/status", {{"status", "deactivated"}})->status, 403);
    CHECK_EQ(a.post("/api/admin/reversal", json::object())->status, 403);
    CHECK_EQ(a.get("/admin")->status, 302);                                              // admin page is not served either
    CHECK_EQ(J(b.get("/api/me/account"))["data"]["status"].get<std::string>(), std::string("active"));
    // and an admin session cannot use customer endpoints
    Browser ad = admin(s);
    CHECK_EQ(ad.get("/api/me/overview")->status, 403);
    CHECK_EQ(ad.post("/api/me/deposit", {{"amount", 5}})->status, 403);
    CHECK_EQ(ad.get("/dashboard")->status, 302);
}

TEST(http_admin_portal_end_to_end) {
    Server s;
    Browser a = customer(s, "a@example.com", "Alice Sharma");
    Browser b = customer(s, "b@example.com", "Bob Verma");
    a.post("/api/me/deposit", {{"amount", 500}});
    const long long accA = accountOf(a);
    Browser ad(s.port);
    CHECK_EQ(ad.login("/api/admin/login", {{"username", "root"}, {"password", "Adm1nPassw0rd!"}}), 401);   // no admin yet
    ad = admin(s);
    CHECK_EQ(ad.get("/admin")->status, 200);
    auto ov = J(ad.get("/api/admin/overview"))["data"];
    CHECK_EQ(ov["totalCustomers"].get<int>(), 2);
    CHECK_EQ(ov["totalDeposits"].get<std::string>(), std::string("500.00"));
    CHECK_EQ(ov["totalTransactions"].get<int>(), 1);
    auto list = J(ad.get("/api/admin/customers?search=alice"));
    CHECK_EQ(list["data"]["total"].get<int>(), 1);
    auto detail = ad.get("/api/admin/customers/CUS000001");
    CHECK_EQ(detail->status, 200);
    CHECK(detail->body.find("argon2") == std::string::npos);
    CHECK(detail->body.find("passwordHash") == std::string::npos);
    CHECK_EQ(J(ad.get("/api/admin/accounts/" + std::to_string(accA)))["data"]["account"]["holderName"].get<std::string>(), std::string("Alice Sharma"));
    CHECK_EQ(ad.get("/api/admin/accounts/999999")->status, 404);
    CHECK_EQ(J(ad.get("/api/admin/transactions"))["data"]["total"].get<int>(), 1);
    // admin without CSRF token cannot change status
    CHECK_EQ(ad.post("/api/admin/accounts/" + std::to_string(accA) + "/status", {{"status", "deactivated"}}, false)->status, 403);
    CHECK_EQ(ad.post("/api/admin/accounts/" + std::to_string(accA) + "/status", {{"status", "deactivated"}})->status, 200);
    CHECK_EQ(a.post("/api/me/deposit", {{"amount", 1}})->status, 403);                    // deactivated: no transactions
    CHECK_EQ(a.post("/api/me/withdraw", {{"amount", 1}})->status, 403);
    CHECK_EQ(J(a.get("/api/me/account"))["data"]["status"].get<std::string>(), std::string("deactivated"));
    CHECK_EQ(ad.post("/api/admin/accounts/" + std::to_string(accA) + "/status", {{"status", "active"}})->status, 200);
    CHECK_EQ(a.post("/api/me/deposit", {{"amount", 1}})->status, 201);
    // queue + reversal keep working from the admin portal
    CHECK_EQ(ad.post("/api/admin/queue", {{"customerName", "Walk In"}, {"purpose", "Loan"}})->status, 201);
    CHECK_EQ(J(ad.get("/api/admin/queue"))["data"]["count"].get<int>(), 1);
    CHECK_EQ(ad.post("/api/admin/queue/serve", json::object())->status, 200);
    CHECK_EQ(J(ad.get("/api/admin/reversal"))["data"]["transaction"]["amount"].get<std::string>(), std::string("1.00"));
    CHECK_EQ(ad.post("/api/admin/reversal", json::object())->status, 201);
    CHECK_EQ(J(a.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("500.00"));
}

TEST(http_brute_force_lockout_and_double_submit) {
    Server s;
    Browser b = customer(s, "lock@example.com");
    // double click: same key twice -> money moves once
    const json dep = {{"amount", 100}, {"idempotencyKey", "form-000001-aaaa"}};
    CHECK_EQ(b.post("/api/me/deposit", dep)->status, 201);
    auto again = b.post("/api/me/deposit", dep);
    CHECK_EQ(again->status, 201);
    CHECK(J(again).contains("replayed"));
    CHECK_EQ(J(b.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("100.00"));
    // brute force
    Browser attacker(s.port);
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(attacker.post("/api/auth/login", {{"email", "lock@example.com"}, {"password", "Guess" + std::to_string(i) + "xyz"}}, false)->status, 401);
    auto locked = attacker.post("/api/auth/login", {{"email", "lock@example.com"}, {"password", "Str0ngPass1"}}, false);
    CHECK_EQ(locked->status, 429);
    CHECK(!locked->get_header_value("Retry-After").empty());
    CHECK(locked->get_header_value("Set-Cookie").empty());
}

TEST(http_concurrent_requests_keep_balances_exact) {
    Server s;
    Browser a = customer(s, "a@example.com", "Alice Sharma");
    Browser b = customer(s, "b@example.com", "Bob Verma");
    const long long accA = accountOf(a), accB = accountOf(b);
    a.post("/api/me/deposit", {{"amount", 1000}});
    b.post("/api/me/deposit", {{"amount", 1000}});
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    // 8 threads: each does 10 deposits of 1 and 10 transfers (A->B and B->A) of 2 and 1 rupees.
    for (int t = 0; t < 8; ++t)
        threads.emplace_back([&, t] {
            Browser& me = (t % 2 == 0) ? a : b;
            const long long other = (t % 2 == 0) ? accB : accA;
            for (int i = 0; i < 10; ++i) {
                if (me.post("/api/me/deposit", {{"amount", 1}})->status != 201) ++failures;
                if (me.post("/api/me/transfer", {{"toAccount", other}, {"amount", (t % 2 == 0) ? 2 : 1}})->status != 201) ++failures;
            }
        });
    for (auto& th : threads) th.join();
    CHECK_EQ(failures.load(), 0);
    // A: +1000 +40 deposits -80 sent (4 threads*10*2) +40 received (4*10*1) = 1000+40-80+40 = 1000
    // B: +1000 +40 deposits -40 sent +80 received = 1080
    CHECK_EQ(J(a.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("1000.00"));
    CHECK_EQ(J(b.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("1080.00"));
    // total money is conserved and the saved files agree after a restart
    Bank again(s.dir.str());
    CHECK_EQ(again.findAccount(accA)->balance() + again.findAccount(accB)->balance(), rs(2080));
}

TEST(http_password_change_and_session_info) {
    Server s;
    Browser b = customer(s, "pw@example.com");
    Browser other(s.port);
    CHECK_EQ(other.login("/api/auth/login", {{"email", "pw@example.com"}, {"password", "Str0ngPass1"}}), 200);
    auto info = J(b.get("/api/auth/session"))["data"];
    CHECK_EQ(info["role"].get<std::string>(), std::string("customer"));
    CHECK_EQ(info["csrfToken"].get<std::string>(), b.csrf);
    CHECK_EQ(b.post("/api/me/password", {{"currentPassword", "wrong"}, {"newPassword", "N3wStrongPass"}, {"confirmPassword", "N3wStrongPass"}})->status, 400);
    CHECK_EQ(b.post("/api/me/password", {{"currentPassword", "Str0ngPass1"}, {"newPassword", "N3wStrongPass"}, {"confirmPassword", "N3wStrongPass"}})->status, 200);
    CHECK_EQ(b.get("/api/me/overview")->status, 200);
    CHECK_EQ(other.get("/api/me/overview")->status, 401);                                // other device signed out
}

TEST(http_data_persists_across_a_server_restart) {
    TempDir dir;
    std::string cookieUnused;
    long long acc = 0;
    {
        Bank bank(dir.str());
        BankService svc(bank);
        SessionManager sessions;
        AuthService auth(svc, sessions);
        ApiConfig cfg; cfg.frontendDir = FRONTEND_DIR;
        ApiServer api(svc, auth, sessions, cfg);
        const int port = api.bindToFreePort("127.0.0.1");
        std::thread t([&] { api.serve(); });
        Browser b(port);
        CHECK_EQ(b.post("/api/auth/register", registration("keep@example.com"), false)->status, 201);
        CHECK_EQ(b.login("/api/auth/login", {{"email", "keep@example.com"}, {"password", "Str0ngPass1"}}), 200);
        b.post("/api/me/deposit", {{"amount", "1234.56"}});
        acc = accountOf(b);
        api.stop();
        t.join();
    }
    // "restart": brand-new objects over the same data folder; sessions are gone, data is not
    Bank bank(dir.str());
    BankService svc(bank);
    SessionManager sessions;
    AuthService auth(svc, sessions);
    ApiConfig cfg; cfg.frontendDir = FRONTEND_DIR;
    ApiServer api(svc, auth, sessions, cfg);
    const int port = api.bindToFreePort("127.0.0.1");
    std::thread t([&] { api.serve(); });
    Browser b(port);
    CHECK_EQ(b.login("/api/auth/login", {{"email", "keep@example.com"}, {"password", "Str0ngPass1"}}), 200);   // password hash persisted
    CHECK_EQ(J(b.get("/api/me/account"))["data"]["balance"].get<std::string>(), std::string("1234.56"));
    CHECK_EQ(accountOf(b), acc);
    CHECK_EQ(J(b.get("/api/me/transactions"))["data"]["total"].get<int>(), 1);
    api.stop();
    t.join();
}
