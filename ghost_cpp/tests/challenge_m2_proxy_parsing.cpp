#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <sstream>
#include <algorithm>
#include "proxy_utils.hpp"
#include "crypto.hpp"

// Lightweight test harness macros
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define CHALLENGE_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "  [FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
    } \
} while(0)

#define CHALLENGE_EQUALS(actual, expected, msg) do { \
    g_tests_run++; \
    if ((actual) == (expected)) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "  [FAIL] Line " << __LINE__ << ": " << msg \
                  << " (Expected: '" << (expected) << "', Got: '" << (actual) << "')" << std::endl; \
    } \
} while(0)

// Helper to check if a wide string contains a narrow substring
static bool wideContainsNarrow(const std::wstring& wstr, const std::string& narrow) {
    if (narrow.empty()) return false;
    std::wstring wn = ProxyUtils::toWide(narrow);
    return (wstr.find(wn) != std::wstring::npos);
}

// -----------------------------------------------------------------------------
// Suite 1: Standard URL Formats
// -----------------------------------------------------------------------------
void testSuite1_StandardUrls() {
    std::cout << "[SUITE 1] Standard URL Formats" << std::endl;

    // 1.1 Full standard HTTP proxy with credentials
    {
        auto p = ProxyUtils::parseProxyUrl("http://user:pass@proxy.example.com:8080");
        CHALLENGE_ASSERT(p.is_valid, "1.1 URL should be valid");
        CHALLENGE_EQUALS(p.scheme, "http", "1.1 Scheme should be http");
        CHALLENGE_ASSERT(p.scheme_type == ProxyUtils::ProxyScheme::HTTP, "1.1 Scheme enum should be HTTP");
        CHALLENGE_EQUALS(p.host, "proxy.example.com", "1.1 Host parsed");
        CHALLENGE_EQUALS(p.port, 8080, "1.1 Port parsed");
        CHALLENGE_EQUALS(p.username, "user", "1.1 Username parsed");
        CHALLENGE_EQUALS(p.password, "pass", "1.1 Password parsed");
        CHALLENGE_ASSERT(p.is_authenticated, "1.1 Authenticated flag true");
        CHALLENGE_ASSERT(!p.is_ipv6, "1.1 Not IPv6");
        CHALLENGE_EQUALS(p.getHostPort(), "proxy.example.com:8080", "1.1 getHostPort()");
        CHALLENGE_EQUALS(p.getCleanUrl(), "http://proxy.example.com:8080", "1.1 getCleanUrl()");
        CHALLENGE_EQUALS(p.getSanitizedUrl(), "http://user:***@proxy.example.com:8080", "1.1 getSanitizedUrl()");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy.example.com:8080", "1.1 toWinHttpProxyName() wide");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "user"), "1.1 No username in WinHttp proxy name");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "pass"), "1.1 No password in WinHttp proxy name");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "@"), "1.1 No @ in WinHttp proxy name");
        CHALLENGE_EQUALS(p.toBasicAuthHeader(), "Proxy-Authorization: Basic dXNlcjpwYXNz", "1.1 toBasicAuthHeader()");
    }

    // 1.2 HTTPS proxy
    {
        auto p = ProxyUtils::parseProxyUrl("https://admin:secret@secure-proxy.corp.net:8443");
        CHALLENGE_ASSERT(p.is_valid, "1.2 URL should be valid");
        CHALLENGE_EQUALS(p.scheme, "https", "1.2 Scheme should be https");
        CHALLENGE_ASSERT(p.scheme_type == ProxyUtils::ProxyScheme::HTTPS, "1.2 Scheme enum should be HTTPS");
        CHALLENGE_EQUALS(p.host, "secure-proxy.corp.net", "1.2 Host parsed");
        CHALLENGE_EQUALS(p.port, 8443, "1.2 Port parsed");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"secure-proxy.corp.net:8443", "1.2 WinHttp proxy name");
    }

    // 1.3 Anonymous standard HTTP proxy
    {
        auto p = ProxyUtils::parseProxyUrl("http://anonymous-proxy.org:3128");
        CHALLENGE_ASSERT(p.is_valid, "1.3 URL should be valid");
        CHALLENGE_EQUALS(p.host, "anonymous-proxy.org", "1.3 Host parsed");
        CHALLENGE_EQUALS(p.port, 3128, "1.3 Port parsed");
        CHALLENGE_ASSERT(!p.is_authenticated, "1.3 Not authenticated");
        CHALLENGE_EQUALS(p.username, "", "1.3 Empty username");
        CHALLENGE_EQUALS(p.password, "", "1.3 Empty password");
        CHALLENGE_EQUALS(p.getSanitizedUrl(), "http://anonymous-proxy.org:3128", "1.3 Clean sanitized URL");
    }

    // 1.4 Scheme default ports
    {
        auto pHttp = ProxyUtils::parseProxyUrl("http://proxy.org");
        CHALLENGE_ASSERT(pHttp.is_valid, "1.4 HTTP default port valid");
        CHALLENGE_EQUALS(pHttp.port, 80, "1.4 HTTP default port 80");

        auto pHttps = ProxyUtils::parseProxyUrl("https://proxy.org");
        CHALLENGE_ASSERT(pHttps.is_valid, "1.4 HTTPS default port valid");
        CHALLENGE_EQUALS(pHttps.port, 443, "1.4 HTTPS default port 443");

        auto pSocks5 = ProxyUtils::parseProxyUrl("socks5://proxy.org");
        CHALLENGE_ASSERT(pSocks5.is_valid, "1.4 SOCKS5 default port valid");
        CHALLENGE_EQUALS(pSocks5.port, 1080, "1.4 SOCKS5 default port 1080");

        auto pSocks4 = ProxyUtils::parseProxyUrl("socks4://proxy.org");
        CHALLENGE_ASSERT(pSocks4.is_valid, "1.4 SOCKS4 default port valid");
        CHALLENGE_EQUALS(pSocks4.port, 1080, "1.4 SOCKS4 default port 1080");
    }
}

