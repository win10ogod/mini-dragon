#include "hooks.hpp"
#include <algorithm>
#include <iostream>
#include <cstdio>
#include <array>

namespace minidragon {

void HookRunner::register_hook(HookEntry entry) {
    auto& vec = hooks_[entry.type];
    vec.push_back(std::move(entry));
    // Keep sorted by priority (lower first)
    std::sort(vec.begin(), vec.end(),
              [](const HookEntry& a, const HookEntry& b) { return a.priority < b.priority; });
}

void HookRunner::fire(HookType type, const HookData& data) {
    auto it = hooks_.find(type);
    if (it == hooks_.end()) return;
    for (auto& entry : it->second) {
        try {
            entry.callback(data);
        } catch (const std::exception& e) {
            std::cerr << "[hook:" << entry.name << "] error: " << e.what() << "\n";
        }
    }
}

HookData HookRunner::run(HookType type, HookData data) {
    auto it = hooks_.find(type);
    if (it == hooks_.end()) return data;
    for (auto& entry : it->second) {
        try {
            data = entry.callback(std::move(data));
        } catch (const std::exception& e) {
            std::cerr << "[hook:" << entry.name << "] error: " << e.what() << "\n";
        }
    }
    return data;
}

bool HookRunner::has_hooks(HookType type) const {
    auto it = hooks_.find(type);
    return it != hooks_.end() && !it->second.empty();
}

int HookRunner::hook_count() const {
    int count = 0;
    for (auto& [_, vec] : hooks_) count += static_cast<int>(vec.size());
    return count;
}

HookType parse_hook_type(const std::string& s) {
    if (s == "agent_start")           return HookType::agent_start;
    if (s == "agent_stop")            return HookType::agent_stop;
    if (s == "pre_tool_call")         return HookType::pre_tool_call;
    if (s == "post_tool_call")        return HookType::post_tool_call;
    if (s == "pre_api_call")          return HookType::pre_api_call;
    if (s == "post_api_call")         return HookType::post_api_call;
    if (s == "pre_user_message")      return HookType::pre_user_message;
    if (s == "post_assistant_message") return HookType::post_assistant_message;
    if (s == "pre_compaction")        return HookType::pre_compaction;
    if (s == "post_compaction")       return HookType::post_compaction;
    if (s == "pre_prune")             return HookType::pre_prune;
    if (s == "post_prune")            return HookType::post_prune;
    if (s == "pre_memory_save")       return HookType::pre_memory_save;
    if (s == "post_memory_save")      return HookType::post_memory_save;
    if (s == "pre_memory_search")     return HookType::pre_memory_search;
    if (s == "post_memory_search")    return HookType::post_memory_search;
    if (s == "pre_provider_select")   return HookType::pre_provider_select;
    if (s == "post_provider_error")   return HookType::post_provider_error;
    if (s == "pre_team_message")      return HookType::pre_team_message;
    if (s == "post_team_message")     return HookType::post_team_message;
    if (s == "session_start")         return HookType::session_start;
    if (s == "session_end")           return HookType::session_end;
    return HookType::agent_start; // fallback
}

// Shell hook: serialize HookData as JSON → stdin, read stdout as modified JSON
HookCallback make_shell_hook(const std::string& command) {
    return [command](HookData data) -> HookData {
        std::string input = data.dump();

        // Open process with stdin/stdout pipes
        std::string full_cmd = "echo '" + input + "' | " + command;
#ifdef _WIN32
        FILE* pipe = _popen(full_cmd.c_str(), "r");
#else
        FILE* pipe = popen(full_cmd.c_str(), "r");
#endif
        if (!pipe) {
            std::cerr << "[hook] Failed to execute: " << command << "\n";
            return data;
        }

        std::string output;
        std::array<char, 4096> buf;
        while (auto n = std::fread(buf.data(), 1, buf.size(), pipe)) {
            output.append(buf.data(), n);
        }

#ifdef _WIN32
        int status = _pclose(pipe);
#else
        int status = pclose(pipe);
#endif

        if (status != 0) {
            std::cerr << "[hook] Command exited with status " << status << ": " << command << "\n";
            return data;
        }

        // Try to parse output as JSON (for modifying hooks)
        if (!output.empty()) {
            try {
                return nlohmann::json::parse(output);
            } catch (...) {
                // Non-JSON output — return original data
            }
        }
        return data;
    };
}

} // namespace minidragon
