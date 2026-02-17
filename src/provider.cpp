#include "provider.hpp"
#include <iostream>

namespace minidragon {

static void parse_url(const std::string& url, std::string& scheme, std::string& host, int& port, std::string& path_prefix) {
    scheme = "http";
    host = "127.0.0.1";
    port = 80;
    path_prefix = "";

    size_t pos = 0;
    if (url.substr(0, 8) == "https://") {
        scheme = "https"; pos = 8; port = 443;
    } else if (url.substr(0, 7) == "http://") {
        scheme = "http"; pos = 7; port = 80;
    }

    size_t slash = url.find('/', pos);
    std::string host_port = (slash != std::string::npos) ? url.substr(pos, slash - pos) : url.substr(pos);
    if (slash != std::string::npos) {
        path_prefix = url.substr(slash);
        while (!path_prefix.empty() && path_prefix.back() == '/') path_prefix.pop_back();
    }

    size_t colon = host_port.find(':');
    if (colon != std::string::npos) {
        host = host_port.substr(0, colon);
        port = std::stoi(host_port.substr(colon + 1));
    } else {
        host = host_port;
    }
}

Provider::Provider(const ProviderConfig& cfg) : config_(cfg) {
    parse_url(config_.api_base, scheme_, host_, port_, path_prefix_);
    base_url_ = scheme_ + "://" + host_ + ":" + std::to_string(port_);
}

// ── Hand-rolled JSON fix (no regex) ──────────────────────────────────

static std::string fix_json(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_string = false;
    bool escape = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (escape) { out += c; escape = false; continue; }
        if (c == '\\' && in_string) { out += c; escape = true; continue; }
        if (c == '"') { in_string = !in_string; out += c; continue; }
        if (in_string) { out += c; continue; }
        // Outside string: skip trailing commas before } or ]
        if (c == ',') {
            // Look ahead (skip whitespace) for } or ]
            size_t j = i + 1;
            while (j < s.size() && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n' || s[j] == '\r')) j++;
            if (j < s.size() && (s[j] == '}' || s[j] == ']')) continue; // skip comma
        }
        out += c;
    }
    return out;
}

// ── Tool call parsing helpers (no regex) ─────────────────────────────

static bool try_parse_tool_call(const nlohmann::json& j, ToolCall& tc, int idx) {
    tc.id = "tc_" + std::to_string(idx);
    tc.name = j.value("name", "");
    if (tc.name.empty()) return false;
    if (j.contains("arguments")) {
        tc.arguments = j["arguments"].is_string() ? j["arguments"].get<std::string>() : j["arguments"].dump();
    } else if (j.contains("parameters")) {
        tc.arguments = j["parameters"].is_string() ? j["parameters"].get<std::string>() : j["parameters"].dump();
    }
    return true;
}

// Find matching closing brace for a JSON object starting at pos (s[pos] == '{')
static size_t find_json_object_end(const std::string& s, size_t pos) {
    if (pos >= s.size() || s[pos] != '{') return std::string::npos;
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    for (size_t i = pos; i < s.size(); i++) {
        char c = s[i];
        if (esc) { esc = false; continue; }
        if (c == '\\' && in_str) { esc = true; continue; }
        if (c == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth == 0) return i; }
    }
    return std::string::npos;
}

// Extract all JSON objects between <tag>...</tag>
static std::vector<ToolCall> parse_tagged_tool_calls(const std::string& text,
                                                      const std::string& open_tag,
                                                      const std::string& close_tag) {
    std::vector<ToolCall> calls;
    size_t pos = 0;
    int idx = 0;
    while (pos < text.size()) {
        size_t tag_start = text.find(open_tag, pos);
        if (tag_start == std::string::npos) break;
        size_t content_start = tag_start + open_tag.size();

        size_t tag_end = text.find(close_tag, content_start);
        if (tag_end == std::string::npos) break;

        std::string inner = text.substr(content_start, tag_end - content_start);
        // Find first { in inner
        size_t brace = inner.find('{');
        if (brace != std::string::npos) {
            size_t brace_end = find_json_object_end(inner, brace);
            if (brace_end != std::string::npos) {
                std::string json_str = inner.substr(brace, brace_end - brace + 1);
                try {
                    auto j = nlohmann::json::parse(fix_json(json_str));
                    ToolCall tc;
                    if (try_parse_tool_call(j, tc, idx++)) calls.push_back(std::move(tc));
                } catch (...) {}
            }
        }
        pos = tag_end + close_tag.size();
    }
    return calls;
}

