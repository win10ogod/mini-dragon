#include "mcp_client.hpp"
#include <iostream>
#include <cstring>
#include <sstream>

#ifdef _WIN32
// Windows implementation
#else
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#endif

namespace minidragon {

McpClient::McpClient(const std::string& name, const McpServerConfig& cfg)
    : name_(name), config_(cfg) {}

McpClient::~McpClient() {
    disconnect();
}

#ifdef _WIN32

bool McpClient::connect() {
    if (config_.command.empty()) {
        std::cerr << "[mcp:" << name_ << "] No command specified\n";
        return false;
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE stdin_read, stdin_write, stdout_read, stdout_write;

    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) return false;
    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        return false;
    }

    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = stdout_write;

    PROCESS_INFORMATION pi = {};

    // Build command line
    std::string cmdline = config_.command;
    for (auto& arg : config_.args) {
        cmdline += " " + arg;
    }

    // Set environment
    std::string env_block;
    if (!config_.env.empty()) {
        for (auto& [k, v] : config_.env) {
            env_block += k + "=" + v;
            env_block.push_back('\0');
        }
        env_block.push_back('\0');
    }

    BOOL ok = CreateProcessA(
        nullptr, const_cast<char*>(cmdline.c_str()),
        nullptr, nullptr, TRUE, 0,
        config_.env.empty() ? nullptr : const_cast<char*>(env_block.c_str()),
        nullptr, &si, &pi
    );

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);

    if (!ok) {
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        std::cerr << "[mcp:" << name_ << "] Failed to create process\n";
        return false;
    }

    child_process_ = pi.hProcess;
    CloseHandle(pi.hThread);
    stdin_write_ = stdin_write;
    stdout_read_ = stdout_read;

    // Send initialize
    auto init_result = send_request("initialize", {
        {"protocolVersion", "2025-06-18"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", {{"name", "minidragon"}, {"version", "1.0"}}}
    });

    if (init_result.is_null() || init_result.contains("error")) {
        std::cerr << "[mcp:" << name_ << "] Initialize failed\n";
        disconnect();
        return false;
    }

    // Send initialized notification
    send_notification("notifications/initialized");
    connected_ = true;
    return true;
}

void McpClient::disconnect() {
    connected_ = false;
    if (stdin_write_ != INVALID_HANDLE_VALUE) {
        CloseHandle(stdin_write_);
        stdin_write_ = INVALID_HANDLE_VALUE;
    }
    if (stdout_read_ != INVALID_HANDLE_VALUE) {
        CloseHandle(stdout_read_);
        stdout_read_ = INVALID_HANDLE_VALUE;
    }
    if (child_process_ != INVALID_HANDLE_VALUE) {
        TerminateProcess(child_process_, 0);
        WaitForSingleObject(child_process_, 3000);
        CloseHandle(child_process_);
        child_process_ = INVALID_HANDLE_VALUE;
    }
}

std::string McpClient::read_line() {
    std::string line;
    char ch;
    DWORD bytes_read;
    while (ReadFile(stdout_read_, &ch, 1, &bytes_read, nullptr) && bytes_read > 0) {
        if (ch == '\n') break;
        if (ch != '\r') line += ch;
    }
    return line;
}

