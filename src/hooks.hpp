#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

namespace minidragon {

enum class HookType {
    // Agent lifecycle
    agent_start,
    agent_stop,

    // Message hooks
    pre_tool_call,
    post_tool_call,
    pre_api_call,
    post_api_call,
    pre_user_message,
    post_assistant_message,

    // Context hooks
    pre_compaction,
    post_compaction,
    pre_prune,
    post_prune,

    // Memory hooks
    pre_memory_save,
    post_memory_save,
    pre_memory_search,
    post_memory_search,

    // Provider hooks
    pre_provider_select,
    post_provider_error,

    // Team hooks
    pre_team_message,
    post_team_message,

    // Session hooks
    session_start,
    session_end,
};

using HookData = nlohmann::json;
using HookCallback = std::function<HookData(HookData)>;

struct HookEntry {
    std::string name;
    HookType type;
    int priority = 0;  // lower runs first
    HookCallback callback;
};

class HookRunner {
public:
    void register_hook(HookEntry entry);

    // Fire-and-forget (void hooks): run all callbacks, ignore return values
    void fire(HookType type, const HookData& data = {});

    // Modifying hooks: run callbacks in sequence, each receives previous output
    HookData run(HookType type, HookData data);

    bool has_hooks(HookType type) const;

    int hook_count() const;

private:
    std::map<HookType, std::vector<HookEntry>> hooks_;
};

// Parse HookType from string (for config)
HookType parse_hook_type(const std::string& s);

// Create a shell-command hook callback
HookCallback make_shell_hook(const std::string& command);

} // namespace minidragon