// -----------------------------------------------------------------------------
// Suite 2: Schemeless Formats
// -----------------------------------------------------------------------------
void testSuite2_Schemeless() {
    std::cout << "[SUITE 2] Schemeless URL Formats" << std::endl;

    // 2.1 user:pass@127.0.0.1:8080
    {
        auto p = ProxyUtils::parseProxyUrl("user:pass@127.0.0.1:8080");
        CHALLENGE_ASSERT(p.is_valid, "2.1 URL valid");
        CHALLENGE_EQUALS(p.scheme, "http", "2.1 Default scheme http");
        CHALLENGE_EQUALS(p.host, "127.0.0.1", "2.1 Host 127.0.0.1");
        CHALLENGE_EQUALS(p.port, 8080, "2.1 Port 8080");
        CHALLENGE_EQUALS(p.username, "user", "2.1 User parsed");
        CHALLENGE_EQUALS(p.password, "pass", "2.1 Pass parsed");
        CHALLENGE_ASSERT(p.is_authenticated, "2.1 Authenticated");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"127.0.0.1:8080", "2.1 WinHttp proxy name");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "user"), "2.1 No user leak");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "pass"), "2.1 No pass leak");
    }

    // 2.2 admin:secret123@proxy:8080
    {
        auto p = ProxyUtils::parseProxyUrl("admin:secret123@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "2.2 URL valid");
        CHALLENGE_EQUALS(p.host, "proxy", "2.2 Host proxy");
        CHALLENGE_EQUALS(p.port, 8080, "2.2 Port 8080");
        CHALLENGE_EQUALS(p.username, "admin", "2.2 User admin");
        CHALLENGE_EQUALS(p.password, "secret123", "2.2 Pass secret123");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy:8080", "2.2 WinHttp proxy name");
    }

    // 2.3 127.0.0.1:8080 (anonymous schemeless)
    {
        auto p = ProxyUtils::parseProxyUrl("127.0.0.1:8080");
        CHALLENGE_ASSERT(p.is_valid, "2.3 URL valid");
        CHALLENGE_EQUALS(p.host, "127.0.0.1", "2.3 Host");
        CHALLENGE_EQUALS(p.port, 8080, "2.3 Port");
        CHALLENGE_ASSERT(!p.is_authenticated, "2.3 Not authenticated");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"127.0.0.1:8080", "2.3 WinHttp proxy name");
    }

    // 2.4 Schemeless host only (default port 8080)
    {
        auto p = ProxyUtils::parseProxyUrl("proxy.local");
        CHALLENGE_ASSERT(p.is_valid, "2.4 URL valid");
        CHALLENGE_EQUALS(p.host, "proxy.local", "2.4 Host");
        CHALLENGE_EQUALS(p.port, 8080, "2.4 Default port 8080 for schemeless");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy.local:8080", "2.4 WinHttp proxy name");
    }
}

// -----------------------------------------------------------------------------
// Suite 3: Host-Only with Discrete Fallback Credentials
// -----------------------------------------------------------------------------
void testSuite3_HostOnlyWithFallback() {
    std::cout << "[SUITE 3] Host-Only with Discrete Fallback Credentials" << std::endl;

    // 3.1 127.0.0.1:8080 with discrete user and pass
    {
        auto p = ProxyUtils::parseProxyUrl("127.0.0.1:8080", "discreteUser", "discretePass123");
        CHALLENGE_ASSERT(p.is_valid, "3.1 Valid");
        CHALLENGE_EQUALS(p.host, "127.0.0.1", "3.1 Host");
        CHALLENGE_EQUALS(p.port, 8080, "3.1 Port");
        CHALLENGE_EQUALS(p.username, "discreteUser", "3.1 Username fallback used");
        CHALLENGE_EQUALS(p.password, "discretePass123", "3.1 Password fallback used");
        CHALLENGE_ASSERT(p.is_authenticated, "3.1 Authenticated");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"127.0.0.1:8080", "3.1 WinHttp proxy name contains host:port only");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "discreteUser"), "3.1 WinHttp proxy name leaks no user");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "discretePass"), "3.1 WinHttp proxy name leaks no pass");
        CHALLENGE_ASSERT(p.toWinHttpUsername() == L"discreteUser", "3.1 WinHttp username");
        CHALLENGE_ASSERT(p.toWinHttpPassword() == L"discretePass123", "3.1 WinHttp password");
    }

    // 3.2 URL credentials supersede discrete credentials
    {
        auto p = ProxyUtils::parseProxyUrl("http://urlUser:urlPass@proxy.corp:8080", "ignoredUser", "ignoredPass");
        CHALLENGE_ASSERT(p.is_valid, "3.2 Valid");
        CHALLENGE_EQUALS(p.username, "urlUser", "3.2 URL user supersedes discrete user");
        CHALLENGE_EQUALS(p.password, "urlPass", "3.2 URL pass supersedes discrete pass");
    }

    // 3.3 Partial fallback (user in URL, pass in discrete fallback)
    {
        auto p = ProxyUtils::parseProxyUrl("http://urlUser@proxy.corp:8080", "", "fallbackPass");
        CHALLENGE_ASSERT(p.is_valid, "3.3 Valid");
        CHALLENGE_EQUALS(p.username, "urlUser", "3.3 URL user preserved");
        CHALLENGE_EQUALS(p.password, "fallbackPass", "3.3 Fallback pass populated");
        CHALLENGE_ASSERT(p.is_authenticated, "3.3 Authenticated");
    }
}

