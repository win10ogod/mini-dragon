#pragma once
#include "config.hpp"
#include "tool_registry.hpp"
#include "session.hpp"
#include "provider.hpp"
#include "provider_chain.hpp"
#include "hooks.hpp"
#include "team.hpp"
#include "skills_loader.hpp"
#include <string>
#include <memory>

namespace minidragon {

// ── Token estimation (4 chars ≈ 1 token, conservative) ──────────────
inline int estimate_tokens(const std::string& text) {
    return static_cast<int>((text.size() + 3) / 4);
}

inline int estimate_tokens(const Message& msg) {
    int tokens = estimate_tokens(msg.content) + 4; // role overhead
    for (auto& tc : msg.tool_calls) {
        tokens += estimate_tokens(tc.name) + estimate_tokens(tc.arguments) + 8;
    }
    return tokens;
}

inline int estimate_tokens(const std::vector<Message>& msgs) {
    int total = 0;
    for (auto& m : msgs) total += estimate_tokens(m);
    return total;
}

// ── Error classification for provider retry ─────────────────────────
enum class ProviderErrorKind {
    unknown,
    rate_limit,
    timeout,
    overloaded,
    context_overflow,
    auth,
    billing
};

ProviderErrorKind classify_provider_error(const std::string& error_text);
bool is_retryable_error(ProviderErrorKind kind);

class Agent {
public:
    Agent(const Config& config, ToolRegistry& tools);
    std::string run(const std::string& user_message);
    void interactive_loop(bool no_markdown, bool logs);

    // Team support
    void set_team(std::shared_ptr<TeamManager> team, const std::string& my_name);
    void teammate_loop(const std::string& initial_prompt);

    // Skills support
    void set_skills(std::shared_ptr<SkillsLoader> skills);

    // Hook access
    HookRunner& hooks() { return hooks_; }
    ProviderChain& provider_chain() { return provider_chain_; }

private:
    Config config_;
    ToolRegistry& tools_;
    SessionLogger session_;
    ProviderChain provider_chain_;
    HookRunner hooks_;

    // Team context (optional)
    std::shared_ptr<TeamManager> team_;
    std::string my_name_;

    // Skills (optional)
    std::shared_ptr<SkillsLoader> skills_;

    // Cached system prompt (rebuilt when stale)
    std::string cached_system_prompt_;
    int64_t system_prompt_built_at_ = 0;

    std::string build_system_prompt();
    void inject_inbox_messages(std::vector<Message>& messages);

    // ── Token optimization ──────────────────────────────────────────
    int effective_max_tool_output() const;
    void prune_context(std::vector<Message>& messages);
    void repair_tool_pairing(std::vector<Message>& messages);
    bool try_auto_compact(std::vector<Message>& messages);
    std::string truncate_at_boundary(const std::string& text, int max_chars) const;
};

int cmd_agent(const std::string& message, bool no_markdown, bool logs,
              const std::string& team_name = "",
              const std::string& agent_name = "",
              const std::string& model_override = "");

} // namespace minidragon
