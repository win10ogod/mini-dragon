#pragma once
#include <imgui.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

namespace minidragon {

struct ChatMessage {
    std::string role;       // "user", "assistant", "tool", "system"
    std::string content;
    std::string tool_name;  // For tool messages
};

class ChatPanel {
public:
    void render(float width, float height);
    void add_message(const std::string& role, const std::string& content, const std::string& tool_name = "");
    void set_send_callback(std::function<void(const std::string&)> cb) { send_callback_ = std::move(cb); }
    void set_busy(bool busy) { busy_ = busy; }
    void clear() { std::lock_guard<std::mutex> lock(mutex_); messages_.clear(); }

private:
    std::mutex mutex_;
    std::deque<ChatMessage> messages_;
    char input_buf_[4096] = {};
    bool scroll_to_bottom_ = false;
    std::atomic<bool> busy_{false};
    std::function<void(const std::string&)> send_callback_;
};

} // namespace minidragon
