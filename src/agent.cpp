#include "agent.hpp"
#include "tools/exec_tool.hpp"
#include "tools/fs_tools.hpp"
#include "tools/cron_tool.hpp"
#include "tools/memory_tool.hpp"
#include "tools/subagent_tool.hpp"
#include "tools/team_tools.hpp"
#include "mcp_manager.hpp"
#include "memory.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace minidragon {

Agent::Agent(const Config& config, ToolRegistry& tools)
    : config_(config)
    , tools_(tools)
    , session_(config.workspace_path() + "/sessions")
    , provider_(config.resolve_provider())
{}

void Agent::set_team(std::shared_ptr<TeamManager> team, const std::string& my_name) {
    team_ = std::move(team);
    my_name_ = my_name;
}

void Agent::set_skills(std::shared_ptr<SkillsLoader> skills) {
    skills_ = std::move(skills);
}

std::string Agent::build_system_prompt() {
    std::string ws = config_.workspace_path();

    // Check for BOOTSTRAP.md — first-run onboarding takes priority
    std::string bootstrap = read_file(ws + "/BOOTSTRAP.md");
    bool is_bootstrap = !bootstrap.empty();

    std::string prompt;
    if (is_bootstrap) {
        prompt = "You are a brand new AI agent, just coming online for the first time.\n"
                 "You have tools available to read and write files in your workspace.\n\n"
                 "--- BOOTSTRAP.md ---\n" + bootstrap + "\n\n";
    } else {
        prompt = "";
    }

    // Load identity files (order matters: SOUL → IDENTITY → USER → AGENTS → TOOLS)
    for (auto& name : {"SOUL.md", "IDENTITY.md", "USER.md", "AGENTS.md", "TOOLS.md"}) {
        std::string content = read_file(ws + "/" + name);
        if (!content.empty()) {
            prompt += "--- " + std::string(name) + " ---\n" + content + "\n\n";
        }
    }

    // Memory: MEMORY.md + today + yesterday
    std::string mem_dir = ws + "/memory";
    std::string memory_md = read_file(ws + "/MEMORY.md");
    if (!memory_md.empty()) {
        prompt += "--- MEMORY.md ---\n" + memory_md + "\n\n";
    }
    std::string today_mem = read_file(mem_dir + "/" + today_str() + ".md");
    if (!today_mem.empty()) {
        prompt += "--- Memory: " + today_str() + " ---\n" + today_mem + "\n\n";
    }

    // Team context
    if (team_ && team_->team_exists()) {
        auto cfg = team_->get_config();
        prompt += "--- Team Context ---\n";
        prompt += "You are '" + my_name_ + "' in team '" + cfg.display_name + "'.\n";
        prompt += "Team lead: " + cfg.lead_name + "\n";
        prompt += "Members: ";
        for (auto& m : cfg.members) {
            prompt += m.name + " (" + m.agent_type + "), ";
        }
        prompt += "\n";
        if (my_name_ == cfg.lead_name) {
            prompt += "You are the TEAM LEAD. Coordinate work, spawn teammates, assign tasks.\n";
            prompt += "Use team tools: team_create, team_spawn, team_send, team_shutdown, team_cleanup.\n";
        } else {
            prompt += "You are a TEAMMATE. Complete your assigned work and report results.\n";
            prompt += "Use team_send to communicate with the lead or other teammates.\n";
        }
        prompt += "Use task_create, task_update, task_list to manage shared tasks.\n";
        prompt += "Use inbox_check to read messages from teammates.\n\n";
    }

    // Skills: always-loaded skills get full content, others get summary
    if (skills_) {
        std::string always_content = skills_->build_always_skills_content();
        if (!always_content.empty()) {
            prompt += "--- Active Skills ---\n" + always_content + "\n\n";
        }

        std::string summary = skills_->build_skills_summary();
        if (!summary.empty()) {
            prompt += summary + "\n";
        }
    }

    return prompt;
}

// Truncate a tool result to keep context bounded
static std::string truncate_output(const std::string& output, int max_len) {
    if (max_len <= 0 || static_cast<int>(output.size()) <= max_len) return output;
    int head = max_len / 2;
    int tail = max_len / 4;
    return output.substr(0, head) +
           "\n...[truncated " + std::to_string(output.size()) + " chars]...\n" +
           output.substr(output.size() - tail);
}

// Compress old tool results in message history to reduce tokens
static void compress_old_tool_results(std::vector<Message>& messages, int keep_recent) {
    int tool_count = 0;
    for (int i = static_cast<int>(messages.size()) - 1; i >= 0; i--) {
        if (messages[i].role == "tool") tool_count++;
    }

    if (tool_count <= keep_recent) return;

    int seen = 0;
    for (int i = static_cast<int>(messages.size()) - 1; i >= 0; i--) {
        if (messages[i].role == "tool") {
            seen++;
            if (seen > keep_recent && messages[i].content.size() > 200) {
                std::string summary = "[tool result: " +
                    std::to_string(messages[i].content.size()) + " chars]";
                messages[i].content = summary;
            }
        }
    }
}

