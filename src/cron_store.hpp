#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <sqlite3.h>

namespace minidragon {

struct CronJob {
    int64_t id = 0;
    std::string name;
    std::string message;
    std::string schedule_type; // "every" or "cron"
    int64_t interval_seconds = 0;
    std::string cron_expr;
    int64_t last_run = 0;
    int64_t created_at = 0;
};

class CronStore {
public:
    explicit CronStore(const std::string& db_path);
    ~CronStore();

    CronStore(const CronStore&) = delete;
    CronStore& operator=(const CronStore&) = delete;

    int64_t add(const CronJob& job);
    std::vector<CronJob> list();
    bool remove(int64_t id);
    std::vector<CronJob> due_jobs();
    void update_last_run(int64_t id, int64_t ts);

private:
    sqlite3* db_ = nullptr;
    void init_db();
};

} // namespace minidragon
