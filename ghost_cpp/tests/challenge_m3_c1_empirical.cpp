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
#include <fstream>
#include <mutex>

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
              << "  [CHALLENGER 1] " << name << "\n" \
              << "======================================================" << std::endl;

enum ServerBehavior {
    BEHAVIOR_OK_OPENAI,
    BEHAVIOR_OK_OLLAMA,
    BEHAVIOR_HTTP_ERROR,
    BEHAVIOR_HANG_TIMEOUT,
    BEHAVIOR_ABORT_CONNECTION,
    BEHAVIOR_CORRUPT_JSON
};

class ControlledTestServer {
public:
    ControlledTestServer()
        : m_listenSock(INVALID_SOCKET), m_port(0), m_running(false),
          m_behavior(BEHAVIOR_OK_OPENAI), m_errorCode(500), m_hitCount(0) {}

    ~ControlledTestServer() { stop(); }

    bool start() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

        m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSock == INVALID_SOCKET) return false;

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // Dynamic port

        if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(m_listenSock);
            return false;
        }

        sockaddr_in boundAddr = {};
        int len = sizeof(boundAddr);
        if (getsockname(m_listenSock, (sockaddr*)&boundAddr, &len) == 0) {
            m_port = ntohs(boundAddr.sin_port);
        }

        if (listen(m_listenSock, 20) != 0) {
            closesocket(m_listenSock);
            return false;
        }

        m_running = true;
        m_serverThread = std::thread(&ControlledTestServer::workerLoop, this);
        return true;
    }

    void stop() {
        m_running = false;
        if (m_listenSock != INVALID_SOCKET) {
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
        }
        if (m_serverThread.joinable()) {
            m_serverThread.join();
        }
        WSACleanup();
    }

    int getPort() const { return m_port; }

    void setBehavior(ServerBehavior b, int errorCode = 500, const std::string& customResponse = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_behavior = b;
        m_errorCode = errorCode;
        m_customResponse = customResponse;
    }

    int getHitCount() const {
        return m_hitCount.load();
    }

    void resetHitCount() {
        m_hitCount = 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastRequestBody.clear();
        m_lastRequestHeaders.clear();
    }

    std::string getLastRequestBody() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastRequestBody;
    }

