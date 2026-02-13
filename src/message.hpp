#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace minidragon {

struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments; // JSON string
};

struct Message {
    std::string role;       // "system", "user", "assistant", "tool"
    std::string content;
    std::string tool_call_id;       // for role="tool"
    std::vector<ToolCall> tool_calls; // for role="assistant" with tool calls

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["role"] = role;
        if (!content.empty()) j["content"] = content;
        if (!tool_call_id.empty()) j["tool_call_id"] = tool_call_id;
        if (!tool_calls.empty()) {
            auto& arr = j["tool_calls"];
            for (auto& tc : tool_calls) {
                arr.push_back({
                    {"id", tc.id},
                    {"type", "function"},
                    {"function", {{"name", tc.name}, {"arguments", tc.arguments}}}
                });
            }
        }
        return j;
    }

    static Message from_json(const nlohmann::json& j) {
        Message m;
        m.role = j.value("role", "");
        m.content = j.value("content", "");
        m.tool_call_id = j.value("tool_call_id", "");
        if (j.contains("tool_calls") && j["tool_calls"].is_array()) {
            for (auto& tc : j["tool_calls"]) {
                ToolCall t;
                t.id = tc.value("id", "");
                if (tc.contains("function")) {
                    t.name = tc["function"].value("name", "");
                    t.arguments = tc["function"].value("arguments", "");
                }
                m.tool_calls.push_back(std::move(t));
            }
        }
        return m;
    }
};

} // namespace minidragon
