#include "cron_store.hpp"
#include "utils.hpp"
#include <stdexcept>
#include <filesystem>
#include <ctime>
#include <sstream>
#include <chrono>

namespace minidragon {

CronStore::CronStore(const std::string& db_path) {
    fs::create_directories(fs::path(db_path).parent_path());
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to open cron DB: " + std::string(sqlite3_errmsg(db_)));
    }
    init_db();
}

CronStore::~CronStore() {
    if (db_) sqlite3_close(db_);
}

void CronStore::init_db() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS cron_jobs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            message TEXT NOT NULL,
            schedule_type TEXT NOT NULL,
            interval_seconds INTEGER DEFAULT 0,
            cron_expr TEXT DEFAULT '',
            last_run INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT 0
        );
    )";
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("Failed to init cron DB: " + msg);
    }
}

int64_t CronStore::add(const CronJob& job) {
    const char* sql = "INSERT INTO cron_jobs (name, message, schedule_type, interval_seconds, cron_expr, last_run, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, job.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, job.message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, job.schedule_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, job.interval_seconds);
    sqlite3_bind_text(stmt, 5, job.cron_expr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, job.last_run);
    sqlite3_bind_int64(stmt, 7, job.created_at);

    int rc = sqlite3_step(stmt);
    int64_t id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to add cron job");
    }
    return id;
}

std::vector<CronJob> CronStore::list() {
    std::vector<CronJob> jobs;
    const char* sql = "SELECT id, name, message, schedule_type, interval_seconds, cron_expr, last_run, created_at FROM cron_jobs ORDER BY id";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CronJob j;
        j.id = sqlite3_column_int64(stmt, 0);
        j.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        j.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        j.schedule_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        j.interval_seconds = sqlite3_column_int64(stmt, 4);
        j.cron_expr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        j.last_run = sqlite3_column_int64(stmt, 6);
        j.created_at = sqlite3_column_int64(stmt, 7);
        jobs.push_back(std::move(j));
    }
    sqlite3_finalize(stmt);
    return jobs;
}

bool CronStore::remove(int64_t id) {
    const char* sql = "DELETE FROM cron_jobs WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, id);
    sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return changes > 0;
}

static bool cron_matches_now(const std::string& expr) {
    std::vector<std::string> fields;
    std::istringstream iss(expr);
    std::string field;
    while (iss >> field && fields.size() < 5) {
        fields.push_back(field);
    }
    if (fields.size() < 5) return false;

    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now_t);
#else
    localtime_r(&now_t, &tm);
#endif

    int values[5] = {tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon + 1, tm.tm_wday};

    auto match_field = [](const std::string& f, int val) -> bool {
        if (f == "*") return true;
        try {
            return std::stoi(f) == val;
        } catch (...) {}
        if (f.size() > 2 && f[0] == '*' && f[1] == '/') {
            try {
                int step = std::stoi(f.substr(2));
                return step > 0 && (val % step) == 0;
            } catch (...) {}
        }
        return false;
    };

    for (int i = 0; i < 5; i++) {
        if (!match_field(fields[i], values[i])) return false;
    }
    return true;
}

std::vector<CronJob> CronStore::due_jobs() {
    auto all = list();
    std::vector<CronJob> due;
    int64_t now = epoch_now();

    for (auto& j : all) {
        if (j.schedule_type == "every") {
            if (j.interval_seconds > 0 && (now - j.last_run) >= j.interval_seconds) {
                due.push_back(j);
            }
        } else if (j.schedule_type == "cron") {
            if (cron_matches_now(j.cron_expr) && (now - j.last_run) >= 60) {
                due.push_back(j);
            }
        }
    }
    return due;
}

void CronStore::update_last_run(int64_t id, int64_t ts) {
    const char* sql = "UPDATE cron_jobs SET last_run = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, ts);
    sqlite3_bind_int64(stmt, 2, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

} // namespace minidragon
