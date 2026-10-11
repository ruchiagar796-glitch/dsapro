#include "ApiServer.h"

#include <httplib.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>

#include "Security.h"

using nlohmann::json;

namespace {

constexpr const char* kCookieName = "bms_session";

void send(httplib::Response& res, const ServiceResult& r) {
    res.status = r.status;
    res.set_content(r.body.dump(), "application/json; charset=utf-8");
}

void sendError(httplib::Response& res, int status, const std::string& code, const std::string& msg) {
    send(res, BankService::error(status, code, msg));
}

std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Reads the whole file `<dir>/<name>`; returns false if it does not exist.
bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Extracts one cookie value from the Cookie header ("a=1; bms_session=abc; b=2").
std::string cookieValue(const httplib::Request& req, const std::string& name) {
    const std::string header = req.get_header_value("Cookie");
    std::size_t pos = 0;
    while (pos < header.size()) {
        while (pos < header.size() && (header[pos] == ' ' || header[pos] == ';')) ++pos;
        const std::size_t eq = header.find('=', pos);
        if (eq == std::string::npos) break;
        std::size_t end = header.find(';', eq);
        if (end == std::string::npos) end = header.size();
        if (header.substr(pos, eq - pos) == name) return header.substr(eq + 1, end - eq - 1);
        pos = end;
    }
    return "";
}

}  // namespace

ApiServer::ApiServer(BankService& service, AuthService& auth, SessionManager& sessions, ApiConfig config)
    : service_(service), auth_(auth), sessions_(sessions), config_(std::move(config)),
      lookupLimiter_(20, 60), server_(std::make_unique<httplib::Server>()) {
    registerRoutes();
}

ApiServer::~ApiServer() = default;