// -----------------------------------------------------------------------------
// Suite 4: Special Characters in Credentials & Percent-Encoding
// -----------------------------------------------------------------------------
void testSuite4_SpecialCharacters() {
    std::cout << "[SUITE 4] Special Characters & Percent-Encoding" << std::endl;

    // 4.1 @ in password (%40)
    {
        auto p = ProxyUtils::parseProxyUrl("http://user:p%40ssword@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "4.1 Valid");
        CHALLENGE_EQUALS(p.username, "user", "4.1 Username");
        CHALLENGE_EQUALS(p.password, "p@ssword", "4.1 Decoded @ in password");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy:8080", "4.1 Clean WinHttp proxy name");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "p@ssword"), "4.1 No pass in proxy name");
        CHALLENGE_ASSERT(p.toWinHttpPassword() == L"p@ssword", "4.1 Wide password");
    }

    // 4.2 : in password (%3A)
    {
        auto p = ProxyUtils::parseProxyUrl("http://user:p%3A%3Aword@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "4.2 Valid");
        CHALLENGE_EQUALS(p.username, "user", "4.2 Username");
        CHALLENGE_EQUALS(p.password, "p::word", "4.2 Decoded :: in password");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy:8080", "4.2 Clean WinHttp proxy name");
    }

    // 4.3 # in password (%23)
    {
        auto p = ProxyUtils::parseProxyUrl("http://user:p%23ss%23123@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "4.3 Valid");
        CHALLENGE_EQUALS(p.username, "user", "4.3 Username");
        CHALLENGE_EQUALS(p.password, "p#ss#123", "4.3 Decoded # in password");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy:8080", "4.3 Clean WinHttp proxy name");
    }

    // 4.4 % in password (%25)
    {
        auto p = ProxyUtils::parseProxyUrl("http://user:100%25secure@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "4.4 Valid");
        CHALLENGE_EQUALS(p.username, "user", "4.4 Username");
        CHALLENGE_EQUALS(p.password, "100%secure", "4.4 Decoded % in password");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy:8080", "4.4 Clean WinHttp proxy name");
    }

    // 4.5 Combined percent-encoded specials in user and password
    {
        auto p = ProxyUtils::parseProxyUrl("http://u%40ser%3A1:p%40ss%3A%23%25@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "4.5 Valid");
        CHALLENGE_EQUALS(p.username, "u@ser:1", "4.5 User decoded");
        CHALLENGE_EQUALS(p.password, "p@ss:#%", "4.5 Pass decoded");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy:8080", "4.5 Clean WinHttp proxy name");
    }

    // 4.6 Unencoded colons in password
    {
        auto p = ProxyUtils::parseProxyUrl("http://admin:colons:in:the:middle@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "4.6 Valid");
        CHALLENGE_EQUALS(p.username, "admin", "4.6 Username");
        CHALLENGE_EQUALS(p.password, "colons:in:the:middle", "4.6 Unencoded colons in password preserved");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy:8080", "4.6 Clean WinHttp proxy name");
    }
}

// -----------------------------------------------------------------------------
// Suite 5: Bracketed IPv6 Addresses
// -----------------------------------------------------------------------------
void testSuite5_IPv6Addresses() {
    std::cout << "[SUITE 5] Bracketed IPv6 Addresses" << std::endl;

    // 5.1 http://[::1]:8080
    {
        auto p = ProxyUtils::parseProxyUrl("http://[::1]:8080");
        CHALLENGE_ASSERT(p.is_valid, "5.1 Valid");
        CHALLENGE_EQUALS(p.host, "::1", "5.1 Unbracketed IPv6 host");
        CHALLENGE_EQUALS(p.port, 8080, "5.1 Port");
        CHALLENGE_ASSERT(p.is_ipv6, "5.1 is_ipv6 flag");
        CHALLENGE_EQUALS(p.getHostPort(), "[::1]:8080", "5.1 getHostPort()");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"[::1]:8080", "5.1 toWinHttpProxyName()");
        CHALLENGE_EQUALS(p.getCleanUrl(), "http://[::1]:8080", "5.1 getCleanUrl()");
    }

    // 5.2 Schemeless bracketed IPv6: [::1]:3128
    {
        auto p = ProxyUtils::parseProxyUrl("[::1]:3128");
        CHALLENGE_ASSERT(p.is_valid, "5.2 Valid");
        CHALLENGE_EQUALS(p.host, "::1", "5.2 Host ::1");
        CHALLENGE_EQUALS(p.port, 3128, "5.2 Port 3128");
        CHALLENGE_ASSERT(p.is_ipv6, "5.2 is_ipv6");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"[::1]:3128", "5.2 toWinHttpProxyName()");
    }

    // 5.3 Bracketed IPv6 with authentication
    {
        auto p = ProxyUtils::parseProxyUrl("http://v6user:v6pass@[2001:db8:85a3::8a2e:370:7334]:9090");
        CHALLENGE_ASSERT(p.is_valid, "5.3 Valid");
        CHALLENGE_EQUALS(p.username, "v6user", "5.3 User");
        CHALLENGE_EQUALS(p.password, "v6pass", "5.3 Pass");
        CHALLENGE_EQUALS(p.host, "2001:db8:85a3::8a2e:370:7334", "5.3 IPv6 host");
        CHALLENGE_EQUALS(p.port, 9090, "5.3 Port");
        CHALLENGE_ASSERT(p.is_ipv6, "5.3 is_ipv6");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"[2001:db8:85a3::8a2e:370:7334]:9090", "5.3 toWinHttpProxyName()");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "v6user"), "5.3 No user leak in IPv6");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "v6pass"), "5.3 No pass leak in IPv6");
    }

    // 5.4 Bracketed IPv6 without port
    {
        auto p = ProxyUtils::parseProxyUrl("http://[::1]");
        CHALLENGE_ASSERT(p.is_valid, "5.4 Valid");
        CHALLENGE_EQUALS(p.host, "::1", "5.4 Host");
        CHALLENGE_EQUALS(p.port, 80, "5.4 Default port 80");
        CHALLENGE_EQUALS(p.getHostPort(), "[::1]:80", "5.4 getHostPort()");
    }

    // 5.5 Malformed IPv6 addresses
    {
        auto pMissingBracket = ProxyUtils::parseProxyUrl("http://[::1:8080");
        CHALLENGE_ASSERT(!pMissingBracket.is_valid, "5.5 Missing closing bracket is invalid");

        auto pExtraChars = ProxyUtils::parseProxyUrl("http://[::1]junk:8080");
        CHALLENGE_ASSERT(!pExtraChars.is_valid, "5.5 Unexpected chars after bracket is invalid");
    }
}