private:
    void workerLoop() {
        while (m_running) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_listenSock, &fds);
            timeval tv = { 0, 30000 }; // 30ms
            int sel = select(0, &fds, NULL, NULL, &tv);
            if (sel <= 0) continue;

            sockaddr_in clientAddr;
            int clientLen = sizeof(clientAddr);
            SOCKET clientSock = accept(m_listenSock, (sockaddr*)&clientAddr, &clientLen);
            if (clientSock == INVALID_SOCKET) continue;

            m_hitCount++;

            ServerBehavior behavior;
            int errCode;
            std::string custom;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                behavior = m_behavior;
                errCode = m_errorCode;
                custom = m_customResponse;
            }

            if (behavior == BEHAVIOR_HANG_TIMEOUT) {
                // Sleep longer than client timeout (e.g. 2.5s) to cause WinHttp 12002
                std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                closesocket(clientSock);
                continue;
            }

            if (behavior == BEHAVIOR_ABORT_CONNECTION) {
                // Send RST or close immediately without writing data (causes WinHttp 12030)
                linger sl = { 1, 0 }; // linger on, 0 sec -> abortive close / RST
                setsockopt(clientSock, SOL_SOCKET, SO_LINGER, (char*)&sl, sizeof(sl));
                closesocket(clientSock);
                continue;
            }

            char buf[8192] = { 0 };
            int n = recv(clientSock, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                std::string req(buf, n);
                size_t bodyPos = req.find("\r\n\r\n");
                size_t clPos = req.find("Content-Length: ");
                size_t expectedLen = 0;
                if (clPos != std::string::npos) {
                    try {
                        expectedLen = std::stoul(req.substr(clPos + 16));
                    } catch (...) {}
                }
                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
                while (body.size() < expectedLen) {
                    char extra[4096] = { 0 };
                    int extraN = recv(clientSock, extra, sizeof(extra) - 1, 0);
                    if (extraN <= 0) break;
                    body.append(extra, extraN);
                }
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_lastRequestHeaders = req;
                    m_lastRequestBody = body;
                }

                std::string respHttp;
                if (behavior == BEHAVIOR_OK_OPENAI) {
                    std::string ans = !custom.empty() ? custom : "Valid OpenAI Response";
                    std::string json = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"" + ans + "\"}}]}";
                    respHttp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(json.size()) + "\r\nConnection: close\r\n\r\n" + json;
                } else if (behavior == BEHAVIOR_OK_OLLAMA) {
                    std::string ans = !custom.empty() ? custom : "Valid Ollama Response";
                    std::string json = "{\"model\":\"llava:latest\",\"response\":\"" + ans + "\",\"done\":true}";
                    respHttp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(json.size()) + "\r\nConnection: close\r\n\r\n" + json;
                } else if (behavior == BEHAVIOR_HTTP_ERROR) {
                    std::string errBody = !custom.empty() ? custom : ("Server Error " + std::to_string(errCode));
                    respHttp = "HTTP/1.1 " + std::to_string(errCode) + " Error\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(errBody.size()) + "\r\nConnection: close\r\n\r\n" + errBody;
                } else if (behavior == BEHAVIOR_CORRUPT_JSON) {
                    std::string corrupt = "{\"choices\": [broken JSON truncated...";
                    respHttp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(corrupt.size()) + "\r\nConnection: close\r\n\r\n" + corrupt;
                }

                send(clientSock, respHttp.data(), (int)respHttp.size(), 0);
            }
            closesocket(clientSock);
        }
    }

    SOCKET m_listenSock;
    int m_port;
    std::atomic<bool> m_running;
    std::thread m_serverThread;
    mutable std::mutex m_mutex;
    ServerBehavior m_behavior;
    int m_errorCode;
    std::string m_customResponse;
    std::atomic<int> m_hitCount;
    std::string m_lastRequestBody;
    std::string m_lastRequestHeaders;
};