void ApiServer::registerRoutes() {
    httplib::Server& s = *server_;
    BankService& svc = service_;
    AuthService& auth = auth_;
    SessionManager& sessions = sessions_;
    const ApiConfig cfg = config_;
    RateLimiter& lookups = lookupLimiter_;
    s.set_payload_max_length(16 * 1024);  // bank requests are tiny; reject anything large

    // ------------------------------------------------------------ request helpers
    auto clientIp = [cfg](const httplib::Request& req) {
        if (cfg.trustProxy) {
            const std::string xff = req.get_header_value("X-Forwarded-For");
            if (!xff.empty()) {
                const std::size_t comma = xff.find(',');
                std::string first = xff.substr(0, comma);
                const auto b = first.find_first_not_of(' ');
                if (b != std::string::npos && first.size() < 64) return first.substr(b, first.find_last_not_of(' ') - b + 1);
            }
        }
        return req.remote_addr;
    };
    auto isHttps = [cfg](const httplib::Request& req) {
        return cfg.secureCookies || (cfg.trustProxy && lowerCase(req.get_header_value("X-Forwarded-Proto")) == "https");
    };
    auto sessionOf = [&sessions](const httplib::Request& req) -> std::optional<Session> {
        return sessions.validate(cookieValue(req, kCookieName));
    };
    auto setCookie = [isHttps](const httplib::Request& req, httplib::Response& res, const std::string& value, int maxAge) {
        // HttpOnly: JavaScript cannot read it (blocks theft through XSS).
        // SameSite=Strict: the browser never sends it on cross-site requests (blocks CSRF).
        std::string c = std::string(kCookieName) + "=" + value + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + std::to_string(maxAge);
        if (isHttps(req)) c += "; Secure";
        res.set_header("Set-Cookie", c);
    };

    // Same-origin check for requests that change something: if the browser says the request
    // comes from another site, refuse it. (Requests without an Origin header, such as curl or
    // the automated tests, are not browser CSRF attacks and are allowed; they still need the
    // session cookie AND the CSRF token.)
    auto sameOriginOk = [](const httplib::Request& req) {
        const std::string fetchSite = lowerCase(req.get_header_value("Sec-Fetch-Site"));
        if (fetchSite == "cross-site") return false;
        const std::string origin = req.get_header_value("Origin");
        if (origin.empty()) return true;
        const auto scheme = origin.find("://");
        const std::string originHost = lowerCase(scheme == std::string::npos ? origin : origin.substr(scheme + 3));
        return originHost == lowerCase(req.get_header_value("Host"));
    };

    enum class Need { Nobody, Customer, Admin };

    // One wrapper for every API route: applies the front-door checks, then calls `fn`.
    using Handler = std::function<ServiceResult(const httplib::Request&, const json&, const std::optional<Session>&)>;
    auto api = [&](const std::string& method, const std::string& pattern, Need need, Handler fn) {
        auto handle = [=](const httplib::Request& req, httplib::Response& res) {
            const bool writes = method != "GET";
            if (writes && !sameOriginOk(req)) return sendError(res, 403, "FORBIDDEN", "Cross-site requests are not allowed.");

            std::optional<Session> session = sessionOf(req);
            if (need != Need::Nobody) {
                if (!session) return sendError(res, 401, "UNAUTHENTICATED", "Please sign in to continue.");
                const Role wanted = need == Need::Admin ? Role::Admin : Role::Customer;
                if (session->role != wanted)
                    return sendError(res, 403, "FORBIDDEN", "You do not have permission to access this resource.");
            }
            // CSRF token: required on every write made with a session (also logout).
            if (writes && session) {
                if (!security::constantTimeEquals(req.get_header_value("X-CSRF-Token"), session->csrfToken))
                    return sendError(res, 403, "CSRF_FAILED", "Security check failed. Please reload the page and try again.");
            }
            json body = json::object();
            if (writes && !req.body.empty()) {
                if (lowerCase(req.get_header_value("Content-Type")).rfind("application/json", 0) != 0)
                    return sendError(res, 415, "UNSUPPORTED_MEDIA_TYPE", "Requests must be sent as JSON.");
                body = json::parse(req.body, nullptr, /*allow_exceptions=*/false);
                if (body.is_discarded()) return sendError(res, 400, "MALFORMED_JSON", "Request body is not valid JSON");
            }
            send(res, fn(req, body, session));
        };
        if (method == "GET") s.Get(pattern, handle);
        else if (method == "POST") s.Post(pattern, handle);
        else if (method == "PUT") s.Put(pattern, handle);
    };
    auto queryOf = [](const httplib::Request& req) {
        Params p;
        for (const auto& kv : req.params) p[kv.first] = kv.second;
        return p;
    };

    // ------------------------------------------------------------ health + auth
    s.Get("/healthz", [](const httplib::Request&, httplib::Response& res) { res.set_content("ok", "text/plain"); });

    api("POST", "/api/auth/register", Need::Nobody, [&, clientIp](const httplib::Request& req, const json& b, const std::optional<Session>&) {
        return auth.registerCustomer(b, clientIp(req)).result;
    });

    // Login routes need the cookie, so they are registered by hand (not through api()).
    auto loginRoute = [&, clientIp, sameOriginOk, setCookie](bool admin) {
        return [&auth, admin, clientIp, sameOriginOk, setCookie, &sessions](const httplib::Request& req, httplib::Response& res) {
            if (!sameOriginOk(req)) return sendError(res, 403, "FORBIDDEN", "Cross-site requests are not allowed.");
            if (lowerCase(req.get_header_value("Content-Type")).rfind("application/json", 0) != 0)
                return sendError(res, 415, "UNSUPPORTED_MEDIA_TYPE", "Requests must be sent as JSON.");
            const json body = json::parse(req.body, nullptr, false);
            if (body.is_discarded()) return sendError(res, 400, "MALFORMED_JSON", "Request body is not valid JSON");
            // If this browser already had a session, end it: a login always starts a fresh session.
            const std::string old = cookieValue(req, kCookieName);
            if (!old.empty()) sessions.destroy(old);
            AuthResult r = admin ? auth.loginAdmin(body, clientIp(req)) : auth.loginCustomer(body, clientIp(req));
            if (!r.sessionId.empty()) setCookie(req, res, r.sessionId, sessions.absoluteSeconds());
            if (r.result.status == 429 && r.result.body["error"].contains("retryAfterSeconds"))
                res.set_header("Retry-After", std::to_string(r.result.body["error"]["retryAfterSeconds"].get<int>()));
            send(res, r.result);
        };
    };
    s.Post("/api/auth/login", loginRoute(false));
    s.Post("/api/admin/login", loginRoute(true));

    // "Who am I?" - 200 even when signed out, so pages can decide where to go without console errors.
    s.Get("/api/auth/session", [&, sessionOf](const httplib::Request& req, httplib::Response& res) {
        const auto session = sessionOf(req);
        if (!session) {
            ServiceResult r;
            r.body = {{"success", true}, {"message", "Not signed in"}, {"data", {{"authenticated", false}}}};
            return send(res, r);
        }
        send(res, auth.sessionInfo(*session));
    });

    s.Post("/api/auth/logout", [&, sessionOf, setCookie, sameOriginOk](const httplib::Request& req, httplib::Response& res) {
        if (!sameOriginOk(req)) return sendError(res, 403, "FORBIDDEN", "Cross-site requests are not allowed.");
        const auto session = sessionOf(req);
        if (session) {
            if (!security::constantTimeEquals(req.get_header_value("X-CSRF-Token"), session->csrfToken))
                return sendError(res, 403, "CSRF_FAILED", "Security check failed. Please reload the page and try again.");
            sessions.destroy(session->id);   // the session is gone on the SERVER, not just in the browser
        }
        setCookie(req, res, "", 0);
        ServiceResult r;
        r.body = {{"success", true}, {"message", "Signed out"}, {"data", json::object()}};
        send(res, r);
    });

    // ------------------------------------------------------------ customer routes
    // Every handler passes session->subjectId (set by the SERVER at login) to the service.
    // Nothing in the URL or body can select another customer's account.
    auto id = [](const std::optional<Session>& s) { return s->subjectId; };
    api("GET", "/api/me/overview", Need::Customer, [&, id](const httplib::Request&, const json&, const std::optional<Session>& ss) { return svc.customerOverview(id(ss)); });
    api("GET", "/api/me/account", Need::Customer, [&, id](const httplib::Request&, const json&, const std::optional<Session>& ss) { return svc.customerAccount(id(ss)); });
    api("GET", "/api/me/profile", Need::Customer, [&, id](const httplib::Request&, const json&, const std::optional<Session>& ss) { return svc.customerProfile(id(ss)); });
    api("PUT", "/api/me/profile", Need::Customer, [&, id](const httplib::Request&, const json& b, const std::optional<Session>& ss) { return svc.updateProfile(id(ss), b); });
    api("POST", "/api/me/password", Need::Customer, [&](const httplib::Request&, const json& b, const std::optional<Session>& ss) { return auth.changePassword(*ss, b); });
    api("POST", "/api/me/deposit", Need::Customer, [&, id](const httplib::Request&, const json& b, const std::optional<Session>& ss) { return svc.deposit(id(ss), b); });
    api("POST", "/api/me/withdraw", Need::Customer, [&, id](const httplib::Request&, const json& b, const std::optional<Session>& ss) { return svc.withdraw(id(ss), b); });
    api("POST", "/api/me/transfer", Need::Customer, [&, id](const httplib::Request&, const json& b, const std::optional<Session>& ss) { return svc.transfer(id(ss), b); });
    api("GET", "/api/me/transactions", Need::Customer, [&, id, queryOf](const httplib::Request& r, const json&, const std::optional<Session>& ss) { return svc.customerTransactions(id(ss), queryOf(r)); });
    api("GET", "/api/me/recipient", Need::Customer, [&, id, queryOf](const httplib::Request& r, const json&, const std::optional<Session>& ss) {
        // Looking up account numbers one after another would let a stranger scan for accounts, so it is rate limited.
        int wait = 0;
        if (lookups.isBlocked(ss->subjectId, &wait)) return BankService::error(429, "RATE_LIMITED", "Too many look-ups. Please wait a minute.");
        lookups.recordFailure(ss->subjectId);
        return svc.lookupRecipient(id(ss), queryOf(r)["account"]);
    });

    // ------------------------------------------------------------ admin routes
    const StorageInfo storage = cfg.storage;
    api("POST", "/api/admin/password", Need::Admin, [&](const httplib::Request&, const json& b, const std::optional<Session>& ss) { return auth.changePassword(*ss, b); });
    api("GET", "/api/admin/overview", Need::Admin, [&, storage](const httplib::Request&, const json&, const std::optional<Session>&) { return svc.adminOverview(storage); });
    api("GET", "/api/admin/customers", Need::Admin, [&, queryOf](const httplib::Request& r, const json&, const std::optional<Session>&) { return svc.adminCustomers(queryOf(r)); });
    api("GET", R"(/api/admin/customers/([A-Za-z0-9]{1,16}))", Need::Admin, [&](const httplib::Request& r, const json&, const std::optional<Session>&) { return svc.adminCustomerDetail(r.matches[1]); });
    api("GET", "/api/admin/accounts", Need::Admin, [&, queryOf](const httplib::Request& r, const json&, const std::optional<Session>&) { return svc.adminAccounts(queryOf(r)); });
    api("GET", R"(/api/admin/accounts/([0-9]{1,15}))", Need::Admin, [&](const httplib::Request& r, const json&, const std::optional<Session>&) { return svc.adminAccountDetail(r.matches[1]); });
    api("POST", R"(/api/admin/accounts/([0-9]{1,15})/status)", Need::Admin, [&](const httplib::Request& r, const json& b, const std::optional<Session>&) { return svc.setAccountStatus(r.matches[1], b); });
    api("GET", "/api/admin/transactions", Need::Admin, [&, queryOf](const httplib::Request& r, const json&, const std::optional<Session>&) { return svc.adminTransactions(queryOf(r)); });
    api("GET", "/api/admin/reversal", Need::Admin, [&](const httplib::Request&, const json&, const std::optional<Session>&) { return svc.nextReversible(); });
    api("POST", "/api/admin/reversal", Need::Admin, [&](const httplib::Request&, const json& b, const std::optional<Session>&) { return svc.reverse(b); });
    api("GET", "/api/admin/queue", Need::Admin, [&](const httplib::Request&, const json&, const std::optional<Session>&) { return svc.queue(); });
    api("POST", "/api/admin/queue", Need::Admin, [&](const httplib::Request&, const json& b, const std::optional<Session>&) { return svc.issueToken(b); });
    api("POST", "/api/admin/queue/serve", Need::Admin, [&](const httplib::Request&, const json&, const std::optional<Session>&) { return svc.serveNext(); });

    // ------------------------------------------------------------ pages
    // HTML pages live in frontend/pages and are served by explicit routes so that the two
    // dashboards can be protected on the SERVER: no session -> redirect to the login page.
    const std::string pagesDir = cfg.frontendDir + "/pages/";
    auto page = [&s, pagesDir, sessionOf](const std::string& route, const std::string& file, Need need) {
        s.Get(route, [pagesDir, file, need, sessionOf](const httplib::Request& req, httplib::Response& res) {
            const auto session = sessionOf(req);
            if (need == Need::Customer && (!session || session->role != Role::Customer)) return res.set_redirect("/login", 302), void();
            if (need == Need::Admin && (!session || session->role != Role::Admin)) return res.set_redirect("/admin-login", 302), void();
            // Already signed in? Skip the login pages.
            if (need == Need::Nobody && session && (file == "login.html" || file == "register.html"))
                if (session->role == Role::Customer) return res.set_redirect("/dashboard", 302), void();
            if (need == Need::Nobody && session && file == "admin-login.html" && session->role == Role::Admin)
                return res.set_redirect("/admin", 302), void();
            std::string html;
            if (!readFile(pagesDir + file, html)) { res.status = 404; res.set_content("Page not found", "text/plain"); return; }
            res.set_header("Cache-Control", need == Need::Nobody ? "no-cache" : "no-store");
            res.set_content(html, "text/html; charset=utf-8");
        });
    };
    page("/", "index.html", Need::Nobody);
    page("/login", "login.html", Need::Nobody);
    page("/register", "register.html", Need::Nobody);
    page("/admin-login", "admin-login.html", Need::Nobody);
    page("/dashboard", "dashboard.html", Need::Customer);
    page("/admin", "admin.html", Need::Admin);

    // CSS / JS / images. These files contain no secrets (anyone can read front-end code);
    // security never depends on hiding them.
    if (!s.set_mount_point("/static", cfg.frontendDir + "/static"))
        std::fprintf(stderr, "Warning: frontend folder '%s/static' not found\n", cfg.frontendDir.c_str());

    s.set_error_handler([pagesDir](const httplib::Request& req, httplib::Response& res) {
        if (req.path.rfind("/api/", 0) == 0) {
            if (res.body.empty()) {   // library-generated error; keep specific messages built by the services
                const std::string msg = res.status == 404 ? "Unknown API route or method" : "Request could not be processed";
                const int st = res.status == 0 ? 500 : res.status;
                sendError(res, st, res.status == 404 ? "NOT_FOUND" : "BAD_REQUEST", msg);
            }
            return;
        }
        if (res.status == 404) {
            std::string html;
            if (readFile(pagesDir + "404.html", html)) res.set_content(html, "text/html; charset=utf-8");
        }
    });
    s.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
        sendError(res, 500, "INTERNAL_ERROR", "Unexpected server error");
    });
    s.set_post_routing_handler([isHttps](const httplib::Request& req, httplib::Response& res) {
        // Defence-in-depth headers on every response.
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("X-Frame-Options", "DENY");
        res.set_header("Referrer-Policy", "same-origin");
        res.set_header("Permissions-Policy", "geolocation=(), camera=(), microphone=()");
        // The page may only load scripts/styles/images from itself: injected <script> or inline
        // handlers (XSS) simply do not run.
        res.set_header("Content-Security-Policy",
                       "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
                       "font-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; "
                       "form-action 'self'; frame-ancestors 'none'");
        if (isHttps(req)) res.set_header("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
        if (req.path.rfind("/api/", 0) == 0) res.set_header("Cache-Control", "no-store");
        else if (req.path.rfind("/static/", 0) == 0) res.set_header("Cache-Control", "no-cache");
    });
}

bool ApiServer::listen(const std::string& host, int port) { return server_->listen(host, port); }
int ApiServer::bindToFreePort(const std::string& host) { return server_->bind_to_any_port(host); }
void ApiServer::serve() { server_->listen_after_bind(); }
void ApiServer::stop() { server_->stop(); }
