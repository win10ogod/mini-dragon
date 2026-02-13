#pragma once
#include "config.hpp"
#include "tool_registry.hpp"
#include "session.hpp"
#include "provider.hpp"
#include "team.hpp"
#include "skills_loader.hpp"
#include <string>
#include <memory>

namespace minidragon {

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

private:
    Config config_;
    ToolRegistry& tools_;
    SessionLogger session_;
    Provider provider_;

    // Team context (optional)
    std::shared_ptr<TeamManager> team_;
    std::string my_name_;

    // Skills (optional)
    std::shared_ptr<SkillsLoader> skills_;

    std::string build_system_prompt();
    void inject_inbox_messages(std::vector<Message>& messages);
};

int cmd_agent(const std::string& message, bool no_markdown, bool logs,
              const std::string& team_name = "",
              const std::string& agent_name = "",
              const std::string& model_override = "");

} // namespace minidragon
