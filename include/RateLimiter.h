#pragma once
// RateLimiter: "sliding window" counter used against brute-force password guessing and
// automated sign-ups. For each key (for example "ip|email") it remembers the times of recent
// failures; once `maxEvents` failures fall inside the last `windowSeconds`, the key is blocked
// until the oldest one leaves the window.
//
// DSA note: unordered_map<key, deque<time>> - the deque is a queue (FIFO): new events are
// pushed at the back, expired ones are popped from the front. Each event is added and removed
// once, so the amortised cost per operation is O(1).
#include <ctime>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

class RateLimiter {
public:
    using Clock = std::function<std::time_t()>;
    RateLimiter(std::size_t maxEvents, int windowSeconds, Clock clock = [] { return std::time(nullptr); });

    // True if the key is currently blocked; sets retryAfterSeconds (>=1) when it is.
    bool isBlocked(const std::string& key, int* retryAfterSeconds = nullptr);
    void recordFailure(const std::string& key);
    void reset(const std::string& key);  // e.g. after a successful login

private:
    void pruneLocked(std::deque<std::time_t>& q, std::time_t now) const;
    std::mutex mutex_;
    std::unordered_map<std::string, std::deque<std::time_t>> events_;
    std::size_t max_;
    int window_;
    Clock clock_;
    std::time_t lastSweep_ = 0;
};
