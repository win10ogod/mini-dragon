#include "memory_search_tool.hpp"
#include <sstream>

namespace minidragon {

void register_memory_search_tool(ToolRegistry& reg,
                                  std::shared_ptr<MemorySearchStore> search_store,
                                  ProviderChain* provider_chain,
                                  const EmbeddingConfig& embedding_cfg) {
    ToolDef td;
    td.name = "memory_search";
    td.description = "Search through saved memories using hybrid text + vector search. "
                     "Returns the most relevant memory entries matching the query.";
    td.parameters = nlohmann::json::parse(R"JSON({
        "type": "object",
        "properties": {
            "query": {
                "type": "string",
                "description": "Search query to find relevant memories"
            },
            "limit": {
                "type": "integer",
                "description": "Maximum number of results to return (default: 5)"
            }
        },
        "required": ["query"]
    })JSON");

    td.func = [search_store, provider_chain, embedding_cfg](const nlohmann::json& args) -> std::string {
        std::string query = args.value("query", "");
        int limit = args.value("limit", 5);

        if (query.empty()) return "[error] query is required";

        std::vector<MemoryEntry> results;

        if (embedding_cfg.enabled && provider_chain) {
            // Hybrid search with embeddings
            try {
                auto emb_resp = provider_chain->embed({query}, embedding_cfg.model);
                if (!emb_resp.embeddings.empty()) {
                    results = search_store->search(query, emb_resp.embeddings[0], limit);
                } else {
                    results = search_store->search_text(query, limit);
                }
            } catch (const std::exception& e) {
                // Embedding failed, fall back to text search
                results = search_store->search_text(query, limit);
            }
        } else {
            // Text-only search
            results = search_store->search_text(query, limit);
        }

        if (results.empty()) {
            return "No matching memories found for: " + query;
        }

        std::ostringstream out;
        out << "Found " << results.size() << " matching memories:\n\n";
        for (size_t i = 0; i < results.size(); i++) {
            auto& r = results[i];
            out << "--- Result " << (i + 1) << " (score: " << r.score << ") ---\n";
            if (!r.source.empty()) out << "Source: " << r.source << "\n";
            out << r.content << "\n\n";
        }
        return out.str();
    };

    reg.register_tool(std::move(td));
}

} // namespace minidragon
