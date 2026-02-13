#pragma once
#include "utils.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace minidragon {

// ── RAII file lock (cross-platform) ─────────────────────────────────

class FileLock {
public:
    explicit FileLock(const std::string& path) : path_(path + ".lock") {
#ifdef _WIN32
        handle_ = CreateFileA(path_.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
        fd_ = open(path_.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd_ >= 0) flock(fd_, LOCK_EX);
#endif
    }
    ~FileLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        DeleteFileA(path_.c_str());
#else
        if (fd_ >= 0) { flock(fd_, LOCK_UN); close(fd_); }
        ::unlink(path_.c_str());
#endif
    }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
private:
    std::string path_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

// ── Helpers ─────────────────────────────────────────────────────────

inline std::string sanitize_name(const std::string& name) {
    std::string r;
    for (char c : name) {
        r += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
    }
    // collapse multiple hyphens
    std::string out;
    bool prev = false;
    for (char c : r) {
        if (c == '-') { if (!prev) out += c; prev = true; }
        else { out += c; prev = false; }
    }
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "team" : out;
}

// ── Data structures ─────────────────────────────────────────────────

struct TeamMember {
    std::string name;
    std::string agent_type = "general-purpose";
    std::string model;

    nlohmann::json to_json() const {
        return {{"name", name}, {"agentType", agent_type}, {"model", model}};
    }
    static TeamMember from_json(const nlohmann::json& j) {
        TeamMember m;
        m.name = j.value("name", "");
        m.agent_type = j.value("agentType", "general-purpose");
        m.model = j.value("model", "");
        return m;
    }
};

struct TeamConfig {
    std::string display_name;
    std::string dir_name;
    std::string lead_name;
    std::string lead_model;
    std::vector<TeamMember> members;

    nlohmann::json to_json() const;
    static TeamConfig from_json(const nlohmann::json& j);
};

struct InboxMessage {
    std::string from;
    std::string text;
    std::string summary;
    std::string timestamp;
    bool read = false;

    nlohmann::json to_json() const {
        return {{"from", from}, {"text", text}, {"summary", summary},
                {"timestamp", timestamp}, {"read", read}};
    }
    static InboxMessage from_json(const nlohmann::json& j) {
        InboxMessage m;
        m.from = j.value("from", "");
        m.text = j.value("text", "");
        m.summary = j.value("summary", "");
        m.timestamp = j.value("timestamp", "");
        m.read = j.value("read", false);
        return m;
    }
};

struct TaskItem {
    std::string id;
    std::string subject;
    std::string description;
    std::string status = "pending";
    std::string owner;
    std::vector<std::string> blocks;
    std::vector<std::string> blocked_by;

    nlohmann::json to_json() const;
    static TaskItem from_json(const nlohmann::json& j);
};

// ── TeamManager ─────────────────────────────────────────────────────

class TeamManager {
public:
    TeamManager() = default;

    // Lifecycle
    bool create_team(const std::string& name, const std::string& lead_name,
                     const std::string& lead_model);
    bool load_team(const std::string& dir_name);
    bool delete_team();
    bool team_exists() const;

    // Members
    bool add_member(const TeamMember& member);
    bool remove_member(const std::string& name);
    TeamConfig get_config() const { return config_; }
    std::vector<TeamMember> get_members() const { return config_.members; }

    // Inbox
    bool send_message(const std::string& from, const std::string& to,
                      const std::string& text, const std::string& summary);
    bool broadcast(const std::string& from, const std::string& text,
                   const std::string& summary);
    std::vector<InboxMessage> read_unread(const std::string& agent_name);

    // Tasks
    std::string create_task(const std::string& subject, const std::string& description);
    bool update_task(const std::string& id, const nlohmann::json& updates);
    TaskItem get_task(const std::string& id) const;
    std::vector<TaskItem> list_tasks() const;

    // Spawn / Shutdown
    int spawn_teammate(const std::string& name, const std::string& model,
                       const std::string& agent_type, const std::string& prompt);
    bool request_shutdown(const std::string& from, const std::string& target);

    // Paths
    std::string teams_base() const { return home_dir() + "/.minidragon/teams"; }
    std::string team_dir() const { return teams_base() + "/" + config_.dir_name; }
    std::string inboxes_dir() const { return team_dir() + "/inboxes"; }
    std::string prompts_dir() const { return team_dir() + "/prompts"; }
    std::string tasks_dir() const { return home_dir() + "/.minidragon/tasks/" + config_.dir_name; }
    std::string dir_name() const { return config_.dir_name; }

private:
    TeamConfig config_;
    mutable std::mutex mutex_;

    void save_config();
    int next_task_id() const;
    std::string inbox_path(const std::string& name) const { return inboxes_dir() + "/" + name + ".json"; }
    std::string task_path(const std::string& id) const { return tasks_dir() + "/" + id + ".json"; }
    static std::string now_iso8601();
};

} // namespace minidragon
