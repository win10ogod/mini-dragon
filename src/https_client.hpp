#pragma once
#include <string>

namespace minidragon {

struct HttpsResponse {
    int status = 0;
    std::string body;
    bool ok() const { return status >= 200 && status < 300; }
};

// Platform-native HTTPS POST. No external dependencies.
// - Windows: WinHTTP (built-in)
// - Linux: httplib + OpenSSL (system package)
HttpsResponse https_post(
    const std::string& host,
    const std::string& path,
    const std::string& body,
    const std::string& content_type = "application/json",
    int timeout_sec = 30
);

} // namespace minidragon
