#pragma once
#include "message.hpp"
#include "utils.hpp"
#include <string>
#include <vector>
#include <fstream>

namespace minidragon {

class SessionLogger {
public:
    explicit SessionLogger(const std::string& sessions_dir) : dir_(sessions_dir) {
        fs::create_directories(dir_);
    }

    void log(const Message& msg) {
        std::string file = current_file();
        std::ofstream f(file, std::ios::app);
        f << msg.to_json().dump() << "\n";
    }

    std::vector<Message> load_recent(int count = 20) {
        std::vector<Message> result;
        std::string file = current_file();
        if (!fs::exists(file)) return result;

        std::ifstream f(file);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty()) lines.push_back(line);
        }

        int start = static_cast<int>(lines.size()) - count;
        if (start < 0) start = 0;
        for (int i = start; i < static_cast<int>(lines.size()); i++) {
            try {
                auto j = nlohmann::json::parse(lines[i]);
                result.push_back(Message::from_json(j));
            } catch (...) {}
        }
        return result;
    }

private:
    std::string dir_;
    std::string current_file() {
        return dir_ + "/" + today_str() + ".jsonl";
    }
};

} // namespace minidragon
