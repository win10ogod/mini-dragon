#pragma once
#include "utils.hpp"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>

namespace minidragon {

class HeartbeatService {
public:
    HeartbeatService(const std::string& workspace,
                     std::function<std::string(const std::string&)> on_heartbeat,
                     int interval_s = 1800, bool enabled = true)
        : workspace_(workspace)
        , on_heartbeat_(std::move(on_heartbeat))
        , interval_s_(interval_s)
        , enabled_(enabled)
    {}

    void start() {
        if (!enabled_) return;
        running_ = true;
        thread_ = std::thread([this]() { run_loop(); });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    std::string workspace_;
    std::function<std::string(const std::string&)> on_heartbeat_;
    int interval_s_;
    bool enabled_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    void run_loop() {
        std::cerr << "[heartbeat] Started (interval=" << interval_s_ << "s)\n";
        while (running_) {
            // Sleep in 1s increments so we can respond to stop quickly
            for (int i = 0; i < interval_s_ && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!running_) break;

            try {
                std::string hb_path = workspace_ + "/HEARTBEAT.md";
                std::string content = read_file(hb_path);

                // Trim whitespace
                while (!content.empty() && (content.back() == '\n' || content.back() == ' '))
                    content.pop_back();

                if (!content.empty()) {
                    std::cerr << "[heartbeat] Firing: " << content.substr(0, 80) << "\n";
                    std::string reply = on_heartbeat_("[heartbeat] " + content);
                    std::cerr << "[heartbeat] Reply: " << reply.substr(0, 200) << "\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "[heartbeat] Error: " << e.what() << "\n";
            }
        }
        std::cerr << "[heartbeat] Stopped\n";
    }
};

} // namespace minidragon
