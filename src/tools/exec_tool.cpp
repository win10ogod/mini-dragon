#include "exec_tool.hpp"
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace minidragon {

// Dangerous patterns to block (no regex — simple string matching)
static bool is_dangerous(const std::string& cmd) {
    // Block destructive system commands
    static const std::vector<std::string> blocked = {
        "rm -rf /", "rm -rf /*", "mkfs", "format c:", "format d:",
        "shutdown", "reboot", "halt", "poweroff",
        "dd if=", ":(){ :|:& };:", "fork bomb"
    };
    std::string lower = cmd;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto& b : blocked) {
        if (lower.find(b) != std::string::npos) return true;
    }
    // Block "rm -rf /" variants: look for rm with -r and -f flags followed by /
    // Simple heuristic: check if cmd contains "rm" and "-rf" or "-r -f" and starts with "/"
    if (lower.find("rm ") != std::string::npos || lower.find("rm\t") != std::string::npos) {
        bool has_r = lower.find("-r") != std::string::npos;
        bool has_f = lower.find("-f") != std::string::npos;
        if (has_r && has_f) {
            // Check if any argument starts with / (root path)
            size_t rm_pos = lower.find("rm");
            std::string after_rm = lower.substr(rm_pos);
            // Find the first path argument after flags
            size_t i = 2; // skip "rm"
            while (i < after_rm.size()) {
                // skip whitespace
                while (i < after_rm.size() && (after_rm[i] == ' ' || after_rm[i] == '\t')) i++;
                if (i >= after_rm.size()) break;
                if (after_rm[i] == '-') {
                    // skip flag
                    while (i < after_rm.size() && after_rm[i] != ' ' && after_rm[i] != '\t') i++;
                } else if (after_rm[i] == '/') {
                    return true; // rm -rf with root path
                } else {
                    break; // non-root path, ok
                }
            }
        }
    }
    return false;
}

static std::string exec_command(const std::string& cmd, const std::string& working_dir, int timeout_sec, int max_output) {
    std::string full_cmd;
    if (!working_dir.empty()) {
        full_cmd = "cd " + working_dir + " && ";
    }
    full_cmd += cmd + " 2>&1";

    // Wrap with timeout on POSIX
#ifndef _WIN32
    if (timeout_sec > 0) {
        full_cmd = "timeout " + std::to_string(timeout_sec) + " sh -c '" + full_cmd + "'";
    }
#endif

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return "[error] Failed to execute command";

    std::string result;
    result.reserve(std::min(max_output + 1024, 1 << 20));
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
        if (static_cast<int>(result.size()) > max_output) {
            result += "\n...[truncated]";
            break;
        }
    }
    int status = pclose(pipe);

    // Normalize exit code
#ifndef _WIN32
    if (WIFEXITED(status)) {
        status = WEXITSTATUS(status);
    }
#endif

    result += "\n[exit code: " + std::to_string(status) + "]";
    return result;
}

void register_exec_tool(ToolRegistry& reg, const Config& cfg) {
    int max_output = cfg.max_tool_output;

    ToolDef def;
    def.name = "exec";
    def.description = "Run a shell command.";
    def.parameters = nlohmann::json::parse(R"JSON({
        "type": "object",
        "properties": {
            "command": {"type": "string"},
            "working_dir": {"type": "string"},
            "timeout": {"type": "integer"}
        },
        "required": ["command"]
    })JSON");

    def.func = [max_output](const nlohmann::json& args) -> std::string {
        std::string command = args.value("command", "");
        std::string working_dir = args.value("working_dir", "");
        int timeout = args.value("timeout", 60);
        if (timeout > 300) timeout = 300;
        if (timeout < 1) timeout = 60;

        if (command.empty()) return "[error] No command provided";

        if (is_dangerous(command)) {
            return "[error] Command blocked by security guard: potentially destructive operation";
        }

        return exec_command(command, working_dir, timeout, max_output);
    };

    reg.register_tool(std::move(def));
}

} // namespace minidragon
