#include "agent.hpp"
#include "tools/exec_tool.hpp"
#include "tools/fs_tools.hpp"
#include "tools/cron_tool.hpp"
#include "tools/memory_tool.hpp"
#include "tools/memory_search_tool.hpp"
#include "tools/subagent_tool.hpp"
#include "tools/team_tools.hpp"
#include "mcp_manager.hpp"
#include "memory.hpp"
#include "memory_search.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <set>

namespace minidragon {

// ── Error classification (openclaw-compatible) ─────────────────────────

static bool text_contains_any(const std::string& text, std::initializer_list<const char*> patterns) {
    for (auto p : patterns) {
        if (text.find(p) != std::string::npos) return true;
    }
    return false;
}

ProviderErrorKind classify_provider_error(const std::string& error_text) {
    if (error_text.empty()) return ProviderErrorKind::unknown;

    // Lowercase for case-insensitive matching
    std::string lower;
    lower.reserve(error_text.size());
    for (char c : error_text) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (text_contains_any(lower, {"rate limit", "rate_limit", "too many requests", "429",
                                   "quota exceeded", "resource_exhausted", "usage limit"}))
        return ProviderErrorKind::rate_limit;

    if (text_contains_any(lower, {"overloaded", "overloaded_error"}))
        return ProviderErrorKind::overloaded;

    if (text_contains_any(lower, {"context overflow", "context window", "prompt too large",
                                   "too long", "token limit", "maximum context",
                                   "exceeds the model", "input too large"}))
        return ProviderErrorKind::context_overflow;

    if (text_contains_any(lower, {"timeout", "timed out", "deadline exceeded"}))
        return ProviderErrorKind::timeout;

    if (text_contains_any(lower, {"401", "403", "unauthorized", "forbidden",
                                   "invalid api key", "invalid_api_key", "authentication"}))
        return ProviderErrorKind::auth;

    if (text_contains_any(lower, {"402", "payment required", "insufficient credits",
                                   "billing", "insufficient balance"}))
        return ProviderErrorKind::billing;

    return ProviderErrorKind::unknown;
}

bool is_retryable_error(ProviderErrorKind kind) {
    return kind == ProviderErrorKind::rate_limit ||
           kind == ProviderErrorKind::timeout ||
           kind == ProviderErrorKind::overloaded;
}

// ── Agent implementation ───────────────────────────────────────────────

Agent::Agent(const Config& config, ToolRegistry& tools)
    : config_(config)
    , tools_(tools)
    , session_(config.workspace_path() + "/sessions")
    , provider_chain_(config)
{
    // Register configured hooks
    for (auto& hc : config.hooks) {
        HookEntry entry;
        entry.name = hc.type + ":" + hc.command;
        entry.type = parse_hook_type(hc.type);
        entry.priority = hc.priority;
        entry.callback = make_shell_hook(hc.command);
        hooks_.register_hook(std::move(entry));
    }
}

void Agent::set_team(std::shared_ptr<TeamManager> team, const std::string& my_name) {
    team_ = std::move(team);
    my_name_ = my_name;
}

void Agent::set_skills(std::shared_ptr<SkillsLoader> skills) {
    skills_ = std::move(skills);
}

int Agent::effective_max_tool_output() const {
    if (config_.max_tool_output > 0) return config_.max_tool_output;
    // Auto: 30% of context window (in chars, ~4 chars/token)
    return static_cast<int>(config_.context_tokens * 4 * 0.3);
}

std::string Agent::build_system_prompt() {
    // Cache: rebuild only every 60 seconds
    int64_t now = epoch_now();
    if (!cached_system_prompt_.empty() && (now - system_prompt_built_at_) < 60) {
        return cached_system_prompt_;
    }

    std::string ws = config_.workspace_path();

    // Check for BOOTSTRAP.md — first-run onboarding takes priority
    std::string bootstrap = read_file(ws + "/BOOTSTRAP.md");
    bool is_bootstrap = !bootstrap.empty();

    std::string prompt;
    int total_chars = 0;
    constexpr int MAX_FILE_CHARS = 20000;
    constexpr int MAX_TOTAL_CHARS = 150000;

    auto inject_file = [&](const std::string& label, const std::string& content) {
        if (content.empty()) return;
        std::string text = content;
        if (static_cast<int>(text.size()) > MAX_FILE_CHARS) {
            text = text.substr(0, MAX_FILE_CHARS) + "\n...[truncated at " + std::to_string(MAX_FILE_CHARS) + " chars]\n";
        }
        if (total_chars + static_cast<int>(text.size()) > MAX_TOTAL_CHARS) {
            int remaining = MAX_TOTAL_CHARS - total_chars;
            if (remaining <= 100) return;
            text = text.substr(0, remaining) + "\n...[context limit reached]\n";
        }
        prompt += "--- " + label + " ---\n" + text + "\n\n";
        total_chars += static_cast<int>(text.size());
    };

    if (is_bootstrap) {
        prompt = "You are a brand new AI agent, just coming online for the first time.\n"
                 "You have tools available to read and write files in your workspace.\n\n";
        inject_file("BOOTSTRAP.md", bootstrap);
    }

    // Load identity files (order matters: SOUL -> IDENTITY -> USER -> AGENTS -> TOOLS)
    for (auto& name : {"SOUL.md", "IDENTITY.md", "USER.md", "AGENTS.md", "TOOLS.md"}) {
        inject_file(name, read_file(ws + "/" + name));
    }

    // Memory: MEMORY.md + today
    std::string mem_dir = ws + "/memory";
    inject_file("MEMORY.md", read_file(ws + "/MEMORY.md"));
    inject_file("Memory: " + today_str(), read_file(mem_dir + "/" + today_str() + ".md"));

    // Team context
    if (team_ && team_->team_exists()) {
        auto cfg = team_->get_config();
        std::string team_ctx = "You are '" + my_name_ + "' in team '" + cfg.display_name + "'.\n";
        team_ctx += "Team lead: " + cfg.lead_name + "\nMembers: ";
        for (auto& m : cfg.members) team_ctx += m.name + " (" + m.agent_type + "), ";
        team_ctx += "\n";
        if (my_name_ == cfg.lead_name) {
            team_ctx += "You are the TEAM LEAD. Coordinate work, spawn teammates, assign tasks.\n"
                        "Use team tools: team_create, team_spawn, team_send, team_shutdown, team_cleanup.\n";
        } else {
            team_ctx += "You are a TEAMMATE. Complete your assigned work and report results.\n"
                        "Use team_send to communicate with the lead or other teammates.\n";
        }
        team_ctx += "Use task_create, task_update, task_list to manage shared tasks.\n"
                    "Use inbox_check to read messages from teammates.\n";
        inject_file("Team Context", team_ctx);
    }

    // Skills: always-loaded skills get full content, others get summary
    if (skills_) {
        std::string always_content = skills_->build_always_skills_content();
        if (!always_content.empty()) {
            inject_file("Active Skills", always_content);
        }
        std::string summary = skills_->build_skills_summary();
        if (!summary.empty()) {
            prompt += summary + "\n";
        }
    }

    cached_system_prompt_ = prompt;
    system_prompt_built_at_ = now;
    return prompt;
}

// ── Truncate at line boundary ──────────────────────────────────────────

std::string Agent::truncate_at_boundary(const std::string& text, int max_chars) const {
    if (max_chars <= 0 || static_cast<int>(text.size()) <= max_chars) return text;

    int head = config_.prune_head_chars;
    int tail = config_.prune_tail_chars;
    if (head + tail >= max_chars) {
        head = max_chars * 2 / 3;
        tail = max_chars / 3;
    }

    // Find newline boundary for head
    int head_end = head;
    for (int i = head; i > head - 200 && i > 0; i--) {
        if (text[i] == '\n') { head_end = i + 1; break; }
    }

    // Find newline boundary for tail
    int tail_start = static_cast<int>(text.size()) - tail;
    for (int i = tail_start; i < tail_start + 200 && i < static_cast<int>(text.size()); i++) {
        if (text[i] == '\n') { tail_start = i + 1; break; }
    }

    return text.substr(0, head_end) +
           "\n...[trimmed " + std::to_string(text.size()) + " chars → " +
           std::to_string(head_end + (static_cast<int>(text.size()) - tail_start)) + "]...\n" +
           text.substr(tail_start);
}

// ── Context-aware pruning (openclaw-compatible) ────────────────────────

void Agent::prune_context(std::vector<Message>& messages) {
    int context_chars = config_.context_tokens * 4; // approximate char budget
    int soft_threshold = static_cast<int>(context_chars * config_.prune_soft_ratio);
    int hard_threshold = static_cast<int>(context_chars * config_.prune_hard_ratio);

    // Calculate total context size
    int total_chars = 0;
    for (auto& m : messages) {
        total_chars += static_cast<int>(m.content.size());
        for (auto& tc : m.tool_calls)
            total_chars += static_cast<int>(tc.arguments.size());
    }

    if (total_chars < soft_threshold) return;

    // Find the index of the last N assistant messages to protect
    int protect_from = static_cast<int>(messages.size());
    int assistant_count = 0;
    for (int i = static_cast<int>(messages.size()) - 1; i >= 0; i--) {
        if (messages[i].role == "assistant") {
            assistant_count++;
            if (assistant_count >= config_.prune_keep_recent) {
                protect_from = i;
                break;
            }
        }
    }

    // Phase 1: Soft trim — keep head+tail of old large tool results
    for (int i = 0; i < protect_from; i++) {
        if (messages[i].role != "tool") continue;
        int sz = static_cast<int>(messages[i].content.size());
        if (sz <= config_.prune_head_chars + config_.prune_tail_chars + 100) continue;

        int old_sz = static_cast<int>(messages[i].content.size());
        messages[i].content = truncate_at_boundary(messages[i].content,
                                                    config_.prune_head_chars + config_.prune_tail_chars);
        total_chars += static_cast<int>(messages[i].content.size()) - old_sz;
        if (total_chars < soft_threshold) return;
    }

    if (total_chars < hard_threshold) return;

    // Phase 2: Hard clear — replace old tool results with placeholder
    for (int i = 0; i < protect_from; i++) {
        if (messages[i].role != "tool") continue;
        if (messages[i].content.size() <= 100) continue;
        messages[i].content = "[tool result cleared: " +
            std::to_string(messages[i].content.size()) + " chars]";
    }
}

// ── Repair orphaned tool_use/tool_result pairing ───────────────────────

void Agent::repair_tool_pairing(std::vector<Message>& messages) {
    // Collect all tool_call IDs from assistant messages
    std::set<std::string> call_ids;
    for (auto& m : messages) {
        if (m.role == "assistant") {
            for (auto& tc : m.tool_calls) {
                if (!tc.id.empty()) call_ids.insert(tc.id);
            }
        }
    }

    // Remove tool results whose call_id doesn't match any tool_call
    messages.erase(
        std::remove_if(messages.begin(), messages.end(), [&](const Message& m) {
            if (m.role != "tool" || m.tool_call_id.empty()) return false;
            return call_ids.find(m.tool_call_id) == call_ids.end();
        }),
        messages.end()
    );

    // Also remove assistant tool_calls whose results are missing
    std::set<std::string> result_ids;
    for (auto& m : messages) {
        if (m.role == "tool" && !m.tool_call_id.empty()) {
            result_ids.insert(m.tool_call_id);
        }
    }
    for (auto& m : messages) {
        if (m.role == "assistant" && !m.tool_calls.empty()) {
            m.tool_calls.erase(
                std::remove_if(m.tool_calls.begin(), m.tool_calls.end(), [&](const ToolCall& tc) {
                    return !tc.id.empty() && result_ids.find(tc.id) == result_ids.end();
                }),
                m.tool_calls.end()
            );
        }
    }
}

// ── Auto-compaction: LLM-based summarization with structural fallback ────

static std::string build_structural_summary(const std::vector<Message>& messages,
                                              int start, int end) {
    std::string summary_text;
    for (int i = start; i < end; i++) {
        auto& m = messages[i];
        if (m.role == "user") {
            summary_text += "User: " + m.content.substr(0, 500) + "\n";
        } else if (m.role == "assistant") {
            std::string content = m.content.substr(0, 500);
            summary_text += "Assistant: " + content;
            if (!m.tool_calls.empty()) {
                summary_text += " [called tools: ";
                for (size_t j = 0; j < m.tool_calls.size(); j++) {
                    if (j > 0) summary_text += ", ";
                    summary_text += m.tool_calls[j].name;
                }
                summary_text += "]";
            }
            summary_text += "\n";
        } else if (m.role == "tool") {
            summary_text += "Tool result: " + m.content.substr(0, 200) + "\n";
        }
    }
    if (summary_text.size() > 4000) {
        summary_text = summary_text.substr(0, 4000) + "\n...[summary truncated]\n";
    }
    return summary_text;
}

bool Agent::try_auto_compact(std::vector<Message>& messages) {
    if (!config_.auto_compact) return false;

    int total_tokens = estimate_tokens(messages);
    int budget = config_.context_tokens - config_.compact_reserve_tokens;
    if (total_tokens < budget) return false;

    // Find compaction point: keep the last N messages after system
    int keep_count = config_.prune_keep_recent * 3; // keep last ~9 messages
    if (keep_count >= static_cast<int>(messages.size())) return false;

    int compact_end = static_cast<int>(messages.size()) - keep_count;
    if (compact_end <= 1) return false; // nothing to compact (just system)

    // Fire pre_compaction hook
    if (hooks_.has_hooks(HookType::pre_compaction)) {
        nlohmann::json hook_data;
        hook_data["message_count"] = compact_end - 1;
        hook_data["total_tokens"] = total_tokens;
        hooks_.run(HookType::pre_compaction, std::move(hook_data));
    }

    // Build conversation text for summarization
    std::string conv_text = build_structural_summary(messages, 1, compact_end);
    int chars_to_summarize = 0;
    for (int i = 1; i < compact_end; i++)
        chars_to_summarize += static_cast<int>(messages[i].content.size());

    std::string compacted;

    // Try LLM-based summarization
    try {
        std::vector<Message> compact_msgs;
        Message sys;
        sys.role = "system";
        sys.content = "Summarize the following conversation concisely. "
                      "Preserve key decisions, file paths, code changes, and action items. "
                      "Keep the summary under 2000 chars.";
        compact_msgs.push_back(sys);

        Message user;
        user.role = "user";
        user.content = conv_text;
        compact_msgs.push_back(user);

        nlohmann::json no_tools = nlohmann::json::array();
        auto resp = provider_chain_.chat(compact_msgs, no_tools,
                                          config_.model, 1024, 0.3);

        compacted = "[Compacted: " + std::to_string(compact_end - 1) +
                    " messages → LLM summary]\n" + resp.content;
    } catch (...) {
        // Fallback to structural summary
        compacted = "[Compacted conversation summary (" +
            std::to_string(compact_end - 1) + " messages, ~" +
            std::to_string(chars_to_summarize / 4) + " tokens)]\n" + conv_text;
    }

    // Replace old messages with compaction summary
    Message compaction_msg;
    compaction_msg.role = "user";
    compaction_msg.content = compacted;

    std::vector<Message> new_messages;
    new_messages.push_back(messages[0]); // system prompt
    new_messages.push_back(compaction_msg);
    for (int i = compact_end; i < static_cast<int>(messages.size()); i++) {
        new_messages.push_back(std::move(messages[i]));
    }

    messages = std::move(new_messages);

    // Fire post_compaction hook
    hooks_.fire(HookType::post_compaction, {{"compacted_size", compacted.size()}});

    return true;
}

void Agent::inject_inbox_messages(std::vector<Message>& messages) {
    if (!team_ || !team_->team_exists()) return;

    auto unread = team_->read_unread(my_name_);
    for (auto& msg : unread) {
        bool is_idle = false;
        try {
            auto j = nlohmann::json::parse(msg.text);
            if (j.contains("type")) {
                std::string type = j["type"].get<std::string>();
                if (type == "idle_notification") {
                    is_idle = true;
                } else if (type == "shutdown_approved") {
                    Message m;
                    m.role = "user";
                    m.content = "[Team] " + msg.from + " has shut down.";
                    messages.push_back(m);
                    continue;
                } else if (type == "shutdown_request") {
                    Message m;
                    m.role = "user";
                    m.content = "[Team] Shutdown request from " + msg.from;
                    messages.push_back(m);
                    continue;
                }
            }
        } catch (...) {}

        if (is_idle) continue;

        Message m;
        m.role = "user";
        m.content = "[Team message from " + msg.from + "]: " + msg.text;
        messages.push_back(m);
    }
}

// ── Main agent run loop ────────────────────────────────────────────────

std::string Agent::run(const std::string& user_message) {
    std::vector<Message> messages;

    Message sys;
    sys.role = "system";
    sys.content = build_system_prompt();
    messages.push_back(sys);

    auto recent = session_.load_recent(config_.context_window);
    for (auto& m : recent) {
        messages.push_back(m);
    }

    // pre_user_message hook
    std::string processed_message = user_message;
    if (hooks_.has_hooks(HookType::pre_user_message)) {
        auto modified = hooks_.run(HookType::pre_user_message, {{"content", user_message}});
        if (modified.contains("content")) processed_message = modified["content"].get<std::string>();
    }

    Message user_msg;
    user_msg.role = "user";
    user_msg.content = processed_message;
    messages.push_back(user_msg);
    session_.log(user_msg);

    // ── Token optimization pipeline ──
    prune_context(messages);
    repair_tool_pairing(messages);
    try_auto_compact(messages);

    auto tools_spec = tools_.tools_spec();
    int tool_spec_tokens = estimate_tokens(tools_spec.dump());

    int iterations = 0;
    int max_iter = config_.max_iterations;
    int max_output = effective_max_tool_output();

    while (iterations < max_iter) {
        inject_inbox_messages(messages);
        iterations++;

        // Pre-flight token check
        int msg_tokens = estimate_tokens(messages) + tool_spec_tokens;
        if (msg_tokens > config_.context_tokens - config_.max_tokens) {
            // Try compaction before giving up
            if (try_auto_compact(messages)) {
                prune_context(messages);
                repair_tool_pairing(messages);
                msg_tokens = estimate_tokens(messages) + tool_spec_tokens;
            }
            if (msg_tokens > config_.context_tokens - config_.max_tokens) {
                // Still too big - aggressive pruning
                prune_context(messages);
            }
        }

        // pre_api_call hook
        if (hooks_.has_hooks(HookType::pre_api_call)) {
            nlohmann::json api_data;
            api_data["message_count"] = messages.size();
            api_data["model"] = config_.model;
            api_data["provider"] = provider_chain_.active_provider_name();
            hooks_.run(HookType::pre_api_call, std::move(api_data));
        }

        ProviderResponse resp;
        bool success = false;
        std::string last_error;

        for (int retry = 0; retry <= config_.max_retries; retry++) {
            try {
                resp = provider_chain_.chat(messages, tools_spec,
                                             config_.model,
                                             config_.max_tokens,
                                             config_.temperature);
                success = true;
                break;
            } catch (const std::exception& e) {
                last_error = e.what();
                auto kind = classify_provider_error(last_error);

                // post_provider_error hook
                hooks_.fire(HookType::post_provider_error, {
                    {"error", last_error},
                    {"provider", provider_chain_.active_provider_name()},
                    {"retry", retry}
                });

                if (kind == ProviderErrorKind::context_overflow) {
                    // Try compaction and retry once
                    if (try_auto_compact(messages)) {
                        prune_context(messages);
                        repair_tool_pairing(messages);
                        continue;
                    }
                    break; // can't recover
                }

                if (!is_retryable_error(kind) || retry >= config_.max_retries) break;

                // Exponential backoff: 1s, 2s, 4s
                int delay_ms = 1000 * (1 << retry);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
        }

        if (!success) {
            return std::string("[error] Provider call failed: ") + last_error;
        }

        // post_api_call hook
        if (hooks_.has_hooks(HookType::post_api_call)) {
            nlohmann::json resp_data;
            resp_data["content_length"] = resp.content.size();
            resp_data["tool_call_count"] = resp.tool_calls.size();
            resp_data["provider"] = provider_chain_.active_provider_name();
            hooks_.run(HookType::post_api_call, std::move(resp_data));
        }

        if (!resp.has_tool_calls()) {
            Message assistant;
            assistant.role = "assistant";
            assistant.content = resp.content;
            messages.push_back(assistant);
            session_.log(assistant);

            // post_assistant_message hook
            hooks_.fire(HookType::post_assistant_message, {{"content", resp.content}});

            return resp.content;
        }

        Message assistant;
        assistant.role = "assistant";
        assistant.content = resp.content;
        assistant.tool_calls = resp.tool_calls;
        messages.push_back(assistant);
        session_.log(assistant);

        for (auto& tc : resp.tool_calls) {
            // pre_tool_call hook
            std::string tool_name = tc.name;
            std::string tool_args = tc.arguments;
            if (hooks_.has_hooks(HookType::pre_tool_call)) {
                auto modified = hooks_.run(HookType::pre_tool_call, {
                    {"name", tc.name}, {"arguments", tc.arguments}
                });
                if (modified.contains("name")) tool_name = modified["name"].get<std::string>();
                if (modified.contains("arguments")) tool_args = modified["arguments"].get<std::string>();
            }

            std::string result;
            try {
                auto args = tool_args.empty() ? nlohmann::json::object() : nlohmann::json::parse(tool_args);
                result = tools_.execute(tool_name, args);
            } catch (const std::exception& e) {
                result = std::string("[error] ") + e.what();
            }

            // post_tool_call hook
            if (hooks_.has_hooks(HookType::post_tool_call)) {
                auto modified = hooks_.run(HookType::post_tool_call, {
                    {"name", tool_name}, {"result", result}
                });
                if (modified.contains("result")) result = modified["result"].get<std::string>();
            }

            // Truncate at line boundary
            if (static_cast<int>(result.size()) > max_output) {
                result = truncate_at_boundary(result, max_output);
            }

            Message tool_msg;
            tool_msg.role = "tool";
            tool_msg.tool_call_id = tc.id;
            tool_msg.content = result;
            messages.push_back(tool_msg);
            session_.log(tool_msg);
        }

        // Mid-loop pruning to keep context bounded during multi-iteration runs
        if (iterations % 3 == 0) {
            prune_context(messages);
        }
    }

    return "[agent] Max tool iterations reached (" + std::to_string(max_iter) + ")";
}

void Agent::interactive_loop(bool no_markdown, bool logs) {
    (void)no_markdown;
    (void)logs;

    // Fire agent_start hook
    hooks_.fire(HookType::agent_start);

    std::cout << "Mini Dragon agent (interactive mode)\n"
              << "Commands: /new /status /model <name> /context /compact | exit/quit/:q | Ctrl+D\n";
    std::string line;
    while (true) {
        // Show inbox notifications
        if (team_ && team_->team_exists()) {
            auto unread = team_->read_unread(my_name_);
            for (auto& msg : unread) {
                bool is_idle = false;
                try {
                    auto j = nlohmann::json::parse(msg.text);
                    if (j.contains("type") && j["type"] == "idle_notification")
                        is_idle = true;
                } catch (...) {}

                if (!is_idle) {
                    std::cerr << "[inbox " << msg.from << "] " << msg.summary << "\n";
                }
            }
        }

        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == "exit" || line == "quit" || line == ":q") break;

        // Chat commands (openclaw-compatible)
        if (line == "/new" || line == "/reset") {
            std::string ws = config_.workspace_path();
            std::string session_file = ws + "/sessions/" + today_str() + ".jsonl";
            if (fs::exists(session_file)) {
                fs::remove(session_file);
            }
            session_ = SessionLogger(ws + "/sessions");
            cached_system_prompt_.clear();
            std::cout << "Session reset. Starting fresh.\n";
            continue;
        }
        if (line.substr(0, 5) == "/new ") {
            std::string new_model = line.substr(5);
            while (!new_model.empty() && new_model[0] == ' ') new_model.erase(0, 1);
            if (!new_model.empty()) {
                config_.model = new_model;
                std::cout << "Switched to model: " << new_model << "\n";
            }
            std::string ws = config_.workspace_path();
            std::string session_file = ws + "/sessions/" + today_str() + ".jsonl";
            if (fs::exists(session_file)) fs::remove(session_file);
            session_ = SessionLogger(ws + "/sessions");
            cached_system_prompt_.clear();
            std::cout << "Session reset.\n";
            continue;
        }
        if (line == "/status") {
            auto recent = session_.load_recent(config_.context_window);
            int session_tokens = estimate_tokens(recent);
            int system_tokens = estimate_tokens(build_system_prompt());
            int tools_tokens = estimate_tokens(tools_.tools_spec().dump());
            int total = session_tokens + system_tokens + tools_tokens;

            std::cout << "Model    : " << config_.model << "\n"
                      << "Provider : " << provider_chain_.active_provider_name()
                      << " (" << provider_chain_.provider_count() << " configured";
            if (config_.fallback.enabled) std::cout << ", fallback ON";
            std::cout << ")\n"
                      << "Tokens   : " << config_.max_tokens << " (output)\n"
                      << "Temp     : " << config_.temperature << "\n"
                      << "Max iter : " << config_.max_iterations << "\n"
                      << "Context  : " << total << " / " << config_.context_tokens
                      << " tokens (~" << (total * 100 / std::max(config_.context_tokens, 1)) << "%)\n"
                      << "  System : ~" << system_tokens << " tokens\n"
                      << "  Tools  : ~" << tools_tokens << " tokens (" << tools_.tool_names().size() << " tools)\n"
                      << "  History: ~" << session_tokens << " tokens (" << recent.size() << " messages)\n"
                      << "Retries  : " << config_.max_retries << "\n"
                      << "Compact  : " << (config_.auto_compact ? "auto (LLM)" : "manual") << "\n"
                      << "Hooks    : " << hooks_.hook_count() << " registered\n"
                      << "Embedding: " << (config_.embedding.enabled ? "enabled" : "disabled") << "\n";
            continue;
        }
        if (line.substr(0, 7) == "/model ") {
            std::string new_model = line.substr(7);
            while (!new_model.empty() && new_model[0] == ' ') new_model.erase(0, 1);
            if (!new_model.empty()) {
                config_.model = new_model;
                std::cout << "Model set to: " << new_model << "\n";
            }
            continue;
        }
        if (line == "/context") {
            std::string prompt = build_system_prompt();
            int prompt_tokens = estimate_tokens(prompt);
            std::cout << "System prompt: " << prompt.size() << " chars (~" << prompt_tokens << " tokens)\n";
            std::string ws = config_.workspace_path();
            for (auto& name : {"SOUL.md", "IDENTITY.md", "USER.md", "AGENTS.md", "TOOLS.md", "MEMORY.md"}) {
                std::string content = read_file(ws + "/" + name);
                if (!content.empty()) {
                    int raw = static_cast<int>(content.size());
                    int injected = std::min(raw, 20000);
                    std::cout << "  " << name << ": " << raw << " chars (~" << (raw/4)
                              << " tok)" << (raw > 20000 ? " TRUNCATED" : " OK")
                              << " | injected " << injected << " chars\n";
                }
            }
            auto recent = session_.load_recent(config_.context_window);
            std::cout << "  Session: " << recent.size() << " messages (~"
                      << estimate_tokens(recent) << " tokens)\n";
            auto tools_json = tools_.tools_spec();
            std::cout << "  Tools: " << tools_.tool_names().size() << " registered (~"
                      << estimate_tokens(tools_json.dump()) << " tokens)\n";
            std::cout << "  Context window: " << config_.context_tokens << " tokens\n";
            continue;
        }
        if (line == "/compact") {
            auto recent = session_.load_recent(config_.context_window);
            int before = estimate_tokens(recent);
            std::vector<Message> msgs;
            Message sys; sys.role = "system"; sys.content = build_system_prompt();
            msgs.push_back(sys);
            for (auto& m : recent) msgs.push_back(m);
            if (try_auto_compact(msgs)) {
                std::cout << "Compacted: ~" << before << " tokens -> ~"
                          << estimate_tokens(msgs) << " tokens\n";
            } else {
                std::cout << "Nothing to compact (context usage is low).\n";
            }
            continue;
        }
        if (line == "/tools") {
            auto names = tools_.tool_names();
            std::cout << "Available tools (" << names.size() << "):\n";
            for (auto& n : names) std::cout << "  - " << n << "\n";
            continue;
        }
        if (line == "/help") {
            std::cout << "Chat commands:\n"
                      << "  /new [model]  Reset session (optionally switch model)\n"
                      << "  /status       Show token usage and session config\n"
                      << "  /model <name> Switch model\n"
                      << "  /context      Show context window breakdown\n"
                      << "  /compact      Force context compaction\n"
                      << "  /tools        List available tools\n"
                      << "  /help         Show this help\n"
                      << "  exit/quit/:q  Exit\n";
            continue;
        }

        std::string reply = run(line);
        std::cout << reply << "\n";
    }

    // Fire agent_stop hook
    hooks_.fire(HookType::agent_stop);
    std::cout << "\nBye.\n";
}

void Agent::teammate_loop(const std::string& initial_prompt) {
    auto cfg = team_->get_config();
    std::cerr << "[teammate:" << my_name_ << "] Started\n";

    if (!initial_prompt.empty()) {
        std::cerr << "[teammate:" << my_name_ << "] Processing initial prompt...\n";
        std::string result = run(initial_prompt);
        team_->send_message(my_name_, cfg.lead_name, result,
                            result.substr(0, std::min<size_t>(60, result.size())));
    }

    auto send_idle = [&]() {
        nlohmann::json idle;
        idle["type"] = "idle_notification";
        idle["from"] = my_name_;
        idle["idleReason"] = "available";
        team_->send_message(my_name_, cfg.lead_name, idle.dump(), "Idle");
    };
    send_idle();

    int idle_cycles = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        auto unread = team_->read_unread(my_name_);
        if (unread.empty()) {
            idle_cycles++;
            if (idle_cycles >= 15) {
                send_idle();
                idle_cycles = 0;
            }
            continue;
        }
        idle_cycles = 0;

        for (auto& msg : unread) {
            try {
                auto j = nlohmann::json::parse(msg.text);
                if (j.contains("type") && j["type"] == "shutdown_request") {
                    nlohmann::json approved;
                    approved["type"] = "shutdown_approved";
                    approved["from"] = my_name_;
                    team_->send_message(my_name_, msg.from, approved.dump(), "Shutdown approved");
                    std::cerr << "[teammate:" << my_name_ << "] Shutting down\n";
                    return;
                }
            } catch (...) {}

            std::cerr << "[teammate:" << my_name_ << "] Message from " << msg.from << "\n";
            std::string context = "[Message from " + msg.from + "]: " + msg.text;
            std::string reply = run(context);

            team_->send_message(my_name_, msg.from, reply,
                                reply.substr(0, std::min<size_t>(60, reply.size())));
        }

        send_idle();
    }
}