// Helper to check network.log content
bool logFileContains(const std::string& needle) {
    std::ifstream file(NetworkLogger::getNetworkLogPath());
    if (!file.is_open()) return false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void clearLogFiles() {
    std::ofstream(NetworkLogger::getNetworkLogPath(), std::ios::trunc).close();
    std::ofstream(NetworkLogger::getStartupLogPath(), std::ios::trunc).close();
}

// =========================================================================
// Suite 1: Tier 1 Failure Transitions to Tier 2 (WinHTTP 12029, 12002, 12030, HTTP 5xx)
// =========================================================================
void testSuite1_Tier1Transitions() {
    CHALLENGE_SECTION("Suite 1: Tier 1 Failure Transitions to Tier 2");

    ControlledTestServer relayServer;
    ControlledTestServer ollamaServer;
    assert(relayServer.start());
    assert(ollamaServer.start());

    relayServer.setBehavior(BEHAVIOR_OK_OPENAI, 200, "SuccessFromTier2Relay");
    ollamaServer.setBehavior(BEHAVIOR_OK_OLLAMA, 200, "SuccessFromTier3Ollama");

    // Case 1.1: Tier 1 WinHTTP 12029 (Connection Refused via closed proxy port)
    {
        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:59998"; // Closed port -> 12029
        cfg.openrouter_api_key = "key1";
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.fallback_api_key = "key2";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        std::vector<std::string> statuses;
        VisionClient client(cfg, [&](const std::string& msg) { statuses.push_back(msg); });

        std::string res;
        try {
            res = client.executeCascade("", "Prompt 12029");
        } catch (...) {}

        CHALLENGE_ASSERT(res == "SuccessFromTier2Relay", "12029 on Tier 1 successfully fell over to Tier 2 Relay");
        CHALLENGE_ASSERT(relayServer.getHitCount() == 1, "Tier 2 Relay was contacted exactly once");
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 0, "Tier 3 Ollama was NOT contacted (Tier 2 succeeded)");
        CHALLENGE_ASSERT(statuses.size() >= 2, "Status sequence has at least 2 updates");
        if (statuses.size() >= 2) {
            CHALLENGE_ASSERT(statuses[0] == "Анализирую...", "First status is 'Анализирую...'");
            CHALLENGE_ASSERT(statuses[1] == "Подключение к резервному серверу...", "Second status is 'Подключение к резервному серверу...'");
        }
    }

    // Case 1.2: Tier 1 WinHTTP 12002 (Timeout)
    {
        ControlledTestServer hangServer;
        assert(hangServer.start());
        hangServer.setBehavior(BEHAVIOR_HANG_TIMEOUT);

        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        // Direct Tier 1 to hangServer
        AppConfig cfg;
        cfg.openrouter_api_key = "key1";
        // To route Tier 1 to our hangServer, we can set proxy to forward or test fallback directly.
        // In VisionClient, Tier 1 URL is fixed to https://openrouter.ai/api/v1/chat/completions,
        // but any proxy configured intercepts Tier 1!
        cfg.proxy_url = "http://127.0.0.1:" + std::to_string(hangServer.getPort());
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 1; // 1s timeout, server hangs for 2.5s

        std::vector<std::string> statuses;
        VisionClient client(cfg, [&](const std::string& msg) { statuses.push_back(msg); });

        std::string res;
        try {
            res = client.executeCascade("", "Prompt Timeout");
        } catch (...) {}

        CHALLENGE_ASSERT(res == "SuccessFromTier2Relay", "12002 Timeout on Tier 1 seamlessly fell over to Tier 2 Relay");
        CHALLENGE_ASSERT(relayServer.getHitCount() == 1, "Tier 2 Relay received request after Tier 1 timeout");
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 0, "Tier 3 Ollama was not needed");

        hangServer.stop();
    }

    // Case 1.3: Tier 1 WinHTTP 12030 (Connection Aborted / RST)
    {
        ControlledTestServer abortServer;
        assert(abortServer.start());
        abortServer.setBehavior(BEHAVIOR_ABORT_CONNECTION);

        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:" + std::to_string(abortServer.getPort());
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        std::string res;
        try {
            VisionClient client(cfg);
            res = client.executeCascade("", "Prompt Abort");
        } catch (...) {}

        CHALLENGE_ASSERT(res == "SuccessFromTier2Relay", "12030 Connection Aborted on Tier 1 seamlessly fell over to Tier 2 Relay");
        CHALLENGE_ASSERT(relayServer.getHitCount() == 1, "Tier 2 Relay received request after Tier 1 abort");

        abortServer.stop();
    }

    // Case 1.4: Tier 1 HTTP 5xx Server Errors (500, 502, 503, 504)
    for (int httpErr : { 500, 502, 503, 504 }) {
        ControlledTestServer errServer;
        assert(errServer.start());
        errServer.setBehavior(BEHAVIOR_HTTP_ERROR, httpErr, "Cloud provider overloaded");

        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:" + std::to_string(errServer.getPort());
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        std::string res;
        try {
            VisionClient client(cfg);
            res = client.executeCascade("", "Prompt HTTP 5xx");
        } catch (...) {}

        CHALLENGE_ASSERT(res == "SuccessFromTier2Relay", "HTTP " + std::to_string(httpErr) + " on Tier 1 successfully fell over to Tier 2 Relay");
        CHALLENGE_ASSERT(relayServer.getHitCount() == 1, "Tier 2 contacted on HTTP " + std::to_string(httpErr));

        errServer.stop();
    }

    relayServer.stop();
    ollamaServer.stop();
}

