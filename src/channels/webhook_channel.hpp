#pragma once
#include "channel.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <iostream>

namespace minidragon {

// Webhook channel: receives POST at a configurable path,
// processes via agent, and optionally sends response to a callback URL.
// Can be added as routes to the main HTTP server or run standalone.
class WebhookChannel : public Channel {
public:
    WebhookChannel(const std::string& host, int port,
                   const std::string& path, const std::string& callback_url,
                   bool enabled_flag)
        : host_(host), port_(port), path_(path)
        , callback_url_(callback_url), enabled_(enabled_flag) {}

    std::string name() const override { return "webhook"; }
    bool enabled() const override { return enabled_; }

    void start(MessageHandler handler) override {
        if (!enabled_) return;
        handler_ = std::move(handler);

        server_.Post(path_, [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto j = nlohmann::json::parse(req.body);
                InboundMessage msg;
                msg.channel = "webhook";
                msg.user = j.value("user", "webhook");
                msg.text = j.value("text", "");

                if (msg.text.empty() && j.contains("message")) {
                    msg.text = j["message"].get<std::string>();
                }

                std::string reply = handler_(msg);

                // Send response back
                nlohmann::json resp;
                resp["reply"] = reply;
                res.set_content(resp.dump(), "application/json");

                // If callback URL is set, also POST there
                if (!callback_url_.empty()) {
                    post_callback(reply, msg.user);
                }
            } catch (const std::exception& e) {
                nlohmann::json err;
                err["error"] = e.what();
                res.status = 400;
                res.set_content(err.dump(), "application/json");
            }
        });

        thread_ = std::thread([this]() {
            std::cerr << "[webhook] Listening on " << host_ << ":" << port_ << path_ << "\n";
            server_.listen(host_, port_);
        });
    }

    void stop() override {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }

private:
    std::string host_;
    int port_;
    std::string path_;
    std::string callback_url_;
    bool enabled_;
    MessageHandler handler_;
    httplib::Server server_;
    std::thread thread_;

    void post_callback(const std::string& reply, const std::string& user) {
        try {
            // Parse callback URL
            std::string url = callback_url_;
            std::string cb_host;
            int cb_port = 80;
            std::string cb_path = "/";

            size_t pos = 0;
            if (url.substr(0, 7) == "http://") pos = 7;
            else if (url.substr(0, 8) == "https://") pos = 8;

            size_t slash = url.find('/', pos);
            std::string host_port = (slash != std::string::npos) ? url.substr(pos, slash - pos) : url.substr(pos);
            if (slash != std::string::npos) cb_path = url.substr(slash);

            size_t colon = host_port.find(':');
            if (colon != std::string::npos) {
                cb_host = host_port.substr(0, colon);
                cb_port = std::stoi(host_port.substr(colon + 1));
            } else {
                cb_host = host_port;
            }

            httplib::Client cli(cb_host, cb_port);
            cli.set_connection_timeout(10);
            cli.set_read_timeout(10);

            nlohmann::json body;
            body["reply"] = reply;
            body["user"] = user;
            cli.Post(cb_path, body.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[webhook] Callback failed: " << e.what() << "\n";
        }
    }
};

} // namespace minidragon
