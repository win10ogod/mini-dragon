#include "config.hpp"
#include <fstream>
#include <iostream>

namespace minidragon {

ProviderConfig Config::resolve_provider() const {
    // 1. If explicit provider name is set, use it
    if (!provider.empty()) {
        auto it = providers.find(provider);
        if (it != providers.end()) return it->second;
        std::cerr << "[config] Warning: provider '" << provider << "' not found, falling back\n";
    }
    // 2. Try "default" key
    if (providers.count("default")) return providers.at("default");
    // 3. Try openai_compat (backward compat)
    if (providers.count("openai_compat")) return providers.at("openai_compat");
    // 4. Use the first configured provider
    if (!providers.empty()) return providers.begin()->second;
    // 5. Default fallback
    return ProviderConfig{"", "http://127.0.0.1:8000/v1", ""};
}

Config Config::make_default() {
    Config c;
    c.providers["default"] = ProviderConfig{
        "", "http://127.0.0.1:8000/v1", ""
    };
    c.tools = {
        {"exec", {{"allowlist", nlohmann::json::array({"git", "ls", "cat", "dir", "type"})}}}
    };
    return c;
}

nlohmann::json Config::to_json() const {
    nlohmann::json j;

    // Top-level flat config
    j["model"] = model;
    j["workspace"] = workspace;
    if (!provider.empty()) j["provider"] = provider;
    j["max_tokens"] = max_tokens;
    j["temperature"] = temperature;
    j["max_iterations"] = max_iterations;
    j["context_window"] = context_window;
    j["max_tool_output"] = max_tool_output;

    // Providers
    for (auto& [k, v] : providers) {
        j["providers"][k] = {{"api_base", v.api_base}};
        if (!v.api_key.empty()) j["providers"][k]["api_key"] = v.api_key;
        if (!v.default_model.empty()) j["providers"][k]["default_model"] = v.default_model;
    }

    // Channels
    {
        auto& ch = j["channels"];

        auto& hc = ch["http"];
        hc["enabled"] = http_channel.enabled;
        if (!http_channel.api_key.empty()) hc["api_key"] = http_channel.api_key;
        if (http_channel.rate_limit_rpm > 0) hc["rate_limit_rpm"] = http_channel.rate_limit_rpm;

        auto& tg = ch["telegram"];
        tg["enabled"] = telegram.enabled;
        if (!telegram.token.empty()) tg["token"] = telegram.token;
        if (!telegram.allow_from.empty()) tg["allow_from"] = telegram.allow_from;
        if (telegram.poll_timeout != 30) tg["poll_timeout"] = telegram.poll_timeout;

        auto& dc = ch["discord"];
        dc["enabled"] = discord.enabled;
        if (!discord.token.empty()) dc["token"] = discord.token;
        if (!discord.allow_from.empty()) dc["allow_from"] = discord.allow_from;

        auto& sc = ch["slack"];
        sc["enabled"] = slack.enabled;
        if (!slack.app_token.empty()) sc["app_token"] = slack.app_token;
        if (!slack.bot_token.empty()) sc["bot_token"] = slack.bot_token;
        if (!slack.allow_from.empty()) sc["allow_from"] = slack.allow_from;
    }

    // Tools
    if (!tools.is_null()) {
        j["tools"] = tools;
    }

    // MCP servers
    if (!mcp_servers.empty()) {
        for (auto& [name, srv] : mcp_servers) {
            nlohmann::json s;
            if (!srv.command.empty()) {
                s["command"] = srv.command;
                if (!srv.args.empty()) s["args"] = srv.args;
                if (!srv.env.empty()) s["env"] = srv.env;
            }
            if (!srv.url.empty()) {
                s["url"] = srv.url;
                if (!srv.headers.empty()) s["headers"] = srv.headers;
            }
            j["mcp_servers"][name] = s;
        }
    }

    return j;
}

static std::vector<std::string> parse_string_array(const nlohmann::json& arr) {
    std::vector<std::string> result;
    if (arr.is_array()) {
        for (auto& item : arr) {
            if (item.is_string()) result.push_back(item.get<std::string>());
        }
    }
    return result;
}

Config Config::from_json(const nlohmann::json& j) {
    Config c;

    // ── Backward compatibility: detect old agents.defaults format ──
    if (j.contains("agents") && j["agents"].contains("defaults")) {
        auto& ad = j["agents"]["defaults"];
        c.workspace = ad.value("workspace", c.workspace);
        c.model = ad.value("model", c.model);
        c.provider = ad.value("provider", c.provider);
        c.max_tokens = ad.value("max_tokens", c.max_tokens);
        c.temperature = ad.value("temperature", c.temperature);
        c.max_iterations = ad.value("max_tool_iterations", c.max_iterations);
    }

    // ── New flat format (overrides old if both present) ──
    c.model = j.value("model", c.model);
    c.workspace = j.value("workspace", c.workspace);
    c.provider = j.value("provider", c.provider);
    c.max_tokens = j.value("max_tokens", c.max_tokens);
    c.temperature = j.value("temperature", c.temperature);
    c.max_iterations = j.value("max_iterations", c.max_iterations);
    c.context_window = j.value("context_window", c.context_window);
    c.max_tool_output = j.value("max_tool_output", c.max_tool_output);

    // Providers
    if (j.contains("providers")) {
        for (auto& [k, v] : j["providers"].items()) {
            c.providers[k] = ProviderConfig{
                v.value("api_key", ""),
                v.value("api_base", ""),
                v.value("default_model", "")
            };
        }
    }

    // Channels
    if (j.contains("channels")) {
        auto& ch = j["channels"];

        // HTTP
        if (ch.contains("http")) {
            auto& hc = ch["http"];
            c.http_channel.enabled = hc.value("enabled", true);
            c.http_channel.api_key = hc.value("api_key", "");
            c.http_channel.rate_limit_rpm = hc.value("rate_limit_rpm", 0);
        }

        // Telegram
        if (ch.contains("telegram")) {
            auto& tg = ch["telegram"];
            c.telegram.enabled = tg.value("enabled", false);
            c.telegram.token = tg.value("token", "");
            c.telegram.poll_timeout = tg.value("poll_timeout", 30);
            if (tg.contains("allow_from")) c.telegram.allow_from = parse_string_array(tg["allow_from"]);
        }

        // Discord
        if (ch.contains("discord")) {
            auto& dc = ch["discord"];
            c.discord.enabled = dc.value("enabled", false);
            c.discord.token = dc.value("token", "");
            if (dc.contains("allow_from")) c.discord.allow_from = parse_string_array(dc["allow_from"]);
        }

        // Slack
        if (ch.contains("slack")) {
            auto& sc = ch["slack"];
            c.slack.enabled = sc.value("enabled", false);
            c.slack.app_token = sc.value("app_token", "");
            c.slack.bot_token = sc.value("bot_token", "");
            if (sc.contains("allow_from")) c.slack.allow_from = parse_string_array(sc["allow_from"]);
        }
    }

    // Tools
    if (j.contains("tools")) {
        c.tools = j["tools"];
    }
    // Backward compat: old "tools.spawn" format -> new "tools.exec"
    if (c.tools.contains("spawn") && !c.tools.contains("exec")) {
        c.tools["exec"] = c.tools["spawn"];
        c.tools.erase("spawn");
    }

    // MCP servers
    if (j.contains("mcp_servers")) {
        for (auto& [name, srv] : j["mcp_servers"].items()) {
            McpServerConfig mcp;
            mcp.command = srv.value("command", "");
            if (srv.contains("args")) mcp.args = parse_string_array(srv["args"]);
            if (srv.contains("env") && srv["env"].is_object()) {
                for (auto& [ek, ev] : srv["env"].items()) {
                    if (ev.is_string()) mcp.env[ek] = ev.get<std::string>();
                }
            }
            mcp.url = srv.value("url", "");
            if (srv.contains("headers") && srv["headers"].is_object()) {
                for (auto& [hk, hv] : srv["headers"].items()) {
                    if (hv.is_string()) mcp.headers[hk] = hv.get<std::string>();
                }
            }
            c.mcp_servers[name] = std::move(mcp);
        }
    }

    return c;
}

Config Config::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "[warn] Config not found at " << path << ", using defaults\n";
        return make_default();
    }
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        return from_json(j);
    } catch (const std::exception& e) {
        std::cerr << "[warn] Failed to parse config: " << e.what() << ", using defaults\n";
        return make_default();
    }
}

void Config::save(const std::string& path) const {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << to_json().dump(2) << std::endl;
}

} // namespace minidragon
