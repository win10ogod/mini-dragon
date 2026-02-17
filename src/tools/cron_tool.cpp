#include "cron_tool.hpp"
#include "../cron_store.hpp"
#include "../utils.hpp"

namespace minidragon {

void register_cron_tool(ToolRegistry& reg, const std::string& db_path) {
    auto store = std::make_shared<CronStore>(db_path);

    ToolDef def;
    def.name = "cron";
    def.description = "Manage cron jobs (add/list/remove).";
    def.parameters = {
        {"type", "object"},
        {"properties", {
            {"action", {{"type", "string"}, {"enum", {"add", "list", "remove"}}}},
            {"name", {{"type", "string"}}},
            {"message", {{"type", "string"}}},
            {"every_seconds", {{"type", "integer"}}},
            {"cron_expr", {{"type", "string"}}},
            {"id", {{"type", "integer"}}}
        }},
        {"required", nlohmann::json::array({"action"})}
    };

    def.func = [store](const nlohmann::json& args) -> std::string {
        std::string action = args.value("action", "");

        if (action == "add") {
            CronJob job;
            job.name = args.value("name", "unnamed");
            job.message = args.value("message", "");

            if (args.contains("every_seconds")) {
                job.schedule_type = "every";
                job.interval_seconds = args["every_seconds"].get<int64_t>();
            } else if (args.contains("cron_expr")) {
                job.schedule_type = "cron";
                job.cron_expr = args["cron_expr"].get<std::string>();
            } else {
                return "[error] Must provide every_seconds or cron_expr";
            }

            job.created_at = epoch_now();
            job.last_run = 0;

            int64_t id = store->add(job);
            return "Added cron job id=" + std::to_string(id) + " name=" + job.name;
        }
        else if (action == "list") {
            auto jobs = store->list();
            if (jobs.empty()) return "No cron jobs.";
            std::string result;
            for (auto& j : jobs) {
                result += "id=" + std::to_string(j.id) + " name=" + j.name +
                         " type=" + j.schedule_type;
                if (j.schedule_type == "every")
                    result += " every=" + std::to_string(j.interval_seconds) + "s";
                else
                    result += " cron=\"" + j.cron_expr + "\"";
                result += " msg=\"" + j.message + "\"\n";
            }
            return result;
        }
        else if (action == "remove") {
            if (!args.contains("id")) return "[error] Must provide job id";
            int64_t id = args["id"].get<int64_t>();
            bool ok = store->remove(id);
            return ok ? "Removed job " + std::to_string(id) : "Job not found: " + std::to_string(id);
        }

        return "[error] Unknown action: " + action;
    };

    reg.register_tool(std::move(def));
}

} // namespace minidragon
