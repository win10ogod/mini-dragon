#pragma once
#include <string>
#include <functional>

namespace minidragon {

struct InboundMessage {
    std::string channel;
    std::string user;
    std::string text;
};

using MessageHandler = std::function<std::string(const InboundMessage&)>;

class Channel {
public:
    virtual ~Channel() = default;
    virtual std::string name() const = 0;
    virtual bool enabled() const = 0;
    virtual void start(MessageHandler handler) = 0;
    virtual void stop() = 0;
};

} // namespace minidragon
