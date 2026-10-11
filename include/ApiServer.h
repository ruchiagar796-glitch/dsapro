#pragma once
// ApiServer: maps HTTP routes to services and serves the frontend files.
// It contains NO banking logic. Its job is the "front door":
//   1. read the session cookie and find the server-side session
//   2. check the ROLE the route needs (customer / admin)       -> 401 / 403
//   3. check CSRF protection on every state-changing request   -> 403
//   4. check Origin / Content-Type, parse JSON, call the service, send the answer
// The HTTP library is hidden behind a forward declaration so only ApiServer.cpp has to compile
// the large httplib header.
#include <memory>
#include <string>

#include "AuthService.h"
#include "BankService.h"
#include "RateLimiter.h"
#include "SessionManager.h"

namespace httplib { class Server; }

struct ApiConfig {
    std::string frontendDir = "frontend";
    bool trustProxy = false;           // read the client IP / protocol from X-Forwarded-* (behind Render's proxy)
    bool secureCookies = false;        // always add the Secure flag (HTTPS-only cookies)
    StorageInfo storage;               // shown on the admin dashboard
};

class ApiServer {
public:
    ApiServer(BankService& service, AuthService& auth, SessionManager& sessions, ApiConfig config);
    ~ApiServer();
    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    // Blocks until stop() is called. Returns false if the port could not be bound.
    bool listen(const std::string& host, int port);
    // For tests: bind to a free port first (returns it, or -1), then serve() blocks.
    int bindToFreePort(const std::string& host);
    void serve();
    void stop();

private:
    void registerRoutes();
    BankService& service_;
    AuthService& auth_;
    SessionManager& sessions_;
    ApiConfig config_;
    RateLimiter lookupLimiter_;        // recipient look-ups: 20 per minute per customer
    std::unique_ptr<httplib::Server> server_;
};