// =========================================================================
// Suite 2: Tier 2 Failure Transitions to Tier 3 (Local Offline Ollama)
// =========================================================================
void testSuite2_Tier2Transitions() {
    CHALLENGE_SECTION("Suite 2: Tier 2 Failure Transitions to Tier 3");

    ControlledTestServer relayServer;
    ControlledTestServer ollamaServer;
    assert(relayServer.start());
    assert(ollamaServer.start());

    ollamaServer.setBehavior(BEHAVIOR_OK_OLLAMA, 200, "FinalResponseFromTier3Ollama");

    for (int tier2Err : { 500, 502, 503, 504 }) {
        relayServer.setBehavior(BEHAVIOR_HTTP_ERROR, tier2Err, "VPS Relay 5xx");
        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:59998"; // Closed port -> Tier 1 fails 12029
        cfg.openrouter_api_key = "key1";
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.fallback_api_key = "key2";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        std::vector<std::string> statuses;
        VisionClient client(cfg, [&](const std::string& msg) { statuses.push_back(msg); });

        std::string res;
        try {
            res = client.executeCascade("", "Prompt Tier 2 fail");
        } catch (const std::exception& e) {
            std::cerr << "[-] Error in tier 2 test: " << e.what() << std::endl;
        }

        CHALLENGE_ASSERT(res == "FinalResponseFromTier3Ollama", "Tier 2 error " + std::to_string(tier2Err) + " cascaded down to Tier 3 Ollama");
        CHALLENGE_ASSERT(relayServer.getHitCount() == 1, "Tier 2 was attempted");
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 1, "Tier 3 Ollama was successfully called");

        // Verify status sequence: "Анализирую..." -> "Подключение к резервному серверу..." -> "Переключение на локальный Ollama..."
        CHALLENGE_ASSERT(statuses.size() >= 3, "Status sequence has 3 notifications");
        if (statuses.size() >= 3) {
            CHALLENGE_ASSERT(statuses[0] == "Анализирую...", "Status 1 is 'Анализирую...'");
            CHALLENGE_ASSERT(statuses[1] == "Подключение к резервному серверу...", "Status 2 is 'Подключение к резервному серверу...'");
            CHALLENGE_ASSERT(statuses[2] == "Переключение на локальный Ollama...", "Status 3 is 'Переключение на локальный Ollama...'");
        }
    }

    relayServer.stop();
    ollamaServer.stop();
}

// =========================================================================
// Suite 3: Client Errors (400, 401, 403, 404) Must Abort Immediately
// =========================================================================
void testSuite3_ClientErrorImmediateAbort() {
    CHALLENGE_SECTION("Suite 3: Client Errors (400, 401, 403, 404) Must Abort Immediately");

    ControlledTestServer relayServer;
    ControlledTestServer ollamaServer;
    assert(relayServer.start());
    assert(ollamaServer.start());

    relayServer.setBehavior(BEHAVIOR_OK_OPENAI, 200, "ShouldNotBeCalled");
    ollamaServer.setBehavior(BEHAVIOR_OK_OLLAMA, 200, "ShouldNotBeCalled");

    // Test 3.1: Tier 1 returns 400 Bad Request
    {
        ControlledTestServer errServer;
        assert(errServer.start());
        errServer.setBehavior(BEHAVIOR_HTTP_ERROR, 400, "Bad Request: malformed parameters");

        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:" + std::to_string(errServer.getPort());
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        std::vector<std::string> statuses;
        VisionClient client(cfg, [&](const std::string& msg) { statuses.push_back(msg); });

        bool threw = false;
        std::string exMsg;
        try {
            client.executeCascade("", "Bad request test");
        } catch (const std::runtime_error& e) {
            threw = true;
            exMsg = e.what();
        }

        CHALLENGE_ASSERT(threw, "HTTP 400 on Tier 1 threw std::runtime_error");
        CHALLENGE_ASSERT(exMsg.find("Tier 1 Cloud Client Error") != std::string::npos, "Exception explains Tier 1 Client Error: " + exMsg);
        CHALLENGE_ASSERT(relayServer.getHitCount() == 0, "Tier 2 Relay was NOT contacted on HTTP 400");
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 0, "Tier 3 Ollama was NOT contacted on HTTP 400");
        CHALLENGE_ASSERT(statuses.size() == 1, "Only initial 'Анализирую...' status emitted, no failover status");

        errServer.stop();
    }

    // Test 3.2: Tier 1 returns 401 Unauthorized
    {
        ControlledTestServer errServer;
        assert(errServer.start());
        errServer.setBehavior(BEHAVIOR_HTTP_ERROR, 401, "Invalid API Key");

        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:" + std::to_string(errServer.getPort());
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        bool threw = false;
        std::string exMsg;
        try {
            VisionClient client(cfg);
            client.executeCascade("", "Auth test");
        } catch (const std::runtime_error& e) {
            threw = true;
            exMsg = e.what();
        }

        CHALLENGE_ASSERT(threw, "HTTP 401 on Tier 1 threw std::runtime_error");
        CHALLENGE_ASSERT(exMsg.find("401") != std::string::npos, "Exception references 401: " + exMsg);
        CHALLENGE_ASSERT(relayServer.getHitCount() == 0, "Tier 2 Relay was NOT contacted on HTTP 401");
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 0, "Tier 3 Ollama was NOT contacted on HTTP 401");

        errServer.stop();
    }

    // Test 3.3: Tier 2 returns 401 Unauthorized -> must NOT cascade to Tier 3!
    {
        relayServer.setBehavior(BEHAVIOR_HTTP_ERROR, 401, "Invalid Fallback Key");
        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:59998"; // Tier 1 fails 12029
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        bool threw = false;
        std::string exMsg;
        try {
            VisionClient client(cfg);
            client.executeCascade("", "Tier 2 auth failure");
        } catch (const std::runtime_error& e) {
            threw = true;
            exMsg = e.what();
        }

        CHALLENGE_ASSERT(threw, "HTTP 401 on Tier 2 threw std::runtime_error");
        CHALLENGE_ASSERT(exMsg.find("Tier 2 VPS Relay Client Error") != std::string::npos, "Exception explains Tier 2 Client Error: " + exMsg);
        CHALLENGE_ASSERT(relayServer.getHitCount() == 1, "Tier 2 was contacted");
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 0, "Tier 3 Ollama was NOT contacted after Tier 2 client error (401)");
    }

    relayServer.stop();
    ollamaServer.stop();
}

