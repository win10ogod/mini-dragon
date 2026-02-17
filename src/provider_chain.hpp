#pragma once
#include "config.hpp"
#include "provider.hpp"
#include "schema_adapter.hpp"
#include <vector>
#include <map>
#include <string>
#include <memory>

namespace minidragon {

// Forward declare — defined in agent.hpp
enum class ProviderErrorKind;
ProviderErrorKind classify_provider_error(const std::string& error_text);

struct ProviderCooldown {
    int64_t until = 0;  // epoch seconds
    ProviderErrorKind reason;
};

class ProviderChain {
public:
    explicit ProviderChain(const Config& cfg);

    // Try providers in order, skip those in cooldown
    ProviderResponse chat(const std::vector<Message>& messages,
                          const nlohmann::json& tools_spec,
                          const std::string& model,
                          int max_tokens, double temperature);

    void chat_stream(const std::vector<Message>& messages,
                     const nlohmann::json& tools_spec,
                     const std::string& model,
                     int max_tokens, double temperature,
                     StreamCallback on_token);

    // Embedding via a specific provider (for memory search)
    EmbeddingResponse embed(const std::vector<std::string>& texts,
                            const std::string& model = "text-embedding-3-small");

    std::string active_provider_name() const;
    size_t provider_count() const { return providers_.size(); }

private:
    Config config_;
    std::vector<std::pair<std::string, Provider>> providers_;  // name → Provider
    std::map<std::string, ProviderCooldown> cooldowns_;
    std::string last_active_;

    // Embedding provider (may differ from chat providers)
    std::unique_ptr<Provider> embed_provider_;

    int cooldown_for(ProviderErrorKind kind) const;
    void mark_cooldown(const std::string& name, ProviderErrorKind kind);
    bool in_cooldown(const std::string& name) const;
};

} // namespace minidragon
