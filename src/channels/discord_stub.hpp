#pragma once
#include "channel.hpp"
#include <iostream>

namespace minidragon {

class DiscordChannel : public Channel {
public:
    std::string name() const override { return "discord"; }
    bool enabled() const override { return false; }
    void start(MessageHandler) override {
        std::cerr << "[discord] Channel not implemented (stub)\n";
    }
    void stop() override {}
};

} // namespace minidragon
