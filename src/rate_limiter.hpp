#pragma once
#include <deque>
#include <mutex>
#include <chrono>

namespace minidragon {

class RateLimiter {
public:
    explicit RateLimiter(int requests_per_minute)
        : rpm_(requests_per_minute) {}

    bool allow() {
        if (rpm_ <= 0) return true;  // 0 = unlimited

        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - std::chrono::seconds(60);

        // Prune old timestamps
        while (!timestamps_.empty() && timestamps_.front() < cutoff) {
            timestamps_.pop_front();
        }

        if (static_cast<int>(timestamps_.size()) >= rpm_) {
            return false;
        }

        timestamps_.push_back(now);
        return true;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        timestamps_.clear();
    }

private:
    int rpm_;
    std::mutex mutex_;
    std::deque<std::chrono::steady_clock::time_point> timestamps_;
};

} // namespace minidragon
