#pragma once
#include "utils.hpp"
#include <string>
#include <filesystem>
#include <fstream>

namespace minidragon {

class MemoryStore {
public:
    explicit MemoryStore(const std::string& workspace)
        : workspace_(workspace)
        , memory_dir_(workspace + "/memory")
    {
        fs::create_directories(memory_dir_);
    }

    std::string read_today() {
        return read_file(memory_dir_ + "/" + today_str() + ".md");
    }

    void append_today(const std::string& content) {
        std::string path = memory_dir_ + "/" + today_str() + ".md";
        std::ofstream f(path, std::ios::app);
        f << content << "\n";
    }

    std::string read_long_term() {
        return read_file(workspace_ + "/MEMORY.md");
    }

    void write_long_term(const std::string& content) {
        std::ofstream f(workspace_ + "/MEMORY.md");
        f << content;
    }

    std::string get_recent(int days = 7) {
        std::string result;
        auto now = std::chrono::system_clock::now();

        for (int d = 0; d < days; ++d) {
            auto day = now - std::chrono::hours(24 * d);
            auto time_t = std::chrono::system_clock::to_time_t(day);
            struct tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &time_t);
#else
            localtime_r(&time_t, &tm_buf);
#endif
            char buf[11];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
            std::string content = read_file(memory_dir_ + "/" + buf + ".md");
            if (!content.empty()) {
                result += "--- " + std::string(buf) + " ---\n" + content + "\n\n";
            }
        }
        return result;
    }

    std::string get_context() {
        std::string ctx;
        std::string lt = read_long_term();
        if (!lt.empty()) {
            ctx += "--- Long-term Memory ---\n" + lt + "\n\n";
        }
        std::string today = read_today();
        if (!today.empty()) {
            ctx += "--- Today's Memory ---\n" + today + "\n\n";
        }
        return ctx;
    }

private:
    std::string workspace_;
    std::string memory_dir_;
};

} // namespace minidragon
