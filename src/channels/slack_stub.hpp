#pragma once
#include "channel.hpp"
#include <iostream>

namespace minidragon {

class SlackChannel : public Channel {
public:
    std::string name() const override { return "slack"; }
    bool enabled() const override { return false; }
    void start(MessageHandler) override {
        std::cerr << "[slack] Channel not implemented (stub)\n";
    }
    void stop() override {}
};

} // namespace minidragon
