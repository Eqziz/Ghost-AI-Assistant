#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <cassert>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

#include "proxy_utils.hpp"
#include "network_logger.hpp"
#include "config.hpp"
#include "crypto.hpp"
#include "conversation.hpp"
#include "vision_client.hpp"

static int g_passCount = 0;
static int g_failCount = 0;

#define CHALLENGE_ASSERT(cond, msg) do { \
    if (cond) { \
        g_passCount++; \
    } else { \
        g_failCount++; \
        std::cerr << "[-] FAIL [Line " << __LINE__ << "]: " << msg << std::endl; \
    } \
} while(0)

#define CHALLENGE_SECTION(name) \
    std::cout << "\n======================================================\n" \
              << "  RUNNING: " << name << "\n" \
              << "======================================================" << std::endl;

// Helper: case-insensitive substring search
static bool containsCaseInsensitive(const std::string& str, const std::string& query) {
    auto it = std::search(
        str.begin(), str.end(),
        query.begin(), query.end(),
        [](char ch1, char ch2) { return std::tolower(static_cast<unsigned char>(ch1)) == std::tolower(static_cast<unsigned char>(ch2)); }
    );
    return (it != str.end());
}

// =========================================================================
// Suite 1: Proxy URL Parsing & Credential Decoupling (F6)
// =========================================================================
void runProxyParsingSuite() {
    CHALLENGE_SECTION("Suite 1: Proxy URL Parsing & Credential Decoupling (F6)");

    // 1.1 Full RFC 3986 format with credentials and port
    {
        auto cfg = ProxyUtils::parseProxyUrl("http://alice:secret123@proxy.lan:8080");
        CHALLENGE_ASSERT(cfg.is_valid, "Valid standard URL");
        CHALLENGE_ASSERT(cfg.scheme == "http", "Scheme is http");
        CHALLENGE_ASSERT(cfg.scheme_type == ProxyUtils::ProxyScheme::HTTP, "Scheme enum HTTP");
        CHALLENGE_ASSERT(cfg.host == "proxy.lan", "Host matches proxy.lan");
        CHALLENGE_ASSERT(cfg.port == 8080, "Port is 8080");
        CHALLENGE_ASSERT(cfg.username == "alice", "Username is alice");
        CHALLENGE_ASSERT(cfg.password == "secret123", "Password is secret123");
        CHALLENGE_ASSERT(cfg.is_authenticated, "Is authenticated");
        CHALLENGE_ASSERT(cfg.toWinHttpProxyName() == L"proxy.lan:8080", "WinHttp proxy name has no creds");
        CHALLENGE_ASSERT(cfg.toWinHttpUsername() == L"alice", "Wide username");
        CHALLENGE_ASSERT(cfg.toWinHttpPassword() == L"secret123", "Wide password");
    }

    // 1.2 Schemeless URL with credentials
    {
        auto cfg = ProxyUtils::parseProxyUrl("bob:p@ssword!@10.0.0.1:3128");
        CHALLENGE_ASSERT(cfg.is_valid, "Valid schemeless URL");
        CHALLENGE_ASSERT(cfg.scheme == "http", "Default scheme is http");
        CHALLENGE_ASSERT(cfg.host == "10.0.0.1", "Host is 10.0.0.1");
        CHALLENGE_ASSERT(cfg.port == 3128, "Port is 3128");
        CHALLENGE_ASSERT(cfg.username == "bob", "Username is bob");
        CHALLENGE_ASSERT(cfg.password == "p@ssword!", "Password with @ preserved");
        CHALLENGE_ASSERT(cfg.is_authenticated, "Is authenticated");
        CHALLENGE_ASSERT(cfg.toWinHttpProxyName() == L"10.0.0.1:3128", "WinHttp proxy name is 10.0.0.1:3128");
    }

    // 1.3 Percent-encoded credentials decoding
    {
        auto cfg = ProxyUtils::parseProxyUrl("http://user%40corp.com:p%23ss%25word@proxy.domain.com:8888");
        CHALLENGE_ASSERT(cfg.is_valid, "Valid URL with percent encoding");
        CHALLENGE_ASSERT(cfg.username == "user@corp.com", "Decoded username with @");
        CHALLENGE_ASSERT(cfg.password == "p#ss%word", "Decoded password with # and %");
        CHALLENGE_ASSERT(cfg.host == "proxy.domain.com", "Host parsed");
        CHALLENGE_ASSERT(cfg.port == 8888, "Port 8888 parsed");
    }

    // 1.4 Bracketed IPv6 proxy URL
    {
        auto cfg = ProxyUtils::parseProxyUrl("http://[2001:db8::1]:8080/ignored_path");
        CHALLENGE_ASSERT(cfg.is_valid, "Valid IPv6 URL");
        CHALLENGE_ASSERT(cfg.is_ipv6, "IPv6 flag set");
        CHALLENGE_ASSERT(cfg.host == "2001:db8::1", "Unbracketed host");
        CHALLENGE_ASSERT(cfg.port == 8080, "Port 8080");
        CHALLENGE_ASSERT(cfg.getHostPort() == "[2001:db8::1]:8080", "Bracketed host:port");
        CHALLENGE_ASSERT(cfg.toWinHttpProxyName() == L"[2001:db8::1]:8080", "Wide IPv6 host:port");
    }

    // 1.5 IPv6 localhost schemeless
    {
        auto cfg = ProxyUtils::parseProxyUrl("[::1]:9090");
        CHALLENGE_ASSERT(cfg.is_valid, "Valid schemeless IPv6");
        CHALLENGE_ASSERT(cfg.is_ipv6, "IPv6 flag set");
        CHALLENGE_ASSERT(cfg.host == "::1", "Host is ::1");
        CHALLENGE_ASSERT(cfg.port == 9090, "Port 9090");
    }

    // 1.6 Discrete fallback credentials from AppConfig
    {
        auto cfg = ProxyUtils::parseProxyUrl("http://corporate.proxy.lan:8080", "discrete_user", "discrete_pass");
        CHALLENGE_ASSERT(cfg.is_valid, "Valid proxy URL");
        CHALLENGE_ASSERT(cfg.username == "discrete_user", "Fallback username applied");
        CHALLENGE_ASSERT(cfg.password == "discrete_pass", "Fallback password applied");
        CHALLENGE_ASSERT(cfg.is_authenticated, "Is authenticated via discrete credentials");
    }

    // 1.7 URL credentials supersede discrete credentials
    {
        auto cfg = ProxyUtils::parseProxyUrl("http://url_user:url_pass@proxy:8080", "fallback_user", "fallback_pass");
        CHALLENGE_ASSERT(cfg.username == "url_user", "URL username takes precedence");
        CHALLENGE_ASSERT(cfg.password == "url_pass", "URL password takes precedence");
    }

    // 1.8 Default ports by scheme
    {
        auto cfgHttp = ProxyUtils::parseProxyUrl("http://proxy.lan");
        CHALLENGE_ASSERT(cfgHttp.port == 80, "Default HTTP port 80");
        auto cfgHttps = ProxyUtils::parseProxyUrl("https://proxy.lan");
        CHALLENGE_ASSERT(cfgHttps.port == 443, "Default HTTPS port 443");
        auto cfgSocks = ProxyUtils::parseProxyUrl("socks5://proxy.lan");
        CHALLENGE_ASSERT(cfgSocks.port == 1080, "Default SOCKS5 port 1080");
        auto cfgSchemeless = ProxyUtils::parseProxyUrl("proxy.lan");
        CHALLENGE_ASSERT(cfgSchemeless.port == 8080, "Default schemeless port 8080");
    }

    // 1.9 Boundary ports: 1 and 65535 valid; 0, 65536, and non-numeric invalid
    {
        auto cfgMin = ProxyUtils::parseProxyUrl("http://proxy:1");
        CHALLENGE_ASSERT(cfgMin.is_valid && cfgMin.port == 1, "Min port 1 valid");
        auto cfgMax = ProxyUtils::parseProxyUrl("http://proxy:65535");
        CHALLENGE_ASSERT(cfgMax.is_valid && cfgMax.port == 65535, "Max port 65535 valid");
        auto cfgZero = ProxyUtils::parseProxyUrl("http://proxy:0");
        CHALLENGE_ASSERT(!cfgZero.is_valid, "Port 0 invalid");
        auto cfgOver = ProxyUtils::parseProxyUrl("http://proxy:65536");
        CHALLENGE_ASSERT(!cfgOver.is_valid, "Port 65536 invalid");
        auto cfgAlpha = ProxyUtils::parseProxyUrl("http://proxy:abc");
        CHALLENGE_ASSERT(!cfgAlpha.is_valid, "Non-numeric port invalid");
        auto cfgEmpty = ProxyUtils::parseProxyUrl("   ");
        CHALLENGE_ASSERT(!cfgEmpty.is_valid, "Empty/whitespace URL invalid");
    }
}