// -----------------------------------------------------------------------------
// Suite 6: Out-of-Range & Non-Numeric Ports
// -----------------------------------------------------------------------------
void testSuite6_PortValidation() {
    std::cout << "[SUITE 6] Out-of-Range & Non-Numeric Ports" << std::endl;

    // 6.1 Port 99999
    {
        auto p = ProxyUtils::parseProxyUrl("http://proxy.example.com:99999");
        CHALLENGE_ASSERT(!p.is_valid, "6.1 Port 99999 must be invalid");
        CHALLENGE_ASSERT(p.error_message.find("99999") != std::string::npos, "6.1 Error message mentions port");
    }

    // 6.2 Port 0
    {
        auto p = ProxyUtils::parseProxyUrl("http://proxy.example.com:0");
        CHALLENGE_ASSERT(!p.is_valid, "6.2 Port 0 must be invalid");
    }

    // 6.3 Port 65536
    {
        auto p = ProxyUtils::parseProxyUrl("http://proxy.example.com:65536");
        CHALLENGE_ASSERT(!p.is_valid, "6.3 Port 65536 must be invalid");
    }

    // 6.4 Non-numeric port
    {
        auto p = ProxyUtils::parseProxyUrl("http://proxy.example.com:abc");
        CHALLENGE_ASSERT(!p.is_valid, "6.4 Non-numeric port must be invalid");
    }

    // 6.5 Negative port
    {
        auto p = ProxyUtils::parseProxyUrl("http://proxy.example.com:-80");
        CHALLENGE_ASSERT(!p.is_valid, "6.5 Negative port must be invalid");
    }

    // 6.6 Float port
    {
        auto p = ProxyUtils::parseProxyUrl("http://proxy.example.com:80.80");
        CHALLENGE_ASSERT(!p.is_valid, "6.6 Float port must be invalid");
    }

    // 6.7 Huge port overflow
    {
        auto p = ProxyUtils::parseProxyUrl("http://proxy.example.com:99999999999999999999999999");
        CHALLENGE_ASSERT(!p.is_valid, "6.7 Overflow port must be invalid");
    }

    // 6.8 Valid boundary ports 1 and 65535
    {
        auto p1 = ProxyUtils::parseProxyUrl("http://proxy.example.com:1");
        CHALLENGE_ASSERT(p1.is_valid, "6.8 Port 1 is valid");
        CHALLENGE_EQUALS(p1.port, 1, "6.8 Port 1 parsed");

        auto pMax = ProxyUtils::parseProxyUrl("http://proxy.example.com:65535");
        CHALLENGE_ASSERT(pMax.is_valid, "6.8 Port 65535 is valid");
        CHALLENGE_EQUALS(pMax.port, 65535, "6.8 Port 65535 parsed");
    }
}

