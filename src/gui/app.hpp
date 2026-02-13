#pragma once
#include "panels/chat_panel.hpp"
#include "panels/status_panel.hpp"
#include "config.hpp"
#include "tool_registry.hpp"
#include "agent.hpp"
#include "skills_loader.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

struct GLFWwindow;

namespace minidragon {

class App {
public:
    App();
    ~App();
    int run();

private:
    GLFWwindow* window_ = nullptr;

    // Core agent
    minidragon::Config config_;
    minidragon::ToolRegistry tools_;
    std::unique_ptr<minidragon::Agent> agent_;
    std::shared_ptr<minidragon::SkillsLoader> skills_;
    std::mutex agent_mutex_;

    // Panels
    ChatPanel chat_panel_;
    StatusPanel status_panel_;
    bool show_status_ = true;

    void setup_agent();
    void render_frame();
    void render_menu_bar();
    void send_message(const std::string& text);
};

} // namespace minidragon
