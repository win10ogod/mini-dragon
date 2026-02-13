#include "team.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <signal.h>
#endif

namespace minidragon {

// ── TeamConfig JSON ─────────────────────────────────────────────────

nlohmann::json TeamConfig::to_json() const {
    nlohmann::json j;
    j["name"] = display_name;
    j["leadAgentName"] = lead_name;
    j["leadModel"] = lead_model;
    j["members"] = nlohmann::json::array();
    for (auto& m : members) j["members"].push_back(m.to_json());
    return j;
}

TeamConfig TeamConfig::from_json(const nlohmann::json& j) {
    TeamConfig c;
    c.display_name = j.value("name", "");
    c.lead_name = j.value("leadAgentName", "team-lead");
    c.lead_model = j.value("leadModel", "");
    if (j.contains("members") && j["members"].is_array()) {
        for (auto& m : j["members"])
            c.members.push_back(TeamMember::from_json(m));
    }
    return c;
}

// ── TaskItem JSON ───────────────────────────────────────────────────

nlohmann::json TaskItem::to_json() const {
    return {{"id", id}, {"subject", subject}, {"description", description},
            {"status", status}, {"owner", owner},
            {"blocks", blocks}, {"blockedBy", blocked_by}};
}

TaskItem TaskItem::from_json(const nlohmann::json& j) {
    TaskItem t;
    t.id = j.value("id", "");
    t.subject = j.value("subject", "");
    t.description = j.value("description", "");
    t.status = j.value("status", "pending");
    t.owner = j.value("owner", "");
    if (j.contains("blocks") && j["blocks"].is_array())
        for (auto& b : j["blocks"]) t.blocks.push_back(b.get<std::string>());
    if (j.contains("blockedBy") && j["blockedBy"].is_array())
        for (auto& b : j["blockedBy"]) t.blocked_by.push_back(b.get<std::string>());
    return t;
}

// ── TeamManager helpers ─────────────────────────────────────────────

std::string TeamManager::now_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

void TeamManager::save_config() {
    std::string path = team_dir() + "/config.json";
    std::ofstream f(path);
    f << config_.to_json().dump(2) << "\n";
}

// ── Lifecycle ───────────────────────────────────────────────────────

bool TeamManager::create_team(const std::string& name,
                              const std::string& lead_name,
                              const std::string& lead_model) {
    config_.display_name = name;
    config_.dir_name = sanitize_name(name);
    config_.lead_name = lead_name;
    config_.lead_model = lead_model;

    TeamMember lead;
    lead.name = lead_name;
    lead.agent_type = "team-lead";
    lead.model = lead_model;
    config_.members.push_back(lead);

    fs::create_directories(team_dir());
    fs::create_directories(inboxes_dir());
    fs::create_directories(prompts_dir());
    fs::create_directories(tasks_dir());

    save_config();
    std::cerr << "[team] Created team '" << name << "' → " << config_.dir_name << "\n";
    return true;
}

bool TeamManager::load_team(const std::string& dir_name) {
    std::string path = teams_base() + "/" + dir_name + "/config.json";
    std::string content = read_file(path);
    if (content.empty()) return false;
    try {
        config_ = TeamConfig::from_json(nlohmann::json::parse(content));
        config_.dir_name = dir_name;
        return true;
    } catch (...) { return false; }
}

bool TeamManager::delete_team() {
    if (config_.dir_name.empty()) return false;
    std::error_code ec;
    fs::remove_all(team_dir(), ec);
    fs::remove_all(tasks_dir(), ec);
    std::cerr << "[team] Deleted team '" << config_.display_name << "'\n";
    config_ = TeamConfig{};
    return true;
}

bool TeamManager::team_exists() const {
    return !config_.dir_name.empty() && fs::exists(team_dir() + "/config.json");
}

// ── Members ─────────────────────────────────────────────────────────

bool TeamManager::add_member(const TeamMember& member) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& m : config_.members)
        if (m.name == member.name) return false;
    config_.members.push_back(member);
    save_config();
    return true;
}

bool TeamManager::remove_member(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(config_.members.begin(), config_.members.end(),
        [&](const TeamMember& m) { return m.name == name; });
    if (it == config_.members.end()) return false;
    config_.members.erase(it, config_.members.end());
    save_config();
    return true;
}

// ── Inbox ───────────────────────────────────────────────────────────

bool TeamManager::send_message(const std::string& from, const std::string& to,
                               const std::string& text, const std::string& summary) {
    fs::create_directories(inboxes_dir());
    std::string path = inbox_path(to);
    FileLock lock(path);

    std::vector<InboxMessage> msgs;
    std::string content = read_file(path);
    if (!content.empty()) {
        try {
            for (auto& m : nlohmann::json::parse(content))
                msgs.push_back(InboxMessage::from_json(m));
        } catch (...) {}
    }

    InboxMessage msg;
    msg.from = from;
    msg.text = text;
    msg.summary = summary;
    msg.timestamp = now_iso8601();
    msg.read = false;
    msgs.push_back(msg);

    nlohmann::json arr = nlohmann::json::array();
    for (auto& m : msgs) arr.push_back(m.to_json());
    std::ofstream f(path);
    f << arr.dump(2) << "\n";
    return true;
}

bool TeamManager::broadcast(const std::string& from, const std::string& text,
                            const std::string& summary) {
    for (auto& m : config_.members)
        if (m.name != from)
            send_message(from, m.name, text, summary);
    return true;
}

