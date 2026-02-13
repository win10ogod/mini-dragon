#include "provider.hpp"
#include <iostream>
#include <regex>

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

// Try to fix common JSON issues before parsing
static std::string fix_json(const std::string& s) {
    std::string fixed = s;
    // Remove trailing commas before } or ]
    std::regex trailing_comma(R"(,\s*([}\]]))");
    fixed = std::regex_replace(fixed, trailing_comma, "$1");
    return fixed;
}

// Try to parse a tool call from a JSON object
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

// Format 1: <toolcall>{"name":"...","arguments":{...}}</toolcall>
static std::vector<ToolCall> parse_toolcall_tags(const std::string& text) {
    std::vector<ToolCall> calls;
    std::regex re(R"(<toolcall>\s*(\{[\s\S]*?\})\s*</toolcall>)");
    auto begin = std::sregex_iterator(text.begin(), text.end(), re);
    auto end = std::sregex_iterator();
    int idx = 0;
    for (auto it = begin; it != end; ++it, ++idx) {
        try {
            auto j = nlohmann::json::parse(fix_json((*it)[1].str()));
            ToolCall tc;
            if (try_parse_tool_call(j, tc, idx)) calls.push_back(std::move(tc));
        } catch (...) {}
    }
    return calls;
}

// Format 2: <tool_call>{"name":"...","arguments":{...}}</tool_call> (Qwen style)
static std::vector<ToolCall> parse_tool_call_tags(const std::string& text) {
    std::vector<ToolCall> calls;
    std::regex re(R"(<tool_call>\s*(\{[\s\S]*?\})\s*</tool_call>)");
    auto begin = std::sregex_iterator(text.begin(), text.end(), re);
    auto end = std::sregex_iterator();
    int idx = 0;
    for (auto it = begin; it != end; ++it, ++idx) {
        try {
            auto j = nlohmann::json::parse(fix_json((*it)[1].str()));
            ToolCall tc;
            if (try_parse_tool_call(j, tc, idx)) calls.push_back(std::move(tc));
        } catch (...) {}
    }
    return calls;
}

// Format 3: ```json\n{"name":"...","arguments":{...}}\n```  (markdown code blocks)
static std::vector<ToolCall> parse_markdown_json_blocks(const std::string& text) {
    std::vector<ToolCall> calls;
    std::regex re(R"(```(?:json|tool)?\s*\n(\{[\s\S]*?\})\s*\n```)");
    auto begin = std::sregex_iterator(text.begin(), text.end(), re);
    auto end = std::sregex_iterator();
    int idx = 0;
    for (auto it = begin; it != end; ++it, ++idx) {
        try {
            auto j = nlohmann::json::parse(fix_json((*it)[1].str()));
            // Must have "name" field to be a tool call
            if (!j.contains("name")) continue;
            ToolCall tc;
            if (try_parse_tool_call(j, tc, idx)) calls.push_back(std::move(tc));
        } catch (...) {}
    }
    return calls;
}

// Strip all tool-call tags/blocks from content
static std::string strip_tool_content(const std::string& text) {
    std::string result = text;
    // Strip <toolcall>...</toolcall>
    result = std::regex_replace(result, std::regex(R"(<toolcall>[\s\S]*?</toolcall>)"), "");
    // Strip <tool_call>...</tool_call>
    result = std::regex_replace(result, std::regex(R"(<tool_call>[\s\S]*?</tool_call>)"), "");
    // Strip ```json blocks that were parsed as tool calls (only if they have "name")
    // We don't strip all markdown blocks, only the ones we actually parsed
    // Trim trailing whitespace
    while (!result.empty() && (result.back() == ' ' || result.back() == '\n'))
        result.pop_back();
    return result;
}

ProviderResponse Provider::chat(const std::vector<Message>& messages,
                                const nlohmann::json& tools_spec,
                                const std::string& model,
                                int max_tokens, double temperature) {
    std::string scheme, host, path_prefix;
    int port;
    parse_url(config_.api_base, scheme, host, port, path_prefix);

    std::string base = scheme + "://" + host + ":" + std::to_string(port);
    httplib::Client cli(base);
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

    std::string path = path_prefix + "/chat/completions";
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
        auto fallback = parse_toolcall_tags(resp.content);

        // Try format 2: <tool_call>...</tool_call> (Qwen)
        if (fallback.empty()) {
            fallback = parse_tool_call_tags(resp.content);
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
    std::string scheme, host, path_prefix;
    int port;
    parse_url(config_.api_base, scheme, host, port, path_prefix);

    std::string base = scheme + "://" + host + ":" + std::to_string(port);
    httplib::Client cli(base);
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

    std::string path = path_prefix + "/chat/completions";
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

} // namespace minidragon