// =========================================================================
// Suite 2: Password Redaction & Sanitization Audit (F6/F4)
// =========================================================================
void runPasswordSanitizationAuditSuite() {
    CHALLENGE_SECTION("Suite 2: Password Redaction & Sanitization Audit (F6/F4)");

    std::vector<std::pair<std::string, std::string>> testCases = {
        {"http://admin:SecretPassword123@proxy.lan:8080", "SecretPassword123"},
        {"https://user:p@ss:word!@10.0.0.1:443", "p@ss:word!"},
        {"userX:MyClearTextSecret@proxy:3128", "MyClearTextSecret"},
        {"http://operator:%24%25%5E%26*@proxy:8080", "$%^&*"}
    };

    for (const auto& tc : testCases) {
        auto cfg = ProxyUtils::parseProxyUrl(tc.first);
        std::string sanitized = cfg.getSanitizedUrl();
        CHALLENGE_ASSERT(sanitized.find("***") != std::string::npos, "Sanitized URL contains *** placeholder");
        CHALLENGE_ASSERT(sanitized.find(tc.second) == std::string::npos, "Sanitized URL NEVER contains cleartext password");
    }

    // Diagnostic NetworkLogger::sanitizeUrl check
    std::string testLogUrl = "http://ghost_user:SUPER_SECRET_TOKEN@proxy.internal:8080/api";
    std::string sanitizedLog = NetworkLogger::sanitizeUrl(testLogUrl);
    CHALLENGE_ASSERT(sanitizedLog.find("SUPER_SECRET_TOKEN") == std::string::npos, "NetworkLogger sanitizes URL passwords");
    CHALLENGE_ASSERT(sanitizedLog.find("***") != std::string::npos, "NetworkLogger places *** placeholder");
}

