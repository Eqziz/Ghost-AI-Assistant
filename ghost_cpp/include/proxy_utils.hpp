#pragma once

#include <string>
#include <cstdint>
#include <windows.h>
#include "config.hpp"

namespace ProxyUtils {

    enum class ProxyScheme {
        HTTP,
        HTTPS,
        SOCKS4,
        SOCKS5,
        UNKNOWN
    };

    struct ProxyConfig {
        std::string raw_url;                  // Original input string
        std::string scheme = "http";          // Normalised lowercase scheme (e.g. "http", "https", "socks5")
        ProxyScheme scheme_type = ProxyScheme::HTTP;
        std::string host;                     // Hostname or unbracketed IPv4/IPv6 (e.g. "proxy.example.com", "127.0.0.1", "::1")
        uint16_t port = 8080;                 // Validated port (1-65535)
        std::string username;                 // Decoded username (if any)
        std::string password;                 // Decoded password (if any)
        bool is_authenticated = false;        // True if username or password is non-empty
        bool is_valid = false;                // True if parsing succeeded and host is non-empty
        bool is_ipv6 = false;                 // True if host is IPv6
        std::string error_message;            // Diagnostic failure reason if invalid

        // Compatibility aliases
        bool has_auth = false;                // Synced with is_authenticated
        std::string server;                   // Synced with getHostPort()

        // Formatted String Helpers & Accessors
        std::string getHostPort() const;       // Returns "host:port" or "[::1]:port"
        std::string getCleanUrl() const;       // Returns "http://host:port" (no credentials or path)
        std::wstring toWinHttpProxyName() const; // Wide string "host:port" for WinHttpOpen (accessType=NAMED_PROXY)
        std::wstring toWinHttpUsername() const;  // Wide string for WINHTTP_OPTION_PROXY_USERNAME
        std::wstring toWinHttpPassword() const;  // Wide string for WINHTTP_OPTION_PROXY_PASSWORD
        std::string toBasicAuthHeader() const;   // Returns "Proxy-Authorization: Basic <base64(user:pass)>"
        std::string getSanitizedUrl() const;     // Returns "scheme://user:***@host:port" for safe logging

        // Additional compatibility accessors
        std::wstring getProxyServerW() const { return toWinHttpProxyName(); }
        std::wstring getProxyServerWide() const { return toWinHttpProxyName(); }
        std::wstring getUsernameW() const { return toWinHttpUsername(); }
        std::wstring getUserWide() const { return toWinHttpUsername(); }
        std::wstring getPasswordW() const { return toWinHttpPassword(); }
        std::wstring getPasswordWide() const { return toWinHttpPassword(); }
    };

    // Core Parsing APIs
    ProxyConfig parseProxyUrl(
        const std::string& rawUrl,
        const std::string& fallbackUser = "",
        const std::string& fallbackPass = ""
    );

    ProxyConfig parseFromConfig(const AppConfig& config);

    inline ProxyConfig parse(
        const std::string& rawUrl,
        const std::string& fallbackUser = "",
        const std::string& fallbackPass = ""
    ) {
        return parseProxyUrl(rawUrl, fallbackUser, fallbackPass);
    }

    // Percent-encoding / decoding helpers
    std::string urlDecode(const std::string& input);
    std::string urlEncode(const std::string& input);

    // UTF-8 / UTF-16 Wide-String conversion helpers
    std::wstring toWide(const std::string& str);
    std::string toNarrow(const std::wstring& wstr);

    // Scheme helpers
    ProxyScheme stringToScheme(const std::string& schemeStr);
    std::string schemeToString(ProxyScheme scheme);
    uint16_t getDefaultPortForScheme(ProxyScheme scheme, bool schemeless = false);

    // Address & Port validation helpers
    bool isValidPort(int port);
    bool isIPv6Address(const std::string& host);
    bool isLocalAddress(const std::string& host);

    // Basic Auth header generator
    std::string makeBasicAuthHeader(const std::string& user, const std::string& pass);

    // Browser stealth headers generator (CRLF separated)
    std::wstring buildBrowserHeadersW(const std::string& authHeader = "");
    std::string sanitizeHeaderValue(const std::string& value);
}

namespace ghost {
    using ProxyConfig = ProxyUtils::ProxyConfig;
    using ProxyUtils = ProxyUtils::ProxyConfig;
}
