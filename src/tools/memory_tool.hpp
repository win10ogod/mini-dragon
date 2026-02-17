#pragma once
#include "../tool_registry.hpp"
#include "../memory_search.hpp"
#include "../provider_chain.hpp"
#include "../config.hpp"
#include <string>
#include <memory>

namespace minidragon {
void register_memory_tool(ToolRegistry& reg, const std::string& workspace,
                           std::shared_ptr<MemorySearchStore> search_store = nullptr,
                           ProviderChain* provider_chain = nullptr,
                           const EmbeddingConfig* embedding_cfg = nullptr);
} // namespace minidragon
