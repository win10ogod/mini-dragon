#pragma once
#include "../tool_registry.hpp"
#include "../team.hpp"
#include <memory>

namespace minidragon {

void register_team_tools(ToolRegistry& tools,
                         std::shared_ptr<TeamManager> team,
                         const std::string& my_name);

} // namespace minidragon
