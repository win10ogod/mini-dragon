#include "subagent_tool.hpp"
#include "../provider.hpp"
#include "../message.hpp"

namespace minidragon {

void register_subagent_tool(ToolRegistry& reg, const Config& cfg) {
    auto provider_cfg = std::make_shared<ProviderConfig>(cfg.resolve_provider());
    auto model = std::make_shared<std::string>(cfg.model);

    ToolDef td;
    td.name = "subagent";
    td.description = "Spawn a sub-agent for a task.";
    td.parameters = nlohmann::json::parse(R"JSON({
        "type": "object",
        "properties": {
            "task": {"type": "string"},
            "label": {"type": "string"}
        },
        "required": ["task"]
    })JSON");

    td.func = [provider_cfg, model](const nlohmann::json& args) -> std::string {
        std::string task = args.value("task", "");
        std::string label = args.value("label", "subtask");

        if (task.empty()) return "[error] task is required";

        try {
            Provider sub_provider(*provider_cfg);

            std::vector<Message> msgs;

            Message sys;
            sys.role = "system";
            sys.content = "You are a focused sub-agent. Complete the following task concisely and accurately. "
                          "Do not ask follow-up questions - just provide the best answer you can.";
            msgs.push_back(sys);

            Message user_msg;
            user_msg.role = "user";
            user_msg.content = task;
            msgs.push_back(user_msg);

            // No tools for subagent, limited tokens
            nlohmann::json no_tools = nlohmann::json::array();
            auto resp = sub_provider.chat(msgs, no_tools, *model, 1024, 0.5);

            std::string result = "[subagent:" + label + "] " + resp.content;
            return result;
        } catch (const std::exception& e) {
            return "[subagent:error] " + std::string(e.what());
        }
    };

    reg.register_tool(std::move(td));
}

} // namespace minidragon
