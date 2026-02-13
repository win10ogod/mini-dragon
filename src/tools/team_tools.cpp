#include "team_tools.hpp"
#include <iostream>

namespace minidragon {

void register_team_tools(ToolRegistry& tools,
                         std::shared_ptr<TeamManager> team,
                         const std::string& my_name) {
    using json = nlohmann::json;

    // ── team_create ─────────────────────────────────────────────────
    tools.register_tool({
        "team_create",
        "Create a new agent team. You become the team lead.",
        json::parse(R"JSON({
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Display name for the team"}
            },
            "required": ["name"]
        })JSON"),
        [team, my_name](const json& args) -> std::string {
            if (team->team_exists())
                return "[error] A team already exists. Delete it first with team_cleanup.";
            std::string name = args.value("name", "my-team");
            team->create_team(name, my_name, "");
            return "Team '" + name + "' created. Dir: " + team->dir_name() +
                   "\nYou are the lead. Use team_spawn to add teammates.";
        }
    });

    // ── team_spawn ──────────────────────────────────────────────────
    tools.register_tool({
        "team_spawn",
        "Spawn a new teammate subprocess. The teammate runs independently and communicates via inbox messages.",
        json::parse(R"JSON({
            "type": "object",
            "properties": {
                "name":       {"type": "string", "description": "Unique name for the teammate (e.g. 'researcher', 'tester')"},
                "prompt":     {"type": "string", "description": "Initial task/instructions for the teammate"},
                "model":      {"type": "string", "description": "Model to use (optional, defaults to team config)"},
                "agent_type": {"type": "string", "description": "Role type (optional, default: general-purpose)"}
            },
            "required": ["name", "prompt"]
        })JSON"),
        [team, my_name](const json& args) -> std::string {
            if (!team->team_exists())
                return "[error] No team exists. Create one first with team_create.";
            std::string name = args.at("name").get<std::string>();
            std::string prompt = args.at("prompt").get<std::string>();
            std::string model = args.value("model", "");
            std::string type = args.value("agent_type", "general-purpose");

            int pid = team->spawn_teammate(name, model, type, prompt);
            if (pid > 0)
                return "Spawned teammate '" + name + "' (PID " + std::to_string(pid) +
                       "). It will process the prompt and send results to your inbox.";
            return "[error] Failed to spawn teammate '" + name + "'";
        }
    });

    // ── team_send ───────────────────────────────────────────────────
    tools.register_tool({
        "team_send",
        "Send a message to a specific teammate, or broadcast to all with to=\"*\".",
        json::parse(R"JSON({
            "type": "object",
            "properties": {
                "to":   {"type": "string", "description": "Teammate name, or '*' to broadcast"},
                "text": {"type": "string", "description": "Message content"}
            },
            "required": ["to", "text"]
        })JSON"),
        [team, my_name](const json& args) -> std::string {
            if (!team->team_exists()) return "[error] No team exists.";
            std::string to = args.at("to").get<std::string>();
            std::string text = args.at("text").get<std::string>();
            std::string summary = text.substr(0, 60);

            if (to == "*") {
                team->broadcast(my_name, text, summary);
                return "Broadcast sent to all teammates.";
            } else {
                team->send_message(my_name, to, text, summary);
                return "Message sent to '" + to + "'.";
            }
        }
    });

    // ── team_shutdown ───────────────────────────────────────────────
    tools.register_tool({
        "team_shutdown",
        "Request a teammate to gracefully shut down.",
        json::parse(R"JSON({
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Teammate name to shut down"}
            },
            "required": ["name"]
        })JSON"),
        [team, my_name](const json& args) -> std::string {
            if (!team->team_exists()) return "[error] No team exists.";
            std::string name = args.at("name").get<std::string>();
            team->request_shutdown(my_name, name);
            return "Shutdown request sent to '" + name + "'. Wait for confirmation in inbox.";
        }
    });

    // ── team_cleanup ────────────────────────────────────────────────
    tools.register_tool({
        "team_cleanup",
        "Delete the team and all its resources (inboxes, tasks). Shut down teammates first.",
        json::parse(R"JSON({"type": "object", "properties": {}})JSON"),
        [team](const json&) -> std::string {
            if (!team->team_exists()) return "[error] No team exists.";
            team->delete_team();
            return "Team deleted. All resources cleaned up.";
        }
    });

    // ── team_status ─────────────────────────────────────────────────
    tools.register_tool({
        "team_status",
        "List all team members and their roles.",
        json::parse(R"JSON({"type": "object", "properties": {}})JSON"),
        [team](const json&) -> std::string {
            if (!team->team_exists()) return "No team active.";
            auto cfg = team->get_config();
            std::string out = "Team: " + cfg.display_name + " (lead: " + cfg.lead_name + ")\nMembers:\n";
            for (auto& m : cfg.members)
                out += "  - " + m.name + " [" + m.agent_type + "] model=" + m.model + "\n";
            return out;
        }
    });

    // ── inbox_check ─────────────────────────────────────────────────
    tools.register_tool({
        "inbox_check",
        "Read all unread messages from your inbox.",
        json::parse(R"JSON({"type": "object", "properties": {}})JSON"),
        [team, my_name](const json&) -> std::string {
            if (!team->team_exists()) return "No team active.";
            auto msgs = team->read_unread(my_name);
            if (msgs.empty()) return "No new messages.";
            std::string out;
            for (auto& m : msgs) {
                out += "[" + m.timestamp + "] " + m.from + ": " + m.text + "\n";
            }
            return out;
        }
    });

    // ── task_create ─────────────────────────────────────────────────
    tools.register_tool({
        "task_create",
        "Create a new task in the shared task list.",
        json::parse(R"JSON({
            "type": "object",
            "properties": {
                "subject":     {"type": "string", "description": "Brief task title"},
                "description": {"type": "string", "description": "Detailed description of what needs to be done"}
            },
            "required": ["subject"]
        })JSON"),
        [team](const json& args) -> std::string {
            if (!team->team_exists()) return "[error] No team exists.";
            std::string subject = args.at("subject").get<std::string>();
            std::string desc = args.value("description", "");
            std::string id = team->create_task(subject, desc);
            return "Task #" + id + " created: " + subject;
        }
    });

    // ── task_update ─────────────────────────────────────────────────
    tools.register_tool({
        "task_update",
        "Update a task's status, owner, or dependencies.",
        json::parse(R"JSON({
            "type": "object",
            "properties": {
                "id":           {"type": "string", "description": "Task ID"},
                "status":       {"type": "string", "description": "New status: pending, in_progress, completed"},
                "owner":        {"type": "string", "description": "Assign to teammate name"},
                "addBlockedBy": {"type": "array", "items": {"type": "string"}, "description": "Task IDs that block this task"}
            },
            "required": ["id"]
        })JSON"),
        [team](const json& args) -> std::string {
            if (!team->team_exists()) return "[error] No team exists.";
            std::string id = args.at("id").get<std::string>();
            json updates;
            if (args.contains("status")) updates["status"] = args["status"];
            if (args.contains("owner")) updates["owner"] = args["owner"];
            if (args.contains("addBlockedBy")) updates["addBlockedBy"] = args["addBlockedBy"];
            if (team->update_task(id, updates))
                return "Task #" + id + " updated.";
            return "[error] Task #" + id + " not found.";
        }
    });

    // ── task_list ───────────────────────────────────────────────────
    tools.register_tool({
        "task_list",
        "List all tasks in the shared task list.",
        json::parse(R"JSON({"type": "object", "properties": {}})JSON"),
        [team](const json&) -> std::string {
            if (!team->team_exists()) return "No team active.";
            auto tasks = team->list_tasks();
            if (tasks.empty()) return "No tasks.";
            std::string out;
            for (auto& t : tasks) {
                out += "#" + t.id + " [" + t.status + "]";
                if (!t.owner.empty()) out += " @" + t.owner;
                out += " " + t.subject;
                if (!t.blocked_by.empty()) {
                    out += " (blocked by:";
                    for (auto& b : t.blocked_by) out += " #" + b;
                    out += ")";
                }
                out += "\n";
            }
            return out;
        }
    });
}

} // namespace minidragon
