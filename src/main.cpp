#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include "onboard.hpp"
#include "agent.hpp"
#include "gateway.hpp"
#include "status.hpp"
#include "cron_cmd.hpp"

static void print_usage() {
    std::cout << "Usage: minidragon <command> [options]\n\n"
              << "Commands:\n"
              << "  onboard                     Initialize ~/.minidragon\n"
              << "  agent [-m MSG] [--no-markdown] [--logs]\n"
              << "        [--team NAME] [--agent-name NAME] [--model MODEL]\n"
              << "                              Run agent (interactive or single message)\n"
              << "  gateway [--host H] [--port P]\n"
              << "                              Start HTTP gateway server\n"
              << "  status                      Show current configuration\n"
              << "  cron add|list|remove        Manage cron jobs\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; i++) {
        args.push_back(argv[i]);
    }

    if (cmd == "onboard") {
        return minidragon::cmd_onboard();
    }
    else if (cmd == "agent") {
        std::string message;
        std::string team_name;
        std::string agent_name;
        std::string model_override;
        bool no_markdown = false;
        bool logs = false;
        for (size_t i = 0; i < args.size(); i++) {
            if ((args[i] == "-m" || args[i] == "--message") && i + 1 < args.size()) {
                message = args[++i];
            } else if (args[i] == "--team" && i + 1 < args.size()) {
                team_name = args[++i];
            } else if (args[i] == "--agent-name" && i + 1 < args.size()) {
                agent_name = args[++i];
            } else if (args[i] == "--model" && i + 1 < args.size()) {
                model_override = args[++i];
            } else if (args[i] == "--no-markdown") {
                no_markdown = true;
            } else if (args[i] == "--logs") {
                logs = true;
            }
        }
        return minidragon::cmd_agent(message, no_markdown, logs,
                                     team_name, agent_name, model_override);
    }
    else if (cmd == "gateway") {
        std::string host = "127.0.0.1";
        int port = 18790;
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i] == "--host" && i + 1 < args.size()) {
                host = args[++i];
            } else if (args[i] == "--port" && i + 1 < args.size()) {
                port = std::stoi(args[++i]);
            }
        }
        return minidragon::cmd_gateway(host, port);
    }
    else if (cmd == "status") {
        return minidragon::cmd_status();
    }
    else if (cmd == "cron") {
        return minidragon::cmd_cron(args);
    }
    else {
        std::cerr << "Unknown command: " << cmd << "\n";
        print_usage();
        return 1;
    }
}