// -----------------------------------------------------------------------------
// Suite 7: Empty User with Password & Edge Credentials
// -----------------------------------------------------------------------------
void testSuite7_EmptyUserWithPassword() {
    std::cout << "[SUITE 7] Empty User with Password & Edge Credentials" << std::endl;

    // 7.1 http://:mypass@proxy:8080
    {
        auto p = ProxyUtils::parseProxyUrl("http://:mypass@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "7.1 Valid");
        CHALLENGE_EQUALS(p.username, "", "7.1 Username is empty");
        CHALLENGE_EQUALS(p.password, "mypass", "7.1 Password is mypass");
        CHALLENGE_ASSERT(p.is_authenticated, "7.1 Authenticated true");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy:8080", "7.1 toWinHttpProxyName()");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), "mypass"), "7.1 No pass leak");
        CHALLENGE_ASSERT(p.toWinHttpUsername() == L"", "7.1 WinHttp user is empty wide string");
        CHALLENGE_ASSERT(p.toWinHttpPassword() == L"mypass", "7.1 WinHttp pass is mypass");

        // Basic auth header should be base64(":mypass") -> "Om15cGFzcw=="
        std::string expectedAuth = "Proxy-Authorization: Basic " + Crypto::base64Encode(reinterpret_cast<const uint8_t*>(":mypass"), 7);
        CHALLENGE_EQUALS(p.toBasicAuthHeader(), expectedAuth, "7.1 Basic Auth header matches base64(:mypass)");
    }

    // 7.2 Explicit empty user prevents discrete fallback user overwrite
    {
        auto p = ProxyUtils::parseProxyUrl("http://:mypass@proxy:8080", "discreteUser", "discretePass");
        CHALLENGE_ASSERT(p.is_valid, "7.2 Valid");
        CHALLENGE_EQUALS(p.username, "", "7.2 Explicit empty user preserved over fallbackUser");
        CHALLENGE_EQUALS(p.password, "mypass", "7.2 URL password preserved over fallbackPass");
    }

    // 7.3 Empty password with non-empty user: http://user:@proxy:8080
    {
        auto p = ProxyUtils::parseProxyUrl("http://myuser:@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "7.3 Valid");
        CHALLENGE_EQUALS(p.username, "myuser", "7.3 Username is myuser");
        CHALLENGE_EQUALS(p.password, "", "7.3 Password is empty");
        CHALLENGE_ASSERT(p.is_authenticated, "7.3 Authenticated true");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy:8080", "7.3 WinHttp proxy name");
    }

    // 7.4 Empty user and empty password: http://:@proxy:8080
    {
        auto p = ProxyUtils::parseProxyUrl("http://:@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "7.4 Valid");
        CHALLENGE_EQUALS(p.username, "", "7.4 Username empty");
        CHALLENGE_EQUALS(p.password, "", "7.4 Password empty");
        CHALLENGE_ASSERT(!p.is_authenticated, "7.4 Not authenticated when both user and pass empty");
    }
}

// -----------------------------------------------------------------------------
// Suite 8: Clean WinHttp Proxy Name Output & Credential Leak Prevention
// -----------------------------------------------------------------------------
void testSuite8_WinHttpProxyNameSanitization() {
    std::cout << "[SUITE 8] Clean WinHttp Proxy Name Output & Leak Prevention" << std::endl;

    std::vector<std::string> testUrls = {
        "http://superadmin:TopSecretPassword123!@proxy1.corp.internal:8080",
        "https://api_user:Token_Key_99%23@gateway.cloud:443",
        "user:p%40ss%3Aword%23123@10.20.30.40:3128",
        "http://:onlypassword@[::1]:8080",
        "http://special_user:a%24b%25c%5Ed@127.0.0.1:8888",
        "http://user:pass@proxy.example.com:8080/api/v1?query=1#frag"
    };

    for (const auto& url : testUrls) {
        auto p = ProxyUtils::parseProxyUrl(url);
        CHALLENGE_ASSERT(p.is_valid, "8. URL valid: " + url);

        std::wstring proxyNameW = p.toWinHttpProxyName();
        std::string proxyName = ProxyUtils::toNarrow(proxyNameW);

        // Assert no credentials in toWinHttpProxyName()
        if (!p.username.empty()) {
            CHALLENGE_ASSERT(proxyName.find(p.username) == std::string::npos, "Username NOT in toWinHttpProxyName: " + p.username);
        }
        if (!p.password.empty()) {
            CHALLENGE_ASSERT(proxyName.find(p.password) == std::string::npos, "Password NOT in toWinHttpProxyName: " + p.password);
        }
        CHALLENGE_ASSERT(proxyName.find('@') == std::string::npos, "No '@' in toWinHttpProxyName");
        CHALLENGE_ASSERT(proxyName.find("http://") == std::string::npos, "No 'http://' in toWinHttpProxyName");
        CHALLENGE_ASSERT(proxyName.find("https://") == std::string::npos, "No 'https://' in toWinHttpProxyName");
        CHALLENGE_ASSERT(proxyName.find('/') == std::string::npos, "No '/' path in toWinHttpProxyName");
        CHALLENGE_ASSERT(proxyName.find('?') == std::string::npos, "No '?' query in toWinHttpProxyName");
        CHALLENGE_ASSERT(proxyName.find('#') == std::string::npos, "No '#' fragment in toWinHttpProxyName");

        // getCleanUrl() must not contain credentials
        std::string clean = p.getCleanUrl();
        if (!p.username.empty()) {
            CHALLENGE_ASSERT(clean.find(p.username) == std::string::npos, "Username NOT in getCleanUrl");
        }
        if (!p.password.empty()) {
            CHALLENGE_ASSERT(clean.find(p.password) == std::string::npos, "Password NOT in getCleanUrl");
        }

        // getSanitizedUrl() must mask password with ***
        std::string sanitized = p.getSanitizedUrl();
        if (!p.password.empty()) {
            CHALLENGE_ASSERT(sanitized.find(p.password) == std::string::npos, "Plain password NEVER in getSanitizedUrl");
            CHALLENGE_ASSERT(sanitized.find("***") != std::string::npos, "'***' masking present in getSanitizedUrl");
        }
    }
}

// -----------------------------------------------------------------------------
// Suite 9: Local Address Proxy Bypass Detection (isLocalAddress)
// -----------------------------------------------------------------------------
void testSuite9_LocalAddressBypass() {
    std::cout << "[SUITE 9] Local Address Proxy Bypass Detection" << std::endl;

    CHALLENGE_ASSERT(ProxyUtils::isLocalAddress("localhost"), "9.1 localhost is local");
    CHALLENGE_ASSERT(ProxyUtils::isLocalAddress("127.0.0.1"), "9.2 127.0.0.1 is local");
    CHALLENGE_ASSERT(ProxyUtils::isLocalAddress("127.0.0.2"), "9.3 127.0.0.2 is local");
    CHALLENGE_ASSERT(ProxyUtils::isLocalAddress("127.255.255.254"), "9.4 127.255.255.254 is local");
    CHALLENGE_ASSERT(ProxyUtils::isLocalAddress("::1"), "9.5 ::1 is local");
    CHALLENGE_ASSERT(ProxyUtils::isLocalAddress("[::1]"), "9.6 [::1] is local");

    CHALLENGE_ASSERT(!ProxyUtils::isLocalAddress("192.168.1.1"), "9.7 192.168.1.1 is NOT local");
    CHALLENGE_ASSERT(!ProxyUtils::isLocalAddress("10.0.0.1"), "9.8 10.0.0.1 is NOT local");
    CHALLENGE_ASSERT(!ProxyUtils::isLocalAddress("172.16.0.1"), "9.9 172.16.0.1 is NOT local");
    CHALLENGE_ASSERT(!ProxyUtils::isLocalAddress("api.openai.com"), "9.10 Remote domain is NOT local");
    CHALLENGE_ASSERT(!ProxyUtils::isLocalAddress("openrouter.ai"), "9.11 Remote domain is NOT local");
}

// -----------------------------------------------------------------------------
// Suite 10: Browser Stealth Masking Headers & Anti-Splitting
// -----------------------------------------------------------------------------
void testSuite10_BrowserMaskingHeaders() {
    std::cout << "[SUITE 10] Browser Stealth Masking Headers" << std::endl;

    std::wstring headersW = ProxyUtils::buildBrowserHeadersW("Bearer mySecretToken123");
    std::string headers = ProxyUtils::toNarrow(headersW);

    CHALLENGE_ASSERT(headers.find("Mozilla/5.0") != std::string::npos, "10.1 Mozilla/5.0 UA present");
    CHALLENGE_ASSERT(headers.find("Chrome/131.0.0.0") != std::string::npos, "10.2 Chrome 131 UA present");
    CHALLENGE_ASSERT(headers.find("Accept: application/json, text/plain, */*") != std::string::npos, "10.3 Accept header present");
    CHALLENGE_ASSERT(headers.find("Connection: keep-alive") != std::string::npos, "10.4 Connection: keep-alive present");
    CHALLENGE_ASSERT(headers.find("Accept-Language: en-US,en;q=0.9") != std::string::npos, "10.5 Accept-Language present");
    CHALLENGE_ASSERT(headers.find("Content-Type: application/json; charset=utf-8") != std::string::npos, "10.6 Content-Type present");
    CHALLENGE_ASSERT(headers.find("Content-Length:") == std::string::npos, "10.7 No duplicate Content-Length in stealth headers");
    CHALLENGE_ASSERT(headers.find("Bearer mySecretToken123") != std::string::npos, "10.8 Injected authHeader present");

    // CRLF line ending checks
    size_t pos = 0;
    while ((pos = headers.find('\n', pos)) != std::string::npos) {
        CHALLENGE_ASSERT(pos > 0 && headers[pos - 1] == '\r', "10.9 Every newline must be preceded by carriage return CRLF");
        pos++;
    }

    // Header injection / CRLF splitting attack stress test
    std::string maliciousHeader = "Bearer token\r\nInjected-Header: evil\r\nX-Splitted: true\r\n";
    std::string sanitized = ProxyUtils::sanitizeHeaderValue(maliciousHeader);
    CHALLENGE_ASSERT(sanitized.find('\r') == std::string::npos, "10.10 Sanitized header strips all CR");
    CHALLENGE_ASSERT(sanitized.find('\n') == std::string::npos, "10.11 Sanitized header strips all LF");
    CHALLENGE_EQUALS(sanitized, "Bearer tokenInjected-Header: evilX-Splitted: true", "10.12 Exact sanitized string");
}

// -----------------------------------------------------------------------------
// Suite 11: Combinatorial Stress Matrix (1,000 Generated Inputs)
// -----------------------------------------------------------------------------
void testSuite11_CombinatorialStress() {
    std::cout << "[SUITE 11] Combinatorial Stress Matrix (1,000 Inputs)" << std::endl;

    std::vector<std::string> schemes = {"http://", "https://", "socks5://", ""};
    std::vector<std::string> users = {"admin", "u%40ser", "test_user_99", "", ":"};
    std::vector<std::string> passwords = {"p%40ssword", "secret%3A123", "p%23ss%2599", "simple", ""};
    std::vector<std::string> hosts = {"127.0.0.1", "proxy.corp.net", "[::1]", "[2001:db8::1]", "custom-proxy"};
    std::vector<std::string> ports = {":8080", ":3128", ":1080", ":443", ""};

    int count = 0;
    for (const auto& sch : schemes) {
        for (const auto& u : users) {
            for (const auto& p : passwords) {
                for (const auto& h : hosts) {
                    for (const auto& pt : ports) {
                        std::string userinfo;
                        if (!u.empty() || !p.empty()) {
                            if (u == ":") {
                                userinfo = ":" + p + "@";
                            } else {
                                userinfo = u + ":" + p + "@";
                            }
                        }
                        std::string fullUrl = sch + userinfo + h + pt;
                        auto cfg = ProxyUtils::parseProxyUrl(fullUrl);

                        CHALLENGE_ASSERT(cfg.is_valid, "Stress test URL must be valid: " + fullUrl);

                        // Credential leak invariant check
                        std::wstring winHttpName = cfg.toWinHttpProxyName();
                        if (!cfg.username.empty()) {
                            CHALLENGE_ASSERT(!wideContainsNarrow(winHttpName, cfg.username),
                                             "Leak check: username in WinHttp name");
                        }
                        if (!cfg.password.empty()) {
                            CHALLENGE_ASSERT(!wideContainsNarrow(winHttpName, cfg.password),
                                             "Leak check: password in WinHttp name");
                        }
                        CHALLENGE_ASSERT(!wideContainsNarrow(winHttpName, "@"),
                                         "Leak check: '@' in WinHttp name");

                        // If authenticated, basic auth header base64 decode check
                        if (cfg.is_authenticated) {
                            std::string authHeader = cfg.toBasicAuthHeader();
                            std::string prefix = "Proxy-Authorization: Basic ";
                            CHALLENGE_ASSERT(authHeader.find(prefix) == 0, "Basic auth header format prefix");
                            std::string b64 = authHeader.substr(prefix.size());
                            auto decodedBytes = Crypto::base64Decode(b64);
                            std::string decoded(decodedBytes.begin(), decodedBytes.end());
                            std::string expectedCreds = cfg.username + ":" + cfg.password;
                            CHALLENGE_EQUALS(decoded, expectedCreds, "Base64 decode matches username:password");
                        }

                        count++;
                    }
                }
            }
        }
    }
    std::cout << "  [INFO] Completed " << count << " combinatorial proxy stress iterations." << std::endl;
}

// -----------------------------------------------------------------------------
// Suite 12: Adversarial Edge Cases & Hostile Inputs
// -----------------------------------------------------------------------------
void testSuite12_AdversarialHostileInputs() {
    std::cout << "[SUITE 12] Adversarial Edge Cases & Hostile Inputs" << std::endl;

    // 12.1 Unencoded '@' in password
    {
        auto p = ProxyUtils::parseProxyUrl("http://user:p@ss@proxy.example.com:8080");
        CHALLENGE_ASSERT(p.is_valid, "12.1 Valid with unencoded @ in pass");
        CHALLENGE_EQUALS(p.username, "user", "12.1 Username user");
        CHALLENGE_EQUALS(p.password, "p@ss", "12.1 Password p@ss");
        CHALLENGE_EQUALS(p.host, "proxy.example.com", "12.1 Host");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy.example.com:8080", "12.1 WinHttp proxy name clean");
    }

    // 12.2 Unencoded '@' in username (email-like username)
    {
        auto p = ProxyUtils::parseProxyUrl("http://user@domain.com:secret@proxy.example.com:8080");
        CHALLENGE_ASSERT(p.is_valid, "12.2 Valid with unencoded @ in user");
        CHALLENGE_EQUALS(p.username, "user@domain.com", "12.2 Email username");
        CHALLENGE_EQUALS(p.password, "secret", "12.2 Password secret");
        CHALLENGE_EQUALS(p.host, "proxy.example.com", "12.2 Host");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy.example.com:8080", "12.2 WinHttp proxy name clean");
    }

    // 12.3 Trailing colon with no port (http://proxy.com:)
    {
        auto p = ProxyUtils::parseProxyUrl("http://proxy.example.com:");
        CHALLENGE_ASSERT(p.is_valid, "12.3 Valid with trailing colon");
        CHALLENGE_EQUALS(p.host, "proxy.example.com", "12.3 Host");
        CHALLENGE_EQUALS(p.port, 80, "12.3 Default port 80 applied");
    }

    // 12.4 Incomplete percent-encoding (% at end, %a, %ZZ)
    {
        auto p1 = ProxyUtils::parseProxyUrl("http://user:pass%@proxy:8080");
        CHALLENGE_ASSERT(p1.is_valid, "12.4 Incomplete % at end does not crash");
        CHALLENGE_EQUALS(p1.password, "pass%", "12.4 Incomplete % preserved literally");

        auto p2 = ProxyUtils::parseProxyUrl("http://user:pass%a@proxy:8080");
        CHALLENGE_ASSERT(p2.is_valid, "12.4 Single hex digit %a does not crash");
        CHALLENGE_EQUALS(p2.password, "pass%a", "12.4 Single hex digit preserved literally");

        auto p3 = ProxyUtils::parseProxyUrl("http://user:pass%ZZ@proxy:8080");
        CHALLENGE_ASSERT(p3.is_valid, "12.4 Invalid hex %ZZ does not crash");
        CHALLENGE_EQUALS(p3.password, "pass%ZZ", "12.4 Invalid hex preserved literally");
    }

    // 12.5 Embedded null byte via percent encoding (%00)
    {
        auto p = ProxyUtils::parseProxyUrl("http://user:pass%00word@proxy:8080");
        CHALLENGE_ASSERT(p.is_valid, "12.5 Embedded null byte parsed safely");
        CHALLENGE_EQUALS(p.password.length(), 9, "12.5 Password contains exactly 9 bytes including null");
        CHALLENGE_ASSERT(p.password[4] == '\0', "12.5 Null byte at position 4");
    }

    // 12.6 Path, query, and fragment stripping
    {
        auto p = ProxyUtils::parseProxyUrl("http://user:pass@proxy.corp.net:8080/v1/api/endpoint?token=abc#section1");
        CHALLENGE_ASSERT(p.is_valid, "12.6 Valid");
        CHALLENGE_EQUALS(p.host, "proxy.corp.net", "12.6 Host isolated");
        CHALLENGE_EQUALS(p.port, 8080, "12.6 Port isolated");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy.corp.net:8080", "12.6 No path/query/fragment in proxy name");
    }

    // 12.7 Whitespace trimming (newlines, tabs, spaces)
    {
        auto p = ProxyUtils::parseProxyUrl("\r\n\t  http://user:pass@proxy.corp.net:8080  \t\r\n");
        CHALLENGE_ASSERT(p.is_valid, "12.7 Leading/trailing whitespace trimmed");
        CHALLENGE_EQUALS(p.host, "proxy.corp.net", "12.7 Host trimmed");
        CHALLENGE_EQUALS(p.port, 8080, "12.7 Port trimmed");
    }

    // 12.8 Very long URL (10,000 characters) - no buffer overflow
    {
        std::string longPassword(8000, 'A');
        std::string longUrl = "http://user:" + longPassword + "@proxy.long.test:8080";
        auto p = ProxyUtils::parseProxyUrl(longUrl);
        CHALLENGE_ASSERT(p.is_valid, "12.8 8KB password URL parsed safely without crash");
        CHALLENGE_EQUALS(p.password.length(), 8000, "12.8 Long password length preserved");
        CHALLENGE_EQUALS(p.host, "proxy.long.test", "12.8 Host isolated");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy.long.test:8080", "12.8 WinHttp proxy name clean");
    }

    // 12.9 Unicode UTF-8 username and password roundtrip
    {
        auto p = ProxyUtils::parseProxyUrl("http://\xD1\x8E\xD0\xB7\xD0\xB5\xD1\x80:\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C@proxy.ru:3128");
        CHALLENGE_ASSERT(p.is_valid, "12.9 Unicode credentials valid");
        CHALLENGE_EQUALS(p.host, "proxy.ru", "12.9 Host proxy.ru");
        CHALLENGE_ASSERT(p.is_authenticated, "12.9 Authenticated");
        CHALLENGE_ASSERT(p.toWinHttpProxyName() == L"proxy.ru:3128", "12.9 Clean proxy name");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), p.username), "12.9 No unicode user leak");
        CHALLENGE_ASSERT(!wideContainsNarrow(p.toWinHttpProxyName(), p.password), "12.9 No unicode pass leak");
    }
}

