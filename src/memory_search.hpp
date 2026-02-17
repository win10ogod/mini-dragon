#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct sqlite3;

namespace minidragon {

struct MemoryEntry {
    int64_t id;
    std::string content;
    std::string source;      // "daily:2024-01-15" or "long_term"
    int64_t created_at;
    float score;             // search result score
};

class MemorySearchStore {
public:
    MemorySearchStore(const std::string& db_path, int dimensions = 1536);
    ~MemorySearchStore();

    // Non-copyable
    MemorySearchStore(const MemorySearchStore&) = delete;
    MemorySearchStore& operator=(const MemorySearchStore&) = delete;

    void upsert(const std::string& content, const std::string& source,
                const std::vector<float>& embedding);

    // Hybrid search: FTS5 + vector cosine similarity
    std::vector<MemoryEntry> search(const std::string& query,
                                     const std::vector<float>& query_embedding,
                                     int limit = 5);

    // Text-only search (when embeddings unavailable)
    std::vector<MemoryEntry> search_text(const std::string& query, int limit = 5);

private:
    sqlite3* db_ = nullptr;
    int dimensions_;

    void init_tables();
    float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);
    std::vector<float> blob_to_vector(const void* data, int bytes);
    std::vector<uint8_t> vector_to_blob(const std::vector<float>& v);
};

} // namespace minidragon
