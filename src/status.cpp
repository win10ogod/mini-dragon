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
    std::cout << "Tools        : exec, read_file, write_file, edit_file, list_dir, apply_patch, grep_file, cron, memory, subagent\n";

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

// ── Doctor command ──────────────────────────────────────────────────

int cmd_doctor() {
    std::cout << "=== minidragon doctor ===\n\n";
    int issues = 0;
    int warnings = 0;

    // 1. Config file check
    std::string cfg_path = default_config_path();
    if (fs::exists(cfg_path)) {
        std::cout << "[OK]   Config file: " << cfg_path << "\n";
        try {
            Config cfg = Config::load(cfg_path);

            // 2. Workspace check
            std::string ws = cfg.workspace_path();
            if (fs::exists(ws)) {
                std::cout << "[OK]   Workspace: " << ws << "\n";

                // Check essential workspace files
                std::vector<std::string> essential = {"IDENTITY.md", "SOUL.md", "AGENTS.md"};
                for (auto& name : essential) {
                    if (fs::exists(ws + "/" + name)) {
                        std::cout << "[OK]   " << name << "\n";
                    } else {
                        std::cout << "[WARN] Missing: " << name << " (run 'minidragon onboard' to create)\n";
                        warnings++;
                    }
                }
            } else {
                std::cout << "[FAIL] Workspace not found: " << ws << "\n";
                std::cout << "       Run 'minidragon onboard' to create workspace\n";
                issues++;
            }

            // 3. Provider check
            if (cfg.providers.empty()) {
                std::cout << "[FAIL] No providers configured\n";
                std::cout << "       Add a provider to " << cfg_path << "\n";
                issues++;
            } else {
                for (auto& [name, p] : cfg.providers) {
                    if (p.api_key.empty() && p.api_base.find("localhost") == std::string::npos &&
                        p.api_base.find("127.0.0.1") == std::string::npos) {
                        std::cout << "[WARN] Provider '" << name << "' has no API key\n";
                        warnings++;
                    } else {
                        std::cout << "[OK]   Provider: " << name << " → " << p.api_base << "\n";
                    }
                }
            }

            // 4. Channels check
            if (cfg.telegram.enabled && cfg.telegram.token.empty()) {
                std::cout << "[FAIL] Telegram enabled but no token set\n";
                issues++;
            }

            // 5. MCP servers check
            for (auto& [name, srv] : cfg.mcp_servers) {
                if (!srv.command.empty()) {
                    // Check if command exists
                    std::string cmd = srv.command;
                    size_t space = cmd.find(' ');
                    if (space != std::string::npos) cmd = cmd.substr(0, space);
                    if (fs::exists(cmd) || cmd.find('/') == std::string::npos) {
                        std::cout << "[OK]   MCP server: " << name << "\n";
                    } else {
                        std::cout << "[WARN] MCP server '" << name << "': command not found: " << cmd << "\n";
                        warnings++;
                    }
                }
            }

            // 6. Cron DB check
            std::string db_path = cfg.workspace_path() + "/cron/cron.db";
            if (fs::exists(db_path)) {
                try {
                    CronStore store(db_path);
                    auto jobs = store.list();
                    std::cout << "[OK]   Cron DB: " << jobs.size() << " job(s)\n";
                } catch (const std::exception& e) {
                    std::cout << "[FAIL] Cron DB corrupt: " << e.what() << "\n";
                    issues++;
                }
            }

        } catch (const std::exception& e) {
            std::cout << "[FAIL] Config parse error: " << e.what() << "\n";
            issues++;
        }
    } else {
        std::cout << "[FAIL] Config file not found: " << cfg_path << "\n";
        std::cout << "       Run 'minidragon onboard' to create configuration\n";
        issues++;
    }

    // Summary
    std::cout << "\n";
    if (issues == 0 && warnings == 0) {
        std::cout << "All checks passed. minidragon is healthy.\n";
    } else {
        if (issues > 0) std::cout << issues << " issue(s) found.\n";
        if (warnings > 0) std::cout << warnings << " warning(s).\n";
    }
    return issues > 0 ? 1 : 0;
}

// ── Sessions command ────────────────────────────────────────────────

int cmd_sessions(const std::string& subcmd, const std::string& arg) {
    Config cfg = Config::load(default_config_path());
    std::string sessions_dir = cfg.workspace_path() + "/sessions";

    if (subcmd == "list" || subcmd.empty()) {
        if (!fs::exists(sessions_dir)) {
            std::cout << "No sessions found.\n";
            return 0;
        }
        std::vector<std::string> files;
        for (auto& entry : fs::directory_iterator(sessions_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
                files.push_back(entry.path().filename().string());
            }
        }
        std::sort(files.rbegin(), files.rend()); // Most recent first
        if (files.empty()) {
            std::cout << "No sessions found.\n";
            return 0;
        }
        std::cout << "Sessions (most recent first):\n";
        for (auto& f : files) {
            std::string date = f.substr(0, f.size() - 6); // strip .jsonl
            std::string path = sessions_dir + "/" + f;
            // Count messages
            int count = 0;
            std::ifstream file(path);
            std::string line;
            while (std::getline(file, line)) if (!line.empty()) count++;
            std::cout << "  " << date << "  (" << count << " messages)\n";
        }
        return 0;
    }

    if (subcmd == "show") {
        std::string date = arg.empty() ? today_str() : arg;
        std::string path = sessions_dir + "/" + date + ".jsonl";
        if (!fs::exists(path)) {
            std::cout << "No session found for " << date << "\n";
            return 1;
        }
        std::ifstream file(path);
        std::string line;
        int msg_num = 0;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            msg_num++;
            try {
                auto j = nlohmann::json::parse(line);
                std::string role = j.value("role", "?");
                std::string content = j.value("content", "");
                // Truncate long content for display
                if (content.size() > 200) content = content.substr(0, 200) + "...";
                std::cout << "[" << msg_num << "] " << role << ": " << content << "\n";
            } catch (...) {
                std::cout << "[" << msg_num << "] (parse error)\n";
            }
        }
        return 0;
    }

    if (subcmd == "clear") {
        if (!fs::exists(sessions_dir)) return 0;
        int removed = 0;
        for (auto& entry : fs::directory_iterator(sessions_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
                // Don't delete today's session unless forced
                std::string fname = entry.path().stem().string();
                if (fname == today_str() && arg != "--force") {
                    std::cout << "Skipping today's session. Use 'sessions clear --force' to include it.\n";
                    continue;
                }
                fs::remove(entry.path());
                removed++;
            }
        }
        std::cout << "Removed " << removed << " session file(s).\n";
        return 0;
    }

    std::cout << "Usage: minidragon sessions [list|show [date]|clear [--force]]\n";
    return 1;
}

} // namespace minidragon