// -----------------------------------------------------------------------------
// Main Runner
// -----------------------------------------------------------------------------
int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " Challenger M2 Empirical Stress Test Harness" << std::endl;
    std::cout << " Target: Feature F6 (Proxy URL & Credential Parsing)" << std::endl;
    std::cout << "========================================================" << std::endl;

    testSuite1_StandardUrls();
    testSuite2_Schemeless();
    testSuite3_HostOnlyWithFallback();
    testSuite4_SpecialCharacters();
    testSuite5_IPv6Addresses();
    testSuite6_PortValidation();
    testSuite7_EmptyUserWithPassword();
    testSuite8_WinHttpProxyNameSanitization();
    testSuite9_LocalAddressBypass();
    testSuite10_BrowserMaskingHeaders();
    testSuite11_CombinatorialStress();
    testSuite12_AdversarialHostileInputs();

    std::cout << "========================================================" << std::endl;
    std::cout << " CHALLENGER EMPIRICAL RESULTS SUMMARY:" << std::endl;
    std::cout << " Total Assertions Checked: " << g_tests_run << std::endl;
    std::cout << " Passed:                   " << g_tests_passed << std::endl;
    std::cout << " Failed:                   " << g_tests_failed << std::endl;
    std::cout << "========================================================" << std::endl;

    if (g_tests_failed == 0) {
        std::cout << "[+] VERDICT: ALL EMPIRICAL CHALLENGES PASSED (100% SUCCESS)!" << std::endl;
        return 0;
    } else {
        std::cerr << "[-] VERDICT: " << g_tests_failed << " EMPIRICAL CHALLENGES FAILED!" << std::endl;
        return 1;
    }
}

