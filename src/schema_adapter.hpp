#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace minidragon {

enum class SchemaFlavor { openai, gemini, anthropic, generic };

inline SchemaFlavor detect_schema_flavor(const std::string& api_base) {
    if (api_base.find("generativelanguage.googleapis") != std::string::npos)
        return SchemaFlavor::gemini;
    if (api_base.find("anthropic") != std::string::npos)
        return SchemaFlavor::anthropic;
    return SchemaFlavor::openai;
}

// Recursively strip keys unsupported by Gemini
inline void strip_gemini_keys(nlohmann::json& j) {
    if (!j.is_object()) return;

    // Keys Gemini doesn't support
    static const char* forbidden[] = {
        "default", "$schema", "additionalProperties", "title", "examples"
    };
    for (auto key : forbidden) {
        j.erase(key);
    }

    // "format" only valid when type is string
    if (j.contains("format") && j.contains("type")) {
        if (!j["type"].is_string() || j["type"].get<std::string>() != "string") {
            j.erase("format");
        }
    }

    // Convert anyOf/oneOf to first variant
    for (auto composite : {"anyOf", "oneOf"}) {
        if (j.contains(composite) && j[composite].is_array() && !j[composite].empty()) {
            nlohmann::json first = j[composite][0];
            j.erase(composite);
            j.merge_patch(first);
        }
    }

    // Recurse into nested objects
    if (j.contains("properties") && j["properties"].is_object()) {
        for (auto& [key, val] : j["properties"].items()) {
            strip_gemini_keys(val);
        }
    }
    if (j.contains("items") && j["items"].is_object()) {
        strip_gemini_keys(j["items"]);
    }
}

inline nlohmann::json adapt_tools_schema(const nlohmann::json& tools_spec, SchemaFlavor flavor) {
    if (!tools_spec.is_array() || tools_spec.empty()) return tools_spec;

    nlohmann::json adapted = tools_spec;

    for (auto& tool : adapted) {
        if (!tool.contains("function")) continue;
        auto& func = tool["function"];
        if (!func.contains("parameters")) continue;
        auto& params = func["parameters"];

        switch (flavor) {
        case SchemaFlavor::gemini:
            strip_gemini_keys(params);
            break;

        case SchemaFlavor::anthropic:
            // Anthropic handles OpenAI format natively — pass through
            break;

        case SchemaFlavor::openai:
        case SchemaFlavor::generic:
            // Ensure root has type:"object"
            if (!params.contains("type")) {
                params["type"] = "object";
            }
            // Add strict:false if missing
            if (!func.contains("strict")) {
                func["strict"] = false;
            }
            break;
        }
    }
    return adapted;
}

} // namespace minidragon
