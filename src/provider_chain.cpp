#include "provider_chain.hpp"
#include "agent.hpp"  // for classify_provider_error, ProviderErrorKind
#include "utils.hpp"
#include <iostream>
#include <stdexcept>

namespace minidragon {

ProviderChain::ProviderChain(const Config& cfg) : config_(cfg) {
    if (cfg.fallback.enabled && !cfg.fallback.provider_order.empty()) {
        // Build providers in fallback order
        for (auto& name : cfg.fallback.provider_order) {
            auto it = cfg.providers.find(name);
            if (it != cfg.providers.end()) {
                providers_.emplace_back(name, Provider(it->second));
            }
        }
    }

    // If no fallback providers configured, use the resolved default
    if (providers_.empty()) {
        auto resolved = cfg.resolve_provider();
        std::string name = cfg.provider.empty() ? "default" : cfg.provider;
        providers_.emplace_back(name, Provider(resolved));
    }

    last_active_ = providers_.front().first;

    // Set up embedding provider (separate from chat providers)
    if (cfg.embedding.enabled && !cfg.embedding.provider.empty()) {
        auto it = cfg.providers.find(cfg.embedding.provider);
        if (it != cfg.providers.end()) {
            embed_provider_ = std::make_unique<Provider>(it->second);
        }
    }
}

int ProviderChain::cooldown_for(ProviderErrorKind kind) const {
    switch (kind) {
    case ProviderErrorKind::rate_limit:  return config_.fallback.rate_limit_cooldown;
    case ProviderErrorKind::billing:     return config_.fallback.billing_cooldown;
    case ProviderErrorKind::auth:        return config_.fallback.auth_cooldown;
    case ProviderErrorKind::timeout:     return config_.fallback.timeout_cooldown;
    default:                             return 30;
    }
}

void ProviderChain::mark_cooldown(const std::string& name, ProviderErrorKind kind) {
    int secs = cooldown_for(kind);
    cooldowns_[name] = ProviderCooldown{epoch_now() + secs, kind};
}

bool ProviderChain::in_cooldown(const std::string& name) const {
    auto it = cooldowns_.find(name);
    if (it == cooldowns_.end()) return false;
    return epoch_now() < it->second.until;
}

std::string ProviderChain::active_provider_name() const {
    return last_active_;
}

ProviderResponse ProviderChain::chat(const std::vector<Message>& messages,
                                      const nlohmann::json& tools_spec,
                                      const std::string& model,
                                      int max_tokens, double temperature) {
    std::string last_error;

    for (auto& [name, provider] : providers_) {
        if (in_cooldown(name)) continue;

        // Adapt schema for this provider's flavor
        auto flavor = detect_schema_flavor(provider.config().api_base);
        auto adapted = adapt_tools_schema(tools_spec, flavor);

        try {
            auto resp = provider.chat(messages, adapted, model, max_tokens, temperature);
            last_active_ = name;
            return resp;
        } catch (const std::exception& e) {
            last_error = e.what();
            auto kind = classify_provider_error(last_error);

            if (config_.fallback.enabled && providers_.size() > 1) {
                std::cerr << "[fallback] Provider '" << name << "' failed: " << last_error
                          << " — trying next\n";
                mark_cooldown(name, kind);
                continue;
            }
            throw;  // single provider — just rethrow
        }
    }

    throw std::runtime_error("All providers exhausted. Last error: " + last_error);
}

void ProviderChain::chat_stream(const std::vector<Message>& messages,
                                 const nlohmann::json& tools_spec,
                                 const std::string& model,
                                 int max_tokens, double temperature,
                                 StreamCallback on_token) {
    std::string last_error;

    for (auto& [name, provider] : providers_) {
        if (in_cooldown(name)) continue;

        auto flavor = detect_schema_flavor(provider.config().api_base);
        auto adapted = adapt_tools_schema(tools_spec, flavor);

        try {
            provider.chat_stream(messages, adapted, model, max_tokens, temperature, on_token);
            last_active_ = name;
            return;
        } catch (const std::exception& e) {
            last_error = e.what();
            auto kind = classify_provider_error(last_error);

            if (config_.fallback.enabled && providers_.size() > 1) {
                std::cerr << "[fallback] Provider '" << name << "' stream failed: " << last_error
                          << " — trying next\n";
                mark_cooldown(name, kind);
                continue;
            }
            throw;
        }
    }

    throw std::runtime_error("All providers exhausted (stream). Last error: " + last_error);
}

EmbeddingResponse ProviderChain::embed(const std::vector<std::string>& texts,
                                        const std::string& model) {
    if (embed_provider_) {
        return embed_provider_->embed(texts, model);
    }
    // Fallback: use the first chat provider
    if (!providers_.empty()) {
        return providers_.front().second.embed(texts, model);
    }
    throw std::runtime_error("No provider available for embeddings");
}

} // namespace minidragon
