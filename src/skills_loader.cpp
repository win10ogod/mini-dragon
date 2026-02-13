#include "skills_loader.hpp"
#include "utils.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace minidragon {

// ── Constructor ──────────────────────────────────────────────────────

SkillsLoader::SkillsLoader(const std::string& workspace_path, const std::string& global_skills_dir)
    : workspace_skills_dir_(workspace_path + "/skills")
    , global_skills_dir_(global_skills_dir.empty() ? home_dir() + "/.minidragon/skills" : global_skills_dir)
{}

// ── Discovery ────────────────────────────────────────────────────────

void SkillsLoader::discover() {
    skills_.clear();

    // Workspace skills have highest priority
    scan_directory(workspace_skills_dir_, "workspace");

    // Global skills (only if not already found in workspace)
    scan_directory(global_skills_dir_, "global");

    std::cerr << "[skills] Discovered " << skills_.size() << " skill(s)\n";
}

void SkillsLoader::scan_directory(const std::string& dir, const std::string& source) {
    if (!fs::exists(dir)) return;

    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;

        std::string skill_md = entry.path().string() + "/SKILL.md";
        if (!fs::exists(skill_md)) continue;

        std::string name = entry.path().filename().string();

        // Skip if already discovered (workspace takes priority)
        bool duplicate = false;
        for (auto& s : skills_) {
            if (s.name == name) { duplicate = true; break; }
        }
        if (duplicate) continue;

        auto info = parse_skill(entry.path().string(), source);
        if (!info.name.empty()) {
            std::cerr << "[skills] " << (info.available ? "+" : "-") << " "
                      << info.name << " (" << source << ")"
                      << (info.always ? " [always]" : "")
                      << (!info.available ? " [missing: " + info.missing + "]" : "")
                      << "\n";
            skills_.push_back(std::move(info));
        }
    }
}

// ── Parsing ──────────────────────────────────────────────────────────

SkillInfo SkillsLoader::parse_skill(const std::string& skill_dir, const std::string& source) const {
    SkillInfo info;
    std::string path = skill_dir + "/SKILL.md";
    std::string content = read_file(path);
    if (content.empty()) return info;

    info.path = path;
    info.source = source;
    info.name = fs::path(skill_dir).filename().string();

    // Parse frontmatter
    auto meta = parse_frontmatter(content);
    if (!meta["name"].empty()) info.name = meta["name"];
    info.description = meta["description"];

    // Parse metadata JSON for requirements
    std::string metadata_str = meta["metadata"];
    if (!metadata_str.empty()) {
        try {
            auto mj = nlohmann::json::parse(metadata_str);

            // Look for minidragon or nanobot metadata (support both)
            nlohmann::json skill_meta;
            if (mj.contains("minidragon")) {
                skill_meta = mj["minidragon"];
            } else if (mj.contains("nanobot")) {
                skill_meta = mj["nanobot"];
            } else {
                skill_meta = mj;  // Flat format
            }

            // Always flag
            if (skill_meta.contains("always") && skill_meta["always"].is_boolean()) {
                info.always = skill_meta["always"].get<bool>();
            }

            // Requirements check
            if (skill_meta.contains("requires")) {
                auto& req = skill_meta["requires"];
                std::vector<std::string> missing;

                if (req.contains("bins") && req["bins"].is_array()) {
                    for (auto& b : req["bins"]) {
                        std::string bin = b.get<std::string>();
                        if (!check_bin(bin)) {
                            missing.push_back("CLI: " + bin);
                        }
                    }
                }

                if (req.contains("env") && req["env"].is_array()) {
                    for (auto& e : req["env"]) {
                        std::string var = e.get<std::string>();
                        if (!check_env(var)) {
                            missing.push_back("ENV: " + var);
                        }
                    }
                }

                if (!missing.empty()) {
                    info.available = false;
                    for (size_t i = 0; i < missing.size(); i++) {
                        if (i > 0) info.missing += ", ";
                        info.missing += missing[i];
                    }
                }
            }

            // OS filter
            if (skill_meta.contains("os") && skill_meta["os"].is_array()) {
                bool os_ok = false;
                for (auto& os : skill_meta["os"]) {
                    std::string os_str = os.get<std::string>();
#ifdef _WIN32
                    if (os_str == "windows") os_ok = true;
#elif defined(__APPLE__)
                    if (os_str == "darwin") os_ok = true;
#else
                    if (os_str == "linux") os_ok = true;
#endif
                }
                if (!os_ok) {
                    info.available = false;
                    if (!info.missing.empty()) info.missing += ", ";
#ifdef _WIN32
                    info.missing += "OS: requires non-windows";
#elif defined(__APPLE__)
                    info.missing += "OS: requires non-darwin";
#else
                    info.missing += "OS: requires non-linux";
#endif
                }
            }
        } catch (...) {
            // Invalid metadata JSON — skill is still usable, just no filtering
        }
    }

    return info;
}