void Agent::inject_inbox_messages(std::vector<Message>& messages) {
    if (!team_ || !team_->team_exists()) return;

    auto unread = team_->read_unread(my_name_);
    for (auto& msg : unread) {
        // Parse protocol messages
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
                    // Teammate should handle this in teammate_loop
                    // If lead receives it somehow, just note it
                    Message m;
                    m.role = "user";
                    m.content = "[Team] Shutdown request from " + msg.from;
                    messages.push_back(m);
                    continue;
                }
            }
        } catch (...) {}

        // Skip idle notifications to avoid flooding context
        if (is_idle) continue;

        // Inject regular messages
        Message m;
        m.role = "user";
        m.content = "[Team message from " + msg.from + "]: " + msg.text;
        messages.push_back(m);
    }
}

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

    // Compress old tool results
    compress_old_tool_results(messages, 6);

    Message user_msg;
    user_msg.role = "user";
    user_msg.content = user_message;
    messages.push_back(user_msg);
    session_.log(user_msg);

    auto tools_spec = tools_.tools_spec();
    int iterations = 0;
    int max_iter = config_.max_iterations;

    while (iterations < max_iter) {
        // Inject inbox messages between turns
        inject_inbox_messages(messages);

        iterations++;

        ProviderResponse resp;
        try {
            resp = provider_.chat(messages, tools_spec,
                                  config_.model,
                                  config_.max_tokens,
                                  config_.temperature);
        } catch (const std::exception& e) {
            return std::string("[error] Provider call failed: ") + e.what();
        }

        if (!resp.has_tool_calls()) {
            Message assistant;
            assistant.role = "assistant";
            assistant.content = resp.content;
            messages.push_back(assistant);
            session_.log(assistant);
            return resp.content;
        }

        Message assistant;
        assistant.role = "assistant";
        assistant.content = resp.content;
        assistant.tool_calls = resp.tool_calls;
        messages.push_back(assistant);
        session_.log(assistant);

        for (auto& tc : resp.tool_calls) {
            std::string result;
            try {
                auto args = tc.arguments.empty() ? nlohmann::json::object() : nlohmann::json::parse(tc.arguments);
                result = tools_.execute(tc.name, args);
            } catch (const std::exception& e) {
                result = std::string("[error] ") + e.what();
            }

            result = truncate_output(result, config_.max_tool_output);

            Message tool_msg;
            tool_msg.role = "tool";
            tool_msg.tool_call_id = tc.id;
            tool_msg.content = result;
            messages.push_back(tool_msg);
            session_.log(tool_msg);
        }
    }

    return "[agent] Max tool iterations reached (" + std::to_string(max_iter) + ")";
}

void Agent::interactive_loop(bool no_markdown, bool logs) {
    (void)no_markdown;
    (void)logs;

    std::cout << "Mini Dragon agent (interactive mode, type exit/quit/:q or Ctrl+D to quit)\n";
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

        std::string reply = run(line);
        std::cout << reply << "\n";
    }
    std::cout << "\nBye.\n";
}

void Agent::teammate_loop(const std::string& initial_prompt) {
    auto cfg = team_->get_config();
    std::cerr << "[teammate:" << my_name_ << "] Started\n";

    // Process initial prompt
    if (!initial_prompt.empty()) {
        std::cerr << "[teammate:" << my_name_ << "] Processing initial prompt...\n";
        std::string result = run(initial_prompt);

        // Send result to lead
        team_->send_message(my_name_, cfg.lead_name, result,
                            result.substr(0, std::min<size_t>(60, result.size())));
    }

    // Send idle notification
    auto send_idle = [&]() {
        nlohmann::json idle;
        idle["type"] = "idle_notification";
        idle["from"] = my_name_;
        idle["idleReason"] = "available";
        team_->send_message(my_name_, cfg.lead_name, idle.dump(), "Idle");
    };
    send_idle();

    // Poll loop
    int idle_cycles = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        auto unread = team_->read_unread(my_name_);
        if (unread.empty()) {
            idle_cycles++;
            // Re-send idle every 30 seconds (15 cycles * 2s)
            if (idle_cycles >= 15) {
                send_idle();
                idle_cycles = 0;
            }
            continue;
        }
        idle_cycles = 0;

        for (auto& msg : unread) {
            // Check for shutdown request
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

            // Process regular message
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
    register_memory_tool(tools, cfg.workspace_path());
    register_subagent_tool(tools, cfg);
    register_team_tools(tools, team, my_name);

    // Skills: discover from workspace and global directories
    auto skills = std::make_shared<SkillsLoader>(cfg.workspace_path());
    skills->discover();

    // MCP servers
    McpManager mcp(cfg.mcp_servers);
    mcp.connect_all();
    mcp.register_tools(tools);

    Agent agent(cfg, tools);
    agent.set_team(team, my_name);
    agent.set_skills(skills);

    if (is_teammate) {
        // Read initial prompt from file
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
