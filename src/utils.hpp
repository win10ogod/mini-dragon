#pragma once
#include <string>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace minidragon {

namespace fs = std::filesystem;

inline std::string home_dir() {
#ifdef _WIN32
    const char* h = std::getenv("USERPROFILE");
    if (!h) h = std::getenv("HOMEDRIVE");
#else
    const char* h = std::getenv("HOME");
#endif
    return h ? std::string(h) : ".";
}

inline std::string expand_path(const std::string& p) {
    if (p.size() >= 2 && p[0] == '~' && (p[1] == '/' || p[1] == '\\')) {
        return home_dir() + p.substr(1);
    }
    return p;
}

inline std::string default_config_path() {
    return home_dir() + "/.minidragon/config.json";
}

inline std::string default_workspace_path() {
    return home_dir() + "/.minidragon/workspace";
}

inline std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline std::string today_str() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

inline int64_t epoch_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string generate_tool_call_id() {
    static int counter = 0;
    return "call_" + std::to_string(epoch_now()) + "_" + std::to_string(counter++);
}

} // namespace minidragon
