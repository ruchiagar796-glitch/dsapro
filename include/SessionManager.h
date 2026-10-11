#pragma once
// SessionManager: server-side login sessions.
//
// After a successful login the browser receives only a long RANDOM session id (in an HttpOnly
// cookie). Everything that matters - who the user is and whether they are a customer or an
// admin - is stored HERE on the server, so the browser cannot forge or change its role,
// customer id or account number.
//
// DSA note: std::unordered_map<sessionId, Session> -> O(1) average lookup per request.
// Sessions live in memory only: restarting the server signs everybody out (safe default).
#include <ctime>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

enum class Role { Customer, Admin };

struct Session {
    std::string id;
    Role role = Role::Customer;
    std::string subjectId;   // customer id ("CUS000001") or admin id ("ADM0001")
    std::string csrfToken;   // must be echoed in the X-CSRF-Token header on every write request
    std::time_t createdAt = 0;
    std::time_t lastSeen = 0;
};

class SessionManager {
public:
    using Clock = std::function<std::time_t()>;
    explicit SessionManager(int idleSeconds = 30 * 60, int absoluteSeconds = 8 * 60 * 60,
                            Clock clock = [] { return std::time(nullptr); });

    // Creates a session and returns its (secret) id.
    std::string create(Role role, const std::string& subjectId);
    // Returns the session if the id is known and not expired (and refreshes its idle timer).
    std::optional<Session> validate(const std::string& id);
    void destroy(const std::string& id);
    // Signs a person out everywhere (used after a password change). keepId stays signed in.
    void destroyAllFor(Role role, const std::string& subjectId, const std::string& keepId = "");
    std::size_t size() const;

    int absoluteSeconds() const noexcept { return absoluteSeconds_; }

private:
    void purgeExpiredLocked(std::time_t now);
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Session> sessions_;
    int idle_, absolute_;
    int absoluteSeconds_;
    Clock clock_;
    std::time_t lastPurge_ = 0;
};