// =========================================================================
// Suite 4: Direct Fallbacks & Disabled Tier Invariants
// =========================================================================
void testSuite4_DirectFallbacksAndDisabledTiers() {
    CHALLENGE_SECTION("Suite 4: Direct Fallbacks & Disabled Tier Invariants");

    ControlledTestServer relayServer;
    ControlledTestServer ollamaServer;
    assert(relayServer.start());
    assert(ollamaServer.start());

    ollamaServer.setBehavior(BEHAVIOR_OK_OLLAMA, 200, "DirectOllamaSuccess");

    // Case 4.1: Tier 2 is disabled/empty -> Tier 1 falls directly to Tier 3
    {
        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:59998"; // Tier 1 fails 12029
        cfg.fallback_url = ""; // Tier 2 disabled / empty
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        std::vector<std::string> statuses;
        VisionClient client(cfg, [&](const std::string& msg) { statuses.push_back(msg); });

        std::string res;
        try {
            res = client.executeCascade("", "Direct fallback test");
        } catch (...) {}

        CHALLENGE_ASSERT(res == "DirectOllamaSuccess", "Direct fallback from Tier 1 to Tier 3 succeeded");
        CHALLENGE_ASSERT(relayServer.getHitCount() == 0, "Tier 2 was never contacted when fallback_url is empty");
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 1, "Tier 3 Ollama was contacted directly");
        CHALLENGE_ASSERT(statuses.size() == 2, "Status sequence has exactly 2 notifications (skipped Tier 2)");
        if (statuses.size() == 2) {
            CHALLENGE_ASSERT(statuses[0] == "Анализирую...", "Status 0 is 'Анализирую...'");
            CHALLENGE_ASSERT(statuses[1] == "Переключение на локальный Ollama...", "Status 1 is 'Переключение на локальный Ollama...'");
        }
    }

    // Case 4.2: Tier 3 is disabled (`enable_local_fallback = false`)
    {
        relayServer.setBehavior(BEHAVIOR_HTTP_ERROR, 502, "Relay Down");
        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:59998"; // Tier 1 fails 12029
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = false; // Tier 3 disabled!
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        bool threw = false;
        std::string exMsg;
        try {
            VisionClient client(cfg);
            client.executeCascade("", "Disabled local fallback test");
        } catch (const std::runtime_error& e) {
            threw = true;
            exMsg = e.what();
        }

        CHALLENGE_ASSERT(threw, "Execution threw when Tier 3 disabled");
        CHALLENGE_ASSERT(exMsg.find("local fallback is disabled") != std::string::npos, "Exception clearly mentions local fallback is disabled: " + exMsg);
        CHALLENGE_ASSERT(relayServer.getHitCount() == 1, "Tier 2 was attempted");
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 0, "Tier 3 was NOT contacted because enable_local_fallback=false");
    }

    // Case 4.3: Tier 2 empty AND Tier 3 disabled
    {
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:59998"; // Tier 1 fails 12029
        cfg.fallback_url = "";
        cfg.enable_local_fallback = false;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        bool threw = false;
        std::string exMsg;
        try {
            VisionClient client(cfg);
            client.executeCascade("", "Both disabled");
        } catch (const std::runtime_error& e) {
            threw = true;
            exMsg = e.what();
        }

        CHALLENGE_ASSERT(threw, "Execution threw when Tier 2 empty and Tier 3 disabled");
        CHALLENGE_ASSERT(exMsg.find("Tier 1 failed") != std::string::npos, "Exception notes Tier 1 failed: " + exMsg);
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 0, "Tier 3 was not contacted");
    }

    // Case 4.4: All 3 Tiers Fail (Exhaustion)
    {
        relayServer.setBehavior(BEHAVIOR_HTTP_ERROR, 502, "Relay Down");
        ollamaServer.setBehavior(BEHAVIOR_HTTP_ERROR, 500, "Ollama Down");
        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:59998"; // Tier 1 fails 12029
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        bool threw = false;
        std::string exMsg;
        try {
            VisionClient client(cfg);
            client.executeCascade("", "All 3 fail");
        } catch (const std::runtime_error& e) {
            threw = true;
            exMsg = e.what();
        }

        CHALLENGE_ASSERT(threw, "Execution threw when all 3 tiers fail");
        CHALLENGE_ASSERT(exMsg.find("All cascade tiers failed") != std::string::npos, "Exception notes all cascade tiers failed: " + exMsg);
        CHALLENGE_ASSERT(relayServer.getHitCount() == 1, "Tier 2 attempted");
        CHALLENGE_ASSERT(ollamaServer.getHitCount() == 1, "Tier 3 attempted");
    }

    relayServer.stop();
    ollamaServer.stop();
}

