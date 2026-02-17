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

struct FallbackConfig {
    bool enabled = false;
    std::vector<std::string> provider_order;  // keys into providers map
    int rate_limit_cooldown = 60;     // seconds
    int billing_cooldown = 18000;     // 5 hours
    int auth_cooldown = 3600;         // 1 hour
    int timeout_cooldown = 30;        // seconds
};

struct EmbeddingConfig {
    bool enabled = false;
    std::string provider;      // key into providers map (separate from main)
    std::string model = "text-embedding-3-small";
    int dimensions = 1536;
};

struct HookConfig {
    std::string type;     // HookType as string
    std::string command;  // shell command to execute
    int priority = 0;
};

struct Config {
    // Top-level (flattened from old agents.defaults)
    std::string model = "gpt-4.1-mini";
    std::string workspace = "~/.minidragon/workspace";
    std::string provider;  // which provider key to use (empty = auto-detect)
    int max_tokens = 2048;
    double temperature = 0.7;
    int max_iterations = 20;
    int context_window = 50;   // sliding window size for session history (message count)
    int context_tokens = 128000; // model context window in tokens (for budget math)
    int max_tool_output = 0;   // max chars per tool output (0 = auto: 30% of context)

    // Context pruning settings (openclaw-compatible)
    double prune_soft_ratio = 0.3;   // trigger soft trim at this context usage
    double prune_hard_ratio = 0.5;   // trigger hard clear at this context usage
    int prune_head_chars = 1500;     // keep first N chars of pruned tool result
    int prune_tail_chars = 1500;     // keep last N chars of pruned tool result
    int prune_keep_recent = 3;       // protect last N assistant messages from pruning
    bool auto_compact = true;        // auto-compaction when context is near limit
    int compact_reserve_tokens = 20000; // reserve tokens for compaction prompt
    int max_retries = 3;             // provider error retries

    std::map<std::string, ProviderConfig> providers;

    // Provider fallback chain
    FallbackConfig fallback;

    // Embedding config (for hybrid memory search)
    EmbeddingConfig embedding;

    // Hook configs
    std::vector<HookConfig> hooks;

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
