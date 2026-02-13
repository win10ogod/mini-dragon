#pragma once
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "utils.hpp"

namespace minidragon {

struct ProviderConfig {
    std::string api_key;
    std::string api_base;
    std::string default_model;  // Optional: default model for this provider
};

struct TelegramChannelConfig {
    bool enabled = false;
    std::string token;
    std::vector<std::string> allow_from;  // Allowed user IDs (empty = all)
    int poll_timeout = 30;
};

struct HTTPChannelConfig {
    bool enabled = true;
    std::string api_key;       // Optional Bearer token auth
    int rate_limit_rpm = 0;    // 0 = unlimited
};

struct DiscordChannelConfig {
    bool enabled = false;
    std::string token;
    std::vector<std::string> allow_from;
};

struct SlackChannelConfig {
    bool enabled = false;
    std::string app_token;     // Socket Mode app token (xapp-...)
    std::string bot_token;     // Bot token (xoxb-...)
    std::vector<std::string> allow_from;
};

struct McpServerConfig {
    std::string command;        // For stdio: executable path
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    std::string url;            // For http: server URL
    std::map<std::string, std::string> headers;
    // type inferred: if command non-empty -> stdio, if url non-empty -> http
};

struct Config {
    // Top-level (flattened from old agents.defaults)
    std::string model = "gpt-4.1-mini";
    std::string workspace = "~/.minidragon/workspace";
    std::string provider;  // which provider key to use (empty = auto-detect)
    int max_tokens = 2048;
    double temperature = 0.7;
    int max_iterations = 20;
    int context_window = 50;   // sliding window size for session history
    int max_tool_output = 4096; // max chars per tool output before truncation

    std::map<std::string, ProviderConfig> providers;

    // Channel configs
    TelegramChannelConfig telegram;
    HTTPChannelConfig http_channel;
    DiscordChannelConfig discord;
    SlackChannelConfig slack;

    // Tool config
    nlohmann::json tools;   // flexible tool-specific config (e.g. exec allowlist)

    // MCP servers
    std::map<std::string, McpServerConfig> mcp_servers;

    // Derived helpers
    std::string workspace_path() const {
        return expand_path(workspace);
    }

    ProviderConfig resolve_provider() const;

    static Config make_default();
    static Config load(const std::string& path);
    void save(const std::string& path) const;
    nlohmann::json to_json() const;
    static Config from_json(const nlohmann::json& j);
};

} // namespace minidragon