std::map<std::string, std::string> SkillsLoader::parse_frontmatter(const std::string& content) {
    std::map<std::string, std::string> result;
    if (content.size() < 4 || content.substr(0, 3) != "---") return result;

    // Find closing ---
    auto end_pos = content.find("\n---", 3);
    if (end_pos == std::string::npos) return result;

    std::string yaml = content.substr(3, end_pos - 3);

    // Simple key: value parsing (handles multiline JSON for metadata)
    std::istringstream stream(yaml);
    std::string line;
    std::string current_key;
    std::string current_value;
    int brace_depth = 0;

    while (std::getline(stream, line)) {
        // Trim leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (brace_depth > 0) {
            // Continue collecting multi-line JSON value
            current_value += "\n" + line;
            for (char c : line) {
                if (c == '{') brace_depth++;
                else if (c == '}') brace_depth--;
            }
            if (brace_depth <= 0) {
                result[current_key] = current_value;
                current_key.clear();
                current_value.clear();
                brace_depth = 0;
            }
            continue;
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // Trim
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        size_t vstart = value.find_first_not_of(" \t\"'");
        if (vstart != std::string::npos) {
            value = value.substr(vstart);
        } else {
            value.clear();
        }
        // Remove trailing quotes
        while (!value.empty() && (value.back() == '"' || value.back() == '\'')) value.pop_back();

        // Check if value starts a JSON object
        bool is_json = false;
        for (char c : value) {
            if (c == ' ' || c == '\t') continue;
            if (c == '{') { is_json = true; break; }
            break;
        }

        if (is_json) {
            current_key = key;
            current_value = value;
            brace_depth = 0;
            for (char c : value) {
                if (c == '{') brace_depth++;
                else if (c == '}') brace_depth--;
            }
            if (brace_depth <= 0) {
                result[key] = value;
                current_key.clear();
                current_value.clear();
                brace_depth = 0;
            }
        } else {
            result[key] = value;
        }
    }

    return result;
}

std::string SkillsLoader::strip_frontmatter(const std::string& content) {
    if (content.size() < 4 || content.substr(0, 3) != "---") return content;

    auto end_pos = content.find("\n---", 3);
    if (end_pos == std::string::npos) return content;

    // Skip past the closing --- and any trailing newline
    size_t after = end_pos + 4;
    while (after < content.size() && (content[after] == '\n' || content[after] == '\r')) {
        after++;
    }
    return content.substr(after);
}

// ── Requirement Checking ─────────────────────────────────────────────

bool SkillsLoader::check_bin(const std::string& name) {
#ifdef _WIN32
    // Use SearchPathW on Windows
    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wname, 256);
    wchar_t result[MAX_PATH];
    if (SearchPathW(NULL, wname, L".exe", MAX_PATH, result, NULL) > 0) return true;
    if (SearchPathW(NULL, wname, L".cmd", MAX_PATH, result, NULL) > 0) return true;
    if (SearchPathW(NULL, wname, L".bat", MAX_PATH, result, NULL) > 0) return true;
    return false;
#else
    // POSIX: check common PATH directories
    const char* path_env = std::getenv("PATH");
    if (!path_env) return false;

    std::string path_str = path_env;
    std::istringstream stream(path_str);
    std::string dir;
    while (std::getline(stream, dir, ':')) {
        std::string full = dir + "/" + name;
        if (access(full.c_str(), X_OK) == 0) return true;
    }
    return false;
#endif
}

bool SkillsLoader::check_env(const std::string& name) {
    return std::getenv(name.c_str()) != nullptr;
}

// ── Loading ──────────────────────────────────────────────────────────

std::string SkillsLoader::load_skill(const std::string& name) const {
    for (auto& s : skills_) {
        if (s.name == name) {
            return read_file(s.path);
        }
    }
    return "";
}

// ── System Prompt Integration ────────────────────────────────────────

std::string SkillsLoader::build_skills_summary() const {
    bool any = false;
    for (auto& s : skills_) {
        if (!s.name.empty()) { any = true; break; }
    }
    if (!any) return "";

    std::string out = "--- Available Skills ---\n";
    out += "Skills extend your capabilities. Use `read_file` to load a skill's full instructions when needed.\n\n";
    out += "<skills>\n";

    for (auto& s : skills_) {
        out += "  <skill available=\"" + std::string(s.available ? "true" : "false") + "\">\n";
        out += "    <name>" + s.name + "</name>\n";
        if (!s.description.empty()) {
            out += "    <description>" + s.description + "</description>\n";
        }
        out += "    <location>" + s.path + "</location>\n";
        if (!s.available && !s.missing.empty()) {
            out += "    <requires>" + s.missing + "</requires>\n";
        }
        out += "  </skill>\n";
    }

    out += "</skills>\n";
    return out;
}

std::string SkillsLoader::build_always_skills_content() const {
    std::string out;

    for (auto& s : skills_) {
        if (!s.always || !s.available) continue;

        std::string content = read_file(s.path);
        if (content.empty()) continue;

        std::string body = strip_frontmatter(content);
        if (body.empty()) continue;

        if (!out.empty()) out += "\n\n---\n\n";
        out += "### Skill: " + s.name + "\n\n" + body;
    }

    return out;
}

} // namespace minidragon
