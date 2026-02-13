#include "gateway.hpp"
#include "config.hpp"
#include "tool_registry.hpp"
#include "tools/exec_tool.hpp"
#include "tools/fs_tools.hpp"
#include "tools/cron_tool.hpp"
#include "tools/memory_tool.hpp"
#include "tools/subagent_tool.hpp"
#include "skills_loader.hpp"
#include "mcp_manager.hpp"
#include "agent.hpp"
#include "cron_store.hpp"
#include "cron_runner.hpp"
#include "heartbeat.hpp"
#include "channels/http_channel.hpp"
#include "channels/cli_channel.hpp"
#include "channels/telegram_channel.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <mutex>

namespace minidragon {

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

int cmd_gateway(const std::string& host, int port) {
    Config cfg = Config::load(default_config_path());
    std::string ws = cfg.workspace_path();

    ToolRegistry tools;
    register_exec_tool(tools, cfg);
    register_fs_tools(tools, cfg);
    register_cron_tool(tools, ws + "/cron/cron.db");
    register_memory_tool(tools, ws);
    register_subagent_tool(tools, cfg);

    // Skills: discover from workspace and global directories
    auto skills = std::make_shared<SkillsLoader>(ws);
    skills->discover();

    // MCP servers
    McpManager mcp(cfg.mcp_servers);
    mcp.connect_all();
    mcp.register_tools(tools);

    Agent agent(cfg, tools);
    agent.set_skills(skills);
    std::mutex agent_mutex;

    auto handle_message = [&](const InboundMessage& msg) -> std::string {
        std::lock_guard<std::mutex> lock(agent_mutex);
        return agent.run(msg.text);
    };

    // Cron runner
    std::string db_path = ws + "/cron/cron.db";
    CronStore cron_store(db_path);
    CronRunner cron_runner(cron_store, [&](const CronJob& job) {
        std::cerr << "[cron] Firing job: " << job.name << " - " << job.message << "\n";
        std::lock_guard<std::mutex> lock(agent_mutex);
        std::string reply = agent.run("[cron:" + job.name + "] " + job.message);
        std::cerr << "[cron] Reply: " << reply << "\n";
    });
    cron_runner.start();
    std::cerr << "[gateway] Cron runner started\n";

    // Heartbeat service
    HeartbeatService heartbeat(ws, [&](const std::string& msg) -> std::string {
        std::lock_guard<std::mutex> lock(agent_mutex);
        return agent.run(msg);
    });
    heartbeat.start();
    std::cerr << "[gateway] Heartbeat service started\n";

    // Channels
    if (!cfg.http_channel.enabled && !cfg.telegram.enabled) {
        std::cerr << "[warn] No channels enabled besides CLI. Gateway will still run.\n";
    }

    HTTPChannel http_ch(host, port, cfg.http_channel);
    if (http_ch.enabled()) {
        http_ch.start(handle_message);
        std::cerr << "[gateway] HTTP channel started on " << host << ":" << port << "\n";
    }

    TelegramChannel telegram_ch(cfg.telegram);
    if (telegram_ch.enabled()) {
        telegram_ch.start(handle_message);
        std::cerr << "[gateway] Telegram channel started\n";
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cerr << "[gateway] Ready. Type messages or Ctrl+C to quit.\n";

    // CLI is always available in gateway mode
    CLIChannel cli_ch;
    cli_ch.start(handle_message);

    std::string line;
    while (g_running) {
        std::cout << "gateway> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == "exit" || line == "quit" || line == ":q") break;

        std::string reply = cli_ch.handle_line("gateway_user", line);
        std::cout << reply << "\n";
    }

    std::cerr << "[gateway] Shutting down...\n";
    http_ch.stop();
    telegram_ch.stop();
    heartbeat.stop();
    cron_runner.stop();
    mcp.disconnect_all();
    std::cerr << "[gateway] Done.\n";
    return 0;
}

} // namespace minidragon