// Parse ```json blocks containing tool calls
static std::vector<ToolCall> parse_markdown_json_blocks(const std::string& text) {
    std::vector<ToolCall> calls;
    size_t pos = 0;
    int idx = 0;
    while (pos < text.size()) {
        // Find opening ```
        size_t fence_start = text.find("```", pos);
        if (fence_start == std::string::npos) break;

        // Check for optional language tag (json, tool)
        size_t line_end = text.find('\n', fence_start);
        if (line_end == std::string::npos) break;

        std::string lang = text.substr(fence_start + 3, line_end - fence_start - 3);
        // Trim whitespace
        while (!lang.empty() && (lang.front() == ' ' || lang.front() == '\t')) lang.erase(lang.begin());
        while (!lang.empty() && (lang.back() == ' ' || lang.back() == '\t' || lang.back() == '\r')) lang.pop_back();

        // Find closing ```
        size_t fence_end = text.find("\n```", line_end);
        if (fence_end == std::string::npos) { pos = line_end; continue; }

        std::string block = text.substr(line_end + 1, fence_end - line_end - 1);
        pos = fence_end + 4;

        // Only parse json/tool blocks, or blocks that start with {
        if (!lang.empty() && lang != "json" && lang != "tool") continue;

        // Find JSON object in block
        size_t brace = block.find('{');
        if (brace == std::string::npos) continue;
        size_t brace_end = find_json_object_end(block, brace);
        if (brace_end == std::string::npos) continue;

        try {
            auto j = nlohmann::json::parse(fix_json(block.substr(brace, brace_end - brace + 1)));
            if (!j.contains("name")) continue;
            ToolCall tc;
            if (try_parse_tool_call(j, tc, idx++)) calls.push_back(std::move(tc));
        } catch (...) {}
    }
    return calls;
}

// Strip tool-call tags from content (no regex)
static std::string strip_tool_content(const std::string& text) {
    std::string result = text;
    // Strip <toolcall>...</toolcall>
    for (;;) {
        size_t s = result.find("<toolcall>");
        if (s == std::string::npos) break;
        size_t e = result.find("</toolcall>", s);
        if (e == std::string::npos) break;
        result.erase(s, e + 11 - s);
    }
    // Strip <tool_call>...</tool_call>
    for (;;) {
        size_t s = result.find("<tool_call>");
        if (s == std::string::npos) break;
        size_t e = result.find("</tool_call>", s);
        if (e == std::string::npos) break;
        result.erase(s, e + 12 - s);
    }
    // Trim trailing whitespace
    while (!result.empty() && (result.back() == ' ' || result.back() == '\n'))
        result.pop_back();
    return result;
}

ProviderResponse Provider::chat(const std::vector<Message>& messages,
                                const nlohmann::json& tools_spec,
                                const std::string& model,
                                int max_tokens, double temperature) {
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(30);
    cli.set_read_timeout(120);

    nlohmann::json body;
    body["model"] = model;
    body["max_tokens"] = max_tokens;
    body["temperature"] = temperature;

    auto& msgs = body["messages"];
    for (auto& m : messages) {
        msgs.push_back(m.to_json());
    }

    if (!tools_spec.empty() && tools_spec.is_array() && tools_spec.size() > 0) {
        body["tools"] = tools_spec;
    }

    std::string path = path_prefix_ + "/chat/completions";
    std::string payload = body.dump();

    httplib::Headers headers = {
        {"Content-Type", "application/json"}
    };
    if (!config_.api_key.empty()) {
        headers.emplace("Authorization", "Bearer " + config_.api_key);
    }

    auto res = cli.Post(path, headers, payload, "application/json");
    if (!res) {
        throw std::runtime_error("Provider request failed: connection error");
    }
    if (res->status != 200) {
        throw std::runtime_error("Provider returned status " + std::to_string(res->status) + ": " + res->body);
    }

    ProviderResponse resp;
    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("choices") && !j["choices"].empty()) {
            auto& msg = j["choices"][0]["message"];
            resp.content = msg.contains("content") && !msg["content"].is_null()
                           ? msg["content"].get<std::string>() : "";

            // Standard OpenAI tool_calls format
            if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                for (auto& tc : msg["tool_calls"]) {
                    ToolCall t;
                    t.id = tc.value("id", "");
                    if (tc.contains("function")) {
                        t.name = tc["function"].value("name", "");
                        t.arguments = tc["function"].value("arguments", "");
                    }
                    if (!t.name.empty()) resp.tool_calls.push_back(std::move(t));
                }
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse provider response: ") + e.what());
    }

    // Fallback parsing: try multiple formats if no standard tool_calls
    if (resp.tool_calls.empty() && !resp.content.empty()) {
        // Try format 1: <toolcall>...</toolcall>
        auto fallback = parse_tagged_tool_calls(resp.content, "<toolcall>", "</toolcall>");

        // Try format 2: <tool_call>...</tool_call> (Qwen)
        if (fallback.empty()) {
            fallback = parse_tagged_tool_calls(resp.content, "<tool_call>", "</tool_call>");
        }

        // Try format 3: ```json blocks with "name" field
        if (fallback.empty()) {
            fallback = parse_markdown_json_blocks(resp.content);
        }

        if (!fallback.empty()) {
            resp.tool_calls = std::move(fallback);
            resp.content = strip_tool_content(resp.content);
        }
    }

    return resp;
}

