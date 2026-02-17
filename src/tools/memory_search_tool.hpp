#pragma once
#include "../tool_registry.hpp"
#include "../memory_search.hpp"
#include "../provider_chain.hpp"
#include "../config.hpp"
#include <memory>
#include <string>

namespace minidragon {
void register_memory_search_tool(ToolRegistry& reg,
                                  std::shared_ptr<MemorySearchStore> search_store,
                                  ProviderChain* provider_chain,
                                  const EmbeddingConfig& embedding_cfg);
} // namespace minidragon
