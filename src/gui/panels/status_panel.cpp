#include "status_panel.hpp"
#include "../theme.hpp"

namespace minidragon {

void StatusPanel::set_status(const std::string& config_path, const std::string& workspace,
                              const std::string& model, const std::string& provider,
                              int cron_count, bool telegram_enabled, bool http_enabled) {
    config_path_ = config_path;
    workspace_ = workspace;
    model_ = model;
    provider_ = provider;
    cron_count_ = cron_count;
    telegram_enabled_ = telegram_enabled;
    http_enabled_ = http_enabled;
}

void StatusPanel::render() {
    if (!ImGui::Begin("Status Dashboard")) {
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, color_assistant());
    ImGui::Text("MINI DRAGON STATUS");
    ImGui::PopStyleColor();
    ImGui::Separator();

    ImGui::Text("Config:    %s", config_path_.c_str());
    ImGui::Text("Workspace: %s", workspace_.c_str());
    ImGui::Text("Model:     %s", model_.c_str());
    ImGui::Text("Provider:  %s", provider_.c_str());
    ImGui::Text("Cron Jobs: %d", cron_count_);

    ImGui::Spacing();
    ImGui::Text("Channels:");

    ImGui::PushStyleColor(ImGuiCol_Text, http_enabled_ ? color_assistant() : color_error());
    ImGui::BulletText("HTTP: %s", http_enabled_ ? "ONLINE" : "OFFLINE");
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, telegram_enabled_ ? color_assistant() : color_error());
    ImGui::BulletText("Telegram: %s", telegram_enabled_ ? "ONLINE" : "OFFLINE");
    ImGui::PopStyleColor();

    ImGui::End();
}

} // namespace minidragon
