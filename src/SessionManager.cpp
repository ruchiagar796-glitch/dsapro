#include "SessionManager.h"

#include <utility>
#include <vector>

#include "Security.h"

SessionManager::SessionManager(int idleSeconds, int absoluteSeconds, Clock clock)
    : idle_(idleSeconds), absolute_(absoluteSeconds), absoluteSeconds_(absoluteSeconds), clock_(std::move(clock)) {}

std::string SessionManager::create(Role role, const std::string& subjectId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::time_t now = clock_();
    purgeExpiredLocked(now);
    Session s;
    s.id = security::randomToken(32);          // 256 bits of randomness: cannot be guessed
    s.csrfToken = security::randomToken(24);
    s.role = role;
    s.subjectId = subjectId;
    s.createdAt = s.lastSeen = now;
    const std::string id = s.id;
    sessions_[id] = std::move(s);
    return id;
}

std::optional<Session> SessionManager::validate(const std::string& id) {
    if (id.size() != 64) return std::nullopt;  // cheap rejection of garbage cookies
    std::lock_guard<std::mutex> lock(mutex_);
    const std::time_t now = clock_();
    purgeExpiredLocked(now);
    const auto it = sessions_.find(id);
    if (it == sessions_.end()) return std::nullopt;
    if (now - it->second.lastSeen > idle_ || now - it->second.createdAt > absolute_) {
        sessions_.erase(it);
        return std::nullopt;
    }
    it->second.lastSeen = now;
    return it->second;
}

void SessionManager::destroy(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(id);
}

void SessionManager::destroyAllFor(Role role, const std::string& subjectId, const std::string& keepId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.role == role && it->second.subjectId == subjectId && it->first != keepId) it = sessions_.erase(it);
        else ++it;
    }
}

std::size_t SessionManager::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

void SessionManager::purgeExpiredLocked(std::time_t now) {
    if (now - lastPurge_ < 60) return;  // at most once a minute
    lastPurge_ = now;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now - it->second.lastSeen > idle_ || now - it->second.createdAt > absolute_) it = sessions_.erase(it);
        else ++it;
    }
}
