#pragma once
#include "cron_store.hpp"
#include "utils.hpp"
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>

namespace minidragon {

class CronRunner {
public:
    CronRunner(CronStore& store, std::function<void(const CronJob&)> on_due)
        : store_(store), on_due_(std::move(on_due)) {}

    ~CronRunner() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread(&CronRunner::run_loop, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    CronStore& store_;
    std::function<void(const CronJob&)> on_due_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    void run_loop() {
        while (running_) {
            try {
                auto jobs = store_.due_jobs();
                for (auto& j : jobs) {
                    on_due_(j);
                    store_.update_last_run(j.id, epoch_now());
                }
            } catch (...) {}
            for (int i = 0; i < 100 && running_; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
};

} // namespace minidragon
