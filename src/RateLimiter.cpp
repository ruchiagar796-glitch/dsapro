#include "RateLimiter.h"

#include <utility>

RateLimiter::RateLimiter(std::size_t maxEvents, int windowSeconds, Clock clock)
    : max_(maxEvents), window_(windowSeconds), clock_(std::move(clock)) {}

void RateLimiter::pruneLocked(std::deque<std::time_t>& q, std::time_t now) const {
    while (!q.empty() && now - q.front() >= window_) q.pop_front();
}

bool RateLimiter::isBlocked(const std::string& key, int* retryAfter) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::time_t now = clock_();
    const auto it = events_.find(key);
    if (it == events_.end()) return false;
    pruneLocked(it->second, now);
    if (it->second.size() < max_) return false;
    if (retryAfter) {
        const long long wait = static_cast<long long>(window_) - (now - it->second.front());
        *retryAfter = wait < 1 ? 1 : static_cast<int>(wait);
    }
    return true;
}

void RateLimiter::recordFailure(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::time_t now = clock_();
    auto& q = events_[key];
    pruneLocked(q, now);
    q.push_back(now);
    if (q.size() > max_ * 2) q.pop_front();  // bounded memory per key
    // Occasionally drop keys whose events have all expired so the map cannot grow forever.
    if (now - lastSweep_ > 300) {
        lastSweep_ = now;
        for (auto i = events_.begin(); i != events_.end();) {
            pruneLocked(i->second, now);
            if (i->second.empty()) i = events_.erase(i);
            else ++i;
        }
    }
}

void RateLimiter::reset(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.erase(key);
}
