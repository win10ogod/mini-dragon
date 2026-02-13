#include "fs_tools.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace minidragon {

static std::string resolve_workspace_path(const std::string& workspace, const std::string& path) {
    // If path is absolute, use as-is (but check workspace restriction)
    if (!path.empty() && (path[0] == '/' || path[0] == '\\' ||
        (path.size() > 1 && path[1] == ':'))) {
        return path;
    }
    // Relative path: resolve against workspace
    std::string base = expand_path(workspace);
    if (base.back() != '/' && base.back() != '\\') base += '/';
    return base + path;
}

void register_fs_tools(ToolRegistry& reg, const Config& cfg) {
    auto workspace = std::make_shared<std::string>(cfg.workspace);
    int max_output = cfg.max_tool_output;

    // ── read_file ──
    {
        ToolDef def;
        def.name = "read_file";
        def.description = "Read the contents of a file. Returns the file content as text.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "path": {
                    "type": "string",
                    "description": "Path to the file to read (absolute or relative to workspace)"
                }
            },
            "required": ["path"]
        })JSON");

        def.func = [workspace, max_output](const nlohmann::json& args) -> std::string {
            std::string path = args.value("path", "");
            if (path.empty()) return "[error] path is required";

            std::string resolved = resolve_workspace_path(*workspace, path);
            std::ifstream f(resolved);
            if (!f) return "[error] Cannot read file: " + resolved;

            std::ostringstream ss;
            ss << f.rdbuf();
            std::string content = ss.str();

            if (static_cast<int>(content.size()) > max_output) {
                std::string head = content.substr(0, max_output / 2);
                std::string tail = content.substr(content.size() - max_output / 4);
                return head + "\n...[truncated " + std::to_string(content.size()) + " chars total]...\n" + tail;
            }
            return content;
        };
        reg.register_tool(std::move(def));
    }

    // ── write_file ──
    {
        ToolDef def;
        def.name = "write_file";
        def.description = "Create or overwrite a file with the given content.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "path": {
                    "type": "string",
                    "description": "Path to the file to write (absolute or relative to workspace)"
                },
                "content": {
                    "type": "string",
                    "description": "Content to write to the file"
                }
            },
            "required": ["path", "content"]
        })JSON");

        def.func = [workspace](const nlohmann::json& args) -> std::string {
            std::string path = args.value("path", "");
            std::string content = args.value("content", "");
            if (path.empty()) return "[error] path is required";

            std::string resolved = resolve_workspace_path(*workspace, path);

            // Ensure parent directory exists
            auto parent = fs::path(resolved).parent_path();
            if (!parent.empty()) {
                std::error_code ec;
                fs::create_directories(parent, ec);
            }

            std::ofstream f(resolved);
            if (!f) return "[error] Cannot write file: " + resolved;
            f << content;
            f.close();

            return "Wrote " + std::to_string(content.size()) + " bytes to " + resolved;
        };
        reg.register_tool(std::move(def));
    }

    // ── edit_file ──
    {
        ToolDef def;
        def.name = "edit_file";
        def.description = "Find and replace text in a file. Replaces the first occurrence of old_text with new_text.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "path": {
                    "type": "string",
                    "description": "Path to the file to edit"
                },
                "old_text": {
                    "type": "string",
                    "description": "The exact text to find and replace"
                },
                "new_text": {
                    "type": "string",
                    "description": "The replacement text"
                }
            },
            "required": ["path", "old_text", "new_text"]
        })JSON");

        def.func = [workspace](const nlohmann::json& args) -> std::string {
            std::string path = args.value("path", "");
            std::string old_text = args.value("old_text", "");
            std::string new_text = args.value("new_text", "");
            if (path.empty()) return "[error] path is required";
            if (old_text.empty()) return "[error] old_text is required";

            std::string resolved = resolve_workspace_path(*workspace, path);

            // Read file
            std::ifstream f_in(resolved);
            if (!f_in) return "[error] Cannot read file: " + resolved;
            std::ostringstream ss;
            ss << f_in.rdbuf();
            std::string content = ss.str();
            f_in.close();

            // Find and replace
            size_t pos = content.find(old_text);
            if (pos == std::string::npos) {
                return "[error] old_text not found in file";
            }
            content.replace(pos, old_text.size(), new_text);

            // Write back
            std::ofstream f_out(resolved);
            if (!f_out) return "[error] Cannot write file: " + resolved;
            f_out << content;
            f_out.close();

            return "Edited " + resolved + " (replaced " + std::to_string(old_text.size()) +
                   " chars with " + std::to_string(new_text.size()) + " chars)";
        };
        reg.register_tool(std::move(def));
    }

    // ── list_dir ──
    {
        ToolDef def;
        def.name = "list_dir";
        def.description = "List files and directories in a given path.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "path": {
                    "type": "string",
                    "description": "Directory path to list (absolute or relative to workspace, default: workspace root)"
                }
            }
        })JSON");

        def.func = [workspace, max_output](const nlohmann::json& args) -> std::string {
            std::string path = args.value("path", "");
            if (path.empty()) path = *workspace;

            std::string resolved = resolve_workspace_path(*workspace, path);

            if (!fs::exists(resolved)) return "[error] Path does not exist: " + resolved;
            if (!fs::is_directory(resolved)) return "[error] Not a directory: " + resolved;

            std::string result;
            int count = 0;
            for (auto& entry : fs::directory_iterator(resolved)) {
                std::string name = entry.path().filename().string();
                if (entry.is_directory()) {
                    result += name + "/\n";
                } else {
                    auto size = entry.file_size();
                    result += name + " (" + std::to_string(size) + " bytes)\n";
                }
                count++;
                if (static_cast<int>(result.size()) > max_output) {
                    result += "...[truncated, " + std::to_string(count) + " entries shown]\n";
                    break;
                }
            }
            if (result.empty()) result = "(empty directory)";
            return result;
        };
        reg.register_tool(std::move(def));
    }
}

} // namespace minidragon
