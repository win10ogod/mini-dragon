#include "status.hpp"
#include <iostream>

namespace minidragon {

int cmd_status() {
    std::string cfg_path = default_config_path();
    Config cfg = Config::load(cfg_path);
    std::string ws = cfg.workspace_path();

    std::cout << "=== minidragon status ===\n";
    std::cout << "Config path  : " << cfg_path << "\n";
    std::cout << "Workspace    : " << ws << "\n";
    std::cout << "Model        : " << cfg.model << "\n";
    std::cout << "Max tokens   : " << cfg.max_tokens << "\n";
    std::cout << "Context win  : " << cfg.context_window << "\n";

    // Active provider
    std::string active = cfg.provider;
    if (active.empty()) {
        if (cfg.providers.count("default")) active = "default";
        else if (cfg.providers.count("openai_compat")) active = "openai_compat";
        else active = cfg.providers.empty() ? "(none)" : cfg.providers.begin()->first;
    }
    std::cout << "Provider     : " << active << "\n";

    // All providers
    std::cout << "Providers    : ";
    bool first = true;
    for (auto& [name, p] : cfg.providers) {
        if (!first) std::cout << ", ";
        std::cout << name;
        if (!p.api_base.empty()) std::cout << " (" << p.api_base << ")";
        first = false;
    }
    if (first) std::cout << "(none)";
    std::cout << "\n";

    // Channels
    std::cout << "Channels     : ";
    first = true;
    if (cfg.http_channel.enabled) {
        std::cout << "http"; first = false;
    }
    if (cfg.telegram.enabled) {
        if (!first) std::cout << ", ";
        std::cout << "telegram";
        first = false;
    }
    if (cfg.discord.enabled) {
        if (!first) std::cout << ", ";
        std::cout << "discord";
        first = false;
    }
    if (cfg.slack.enabled) {
        if (!first) std::cout << ", ";
        std::cout << "slack";
        first = false;
    }
    if (first) std::cout << "(none)";
    std::cout << "\n";

    // Security
    if (!cfg.http_channel.api_key.empty()) {
        std::cout << "HTTP Auth    : Bearer token enabled\n";
    }
    if (cfg.http_channel.rate_limit_rpm > 0) {
        std::cout << "Rate Limit   : " << cfg.http_channel.rate_limit_rpm << " req/min\n";
    }

    // Tools and Skills
    std::cout << "Tools        : exec, read_file, write_file, edit_file, list_dir, cron, memory, subagent\n";

    SkillsLoader skills(ws);
    skills.discover();
    auto& skill_list = skills.skills();
    if (!skill_list.empty()) {
        std::cout << "Skills       : ";
        first = true;
        for (auto& s : skill_list) {
            if (!first) std::cout << ", ";
            std::cout << s.name;
            if (!s.available) std::cout << " (unavailable)";
            else if (s.always) std::cout << " (always)";
            std::cout << " [" << s.source << "]";
            first = false;
        }
        std::cout << "\n";
    } else {
        std::cout << "Skills       : (none)\n";
    }

    // MCP servers
    if (!cfg.mcp_servers.empty()) {
        std::cout << "MCP servers  : ";
        first = true;
        for (auto& [name, srv] : cfg.mcp_servers) {
            if (!first) std::cout << ", ";
            std::cout << name;
            if (!srv.command.empty()) std::cout << " (stdio: " << srv.command << ")";
            else if (!srv.url.empty()) std::cout << " (http: " << srv.url << ")";
            first = false;
        }
        std::cout << "\n";
    }

    // Cron jobs
    try {
        std::string db_path = ws + "/cron/cron.db";
        if (fs::exists(db_path)) {
            CronStore store(db_path);
            auto jobs = store.list();
            std::cout << "Cron jobs    : " << jobs.size() << "\n";
        } else {
            std::cout << "Cron jobs    : 0\n";
        }
    } catch (...) {
        std::cout << "Cron jobs    : (error reading)\n";
    }

    // Workspace files
    std::cout << "Workspace    : ";
    std::vector<std::string> ws_files = {"IDENTITY.md", "SOUL.md", "AGENTS.md", "TOOLS.md",
                                          "USER.md", "MEMORY.md", "HEARTBEAT.md"};
    first = true;
    for (auto& name : ws_files) {
        if (fs::exists(ws + "/" + name)) {
            if (!first) std::cout << ", ";
            std::cout << name;
            first = false;
        }
    }
    if (first) std::cout << "(none)";
    std::cout << "\n";

    return 0;
}

} // namespace minidragon
