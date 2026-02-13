#include "memory_tool.hpp"
#include "../memory.hpp"
#include <memory>

namespace minidragon {

void register_memory_tool(ToolRegistry& reg, const std::string& workspace) {
    auto store = std::make_shared<MemoryStore>(workspace);

    ToolDef td;
    td.name = "memory";
    td.description = "Save and recall memories. Actions: save (append to today), recall (get recent N days), long_term_save, long_term_read";
    td.parameters = nlohmann::json::parse(R"JSON({
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["save", "recall", "long_term_save", "long_term_read"]
            },
            "content": {
                "type": "string",
                "description": "Content to save (required for save/long_term_save)"
            },
            "days": {
                "type": "integer",
                "description": "Number of days to recall (default 7)"
            }
        },
        "required": ["action"]
    })JSON");

    td.func = [store](const nlohmann::json& args) -> std::string {
        std::string action = args.value("action", "");

        if (action == "save") {
            std::string content = args.value("content", "");
            if (content.empty()) return "[error] content is required for save action";
            store->append_today(content);
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
