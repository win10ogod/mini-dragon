#include "memory_tool.hpp"
#include "../memory.hpp"
#include <memory>

namespace minidragon {

void register_memory_tool(ToolRegistry& reg, const std::string& workspace,
                           std::shared_ptr<MemorySearchStore> search_store,
                           ProviderChain* provider_chain,
                           const EmbeddingConfig* embedding_cfg) {
    auto store = std::make_shared<MemoryStore>(workspace);

    ToolDef td;
    td.name = "memory";
    td.description = "Save/recall memories.";
    td.parameters = nlohmann::json::parse(R"JSON({
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["save", "recall", "long_term_save", "long_term_read"]
            },
            "content": {"type": "string"},
            "days": {"type": "integer"}
        },
        "required": ["action"]
    })JSON");

    // Helper lambda to auto-index into search store
    auto index_memory = [search_store, provider_chain, embedding_cfg](
            const std::string& content, const std::string& source) {
        if (!search_store) return;
        std::vector<float> embedding;
        if (embedding_cfg && embedding_cfg->enabled && provider_chain) {
            try {
                auto resp = provider_chain->embed({content}, embedding_cfg->model);
                if (!resp.embeddings.empty()) {
                    embedding = std::move(resp.embeddings[0]);
                }
            } catch (...) {
                // Embedding failed — index without vector
            }
        }
        search_store->upsert(content, source, embedding);
    };

    td.func = [store, index_memory](const nlohmann::json& args) -> std::string {
        std::string action = args.value("action", "");

        if (action == "save") {
            std::string content = args.value("content", "");
            if (content.empty()) return "[error] content is required for save action";
            store->append_today(content);
            index_memory(content, "daily:" + today_str());
            return "Memory saved for today.";
        }
        else if (action == "recall") {
            int days = args.value("days", 7);
            std::string result = store->get_recent(days);
            return result.empty() ? "No memories found for the last " + std::to_string(days) + " days." : result;
        }
        else if (action == "long_term_save") {
            std::string content = args.value("content", "");
            if (content.empty()) return "[error] content is required for long_term_save action";
            store->write_long_term(content);
            index_memory(content, "long_term");
            return "Long-term memory saved.";
        }
        else if (action == "long_term_read") {
            std::string result = store->read_long_term();
            return result.empty() ? "No long-term memory found." : result;
        }

        return "[error] Unknown action: " + action;
    };

    reg.register_tool(std::move(td));
}

} // namespace minidragon
