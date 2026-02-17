#include "memory_search.hpp"
#include "utils.hpp"
#include <sqlite3.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <set>

namespace minidragon {

MemorySearchStore::MemorySearchStore(const std::string& db_path, int dimensions)
    : dimensions_(dimensions) {
    fs::create_directories(fs::path(db_path).parent_path());

    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "[memory_search] Failed to open database: " << sqlite3_errmsg(db_) << "\n";
        db_ = nullptr;
        return;
    }

    init_tables();
}

MemorySearchStore::~MemorySearchStore() {
    if (db_) sqlite3_close(db_);
}

void MemorySearchStore::init_tables() {
    if (!db_) return;

    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS memories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            content TEXT NOT NULL,
            source TEXT,
            created_at INTEGER,
            embedding BLOB
        );

        CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(
            content, content='memories', content_rowid='id'
        );

        CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN
            INSERT INTO memories_fts(rowid, content) VALUES (new.id, new.content);
        END;

        CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN
            INSERT INTO memories_fts(memories_fts, rowid, content)
                VALUES ('delete', old.id, old.content);
        END;

        CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN
            INSERT INTO memories_fts(memories_fts, rowid, content)
                VALUES ('delete', old.id, old.content);
            INSERT INTO memories_fts(rowid, content) VALUES (new.id, new.content);
        END;
    )SQL";

    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "[memory_search] Schema init error: " << (err ? err : "unknown") << "\n";
        if (err) sqlite3_free(err);
    }
}

std::vector<uint8_t> MemorySearchStore::vector_to_blob(const std::vector<float>& v) {
    std::vector<uint8_t> blob(v.size() * sizeof(float));
    std::memcpy(blob.data(), v.data(), blob.size());
    return blob;
}

std::vector<float> MemorySearchStore::blob_to_vector(const void* data, int bytes) {
    int count = bytes / static_cast<int>(sizeof(float));
    std::vector<float> v(count);
    std::memcpy(v.data(), data, count * sizeof(float));
    return v;
}

float MemorySearchStore::cosine_similarity(const std::vector<float>& a,
                                            const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < a.size(); i++) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (denom < 1e-8f) return 0.0f;
    return dot / denom;
}