// =========================================================================
// Suite 3: Basic Auth RFC 7617 Generation & Invariants (F7)
// =========================================================================
void runBasicAuthGenerationSuite() {
    CHALLENGE_SECTION("Suite 3: Basic Auth RFC 7617 Generation & Invariants (F7)");

    // 3.1 Standard user:pass
    {
        std::string header = ProxyUtils::makeBasicAuthHeader("aladdin", "opensesame");
        CHALLENGE_ASSERT(header.rfind("Proxy-Authorization: Basic ", 0) == 0, "Header has correct prefix");
        std::string b64 = header.substr(27);
        auto decodedBytes = Crypto::base64Decode(b64);
        std::string decoded(decodedBytes.begin(), decodedBytes.end());
        CHALLENGE_ASSERT(decoded == "aladdin:opensesame", "Decoded credentials match RFC 7617");
    }

    // 3.2 Password with special characters and colons
    {
        std::string user = "admin";
        std::string pass = "complex:pass@word#123!";
        std::string header = ProxyUtils::makeBasicAuthHeader(user, pass);
        std::string b64 = header.substr(27);
        auto decodedBytes = Crypto::base64Decode(b64);
        std::string decoded(decodedBytes.begin(), decodedBytes.end());
        CHALLENGE_ASSERT(decoded == (user + ":" + pass), "Special character password decoded verbatim");
    }

    // 3.3 Empty credentials handling
    {
        std::string header = ProxyUtils::makeBasicAuthHeader("", "");
        CHALLENGE_ASSERT(header.empty(), "Empty user and pass produces empty header");
    }
}

