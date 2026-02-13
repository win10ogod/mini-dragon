#include "https_client.hpp"
#include <iostream>

#ifdef _WIN32
// ═══════════════════════════════════════════════════════════════════
//  Windows: WinHTTP (built into every Windows since Vista)
// ═══════════════════════════════════════════════════════════════════
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace minidragon {

HttpsResponse https_post(
    const std::string& host,
    const std::string& path,
    const std::string& body,
    const std::string& content_type,
    int timeout_sec)
{
    HttpsResponse resp;

    // Convert host to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> whost(wlen);
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, whost.data(), wlen);

    // Convert path to wide string
    wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wpath(wlen);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    // Content-Type header
    std::string header_str = "Content-Type: " + content_type;
    wlen = MultiByteToWideChar(CP_UTF8, 0, header_str.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wheader(wlen);
    MultiByteToWideChar(CP_UTF8, 0, header_str.c_str(), -1, wheader.data(), wlen);

    HINTERNET hSession = WinHttpOpen(
        L"minidragon/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { resp.body = "[error] WinHttpOpen failed"; return resp; }

    // Set timeouts
    DWORD ms = static_cast<DWORD>(timeout_sec * 1000);
    WinHttpSetTimeouts(hSession, ms, ms, ms, ms);

    HINTERNET hConnect = WinHttpConnect(hSession, whost.data(),
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        resp.body = "[error] WinHttpConnect failed";
        return resp;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", wpath.data(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.body = "[error] WinHttpOpenRequest failed";
        return resp;
    }

    // Disable certificate validation (like the old SSLClient code)
    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                  SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                  SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));

    // Add headers
    WinHttpAddRequestHeaders(hRequest, wheader.data(), -1, WINHTTP_ADDREQ_FLAG_ADD);

    // Send
    BOOL ok = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (LPVOID)body.c_str(), static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);

    if (!ok) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.body = "[error] WinHttpSendRequest failed: " + std::to_string(GetLastError());
        return resp;
    }

    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.body = "[error] WinHttpReceiveResponse failed";
        return resp;
    }

    // Status code
    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size,
        WINHTTP_NO_HEADER_INDEX);
    resp.status = static_cast<int>(status_code);

    // Read body
    std::string result;
    DWORD bytes_available = 0;
    do {
        bytes_available = 0;
        WinHttpQueryDataAvailable(hRequest, &bytes_available);
        if (bytes_available == 0) break;

        std::vector<char> buf(bytes_available + 1, 0);
        DWORD bytes_read = 0;
        WinHttpReadData(hRequest, buf.data(), bytes_available, &bytes_read);
        result.append(buf.data(), bytes_read);
    } while (bytes_available > 0);

    resp.body = result;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}

} // namespace minidragon

#else
// ═══════════════════════════════════════════════════════════════════
//  POSIX (Linux/macOS): httplib + OpenSSL
// ═══════════════════════════════════════════════════════════════════

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

namespace minidragon {

HttpsResponse https_post(
    const std::string& host,
    const std::string& path,
    const std::string& body,
    const std::string& content_type,
    int timeout_sec)
{
    HttpsResponse resp;

    httplib::SSLClient cli(host, 443);
    cli.set_connection_timeout(timeout_sec);
    cli.set_read_timeout(timeout_sec);
    cli.enable_server_certificate_verification(false);

    auto res = cli.Post(path, body, content_type);
    if (!res) {
        resp.body = "[error] HTTPS connection failed";
        return resp;
    }
    resp.status = res->status;
    resp.body = res->body;
    return resp;
}

} // namespace minidragon

#else
// No SSL at all - fallback stub
namespace minidragon {

HttpsResponse https_post(
    const std::string&,
    const std::string&,
    const std::string&,
    const std::string&,
    int)
{
    HttpsResponse resp;
    resp.body = "[error] HTTPS not available (no OpenSSL, not Windows)";
    return resp;
}

} // namespace minidragon
#endif // CPPHTTPLIB_OPENSSL_SUPPORT
#endif // _WIN32