void MemorySearchStore::upsert(const std::string& content, const std::string& source,
                                const std::vector<float>& embedding) {
    if (!db_) return;

    const char* sql = "INSERT INTO memories (content, source, created_at, embedding) VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[memory_search] upsert prepare error: " << sqlite3_errmsg(db_) << "\n";
        return;
    }

    sqlite3_bind_text(stmt, 1, content.c_str(), static_cast<int>(content.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, source.c_str(), static_cast<int>(source.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, epoch_now());

    if (!embedding.empty()) {
        auto blob = vector_to_blob(embedding);
        sqlite3_bind_blob(stmt, 4, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 4);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "[memory_search] upsert error: " << sqlite3_errmsg(db_) << "\n";
    }
    sqlite3_finalize(stmt);
}

std::vector<MemoryEntry> MemorySearchStore::search(const std::string& query,
                                                     const std::vector<float>& query_embedding,
                                                     int limit) {
    if (!db_) return {};

    // Step 1: Get candidate rows via FTS5 (top N*3 to have room for re-ranking)
    int candidate_limit = limit * 3;
    const char* fts_sql = R"SQL(
        SELECT m.id, m.content, m.source, m.created_at, m.embedding,
               rank AS fts_rank
        FROM memories_fts f
        JOIN memories m ON m.id = f.rowid
        WHERE memories_fts MATCH ?
        ORDER BY rank
        LIMIT ?
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, fts_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[memory_search] FTS prepare error: " << sqlite3_errmsg(db_) << "\n";
        return search_text(query, limit);  // fallback
    }

    sqlite3_bind_text(stmt, 1, query.c_str(), static_cast<int>(query.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, candidate_limit);

    struct Candidate {
        MemoryEntry entry;
        float fts_score;
        float vector_score;
    };
    std::vector<Candidate> candidates;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Candidate c;
        c.entry.id = sqlite3_column_int64(stmt, 0);
        c.entry.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        c.entry.source = src ? src : "";
        c.entry.created_at = sqlite3_column_int64(stmt, 3);

        // FTS score: convert BM25 rank to [0,1]
        double bm25_rank = sqlite3_column_double(stmt, 5);
        c.fts_score = 1.0f / (1.0f + static_cast<float>(std::abs(bm25_rank)));

        // Vector score
        c.vector_score = 0.0f;
        if (sqlite3_column_type(stmt, 4) == SQLITE_BLOB && !query_embedding.empty()) {
            const void* blob_data = sqlite3_column_blob(stmt, 4);
            int blob_bytes = sqlite3_column_bytes(stmt, 4);
            if (blob_data && blob_bytes > 0) {
                auto row_embedding = blob_to_vector(blob_data, blob_bytes);
                c.vector_score = cosine_similarity(query_embedding, row_embedding);
                // Normalize to [0,1] — cosine similarity is already [-1,1], shift to [0,1]
                c.vector_score = (c.vector_score + 1.0f) / 2.0f;
            }
        }

        // Hybrid score: 0.7 * vector + 0.3 * fts
        c.entry.score = 0.7f * c.vector_score + 0.3f * c.fts_score;
        candidates.push_back(std::move(c));
    }
    sqlite3_finalize(stmt);

    // Also fetch rows that have embeddings but didn't match FTS (pure vector search)
    if (!query_embedding.empty()) {
        const char* vec_sql = "SELECT id, content, source, created_at, embedding FROM memories WHERE embedding IS NOT NULL LIMIT 100";
        if (sqlite3_prepare_v2(db_, vec_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::set<int64_t> seen;
            for (auto& c : candidates) seen.insert(c.entry.id);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int64_t id = sqlite3_column_int64(stmt, 0);
                if (seen.count(id)) continue;

                Candidate c;
                c.entry.id = id;
                c.entry.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                const char* src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                c.entry.source = src ? src : "";
                c.entry.created_at = sqlite3_column_int64(stmt, 3);
                c.fts_score = 0.0f;

                const void* blob_data = sqlite3_column_blob(stmt, 4);
                int blob_bytes = sqlite3_column_bytes(stmt, 4);
                if (blob_data && blob_bytes > 0) {
                    auto row_embedding = blob_to_vector(blob_data, blob_bytes);
                    c.vector_score = cosine_similarity(query_embedding, row_embedding);
                    c.vector_score = (c.vector_score + 1.0f) / 2.0f;
                } else {
                    c.vector_score = 0.0f;
                }

                c.entry.score = 0.7f * c.vector_score + 0.3f * c.fts_score;
                if (c.entry.score > 0.3f) {  // threshold to avoid noise
                    candidates.push_back(std::move(c));
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    // Sort by final score descending
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.entry.score > b.entry.score; });

    std::vector<MemoryEntry> results;
    for (int i = 0; i < limit && i < static_cast<int>(candidates.size()); i++) {
        results.push_back(std::move(candidates[i].entry));
    }
    return results;
}

std::vector<MemoryEntry> MemorySearchStore::search_text(const std::string& query, int limit) {
    if (!db_) return {};

    const char* sql = R"SQL(
        SELECT m.id, m.content, m.source, m.created_at, rank
        FROM memories_fts f
        JOIN memories m ON m.id = f.rowid
        WHERE memories_fts MATCH ?
        ORDER BY rank
        LIMIT ?
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[memory_search] text search prepare error: " << sqlite3_errmsg(db_) << "\n";
        return {};
    }

    sqlite3_bind_text(stmt, 1, query.c_str(), static_cast<int>(query.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    std::vector<MemoryEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry e;
        e.id = sqlite3_column_int64(stmt, 0);
        e.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        e.source = src ? src : "";
        e.created_at = sqlite3_column_int64(stmt, 3);
        double bm25_rank = sqlite3_column_double(stmt, 4);
        e.score = 1.0f / (1.0f + static_cast<float>(std::abs(bm25_rank)));
        results.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return results;
}

} // namespace minidragon