// =========================================================================
// Suite 4: Stealth Browser Masking Headers Audit (F8)
// =========================================================================
void runStealthHeadersAuditSuite() {
    CHALLENGE_SECTION("Suite 4: Stealth Browser Masking Headers Audit (F8)");

    std::wstring headersW = ProxyUtils::buildBrowserHeadersW("Authorization: Bearer test_token_123");
    std::string headers = ProxyUtils::toNarrow(headersW);

    // 4.1 Chrome 131 User-Agent exact match
    const std::string expectedUA = "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
    CHALLENGE_ASSERT(headers.find(expectedUA) != std::string::npos, "Exact Chrome 131 User-Agent present");

    // 4.2 Standard browser headers present
    CHALLENGE_ASSERT(headers.find("Accept: application/json, text/plain, */*") != std::string::npos, "Accept header present");
    CHALLENGE_ASSERT(headers.find("Connection: keep-alive") != std::string::npos, "Connection: keep-alive header present");
    CHALLENGE_ASSERT(headers.find("Accept-Language: en-US,en;q=0.9") != std::string::npos, "Accept-Language header present");
    CHALLENGE_ASSERT(headers.find("Content-Type: application/json; charset=utf-8") != std::string::npos, "Content-Type header present");
    CHALLENGE_ASSERT(headers.find("Authorization: Bearer test_token_123") != std::string::npos, "Sanitized auth header appended");

    // 4.3 Absence of Content-Length (must be handled by WinHttp)
    CHALLENGE_ASSERT(headers.find("Content-Length") == std::string::npos, "Content-Length NOT in static header block (prevents duplicate)");

    // 4.4 Absence of identifying markers
    std::vector<std::string> bannedMarkers = {"ghost", "ghostai", "assistant", "bot", "crawler", "python", "curl", "libwinhttp"};
    for (const auto& marker : bannedMarkers) {
        CHALLENGE_ASSERT(!containsCaseInsensitive(headers, marker), "Headers free from marker: " + marker);
    }

    // 4.5 CRLF delimiter conformity
    CHALLENGE_ASSERT(headers.find("\r\n") != std::string::npos, "Contains CRLF delimiters");
    std::stringstream ss(headers);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            CHALLENGE_ASSERT(line.find(':') != std::string::npos, "Valid Header: Value line structure: " + line);
        }
    }
}

// =========================================================================
// Suite 5: CRLF Injection Defense Stress (F8)
// =========================================================================
void runCrlfInjectionDefenseSuite() {
    CHALLENGE_SECTION("Suite 5: CRLF Injection Defense Stress (F8)");

    std::vector<std::string> attackVectors = {
        "Bearer legit_token\r\nX-Injected-Header: evil_value",
        "Bearer legit_token\nInjected-Header: evil_value",
        "Bearer legit_token\rInjected-Header: evil_value",
        "Bearer legit_token\r\n\r\nPOST /smuggled HTTP/1.1\r\nHost: evil.com",
        "Bearer token\r\nProxy-Authorization: Basic evil_creds\r\n"
    };

    for (const auto& attack : attackVectors) {
        std::string sanitized = ProxyUtils::sanitizeHeaderValue(attack);
        CHALLENGE_ASSERT(sanitized.find('\r') == std::string::npos, "Stripped \\r from attack vector");
        CHALLENGE_ASSERT(sanitized.find('\n') == std::string::npos, "Stripped \\n from attack vector");

        std::wstring headersW = ProxyUtils::buildBrowserHeadersW(attack);
        std::string headers = ProxyUtils::toNarrow(headersW);

        // Count lines in generated headers: standard is 6 lines (5 browser + 1 auth)
        int lineCount = 0;
        size_t pos = 0;
        while ((pos = headers.find("\r\n", pos)) != std::string::npos) {
            lineCount++;
            pos += 2;
        }
        CHALLENGE_ASSERT(lineCount == 6, "CRLF injection did not add extra header lines (exactly 6 headers)");
        CHALLENGE_ASSERT(headers.find("\r\nX-Injected-Header:") == std::string::npos, "Did not create distinct injected header line");
        CHALLENGE_ASSERT(headers.find("\r\nInjected-Header:") == std::string::npos, "Did not create distinct injected header line");
        CHALLENGE_ASSERT(headers.find("\r\nProxy-Authorization:") == std::string::npos, "Did not smuggle extra Proxy-Authorization header");
        CHALLENGE_ASSERT(headers.find("\r\n\r\nPOST /smuggled") == std::string::npos, "Did not split HTTP request into smuggled POST");
    }
}

// =========================================================================
// Suite 6: Local Address Bypass Detection Logic (F7)
// =========================================================================
void runLocalAddressBypassSuite() {
    CHALLENGE_SECTION("Suite 6: Local Address Bypass Detection Logic (F7)");

    std::vector<std::pair<std::string, bool>> testVectors = {
        {"localhost", true},
        {"127.0.0.1", true},
        {"127.0.0.2", true},
        {"127.100.200.1", true},
        {"::1", true},
        {"[::1]", true},
        {"192.168.1.1", false},
        {"10.0.0.1", false},
        {"172.16.0.1", false},
        {"proxy.corporate.lan", false},
        {"openrouter.ai", false},
        {"api.openai.com", false}
    };

    for (const auto& tv : testVectors) {
        bool res = ProxyUtils::isLocalAddress(tv.first);
        CHALLENGE_ASSERT(res == tv.second, "isLocalAddress('" + tv.first + "') == " + (tv.second ? "true" : "false"));
    }
}

