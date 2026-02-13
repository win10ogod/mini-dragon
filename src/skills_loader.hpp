#pragma once
#include <string>
#include <vector>
#include <map>

namespace minidragon {

struct SkillInfo {
    std::string name;
    std::string description;
    std::string path;       // Full path to SKILL.md
    std::string source;     // "workspace" or "global"
    bool available = true;  // Requirements met?
    bool always = false;    // Always load into context?
    std::string missing;    // Missing requirements description
};

class SkillsLoader {
public:
    SkillsLoader(const std::string& workspace_path, const std::string& global_skills_dir = "");

    // Discover all skills from workspace and global directories
    void discover();

    // Get all discovered skills
    const std::vector<SkillInfo>& skills() const { return skills_; }

    // Load full SKILL.md content for a skill by name
    std::string load_skill(const std::string& name) const;

    // Build system prompt section: summary of all skills
    std::string build_skills_summary() const;

    // Build system prompt section: full content of always-loaded skills
    std::string build_always_skills_content() const;

private:
    std::string workspace_skills_dir_;
    std::string global_skills_dir_;
    std::vector<SkillInfo> skills_;

    void scan_directory(const std::string& dir, const std::string& source);
    SkillInfo parse_skill(const std::string& skill_dir, const std::string& source) const;

    // YAML frontmatter parsing
    static std::map<std::string, std::string> parse_frontmatter(const std::string& content);
    static std::string strip_frontmatter(const std::string& content);

    // Requirement checking
    static bool check_bin(const std::string& name);
    static bool check_env(const std::string& name);
};

} // namespace minidragon
