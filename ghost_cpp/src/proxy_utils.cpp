#include "proxy_utils.hpp"
#include "crypto.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace ProxyUtils {

    std::wstring toWide(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        if (size <= 1) {
            size = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
            if (size <= 1) return L"";
            std::wstring w(size - 1, 0);
            MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &w[0], size);
            return w;
        }
        std::wstring w(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &w[0], size);
        return w;
    }

    std::string toNarrow(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (size <= 1) return "";
        std::string s(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &s[0], size, NULL, NULL);
        return s;
    }

    std::string urlDecode(const std::string& input) {
        std::string out;
        out.reserve(input.size());
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '%' && i + 2 < input.size()) {
                int h1 = hexVal(input[i + 1]);
                int h2 = hexVal(input[i + 2]);
                if (h1 >= 0 && h2 >= 0) {
                    out.push_back(static_cast<char>((h1 << 4) | h2));
                    i += 2;
                    continue;
                }
            }
            out.push_back(input[i]);
        }
        return out;
    }

    std::string urlEncode(const std::string& input) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;

        for (unsigned char c : input) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << '%' << std::setw(2) << std::uppercase << int(c);
            }
        }
        return escaped.str();
    }

    ProxyScheme stringToScheme(const std::string& schemeStr) {
        std::string s = schemeStr;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (s == "http") return ProxyScheme::HTTP;
        if (s == "https") return ProxyScheme::HTTPS;
        if (s == "socks5") return ProxyScheme::SOCKS5;
        if (s == "socks4") return ProxyScheme::SOCKS4;
        return ProxyScheme::UNKNOWN;
    }

    std::string schemeToString(ProxyScheme scheme) {
        switch (scheme) {
            case ProxyScheme::HTTP:   return "http";
            case ProxyScheme::HTTPS:  return "https";
            case ProxyScheme::SOCKS5: return "socks5";
            case ProxyScheme::SOCKS4: return "socks4";
            default:                  return "http";
        }
    }

    uint16_t getDefaultPortForScheme(ProxyScheme scheme, bool schemeless) {
        if (schemeless) return 8080;
        switch (scheme) {
            case ProxyScheme::HTTP:   return 80;
            case ProxyScheme::HTTPS:  return 443;
            case ProxyScheme::SOCKS4:
            case ProxyScheme::SOCKS5: return 1080;
            default:                  return 8080;
        }
    }

    bool isValidPort(int port) {
        return (port >= 1 && port <= 65535);
    }

    bool isIPv6Address(const std::string& host) {
        if (host.empty()) return false;
        return (host.find(':') != std::string::npos);
    }

    bool isLocalAddress(const std::string& host) {
        if (host.empty()) return false;
        std::string h = host;
        if (h.size() >= 2 && h.front() == '[' && h.back() == ']') {
            h = h.substr(1, h.length() - 2);
        }
        if (h == "localhost" || h == "127.0.0.1" || h == "::1") {
            return true;
        }
        if (h.rfind("127.", 0) == 0) {
            return true;
        }
        return false;
    }

    std::string makeBasicAuthHeader(const std::string& user, const std::string& pass) {
        if (user.empty() && pass.empty()) return "";
        std::string creds = user + ":" + pass;
        std::string encoded = Crypto::base64Encode(reinterpret_cast<const uint8_t*>(creds.data()), creds.size());
        return "Proxy-Authorization: Basic " + encoded;
    }

    std::string sanitizeHeaderValue(const std::string& value) {
        std::string clean;
        clean.reserve(value.size());
        for (char c : value) {
            if (c != '\r' && c != '\n') {
                clean.push_back(c);
            }
        }
        return clean;
    }

    std::wstring buildBrowserHeadersW(const std::string& authHeader) {
        std::wstring headers =
            L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36\r\n"
            L"Accept: application/json, text/plain, */*\r\n"
            L"Connection: keep-alive\r\n"
            L"Accept-Language: en-US,en;q=0.9\r\n"
            L"Content-Type: application/json; charset=utf-8\r\n";

        if (!authHeader.empty()) {
            std::string cleanAuth = sanitizeHeaderValue(authHeader);
            if (!cleanAuth.empty()) {
                headers += toWide(cleanAuth) + L"\r\n";
            }
        }
        return headers;
    }

    ProxyConfig parseProxyUrl(
        const std::string& rawUrl,
        const std::string& fallbackUser,
        const std::string& fallbackPass
    ) {
        ProxyConfig config;
        config.raw_url = rawUrl;

        // 1. Trim leading and trailing whitespace
        std::string url = rawUrl;
        while (!url.empty() && isspace(static_cast<unsigned char>(url.front()))) url.erase(url.begin());
        while (!url.empty() && isspace(static_cast<unsigned char>(url.back()))) url.pop_back();

        if (url.empty()) {
            config.is_valid = false;
            config.error_message = "Empty proxy URL";
            return config;
        }

        // 2. Extract scheme if present (e.g. "http://", "https://", "socks5://")
        size_t schemePos = url.find("://");
        std::string rest;
        bool isSchemeless = false;
        if (schemePos != std::string::npos) {
            config.scheme = url.substr(0, schemePos);
            std::transform(config.scheme.begin(), config.scheme.end(), config.scheme.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            config.scheme_type = stringToScheme(config.scheme);
            rest = url.substr(schemePos + 3);
        } else {
            config.scheme = "http";
            config.scheme_type = ProxyScheme::HTTP;
            isSchemeless = true;
            rest = url;
        }

        // 3. Strip trailing path, query, or fragment from authority
        size_t pathPos = rest.find_first_of("/?#");
        if (pathPos != std::string::npos) {
            rest = rest.substr(0, pathPos);
        }

        // 4. Extract credentials if present (user[:password]@)
        size_t lastAt = rest.rfind('@');
        std::string hostPort;
        bool hadExplicitEmptyUser = false;
        if (lastAt != std::string::npos) {
            std::string userinfo = rest.substr(0, lastAt);
            hostPort = rest.substr(lastAt + 1);

            size_t colonPos = userinfo.find(':');
            if (colonPos != std::string::npos) {
                config.username = urlDecode(userinfo.substr(0, colonPos));
                config.password = urlDecode(userinfo.substr(colonPos + 1));
                if (colonPos == 0) {
                    hadExplicitEmptyUser = true;
                }
            } else {
                config.username = urlDecode(userinfo);
                config.password = "";
            }
        } else {
            hostPort = rest;
        }

        // 5. Fallback logic: If credentials not in URL, use discrete config fields
        if (config.username.empty() && !hadExplicitEmptyUser && !fallbackUser.empty()) {
            config.username = fallbackUser;
        }
        if (config.password.empty() && !fallbackPass.empty()) {
            config.password = fallbackPass;
        }
        config.is_authenticated = (!config.username.empty() || !config.password.empty());
        config.has_auth = config.is_authenticated;

        // 6. Extract Host and Port
        std::string portStr;
        if (!hostPort.empty() && hostPort.front() == '[') {
            // Bracketed IPv6: [::1]:8080 or [::1]
            size_t closeBracket = hostPort.find(']');
            if (closeBracket == std::string::npos) {
                config.is_valid = false;
                config.error_message = "Malformed IPv6 address: missing closing bracket ']'";
                return config;
            }
            config.host = hostPort.substr(1, closeBracket - 1);
            config.is_ipv6 = true;

            std::string after = hostPort.substr(closeBracket + 1);
            if (!after.empty()) {
                if (after.front() == ':') {
                    portStr = after.substr(1);
                } else {
                    config.is_valid = false;
                    config.error_message = "Unexpected characters after IPv6 bracket: " + after;
                    return config;
                }
            }
        } else {
            // IPv4, Hostname, or unbracketed IPv6
            size_t colonCount = std::count(hostPort.begin(), hostPort.end(), ':');
            if (colonCount > 1) {
                // Raw unbracketed IPv6 without port
                config.host = hostPort;
                config.is_ipv6 = true;
            } else if (colonCount == 1) {
                size_t colon = hostPort.find(':');
                config.host = hostPort.substr(0, colon);
                portStr = hostPort.substr(colon + 1);
                config.is_ipv6 = isIPv6Address(config.host);
            } else {
                config.host = hostPort;
                config.is_ipv6 = false;
            }
        }

        // 7. Validate Host
        if (config.host.empty()) {
            config.is_valid = false;
            config.error_message = "Missing host in proxy URL";
            return config;
        }

        // 8. Parse and Validate Port
        if (portStr.empty()) {
            config.port = getDefaultPortForScheme(config.scheme_type, isSchemeless);
        } else {
            for (char c : portStr) {
                if (!isdigit(static_cast<unsigned char>(c))) {
                    config.is_valid = false;
                    config.error_message = "Invalid non-numeric port: " + portStr;
                    return config;
                }
            }
            try {
                long long p = std::stoll(portStr);
                if (!isValidPort(static_cast<int>(p))) {
                    config.is_valid = false;
                    config.error_message = "Port out of valid range (1-65535): " + portStr;
                    return config;
                }
                config.port = static_cast<uint16_t>(p);
            } catch (...) {
                config.is_valid = false;
                config.error_message = "Failed to parse port integer: " + portStr;
                return config;
            }
        }

        config.is_valid = true;
        config.server = config.getHostPort();
        return config;
    }

    ProxyConfig parseFromConfig(const AppConfig& config) {
        return parseProxyUrl(config.proxy_url, config.proxy_user, config.proxy_pass);
    }

    std::string ProxyConfig::getHostPort() const {
        if (is_ipv6) {
            return "[" + host + "]:" + std::to_string(port);
        }
        return host + ":" + std::to_string(port);
    }

    std::string ProxyConfig::getCleanUrl() const {
        return scheme + "://" + getHostPort();
    }

    std::wstring ProxyConfig::toWinHttpProxyName() const {
        return toWide(getHostPort());
    }

    std::wstring ProxyConfig::toWinHttpUsername() const {
        return toWide(username);
    }

    std::wstring ProxyConfig::toWinHttpPassword() const {
        return toWide(password);
    }

    std::string ProxyConfig::toBasicAuthHeader() const {
        return makeBasicAuthHeader(username, password);
    }

    std::string ProxyConfig::getSanitizedUrl() const {
        if (!is_valid) return raw_url;
        std::string res = scheme + "://";
        if (is_authenticated) {
            res += username + ":***@";
        }
        res += getHostPort();
        return res;
    }
}
