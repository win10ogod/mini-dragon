#pragma once
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace minidragon {

using ToolFunction = std::function<std::string(const nlohmann::json&)>;

struct ToolDef {
    std::string name;
    std::string description;
    nlohmann::json parameters;
    ToolFunction func;
};

class ToolRegistry {
public:
    void register_tool(ToolDef def) {
        tools_[def.name] = std::move(def);
    }

    bool has(const std::string& name) const {
        return tools_.count(name) > 0;
    }

    std::string execute(const std::string& name, const nlohmann::json& args) const {
        auto it = tools_.find(name);
        if (it == tools_.end()) {
            throw std::runtime_error("Unknown tool: " + name);
        }
        return it->second.func(args);
    }

    nlohmann::json tools_spec() const {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& [name, def] : tools_) {
            arr.push_back({
                {"type", "function"},
                {"function", {
                    {"name", def.name},
                    {"description", def.description},
                    {"parameters", def.parameters}
                }}
            });
        }
        return arr;
    }

    std::vector<std::string> tool_names() const {
        std::vector<std::string> names;
        for (auto& [n, _] : tools_) names.push_back(n);
        return names;
    }

private:
    std::map<std::string, ToolDef> tools_;
};

} // namespace minidragon
