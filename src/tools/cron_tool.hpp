#pragma once
#include "../tool_registry.hpp"
#include <string>

namespace minidragon {
void register_cron_tool(ToolRegistry& reg, const std::string& db_path);
} // namespace minidragon