int cmd_agent(const std::string& message, bool no_markdown, bool logs,
              const std::string& team_name, const std::string& agent_name,
              const std::string& model_override) {
    Config cfg = Config::load(default_config_path());
    if (!model_override.empty()) cfg.model = model_override;

    auto team = std::make_shared<TeamManager>();
    std::string my_name = agent_name.empty() ? "team-lead" : agent_name;
    bool is_teammate = !team_name.empty() && !agent_name.empty();

    if (is_teammate) {
        if (!team->load_team(team_name)) {
            std::cerr << "[error] Could not load team '" << team_name << "'\n";
            return 1;
        }
    }

    ToolRegistry tools;
    register_exec_tool(tools, cfg);
    register_fs_tools(tools, cfg);
    register_cron_tool(tools, cfg.workspace_path() + "/cron/cron.db");
    register_subagent_tool(tools, cfg);
    if (is_teammate || !team_name.empty()) {
        register_team_tools(tools, team, my_name);
    }

    // Create memory search store early (needed by both memory tools)
    auto search_store = std::make_shared<MemorySearchStore>(
        cfg.workspace_path() + "/memory/search.db", cfg.embedding.dimensions);

    // Register memory tool with search store (auto-indexes on save, FTS5 only)
    register_memory_tool(tools, cfg.workspace_path(), search_store, nullptr, nullptr);

    auto skills = std::make_shared<SkillsLoader>(cfg.workspace_path());
    skills->discover();

    McpManager mcp(cfg.mcp_servers);
    mcp.connect_all();
    mcp.register_tools(tools);

    Agent agent(cfg, tools);
    agent.set_team(team, my_name);
    agent.set_skills(skills);

    // Now register memory_search tool with provider chain (Agent is constructed)
    register_memory_search_tool(tools, search_store, &agent.provider_chain(), cfg.embedding);

    if (is_teammate) {
        std::string prompt_file = team->prompts_dir() + "/" + agent_name + ".txt";
        std::string initial_prompt = read_file(prompt_file);
        agent.teammate_loop(initial_prompt);
    } else if (message.empty()) {
        agent.interactive_loop(no_markdown, logs);
    } else {
        std::string reply = agent.run(message);
        std::cout << reply << "\n";
    }

    return 0;
}

} // namespace minidragon