void McpClient::write_line(const std::string& json_str) {
    std::string line = json_str + "\n";
    DWORD written;
    WriteFile(stdin_write_, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
    FlushFileBuffers(stdin_write_);
}

#else // POSIX

bool McpClient::connect() {
    if (config_.command.empty()) {
        std::cerr << "[mcp:" << name_ << "] No command specified\n";
        return false;
    }

    int pipe_stdin[2], pipe_stdout[2];
    if (pipe(pipe_stdin) != 0 || pipe(pipe_stdout) != 0) {
        std::cerr << "[mcp:" << name_ << "] Failed to create pipes\n";
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[mcp:" << name_ << "] Fork failed\n";
        close(pipe_stdin[0]); close(pipe_stdin[1]);
        close(pipe_stdout[0]); close(pipe_stdout[1]);
        return false;
    }

    if (pid == 0) {
        // Child process
        close(pipe_stdin[1]);
        close(pipe_stdout[0]);
        dup2(pipe_stdin[0], STDIN_FILENO);
        dup2(pipe_stdout[1], STDOUT_FILENO);
        dup2(pipe_stdout[1], STDERR_FILENO);
        close(pipe_stdin[0]);
        close(pipe_stdout[1]);

        // Set environment variables
        for (auto& [k, v] : config_.env) {
            setenv(k.c_str(), v.c_str(), 1);
        }

        // Build argv
        std::vector<const char*> argv;
        argv.push_back(config_.command.c_str());
        for (auto& arg : config_.args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        execvp(config_.command.c_str(), const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    // Parent process
    close(pipe_stdin[0]);
    close(pipe_stdout[1]);

    child_pid_ = pid;
    stdin_fd_ = pipe_stdin[1];
    stdout_fd_ = pipe_stdout[0];

    // Send initialize
    auto init_result = send_request("initialize", {
        {"protocolVersion", "2025-06-18"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", {{"name", "minidragon"}, {"version", "1.0"}}}
    });

    if (init_result.is_null() || init_result.contains("error")) {
        std::cerr << "[mcp:" << name_ << "] Initialize failed\n";
        disconnect();
        return false;
    }

    // Send initialized notification
    send_notification("notifications/initialized");
    connected_ = true;
    return true;
}

void McpClient::disconnect() {
    connected_ = false;
    if (stdin_fd_ >= 0) {
        close(stdin_fd_);
        stdin_fd_ = -1;
    }
    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }
    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        int status;
        // Wait up to 3 seconds
        for (int i = 0; i < 30; i++) {
            if (waitpid(child_pid_, &status, WNOHANG) != 0) break;
            usleep(100000);
        }
        // Force kill if still running
        if (waitpid(child_pid_, &status, WNOHANG) == 0) {
            kill(child_pid_, SIGKILL);
            waitpid(child_pid_, &status, 0);
        }
        child_pid_ = -1;
    }
}

std::string McpClient::read_line() {
    std::string line;
    char ch;

    // Use poll with a 30s timeout
    struct pollfd pfd;
    pfd.fd = stdout_fd_;
    pfd.events = POLLIN;

    while (true) {
        int ret = poll(&pfd, 1, 30000);
        if (ret <= 0) break; // timeout or error

        ssize_t n = read(stdout_fd_, &ch, 1);
        if (n <= 0) break;
        if (ch == '\n') break;
        if (ch != '\r') line += ch;
    }
    return line;
}

void McpClient::write_line(const std::string& json_str) {
    std::string line = json_str + "\n";
    ssize_t total = 0;
    while (total < static_cast<ssize_t>(line.size())) {
        ssize_t n = write(stdin_fd_, line.c_str() + total, line.size() - total);
        if (n <= 0) break;
        total += n;
    }
}

#endif // _WIN32 / POSIX

// ── Common methods ──

nlohmann::json McpClient::send_request(const std::string& method, const nlohmann::json& params) {
    int id = next_id_++;
    nlohmann::json req = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method}
    };
    if (!params.is_null() && !params.empty()) {
        req["params"] = params;
    }

    write_line(req.dump());

    // Read response lines until we get one with matching id
    for (int attempt = 0; attempt < 100; attempt++) {
        std::string line = read_line();
        if (line.empty()) continue;

        try {
            auto resp = nlohmann::json::parse(line);
            // Skip notifications (no "id" field)
            if (!resp.contains("id")) continue;
            if (resp["id"].get<int>() == id) {
                if (resp.contains("result")) return resp["result"];
                if (resp.contains("error")) return resp;
                return resp;
            }
        } catch (...) {
            continue;
        }
    }
    return nlohmann::json();
}

void McpClient::send_notification(const std::string& method, const nlohmann::json& params) {
    nlohmann::json notif = {
        {"jsonrpc", "2.0"},
        {"method", method}
    };
    if (!params.is_null() && !params.empty()) {
        notif["params"] = params;
    }
    write_line(notif.dump());
}

std::vector<ToolDef> McpClient::list_tools() {
    std::vector<ToolDef> tools;

    auto result = send_request("tools/list", nlohmann::json::object());
    if (result.is_null() || !result.contains("tools")) return tools;

    for (auto& t : result["tools"]) {
        ToolDef td;
        td.name = t.value("name", "");
        td.description = t.value("description", "");
        if (t.contains("inputSchema")) {
            td.parameters = t["inputSchema"];
        } else {
            td.parameters = {{"type", "object"}, {"properties", nlohmann::json::object()}};
        }
        tools.push_back(std::move(td));
    }
    return tools;
}

std::string McpClient::call_tool(const std::string& tool_name, const nlohmann::json& args) {
    auto result = send_request("tools/call", {
        {"name", tool_name},
        {"arguments", args}
    });

    if (result.is_null()) return "[error] MCP tool call returned null";

    // Extract text content from result
    if (result.contains("content") && result["content"].is_array()) {
        std::string output;
        for (auto& item : result["content"]) {
            if (item.value("type", "") == "text") {
                if (!output.empty()) output += "\n";
                output += item.value("text", "");
            }
        }
        return output.empty() ? result.dump() : output;
    }

    if (result.contains("error")) {
        auto& err = result["error"];
        return "[error] MCP: " + err.value("message", result.dump());
    }

    return result.dump();
}

} // namespace minidragon
