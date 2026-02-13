#pragma once
#include "../tool_registry.hpp"
#include <string>

namespace minidragon {
void register_memory_tool(ToolRegistry& reg, const std::string& workspace);
} // namespace minidragon
