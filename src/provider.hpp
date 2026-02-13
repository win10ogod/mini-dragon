#pragma once
#include "config.hpp"
#include "message.hpp"
#include <httplib.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace minidragon {

struct ProviderResponse {
    std::string content;
    std::vector<ToolCall> tool_calls;
    bool has_tool_calls() const { return !tool_calls.empty(); }
};

using StreamCallback = std::function<void(const std::string& token, bool done)>;

class Provider {
public:
    explicit Provider(const ProviderConfig& cfg) : config_(cfg) {}

    ProviderResponse chat(const std::vector<Message>& messages,
                          const nlohmann::json& tools_spec,
                          const std::string& model,
                          int max_tokens, double temperature);

    void chat_stream(const std::vector<Message>& messages,
                     const nlohmann::json& tools_spec,
                     const std::string& model,
                     int max_tokens, double temperature,
                     StreamCallback on_token);

private:
    ProviderConfig config_;
};

} // namespace minidragon
