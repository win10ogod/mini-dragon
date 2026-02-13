#pragma once
#include <imgui.h>
#include <string>

namespace minidragon {

class StatusPanel {
public:
    void render();
    void set_status(const std::string& config_path, const std::string& workspace,
                    const std::string& model, const std::string& provider,
                    int cron_count, bool telegram_enabled, bool http_enabled);

private:
    std::string config_path_ = "Not loaded";
    std::string workspace_ = "Not loaded";
    std::string model_ = "Unknown";
    std::string provider_ = "Unknown";
    int cron_count_ = 0;
    bool telegram_enabled_ = false;
    bool http_enabled_ = false;
};

} // namespace minidragon
