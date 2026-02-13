#include "app.hpp"
#include "theme.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "tools/exec_tool.hpp"
#include "tools/fs_tools.hpp"
#include "tools/cron_tool.hpp"
#include "tools/memory_tool.hpp"
#include "tools/subagent_tool.hpp"
#include "skills_loader.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <thread>

namespace minidragon {

App::App() {
    setup_agent();
}

App::~App() = default;

void App::setup_agent() {
    config_ = minidragon::Config::load(minidragon::default_config_path());
    std::string ws = config_.workspace_path();

    minidragon::register_exec_tool(tools_, config_);
    minidragon::register_fs_tools(tools_, config_);
    minidragon::register_cron_tool(tools_, ws + "/cron/cron.db");
    minidragon::register_memory_tool(tools_, ws);
    minidragon::register_subagent_tool(tools_, config_);

    // Skills: discover from workspace and global directories
    skills_ = std::make_shared<minidragon::SkillsLoader>(ws);
    skills_->discover();

    agent_ = std::make_unique<minidragon::Agent>(config_, tools_);
    agent_->set_skills(skills_);

    // Update status panel
    std::string provider_name = config_.provider;
    if (provider_name.empty()) {
        provider_name = config_.providers.empty() ? "none" : config_.providers.begin()->first;
    }
    status_panel_.set_status(
        minidragon::default_config_path(),
        ws,
        config_.model,
        provider_name,
        0,
        config_.telegram.enabled,
        config_.http_channel.enabled
    );
}

void App::send_message(const std::string& text) {
    chat_panel_.set_busy(true);

    std::thread([this, text]() {
        std::string reply;
        try {
            std::lock_guard<std::mutex> lock(agent_mutex_);
            reply = agent_->run(text);
        } catch (const std::exception& e) {
            reply = std::string("[error] ") + e.what();
        }
        chat_panel_.add_message("assistant", reply);
        chat_panel_.set_busy(false);
    }).detach();
}

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "[glfw] Error " << error << ": " << description << "\n";
}

int App::run() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "[minidragon] Failed to initialize GLFW\n";
        return 1;
    }

    // GL 3.0 + GLSL 130
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    window_ = glfwCreateWindow(1200, 800, "Mini Dragon - AI Agent", nullptr, nullptr);
    if (!window_) {
        std::cerr << "[minidragon] Failed to create window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Apply sci-fi theme
    apply_scifi_theme();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Set up chat callback
    chat_panel_.set_send_callback([this](const std::string& text) {
        send_message(text);
    });

    // Welcome message
    chat_panel_.add_message("system", "Welcome to Mini Dragon! Type a message to start chatting.");

    // Main loop
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render_frame();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.04f, 0.04f, 0.08f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window_);
    glfwTerminate();
    return 0;
}

void App::render_frame() {
    render_menu_bar();

    // Main window - fullscreen docked
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + 20));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x,viewport->WorkSize.y - 20));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("MainArea", nullptr, flags);

    float panel_width = show_status_ ? viewport->WorkSize.x * 0.7f : viewport->WorkSize.x - 20;
    float panel_height = viewport->WorkSize.y - 40;

    // Chat panel
    ImGui::BeginChild("ChatArea", ImVec2(panel_width, panel_height));
    ImGui::PushStyleColor(ImGuiCol_Text, color_assistant());
    ImGui::Text("MINI DRAGON");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, color_system());
    ImGui::Text("- AI Agent Console");
    ImGui::PopStyleColor();
    ImGui::Separator();
    chat_panel_.render(panel_width - 10, panel_height - 40);
    ImGui::EndChild();

    // Status panel (side panel)
    if (show_status_) {
        ImGui::SameLine();
        ImGui::BeginChild("StatusArea", ImVec2(0, panel_height));
        status_panel_.render();
        ImGui::EndChild();
    }

    ImGui::End();
}

void App::render_menu_bar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Clear Chat")) {
                chat_panel_.clear();
                chat_panel_.add_message("system", "Chat cleared.");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Status Panel", nullptr, &show_status_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                chat_panel_.add_message("system",
                    "Mini Dragon v1.0\nA sci-fi AI agent interface built with Mini Dragon.\n"
                    "Powered by Dear ImGui + GLFW + OpenGL.");
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

} // namespace minidragon