// =========================================================================
// Mock Winsock Server Helper for Live Network Tests
// =========================================================================
class MockHttpTestServer {
public:
    enum class ServerMode {
        CAPTURE_HEADERS_200,
        PROXY_407_THEN_200,
        PROXY_407_ALWAYS_LOOP,
        PROXY_502_BAD_GATEWAY,
        PROXY_DROP_CONNECTION
    };

    MockHttpTestServer(ServerMode mode, const std::string& expectedUser = "ghost_user", const std::string& expectedPass = "ghost_secret")
        : m_mode(mode), m_expectedUser(expectedUser), m_expectedPass(expectedPass), m_port(0), m_running(false), m_listenSock(INVALID_SOCKET), m_requestCount(0)
    {}

    ~MockHttpTestServer() {
        stop();
    }

    bool start() {
        m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSock == INVALID_SOCKET) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // Ephemeral port

        if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(m_listenSock);
            return false;
        }

        if (listen(m_listenSock, 10) == SOCKET_ERROR) {
            closesocket(m_listenSock);
            return false;
        }

        int addrLen = sizeof(addr);
        getsockname(m_listenSock, (sockaddr*)&addr, &addrLen);
        m_port = ntohs(addr.sin_port);

        m_running = true;
        m_thread = std::thread(&MockHttpTestServer::workerLoop, this);
        return true;
    }

    void stop() {
        if (!m_running) return;
        m_running = false;
        if (m_listenSock != INVALID_SOCKET) {
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    int getPort() const { return m_port; }
    int getRequestCount() const { return m_requestCount.load(); }
    std::string getLastRawRequest() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastRawRequest;
    }

private:
    void workerLoop() {
        while (m_running) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(m_listenSock, &readSet);
            timeval tv{0, 100000}; // 100ms

            int sel = select(0, &readSet, nullptr, nullptr, &tv);
            if (sel <= 0) continue;

            sockaddr_in clientAddr{};
            int clientLen = sizeof(clientAddr);
            SOCKET clientSock = accept(m_listenSock, (sockaddr*)&clientAddr, &clientLen);
            if (clientSock == INVALID_SOCKET) continue;

            handleClient(clientSock);
        }
    }

    bool readFullHttpRequest(SOCKET sock, std::string& outHeaders, std::string& outBody) {
        std::string raw;
        size_t headerEnd = std::string::npos;
        while (headerEnd == std::string::npos && m_running) {
            char buf[1024];
            int n = recv(sock, buf, sizeof(buf), 0);
            if (n <= 0) return false;
            raw.append(buf, n);
            headerEnd = raw.find("\r\n\r\n");
        }
        if (headerEnd == std::string::npos) return false;

        outHeaders = raw.substr(0, headerEnd + 4);
        std::string bodySoFar = raw.substr(headerEnd + 4);

        size_t clPos = outHeaders.find("Content-Length:");
        if (clPos == std::string::npos) clPos = outHeaders.find("content-length:");
        int contentLen = 0;
        if (clPos != std::string::npos) {
            size_t lineEnd = outHeaders.find("\r\n", clPos);
            std::string clVal = outHeaders.substr(clPos + 15, lineEnd - (clPos + 15));
            try { contentLen = std::stoi(clVal); } catch (...) { contentLen = 0; }
        }

        outBody = bodySoFar;
        while ((int)outBody.length() < contentLen && m_running) {
            char buf[1024];
            int n = recv(sock, buf, sizeof(buf), 0);
            if (n <= 0) break;
            outBody.append(buf, n);
        }
        return true;
    }

    void handleClient(SOCKET clientSock) {
        if (m_mode == ServerMode::PROXY_DROP_CONNECTION) {
            closesocket(clientSock);
            m_requestCount++;
            return;
        }

        while (m_running) {
            std::string headers, body;
            if (!readFullHttpRequest(clientSock, headers, body)) {
                break;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_lastRawRequest = headers + body;
            }
            m_requestCount++;

            if (m_mode == ServerMode::CAPTURE_HEADERS_200) {
                std::string resp =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: 23\r\n"
                    "Connection: close\r\n\r\n"
                    "{\"response\":\"success\"}";
                send(clientSock, resp.c_str(), (int)resp.length(), 0);
                break;
            }
            else if (m_mode == ServerMode::PROXY_407_THEN_200) {
                std::string expectedAuth = ProxyUtils::makeBasicAuthHeader(m_expectedUser, m_expectedPass);
                if (headers.find(expectedAuth) != std::string::npos) {
                    // Authenticated successfully!
                    std::string resp =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: 28\r\n"
                        "Connection: close\r\n\r\n"
                        "{\"response\":\"auth_success\"}";
                    send(clientSock, resp.c_str(), (int)resp.length(), 0);
                    break;
                } else {
                    // Send 407 Challenge with keep-alive
                    std::string respBody = "Proxy Authentication Required";
                    std::string resp =
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Basic realm=\"GhostAI Test Proxy\"\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: " + std::to_string(respBody.length()) + "\r\n"
                        "Connection: keep-alive\r\n\r\n" + respBody;
                    send(clientSock, resp.c_str(), (int)resp.length(), 0);
                }
            }
            else if (m_mode == ServerMode::PROXY_407_ALWAYS_LOOP) {
                std::string respBody = "Proxy Authentication Required";
                std::string resp =
                    "HTTP/1.1 407 Proxy Authentication Required\r\n"
                    "Proxy-Authenticate: Basic realm=\"GhostAI Test Proxy\"\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: " + std::to_string(respBody.length()) + "\r\n"
                    "Connection: keep-alive\r\n\r\n" + respBody;
                send(clientSock, resp.c_str(), (int)resp.length(), 0);
            }
            else if (m_mode == ServerMode::PROXY_502_BAD_GATEWAY) {
                std::string resp =
                    "HTTP/1.1 502 Bad Gateway\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 17\r\n"
                    "Connection: close\r\n\r\n"
                    "Proxy 502 Gateway";
                send(clientSock, resp.c_str(), (int)resp.length(), 0);
                break;
            }
        }

        closesocket(clientSock);
    }

    ServerMode m_mode;
    std::string m_expectedUser;
    std::string m_expectedPass;
    int m_port;
    std::atomic<bool> m_running;
    SOCKET m_listenSock;
    std::thread m_thread;
    std::atomic<int> m_requestCount;
    mutable std::mutex m_mutex;
    std::string m_lastRawRequest;
};

