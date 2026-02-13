#pragma once
#include "config.hpp"
#include "tool_registry.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

namespace minidragon {

class McpClient {
public:
    McpClient(const std::string& name, const McpServerConfig& cfg);
    ~McpClient();

    bool connect();
    void disconnect();

    std::vector<ToolDef> list_tools();
    std::string call_tool(const std::string& tool_name, const nlohmann::json& args);

    const std::string& name() const { return name_; }
    bool connected() const { return connected_; }

private:
    std::string name_;
    McpServerConfig config_;
    bool connected_ = false;
    int next_id_ = 1;

#ifdef _WIN32
    HANDLE child_process_ = INVALID_HANDLE_VALUE;
    HANDLE stdin_write_ = INVALID_HANDLE_VALUE;
    HANDLE stdout_read_ = INVALID_HANDLE_VALUE;
#else
    pid_t child_pid_ = -1;
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
#endif

    nlohmann::json send_request(const std::string& method, const nlohmann::json& params);
    void send_notification(const std::string& method, const nlohmann::json& params = {});
    std::string read_line();
    void write_line(const std::string& json_str);
};

} // namespace minidragon
