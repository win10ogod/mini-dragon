#include "cron_cmd.hpp"
#include "cron_store.hpp"
#include "config.hpp"
#include "utils.hpp"
#include <iostream>

namespace minidragon {

int cmd_cron(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: minidragon cron <add|list|remove> [options]\n";
        return 1;
    }

    Config cfg = Config::load(default_config_path());
    std::string db_path = cfg.workspace_path() + "/cron/cron.db";
    CronStore store(db_path);

    std::string subcmd = args[0];

    if (subcmd == "add") {
        std::string name, message, cron_expr;
        int64_t every = 0;

        for (size_t i = 1; i < args.size(); i++) {
            if (args[i] == "--name" && i + 1 < args.size()) {
                name = args[++i];
            } else if (args[i] == "--message" && i + 1 < args.size()) {
                message = args[++i];
            } else if (args[i] == "--every" && i + 1 < args.size()) {
                every = std::stoll(args[++i]);
            } else if (args[i] == "--cron" && i + 1 < args.size()) {
                cron_expr = args[++i];
            }
        }

        if (name.empty() || message.empty()) {
            std::cerr << "Usage: minidragon cron add --name N --message M [--every SECONDS | --cron \"EXPR\"]\n";
            return 1;
        }

        CronJob job;
        job.name = name;
        job.message = message;
        job.created_at = epoch_now();
        job.last_run = 0;

        if (every > 0) {
            job.schedule_type = "every";
            job.interval_seconds = every;
        } else if (!cron_expr.empty()) {
            job.schedule_type = "cron";
            job.cron_expr = cron_expr;
        } else {
            std::cerr << "Must specify --every or --cron\n";
            return 1;
        }

        int64_t id = store.add(job);
        std::cout << "Added cron job: id=" << id << " name=" << name << "\n";
        return 0;
    }
    else if (subcmd == "list") {
        auto jobs = store.list();
        if (jobs.empty()) {
            std::cout << "No cron jobs.\n";
            return 0;
        }
        for (auto& j : jobs) {
            std::cout << "id=" << j.id << " name=" << j.name
                      << " type=" << j.schedule_type;
            if (j.schedule_type == "every")
                std::cout << " every=" << j.interval_seconds << "s";
            else
                std::cout << " cron=\"" << j.cron_expr << "\"";
            std::cout << " message=\"" << j.message << "\"\n";
        }
        return 0;
    }
    else if (subcmd == "remove") {
        if (args.size() < 2) {
            std::cerr << "Usage: minidragon cron remove <job_id>\n";
            return 1;
        }
        int64_t id = std::stoll(args[1]);
        bool ok = store.remove(id);
        if (ok) {
            std::cout << "Removed cron job: id=" << id << "\n";
        } else {
            std::cerr << "Job not found: id=" << id << "\n";
            return 1;
        }
        return 0;
    }

    std::cerr << "Unknown cron subcommand: " << subcmd << "\n";
    return 1;
}

} // namespace minidragon