void Provider::chat_stream(const std::vector<Message>& messages,
                           const nlohmann::json& tools_spec,
                           const std::string& model,
                           int max_tokens, double temperature,
                           StreamCallback on_token) {
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(30);
    cli.set_read_timeout(120);

    nlohmann::json body;
    body["model"] = model;
    body["max_tokens"] = max_tokens;
    body["temperature"] = temperature;
    body["stream"] = true;

    auto& msgs = body["messages"];
    for (auto& m : messages) {
        msgs.push_back(m.to_json());
    }

    if (!tools_spec.empty() && tools_spec.is_array() && tools_spec.size() > 0) {
        body["tools"] = tools_spec;
    }

    std::string path = path_prefix_ + "/chat/completions";
    std::string payload = body.dump();

    httplib::Headers headers = {
        {"Content-Type", "application/json"}
    };
    if (!config_.api_key.empty()) {
        headers.emplace("Authorization", "Bearer " + config_.api_key);
    }

    auto res = cli.Post(path, headers, payload, "application/json");
    if (!res) {
        throw std::runtime_error("Provider stream request failed: connection error");
    }
    if (res->status != 200) {
        throw std::runtime_error("Provider stream returned status " + std::to_string(res->status));
    }

    // Parse SSE events from the buffered response body
    std::string& data = res->body;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t event_end = data.find("\n\n", pos);
        if (event_end == std::string::npos) event_end = data.size();

        std::string event = data.substr(pos, event_end - pos);
        pos = event_end + 2;

        // Parse lines in this event
        size_t line_start = 0;
        while (line_start < event.size()) {
            size_t line_end = event.find('\n', line_start);
            if (line_end == std::string::npos) line_end = event.size();
            std::string line = event.substr(line_start, line_end - line_start);
            line_start = line_end + 1;

            if (line.size() > 6 && line.substr(0, 6) == "data: ") {
                std::string data_str = line.substr(6);
                if (data_str == "[DONE]") {
                    on_token("", true);
                    return;
                }
                try {
                    auto j = nlohmann::json::parse(data_str);
                    if (j.contains("choices") && !j["choices"].empty()) {
                        auto& delta = j["choices"][0]["delta"];
                        if (delta.contains("content")) {
                            std::string token = delta["content"].get<std::string>();
                            if (!token.empty()) {
                                on_token(token, false);
                            }
                        }
                    }
                } catch (...) {}
            }
        }
    }
    on_token("", true);
}

EmbeddingResponse Provider::embed(const std::vector<std::string>& texts,
                                   const std::string& model) {
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(30);
    cli.set_read_timeout(60);

    nlohmann::json body;
    body["model"] = model;
    body["input"] = texts;

    std::string path = path_prefix_ + "/embeddings";
    std::string payload = body.dump();

    httplib::Headers headers = {
        {"Content-Type", "application/json"}
    };
    if (!config_.api_key.empty()) {
        headers.emplace("Authorization", "Bearer " + config_.api_key);
    }

    auto res = cli.Post(path, headers, payload, "application/json");
    if (!res) {
        throw std::runtime_error("Embedding request failed: connection error");
    }
    if (res->status != 200) {
        throw std::runtime_error("Embedding returned status " + std::to_string(res->status) + ": " + res->body);
    }

    EmbeddingResponse resp;
    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("data") && j["data"].is_array()) {
            for (auto& item : j["data"]) {
                if (item.contains("embedding") && item["embedding"].is_array()) {
                    std::vector<float> vec;
                    vec.reserve(item["embedding"].size());
                    for (auto& v : item["embedding"]) {
                        vec.push_back(v.get<float>());
                    }
                    resp.embeddings.push_back(std::move(vec));
                }
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse embedding response: ") + e.what());
    }
    return resp;
}

} // namespace minidragon
