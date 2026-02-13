#pragma once
#include "channel.hpp"
#include "../config.hpp"
#include "../https_client.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <iostream>

namespace minidragon {

class TelegramChannel : public Channel {
public:
    explicit TelegramChannel(const TelegramChannelConfig& cfg)
        : config_(cfg) {}

    std::string name() const override { return "telegram"; }
    bool enabled() const override { return config_.enabled && !config_.token.empty(); }

    void start(MessageHandler handler) override {
        if (!enabled()) return;
        handler_ = std::move(handler);
        running_ = true;
        poll_thread_ = std::thread([this]() { poll_loop(); });
    }

    void stop() override {
        running_ = false;
        if (poll_thread_.joinable()) poll_thread_.join();
    }

private:
    TelegramChannelConfig config_;
    MessageHandler handler_;
    std::atomic<bool> running_{false};
    std::thread poll_thread_;
    int64_t last_update_id_ = 0;

    nlohmann::json api_call(const std::string& method, const nlohmann::json& params = {}) {
        std::string path = "/bot" + config_.token + "/" + method;
        std::string body = params.dump();

        auto resp = https_post("api.telegram.org", path, body,
                               "application/json", config_.poll_timeout + 10);

        if (resp.status == 0) {
            std::cerr << "[telegram] API call failed: " << method
                      << " (" << resp.body << ")\n";
            return {};
        }
        if (!resp.ok()) {
            std::cerr << "[telegram] API call failed: " << method
                      << " status=" << resp.status << " body=" << resp.body << "\n";
            return {};
        }

        try {
            auto j = nlohmann::json::parse(resp.body);
            if (j.value("ok", false)) {
                return j["result"];
            }
            std::cerr << "[telegram] API error: " << j.value("description", "unknown") << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[telegram] JSON parse error: " << e.what() << "\n";
        }
        return {};
    }

    void poll_loop() {
        std::cerr << "[telegram] Polling started\n";

        while (running_) {
            try {
                nlohmann::json params;
                params["timeout"] = config_.poll_timeout;
                if (last_update_id_ > 0) {
                    params["offset"] = last_update_id_ + 1;
                }

                auto updates = api_call("getUpdates", params);
                if (updates.is_null() || !updates.is_array()) {
                    if (running_) {
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                    }
                    continue;
                }

                for (auto& update : updates) {
                    int64_t uid = update.value("update_id", (int64_t)0);
                    if (uid > last_update_id_) {
                        last_update_id_ = uid;
                    }
                    handle_update(update);
                }
            } catch (const std::exception& e) {
                std::cerr << "[telegram] Poll error: " << e.what() << "\n";
                if (running_) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
        }

        std::cerr << "[telegram] Polling stopped\n";
    }

    void handle_update(const nlohmann::json& update) {
        if (!update.contains("message")) return;

        auto& msg = update["message"];
        if (!msg.contains("text")) return;

        std::string text = msg["text"].get<std::string>();
        int64_t chat_id = msg["chat"].value("id", (int64_t)0);
        std::string user_id = std::to_string(msg["from"].value("id", (int64_t)0));
        std::string username = msg["from"].value("username", user_id);

        // Check allow_from if set
        if (!config_.allow_from.empty()) {
            bool allowed = false;
            for (auto& a : config_.allow_from) {
                if (a == user_id || a == username) {
                    allowed = true;
                    break;
                }
            }
            if (!allowed) {
                std::cerr << "[telegram] Blocked message from user " << user_id
                          << " (" << username << ")\n";
                return;
            }
        }

        // Handle /start command
        if (text == "/start") {
            send_message(chat_id, "Hello! I'm Mini Dragon, an AI assistant. Send me a message and I'll respond.");
            return;
        }

        std::cerr << "[telegram] Message from " << username << ": " << text.substr(0, 80) << "\n";

        // Call agent
        InboundMessage inbound;
        inbound.channel = "telegram";
        inbound.user = username;
        inbound.text = text;

        std::string reply;
        try {
            reply = handler_(inbound);
        } catch (const std::exception& e) {
            reply = std::string("[error] ") + e.what();
        }

        if (reply.empty()) {
            reply = "(empty response)";
        }

        send_message(chat_id, reply);
    }

    void send_message(int64_t chat_id, const std::string& text) {
        const size_t MAX_LEN = 4000;

        if (text.size() <= MAX_LEN) {
            nlohmann::json params;
            params["chat_id"] = chat_id;
            params["text"] = text;
            api_call("sendMessage", params);
        } else {
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = std::min(pos + MAX_LEN, text.size());
                if (end < text.size()) {
                    size_t nl = text.rfind('\n', end);
                    if (nl != std::string::npos && nl > pos) {
                        end = nl + 1;
                    }
                }
                nlohmann::json params;
                params["chat_id"] = chat_id;
                params["text"] = text.substr(pos, end - pos);
                api_call("sendMessage", params);
                pos = end;
            }
        }
    }
};

} // namespace minidragon
