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

// Simple glob pattern matching: supports * and ** wildcards
static bool glob_match(const std::string& pattern, const std::string& name) {
    // Simple *.ext matching
    if (pattern.size() > 1 && pattern[0] == '*' && pattern.find('*', 1) == std::string::npos) {
        std::string suffix = pattern.substr(1);
        return name.size() >= suffix.size() &&
               name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    // Simple prefix* matching
    if (pattern.size() > 1 && pattern.back() == '*' && pattern.find('*') == pattern.size() - 1) {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return name.size() >= prefix.size() &&
               name.compare(0, prefix.size(), prefix) == 0;
    }
    // Exact match
    return name == pattern;
}

void register_fs_tools(ToolRegistry& reg, const Config& cfg) {
    auto workspace = std::make_shared<std::string>(cfg.workspace);
    int max_output = cfg.max_tool_output;

    // ── read_file ──
    {
        ToolDef def;
        def.name = "read_file";
        def.description = "Read file contents. Supports offset/limit for line ranges.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "path": {"type": "string"},
                "offset": {"type": "integer"},
                "limit": {"type": "integer"}
            },
            "required": ["path"]
        })JSON");

        def.func = [workspace, max_output](const nlohmann::json& args) -> std::string {
            std::string path = args.value("path", "");
            int offset = args.value("offset", 1);
            int limit = args.value("limit", 0);
            if (path.empty()) return "[error] path is required";
            if (offset < 1) offset = 1;

            std::string resolved = resolve_workspace_path(*workspace, path);
            std::ifstream f(resolved);
            if (!f) return "[error] Cannot read file: " + resolved;

            std::string result;
            std::string line;
            int line_num = 0;
            int collected = 0;
            int total_chars = 0;

            while (std::getline(f, line)) {
                line_num++;
                if (line_num < offset) continue;
                if (limit > 0 && collected >= limit) break;

                result += line;
                result += '\n';
                collected++;
                total_chars += static_cast<int>(line.size()) + 1;

                if (total_chars > max_output) {
                    result += "\n...[truncated at " + std::to_string(total_chars) + " chars, line " + std::to_string(line_num) + "]";
                    break;
                }
            }

            if (result.empty()) {
                if (line_num == 0) return "[error] Empty file: " + resolved;
                return "[error] Offset " + std::to_string(offset) + " beyond file (" + std::to_string(line_num) + " lines)";
            }
            return result;
        };
        reg.register_tool(std::move(def));
    }

    // ── write_file ──
    {
        ToolDef def;
        def.name = "write_file";
        def.description = "Create or overwrite a file.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "path": {"type": "string"},
                "content": {"type": "string"}
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
        def.description = "Find and replace text in a file.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "path": {"type": "string"},
                "old_text": {"type": "string"},
                "new_text": {"type": "string"}
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
        def.description = "List directory contents.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "path": {"type": "string"}
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

    // ── glob (file finder) ──
    {
        ToolDef def;
        def.name = "glob";
        def.description = "Find files matching a pattern (e.g. *.cpp, *.hpp).";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "pattern": {"type": "string"},
                "path": {"type": "string"}
            },
            "required": ["pattern"]
        })JSON");

        def.func = [workspace, max_output](const nlohmann::json& args) -> std::string {
            std::string pattern = args.value("pattern", "");
            std::string path = args.value("path", "");
            if (pattern.empty()) return "[error] pattern is required";
            if (path.empty()) path = *workspace;

            std::string resolved = resolve_workspace_path(*workspace, path);
            if (!fs::exists(resolved) || !fs::is_directory(resolved))
                return "[error] Directory does not exist: " + resolved;

            // Extract the filename pattern from path-like patterns (e.g. "src/**/*.cpp" -> dir="src", pat="*.cpp")
            std::string dir = resolved;
            std::string file_pattern = pattern;

            // Check if pattern contains path separators
            size_t last_sep = pattern.rfind('/');
            if (last_sep != std::string::npos) {
                std::string path_part = pattern.substr(0, last_sep);
                file_pattern = pattern.substr(last_sep + 1);
                // Remove ** from path part
                std::string clean_path;
                for (size_t i = 0; i < path_part.size(); i++) {
                    if (path_part[i] == '*') continue;
                    clean_path += path_part[i];
                }
                // Remove leading/trailing slashes
                while (!clean_path.empty() && clean_path.front() == '/') clean_path.erase(0, 1);
                while (!clean_path.empty() && clean_path.back() == '/') clean_path.pop_back();
                if (!clean_path.empty()) {
                    dir += "/" + clean_path;
                }
            }

            if (!fs::exists(dir) || !fs::is_directory(dir))
                return "[error] Directory does not exist: " + dir;

            std::string result;
            int count = 0;
            std::error_code ec;
            for (auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_regular_file()) continue;
                std::string name = entry.path().filename().string();
                if (glob_match(file_pattern, name)) {
                    // Show path relative to resolved workspace
                    std::string rel = entry.path().string();
                    if (rel.size() > resolved.size() && rel.substr(0, resolved.size()) == resolved) {
                        rel = rel.substr(resolved.size());
                        if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
                    }
                    result += rel + "\n";
                    count++;
                    if (static_cast<int>(result.size()) > max_output) {
                        result += "...[truncated at " + std::to_string(count) + " files]\n";
                        break;
                    }
                }
            }

            if (result.empty()) return "No files matching '" + pattern + "'";
            return std::to_string(count) + " file(s):\n" + result;
        };
        reg.register_tool(std::move(def));
    }

    // ── apply_patch (unified diff format) ──
    {
        ToolDef def;
        def.name = "apply_patch";
        def.description = "Apply a unified diff patch.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "path": {"type": "string"},
                "patch": {"type": "string"}
            },
            "required": ["path", "patch"]
        })JSON");

        def.func = [workspace](const nlohmann::json& args) -> std::string {
            std::string path = args.value("path", "");
            std::string patch = args.value("patch", "");
            if (path.empty()) return "[error] path is required";
            if (patch.empty()) return "[error] patch is required";

            std::string resolved = resolve_workspace_path(*workspace, path);

            // Read original file (may not exist for new files)
            std::vector<std::string> lines;
            {
                std::ifstream f(resolved);
                if (f) {
                    std::string line;
                    while (std::getline(f, line)) lines.push_back(line);
                }
            }

            // Parse and apply unified diff hunks
            std::istringstream iss(patch);
            std::string line;
            int applied = 0;
            std::vector<std::string> result = lines;

            // Skip header lines (---, +++)
            while (std::getline(iss, line)) {
                if (line.size() >= 2 && line[0] == '@' && line[1] == '@') break;
                // skip --- and +++ lines
            }

            // Process hunks
            do {
                if (line.size() < 4 || line[0] != '@' || line[1] != '@') continue;

                // Parse @@ -start,count +start,count @@
                int old_start = 1;
                size_t pos = line.find('-', 3);
                if (pos != std::string::npos) {
                    try { old_start = std::stoi(line.substr(pos + 1)); } catch (...) {}
                }

                int idx = old_start - 1; // 0-based
                if (idx < 0) idx = 0;

                // Apply hunk lines
                while (std::getline(iss, line)) {
                    if (line.empty()) { idx++; continue; }
                    if (line[0] == '@') break; // next hunk
                    if (line[0] == '-') {
                        // Remove line
                        if (idx >= 0 && idx < static_cast<int>(result.size())) {
                            result.erase(result.begin() + idx);
                            applied++;
                        }
                    } else if (line[0] == '+') {
                        // Add line
                        std::string content = line.substr(1);
                        if (idx >= static_cast<int>(result.size())) {
                            result.push_back(content);
                        } else {
                            result.insert(result.begin() + idx, content);
                        }
                        idx++;
                        applied++;
                    } else {
                        // Context line (space prefix or no prefix)
                        idx++;
                    }
                }
            } while (line.size() >= 2 && line[0] == '@' && line[1] == '@');

            if (applied == 0) return "[error] No hunks applied — patch may not match file content";

            // Write result
            auto parent = fs::path(resolved).parent_path();
            if (!parent.empty()) {
                std::error_code ec;
                fs::create_directories(parent, ec);
            }
            std::ofstream f(resolved);
            if (!f) return "[error] Cannot write file: " + resolved;
            for (size_t i = 0; i < result.size(); i++) {
                if (i > 0) f << "\n";
                f << result[i];
            }
            if (!result.empty()) f << "\n";

            return "Patch applied to " + resolved + " (" + std::to_string(applied) + " changes)";
        };
        reg.register_tool(std::move(def));
    }

    // ── grep_file (search within files) ──
    {
        ToolDef def;
        def.name = "grep_file";
        def.description = "Search text in files.";
        def.parameters = nlohmann::json::parse(R"JSON({
            "type": "object",
            "properties": {
                "pattern": {"type": "string"},
                "path": {"type": "string"},
                "glob": {"type": "string", "description": "File filter, e.g. '*.py'"}
            },
            "required": ["pattern"]
        })JSON");

        def.func = [workspace, max_output](const nlohmann::json& args) -> std::string {
            std::string pattern = args.value("pattern", "");
            std::string path = args.value("path", "");
            std::string glob_filter = args.value("glob", "");
            if (pattern.empty()) return "[error] pattern is required";

            if (path.empty()) path = *workspace;
            std::string resolved = resolve_workspace_path(*workspace, path);

            // Case-insensitive pattern
            std::string lower_pattern = pattern;
            std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);

            std::string result;
            int match_count = 0;
            int file_count = 0;

            auto search_file = [&](const std::string& fpath) {
                // Check glob filter
                if (!glob_filter.empty()) {
                    std::string fname = fs::path(fpath).filename().string();
                    // Simple *.ext matching
                    if (glob_filter.size() > 1 && glob_filter[0] == '*') {
                        std::string ext = glob_filter.substr(1);
                        if (fname.size() < ext.size() || fname.substr(fname.size() - ext.size()) != ext) return;
                    }
                }

                std::ifstream f(fpath);
                if (!f) return;
                std::string line;
                int line_num = 0;
                bool file_header = false;
                while (std::getline(f, line)) {
                    line_num++;
                    std::string lower_line = line;
                    std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
                    if (lower_line.find(lower_pattern) != std::string::npos) {
                        if (!file_header) {
                            result += "\n" + fpath + ":\n";
                            file_header = true;
                            file_count++;
                        }
                        result += "  " + std::to_string(line_num) + ": " + line + "\n";
                        match_count++;
                        if (static_cast<int>(result.size()) > max_output) return;
                    }
                }
            };

            if (fs::is_regular_file(resolved)) {
                search_file(resolved);
            } else if (fs::is_directory(resolved)) {
                std::error_code ec;
                for (auto& entry : fs::recursive_directory_iterator(resolved, fs::directory_options::skip_permission_denied, ec)) {
                    if (!entry.is_regular_file()) continue;
                    // Skip binary-looking files and hidden directories
                    std::string name = entry.path().filename().string();
                    if (name[0] == '.') continue;
                    std::string ext = entry.path().extension().string();
                    if (ext == ".exe" || ext == ".dll" || ext == ".so" || ext == ".o" ||
                        ext == ".a" || ext == ".lib" || ext == ".bin" || ext == ".png" ||
                        ext == ".jpg" || ext == ".gif" || ext == ".zip" || ext == ".gz") continue;

                    search_file(entry.path().string());
                    if (static_cast<int>(result.size()) > max_output) {
                        result += "\n...[truncated]\n";
                        break;
                    }
                }
            } else {
                return "[error] Path does not exist: " + resolved;
            }

            if (result.empty()) return "No matches found for '" + pattern + "'";
            return std::to_string(match_count) + " match(es) in " + std::to_string(file_count) + " file(s):" + result;
        };
        reg.register_tool(std::move(def));
    }
}

} // namespace minidragon
