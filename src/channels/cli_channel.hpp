#pragma once
#include "channel.hpp"

namespace minidragon {

class CLIChannel : public Channel {
public:
    std::string name() const override { return "cli"; }
    bool enabled() const override { return true; }
    void start(MessageHandler handler) override {
        handler_ = std::move(handler);
    }
    void stop() override {}

    std::string handle_line(const std::string& user, const std::string& text) {
        if (handler_) {
            return handler_(InboundMessage{"cli", user, text});
        }
        return "[error] No handler";
    }

private:
    MessageHandler handler_;
};

} // namespace minidragon
