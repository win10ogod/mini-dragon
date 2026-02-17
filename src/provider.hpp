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

struct EmbeddingResponse {
    std::vector<std::vector<float>> embeddings;
};

using StreamCallback = std::function<void(const std::string& token, bool done)>;

class Provider {
public:
    explicit Provider(const ProviderConfig& cfg);

    ProviderResponse chat(const std::vector<Message>& messages,
                          const nlohmann::json& tools_spec,
                          const std::string& model,
                          int max_tokens, double temperature);

    void chat_stream(const std::vector<Message>& messages,
                     const nlohmann::json& tools_spec,
                     const std::string& model,
                     int max_tokens, double temperature,
                     StreamCallback on_token);

    EmbeddingResponse embed(const std::vector<std::string>& texts,
                            const std::string& model = "text-embedding-3-small");

    const ProviderConfig& config() const { return config_; }

private:
    ProviderConfig config_;
    // Cached URL components (parsed once in constructor)
    std::string scheme_;
    std::string host_;
    int port_;
    std::string path_prefix_;
    std::string base_url_;  // scheme://host:port
};

} // namespace minidragon
