#pragma once
#include "../tool_registry.hpp"
#include "../config.hpp"
#include <string>

namespace minidragon {
void register_subagent_tool(ToolRegistry& reg, const Config& cfg);
} // namespace minidragon
