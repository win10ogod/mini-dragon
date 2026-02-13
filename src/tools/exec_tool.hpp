#pragma once
#include "../tool_registry.hpp"
#include "../config.hpp"

namespace minidragon {
void register_exec_tool(ToolRegistry& reg, const Config& cfg);
} // namespace minidragon