// =========================================================================
// Suite 7: Live Wire Header Capture & Verification (F8)
// =========================================================================
void runLiveWireHeaderCaptureSuite() {
    CHALLENGE_SECTION("Suite 7: Live Wire Header Capture & Verification (F8)");

    MockHttpTestServer server(MockHttpTestServer::ServerMode::CAPTURE_HEADERS_200);
    CHALLENGE_ASSERT(server.start(), "Started local mock HTTP server");

    AppConfig config;
    config.backend = "ollama";
    config.ollama_url = "http://127.0.0.1:" + std::to_string(server.getPort());
    config.ollama_model = "test-model";
    config.request_timeout = 5;

    ConversationMemory mem;
    VisionClient client(config, mem);

    std::string resp = client.analyze("mock_base64_image", "test prompt");
    CHALLENGE_ASSERT(resp == "success", "Client received valid response");

    std::string rawReq = server.getLastRawRequest();
    CHALLENGE_ASSERT(!rawReq.empty(), "Captured raw HTTP request over wire");

    // Wire verification
    CHALLENGE_ASSERT(rawReq.find("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36") != std::string::npos,
                     "Wire request contains exact Chrome 131 User-Agent");
    CHALLENGE_ASSERT(rawReq.find("Accept: application/json, text/plain, */*") != std::string::npos,
                     "Wire request contains standard Accept header");
    CHALLENGE_ASSERT(containsCaseInsensitive(rawReq, "Connection: keep-alive"),
                     "Wire request contains Connection: keep-alive header");
    CHALLENGE_ASSERT(rawReq.find("Accept-Language: en-US,en;q=0.9") != std::string::npos,
                     "Wire request contains Accept-Language header");
    CHALLENGE_ASSERT(rawReq.find("Content-Type: application/json; charset=utf-8") != std::string::npos,
                     "Wire request contains Content-Type header");

    // Zero ghost/identifying tokens in wire headers
    size_t headerEnd = rawReq.find("\r\n\r\n");
    std::string wireHeaders = (headerEnd != std::string::npos) ? rawReq.substr(0, headerEnd) : rawReq;
    CHALLENGE_ASSERT(!containsCaseInsensitive(wireHeaders, "ghost"), "Zero 'ghost' tokens in wire headers");
    CHALLENGE_ASSERT(!containsCaseInsensitive(wireHeaders, "bot"), "Zero 'bot' tokens in wire headers");
}