std::vector<InboxMessage> TeamManager::read_unread(const std::string& agent_name) {
    std::string path = inbox_path(agent_name);
    FileLock lock(path);

    std::string content = read_file(path);
    if (content.empty()) return {};

    std::vector<InboxMessage> all;
    try {
        for (auto& m : nlohmann::json::parse(content))
            all.push_back(InboxMessage::from_json(m));
    } catch (...) { return {}; }

    std::vector<InboxMessage> unread;
    bool changed = false;
    for (auto& m : all) {
        if (!m.read) {
            unread.push_back(m);
            m.read = true;
            changed = true;
        }
    }

    if (changed) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& m : all) arr.push_back(m.to_json());
        std::ofstream f(path);
        f << arr.dump(2) << "\n";
    }
    return unread;
}

// ── Tasks ───────────────────────────────────────────────────────────

int TeamManager::next_task_id() const {
    int max_id = 0;
    if (!fs::exists(tasks_dir())) return 1;
    for (auto& e : fs::directory_iterator(tasks_dir())) {
        if (e.is_regular_file() && e.path().extension() == ".json") {
            try { int id = std::stoi(e.path().stem().string()); if (id > max_id) max_id = id; }
            catch (...) {}
        }
    }
    return max_id + 1;
}

std::string TeamManager::create_task(const std::string& subject, const std::string& description) {
    std::lock_guard<std::mutex> lock(mutex_);
    fs::create_directories(tasks_dir());

    TaskItem t;
    t.id = std::to_string(next_task_id());
    t.subject = subject;
    t.description = description;
    t.status = "pending";

    std::ofstream f(task_path(t.id));
    f << t.to_json().dump(2) << "\n";
    return t.id;
}

bool TeamManager::update_task(const std::string& id, const nlohmann::json& u) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string content = read_file(task_path(id));
    if (content.empty()) return false;

    TaskItem t;
    try { t = TaskItem::from_json(nlohmann::json::parse(content)); }
    catch (...) { return false; }

    if (u.contains("status")) t.status = u["status"].get<std::string>();
    if (u.contains("owner")) t.owner = u["owner"].get<std::string>();
    if (u.contains("subject")) t.subject = u["subject"].get<std::string>();
    if (u.contains("description")) t.description = u["description"].get<std::string>();
    if (u.contains("addBlocks") && u["addBlocks"].is_array())
        for (auto& b : u["addBlocks"]) t.blocks.push_back(b.get<std::string>());
    if (u.contains("addBlockedBy") && u["addBlockedBy"].is_array())
        for (auto& b : u["addBlockedBy"]) t.blocked_by.push_back(b.get<std::string>());

    std::ofstream f(task_path(id));
    f << t.to_json().dump(2) << "\n";
    return true;
}

TaskItem TeamManager::get_task(const std::string& id) const {
    std::string content = read_file(task_path(id));
    if (content.empty()) return {};
    try { return TaskItem::from_json(nlohmann::json::parse(content)); }
    catch (...) { return {}; }
}

std::vector<TaskItem> TeamManager::list_tasks() const {
    std::vector<TaskItem> tasks;
    if (!fs::exists(tasks_dir())) return tasks;
    for (auto& e : fs::directory_iterator(tasks_dir())) {
        if (e.is_regular_file() && e.path().extension() == ".json") {
            std::string content = read_file(e.path().string());
            try { tasks.push_back(TaskItem::from_json(nlohmann::json::parse(content))); }
            catch (...) {}
        }
    }
    std::sort(tasks.begin(), tasks.end(), [](const TaskItem& a, const TaskItem& b) {
        try { return std::stoi(a.id) < std::stoi(b.id); }
        catch (...) { return a.id < b.id; }
    });
    return tasks;
}

// ── Spawn ───────────────────────────────────────────────────────────

int TeamManager::spawn_teammate(const std::string& name, const std::string& model,
                                const std::string& agent_type, const std::string& prompt) {
    // Register member
    TeamMember member;
    member.name = name;
    member.model = model;
    member.agent_type = agent_type;
    add_member(member);

    // Write prompt to file (avoids shell escaping issues)
    fs::create_directories(prompts_dir());
    {
        std::ofstream f(prompts_dir() + "/" + name + ".txt");
        f << prompt;
    }

    // Resolve current executable path
    std::string exe;
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    exe = buf;
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) { buf[len] = '\0'; exe = buf; }
    else exe = "minidragon";
#endif

#ifdef _WIN32
    std::string cmd = "\"" + exe + "\" agent --team " + config_.dir_name +
                      " --agent-name " + name;
    if (!model.empty()) cmd += " --model " + model;

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()),
                       nullptr, nullptr, FALSE,
                       CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        int pid = static_cast<int>(pi.dwProcessId);
        std::cerr << "[team] Spawned '" << name << "' (PID " << pid << ")\n";
        return pid;
    }
    std::cerr << "[team] Failed to spawn '" << name << "'\n";
    return -1;
#else
    pid_t pid = fork();
    if (pid == 0) {
        // Child — redirect stdout to /dev/null, keep stderr
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); close(devnull); }

        std::vector<std::string> argv_strs = {
            exe, "agent", "--team", config_.dir_name, "--agent-name", name
        };
        if (!model.empty()) {
            argv_strs.push_back("--model");
            argv_strs.push_back(model);
        }
        std::vector<char*> argv;
        for (auto& s : argv_strs) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);

        execvp(exe.c_str(), argv.data());
        _exit(1);
    } else if (pid > 0) {
        std::cerr << "[team] Spawned '" << name << "' (PID " << pid << ")\n";
        return static_cast<int>(pid);
    }
    std::cerr << "[team] Fork failed for '" << name << "'\n";
    return -1;
#endif
}

bool TeamManager::request_shutdown(const std::string& from, const std::string& target) {
    nlohmann::json msg;
    msg["type"] = "shutdown_request";
    msg["from"] = from;
    return send_message(from, target, msg.dump(), "Shutdown request");
}

} // namespace minidragon
