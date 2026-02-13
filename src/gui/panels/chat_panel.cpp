#include "chat_panel.hpp"
#include "../theme.hpp"

namespace minidragon {

void ChatPanel::add_message(const std::string& role, const std::string& content, const std::string& tool_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back({role, content, tool_name});
    if (messages_.size() > 500) messages_.pop_front();
    scroll_to_bottom_ = true;
}

void ChatPanel::render(float width, float height) {
    ImGui::BeginChild("ChatRegion", ImVec2(width, height - 60), true);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& msg : messages_) {
            ImVec4 color;
            std::string prefix;

            if (msg.role == "user") {
                color = color_user();
                prefix = "You > ";
            } else if (msg.role == "assistant") {
                color = color_assistant();
                prefix = "Dragon > ";
            } else if (msg.role == "tool") {
                color = color_tool();
                prefix = "[" + msg.tool_name + "] ";
            } else {
                color = color_system();
                prefix = "[system] ";
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s%s", prefix.c_str(), msg.content.c_str());
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
    }

    if (busy_) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.7f, 0.8f, 0.7f + 0.3f * (float)sin(ImGui::GetTime() * 3.0)));
        ImGui::TextWrapped("Dragon is thinking...");
        ImGui::PopStyleColor();
    }

    if (scroll_to_bottom_) {
        ImGui::SetScrollHereY(1.0f);
        scroll_to_bottom_ = false;
    }

    ImGui::EndChild();

    // Input area
    ImGui::Separator();
    float btn_width = 80;
    ImGui::PushItemWidth(width - btn_width - 20);

    bool send = false;
    if (ImGui::InputText("##ChatInput", input_buf_, sizeof(input_buf_),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        send = true;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (ImGui::Button("Send", ImVec2(btn_width, 0)) || send) {
        std::string text(input_buf_);
        if (!text.empty() && !busy_ && send_callback_) {
            add_message("user", text);
            send_callback_(text);
            input_buf_[0] = '\0';
        }
    }

    // Focus input on first frame
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere(-1);
    }
}

} // namespace minidragon