// =========================================================================
// Suite 8: Live HTTP 407 Challenge & Basic Auth Negotiation (F7)
// =========================================================================
void runLive407ChallengeNegotiationSuite() {
    CHALLENGE_SECTION("Suite 8: Live HTTP 407 Challenge & Basic Auth Negotiation (F7)");

    MockHttpTestServer proxy(MockHttpTestServer::ServerMode::PROXY_407_THEN_200, "ghost_admin", "p@ssword!123");
    CHALLENGE_ASSERT(proxy.start(), "Started local mock 407 authenticating proxy");

    AppConfig config;
    config.backend = "ollama";
    // Non-local origin domain so proxy is used
    config.ollama_url = "http://remote-origin.corp.internal:80";
    config.ollama_model = "test-model";
    config.proxy_url = "http://ghost_admin:p%40ssword%21123@127.0.0.1:" + std::to_string(proxy.getPort());
    config.request_timeout = 5;

    ConversationMemory mem;
    VisionClient client(config, mem);

    std::string resp;
    bool success = false;
    try {
        resp = client.analyze("mock_base64_image", "test prompt");
        success = true;
    } catch (const std::exception& e) {
        std::cerr << "analyze through proxy failed: " << e.what() << std::endl;
    }

    CHALLENGE_ASSERT(success, "407 Challenge negotiated and succeeded");
    CHALLENGE_ASSERT(resp == "auth_success", "Received authenticated response from proxy");
    CHALLENGE_ASSERT(proxy.getRequestCount() == 2, "Proxy received exactly 2 requests (1st: 407 challenge, 2nd: authenticated)");
}

// =========================================================================
// Suite 9: Live 407 Infinite Loop Prevention Stress (F7)
// =========================================================================
void runLive407LoopPreventionSuite() {
    CHALLENGE_SECTION("Suite 9: Live 407 Infinite Loop Prevention Stress (F7)");

    MockHttpTestServer proxy(MockHttpTestServer::ServerMode::PROXY_407_ALWAYS_LOOP);
    CHALLENGE_ASSERT(proxy.start(), "Started mock 407 rejecting proxy");

    AppConfig config;
    config.backend = "ollama";
    config.ollama_url = "http://remote-origin.corp.internal:80";
    config.ollama_model = "test-model";
    config.proxy_url = "http://bad_user:WRONG_PASSWORD@127.0.0.1:" + std::to_string(proxy.getPort());
    config.request_timeout = 5;

    ConversationMemory mem;
    VisionClient client(config, mem);

    auto tStart = std::chrono::steady_clock::now();
    bool threwExpected407 = false;
    try {
        client.analyze("mock_base64_image", "test prompt");
    } catch (const std::runtime_error& e) {
        std::string errStr = e.what();
        if (errStr.find("407") != std::string::npos) {
            threwExpected407 = true;
        }
    }
    auto tEnd = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

    CHALLENGE_ASSERT(threwExpected407, "Client cleanly threw expected HTTP 407 exception on rejected auth");
    CHALLENGE_ASSERT(proxy.getRequestCount() <= 3, "Strict loop prevention: request count <= 3 (actual: " + std::to_string(proxy.getRequestCount()) + ")");
    CHALLENGE_ASSERT(elapsedMs < 2000, "Terminated promptly without hanging/looping (elapsed " + std::to_string(elapsedMs) + "ms)");
}