// =========================================================================
// Suite 5: Diagnostic Logging (network.log & ghost_startup.log) & Malformed Payload Cascade
// =========================================================================
void testSuite5_LoggingAndMalformedPayloads() {
    CHALLENGE_SECTION("Suite 5: Diagnostic Logging & Malformed Payload Cascade");

    ControlledTestServer relayServer;
    ControlledTestServer ollamaServer;
    assert(relayServer.start());
    assert(ollamaServer.start());

    // Case 5.1: Malformed HTTP 200 on Tier 1 cascades to Tier 2
    {
        ControlledTestServer corruptCloud;
        assert(corruptCloud.start());
        corruptCloud.setBehavior(BEHAVIOR_CORRUPT_JSON);

        relayServer.setBehavior(BEHAVIOR_OK_OPENAI, 200, "RelayRescuedMalformedCloud");
        relayServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:" + std::to_string(corruptCloud.getPort());
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        std::string res;
        try {
            VisionClient client(cfg);
            res = client.executeCascade("", "Malformed JSON test");
        } catch (...) {}

        CHALLENGE_ASSERT(res == "RelayRescuedMalformedCloud", "Malformed 200 OK on Tier 1 successfully cascaded to Tier 2 Relay");
        CHALLENGE_ASSERT(relayServer.getHitCount() == 1, "Tier 2 Relay was contacted and rescued transaction");

        corruptCloud.stop();
    }

    // Case 5.2: Verification of network.log entries
    {
        clearLogFiles();

        relayServer.setBehavior(BEHAVIOR_HTTP_ERROR, 502, "Relay Down");
        ollamaServer.setBehavior(BEHAVIOR_OK_OLLAMA, 200, "OllamaLogAuditSuccess");
        relayServer.resetHitCount();
        ollamaServer.resetHitCount();

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:59998"; // Tier 1 fails 12029
        cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()) + "/v1/chat/completions";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        try {
            VisionClient client(cfg);
            client.executeCascade("", "Log audit prompt");
        } catch (...) {}

        // Check network.log tags
        CHALLENGE_ASSERT(logFileContains("[TIER-1: Primary Cloud]"), "network.log recorded [TIER-1: Primary Cloud]");
        CHALLENGE_ASSERT(logFileContains("[ERROR] [TIER-1: Primary Cloud]"), "network.log recorded [ERROR] [TIER-1: Primary Cloud]");
        CHALLENGE_ASSERT(logFileContains("[TIER-1 -> TIER-2]"), "network.log recorded [TIER-1 -> TIER-2] transition");
        CHALLENGE_ASSERT(logFileContains("[TIER-2: Backup VPS Relay]"), "network.log recorded [TIER-2: Backup VPS Relay]");
        CHALLENGE_ASSERT(logFileContains("[ERROR] [TIER-2: Backup VPS Relay]"), "network.log recorded [ERROR] [TIER-2: Backup VPS Relay]");
        CHALLENGE_ASSERT(logFileContains("[TIER-2 -> TIER-3]"), "network.log recorded [TIER-2 -> TIER-3] transition");
        CHALLENGE_ASSERT(logFileContains("[TIER-3: Local Offline Ollama]"), "network.log recorded [TIER-3: Local Offline Ollama]");
        CHALLENGE_ASSERT(logFileContains("[SUCCESS] [TIER-3: Local Offline Ollama]"), "network.log recorded [SUCCESS] [TIER-3: Local Offline Ollama]");
    }

    // Case 5.3: Russian Unicode Prompt & Empty Image Invariants
    {
        ollamaServer.resetHitCount();
        ollamaServer.setBehavior(BEHAVIOR_OK_OLLAMA, 200, "Ответ на русском языке: 42");

        AppConfig cfg;
        cfg.proxy_url = "http://127.0.0.1:59998";
        cfg.fallback_url = "";
        cfg.enable_local_fallback = true;
        cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaServer.getPort());
        cfg.request_timeout = 2;

        std::string rusPrompt = "Найди правильный ответ на вопрос: Чему равен предел функции при x->0?";
        VisionClient client(cfg);
        std::string res = client.executeCascade("", rusPrompt);

        CHALLENGE_ASSERT(res == "Ответ на русском языке: 42", "Russian prompt processed with Cyrillic response correctly");
        std::string reqBody = ollamaServer.getLastRequestBody();
        CHALLENGE_ASSERT(reqBody.find("предел функции") != std::string::npos, "Prompt arrived intact in request body");
        CHALLENGE_ASSERT(reqBody.find("\"images\":[]") != std::string::npos, "Empty images array present in JSON payload");
    }

    relayServer.stop();
    ollamaServer.stop();
}

int main() {
    std::cout << "======================================================\n"
              << " GhostAI Milestone 3 Empirical Challenger 1 Harness\n"
              << " Scope: Error Interception, 3-Tier Cascades, UI & Logs\n"
              << "======================================================" << std::endl;

    testSuite1_Tier1Transitions();
    testSuite2_Tier2Transitions();
    testSuite3_ClientErrorImmediateAbort();
    testSuite4_DirectFallbacksAndDisabledTiers();
    testSuite5_LoggingAndMalformedPayloads();

    std::cout << "\n======================================================\n"
              << " Challenger 1 Results Summary:\n"
              << " PASSED: " << g_passCount << "\n"
              << " FAILED: " << g_failCount << "\n"
              << "======================================================\n";

    if (g_failCount == 0) {
        std::cout << "[+] ALL 100% EMPIRICAL CHALLENGES PASSED SUCCESSFULLY!\n";
        return 0;
    } else {
        std::cerr << "[-] CHALLENGES FAILED: " << g_failCount << std::endl;
        return 1;
    }
}
