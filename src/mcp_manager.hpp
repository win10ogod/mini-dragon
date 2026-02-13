#pragma once
#include "mcp_client.hpp"
#include "tool_registry.hpp"
#include <map>
#include <memory>
#include <iostream>

namespace minidragon {

class McpManager {
public:
    explicit McpManager(const std::map<std::string, McpServerConfig>& servers) {
        for (auto& [name, cfg] : servers) {
            clients_[name] = std::make_unique<McpClient>(name, cfg);
        }
    }

    ~McpManager() { disconnect_all(); }

    void connect_all() {
        for (auto& [name, client] : clients_) {
            if (client->connect()) {
                std::cerr << "[mcp] Connected to server: " << name << "\n";
            } else {
                std::cerr << "[mcp] Failed to connect to server: " << name << "\n";
            }
        }
    }

    void disconnect_all() {
        for (auto& [name, client] : clients_) {
            if (client->connected()) {
                client->disconnect();
                std::cerr << "[mcp] Disconnected from server: " << name << "\n";
            }
        }
    }

    void register_tools(ToolRegistry& reg) {
        for (auto& [server_name, client] : clients_) {
            if (!client->connected()) continue;

            auto tools = client->list_tools();
            for (auto& tool : tools) {
                std::string prefixed_name = "mcp_" + server_name + "_" + tool.name;
                auto client_ptr = client.get();
                std::string orig_name = tool.name;

                ToolDef def;
                def.name = prefixed_name;
                def.description = "[MCP:" + server_name + "] " + tool.description;
                def.parameters = tool.parameters;
                def.func = [client_ptr, orig_name](const nlohmann::json& args) -> std::string {
                    return client_ptr->call_tool(orig_name, args);
                };
                reg.register_tool(std::move(def));
                std::cerr << "[mcp] Registered tool: " << prefixed_name << "\n";
            }
        }
    }

    size_t server_count() const { return clients_.size(); }
    size_t connected_count() const {
        size_t n = 0;
        for (auto& [_, c] : clients_) if (c->connected()) n++;
        return n;
    }

    std::vector<std::string> server_names() const {
        std::vector<std::string> names;
        for (auto& [n, _] : clients_) names.push_back(n);
        return names;
    }

private:
    std::map<std::string, std::unique_ptr<McpClient>> clients_;
};

} // namespace minidragon