// =========================================================================
// Suite 10: Live Local Address Proxy Bypass (F7)
// =========================================================================
void runLiveLocalAddressBypassSuite() {
    CHALLENGE_SECTION("Suite 10: Live Local Address Proxy Bypass (F7)");

    // Start local server (target)
    MockHttpTestServer targetServer(MockHttpTestServer::ServerMode::CAPTURE_HEADERS_200);
    CHALLENGE_ASSERT(targetServer.start(), "Started local destination server");

    // Configure a completely DEAD/INVALID proxy port (e.g. 59999)
    AppConfig config;
    config.backend = "ollama";
    config.ollama_url = "http://127.0.0.1:" + std::to_string(targetServer.getPort());
    config.ollama_model = "test-model";
    config.proxy_url = "http://127.0.0.1:59999"; // Non-existent proxy
    config.request_timeout = 5;

    ConversationMemory mem;
    VisionClient client(config, mem);

    // Because target 127.0.0.1 is local, it MUST bypass the proxy at 59999 and connect directly!
    bool bypassedAndSucceeded = false;
    try {
        std::string resp = client.analyze("mock_base64", "test prompt");
        if (resp == "success") {
            bypassedAndSucceeded = true;
        }
    } catch (const std::exception& e) {
        std::cerr << "Local request failed (proxy was not bypassed!): " << e.what() << std::endl;
    }

    CHALLENGE_ASSERT(bypassedAndSucceeded, "Local address 127.0.0.1 bypassed non-existent proxy and succeeded directly");
}

// =========================================================================
// Suite 11: Live Proxy 502 Bad Gateway & Connection Drop Recovery (F7)
// =========================================================================
void runLiveProxyFailuresRecoverySuite() {
    CHALLENGE_SECTION("Suite 11: Live Proxy 502 Bad Gateway & Connection Drop Recovery (F7)");

    // 11.1 Proxy 502 Bad Gateway
    {
        MockHttpTestServer proxy502(MockHttpTestServer::ServerMode::PROXY_502_BAD_GATEWAY);
        CHALLENGE_ASSERT(proxy502.start(), "Started mock 502 Bad Gateway proxy");

        AppConfig config;
        config.backend = "ollama";
        config.ollama_url = "http://remote.origin:80";
        config.ollama_model = "test-model";
        config.proxy_url = "http://127.0.0.1:" + std::to_string(proxy502.getPort());
        config.request_timeout = 5;

        ConversationMemory mem;
        VisionClient client(config, mem);

        bool threw502 = false;
        try {
            client.analyze("mock_base64", "test prompt");
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()).find("502") != std::string::npos) {
                threw502 = true;
            }
        }
        CHALLENGE_ASSERT(threw502, "Client cleanly detected and threw HTTP 502 error");
    }

    // 11.2 Proxy connection drop (immediate TCP close)
    {
        MockHttpTestServer proxyDrop(MockHttpTestServer::ServerMode::PROXY_DROP_CONNECTION);
        CHALLENGE_ASSERT(proxyDrop.start(), "Started mock drop proxy");

        AppConfig config;
        config.backend = "ollama";
        config.ollama_url = "http://remote.origin:80";
        config.ollama_model = "test-model";
        config.proxy_url = "http://127.0.0.1:" + std::to_string(proxyDrop.getPort());
        config.request_timeout = 5;

        ConversationMemory mem;
        VisionClient client(config, mem);

        bool caughtDrop = false;
        try {
            client.analyze("mock_base64", "test prompt");
        } catch (const std::exception& e) {
            caughtDrop = true;
        }
        CHALLENGE_ASSERT(caughtDrop, "Client handled dropped proxy connection without crash");
    }
}

// =========================================================================
// Main Test Entry Point
// =========================================================================
int main() {
    std::cout << "======================================================\n"
              << " GhostAI M2 Empirical Challenger 2 Stress Harness\n"
              << " Features F7 (Proxy Auth) & F8 (Stealth Protocol)\n"
              << "======================================================\n";

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return 1;
    }

    runProxyParsingSuite();
    runPasswordSanitizationAuditSuite();
    runBasicAuthGenerationSuite();
    runStealthHeadersAuditSuite();
    runCrlfInjectionDefenseSuite();
    runLocalAddressBypassSuite();
    runLiveWireHeaderCaptureSuite();
    runLive407ChallengeNegotiationSuite();
    runLive407LoopPreventionSuite();
    runLiveLocalAddressBypassSuite();
    runLiveProxyFailuresRecoverySuite();

    WSACleanup();

    std::cout << "\n======================================================\n"
              << " Challenger 2 M2 Results Summary:\n"
              << " PASSED: " << g_passCount << "\n"
              << " FAILED: " << g_failCount << "\n"
              << "======================================================\n";

    if (g_failCount == 0) {
        std::cout << "[+] ALL EMPIRICAL CHALLENGES PASSED SUCCESSFULLY!\n";
        return 0;
    } else {
        std::cerr << "[-] EMPIRICAL CHALLENGES FAILED: " << g_failCount << " failure(s)\n";
        return 1;
    }
}
