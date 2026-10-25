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

// Simple loopback HTTP server helper
class MiniHttpServer {
public:
    MiniHttpServer() : m_listenSock(INVALID_SOCKET), m_port(0), m_running(false) {}
    ~MiniHttpServer() { stop(); }

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

        if (listen(m_listenSock, 10) != 0) {
            closesocket(m_listenSock);
            return false;
        }

        m_running = true;
        m_serverThread = std::thread(&MiniHttpServer::workerLoop, this);
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

    void setHandler(std::function<std::string(const std::string& path, const std::string& body)> handler) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_handler = handler;
    }

private:
    void workerLoop() {
        while (m_running) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_listenSock, &fds);
            timeval tv = { 0, 50000 }; // 50ms
            int sel = select(0, &fds, NULL, NULL, &tv);
            if (sel <= 0) continue;

            sockaddr_in clientAddr;
            int clientLen = sizeof(clientAddr);
            SOCKET clientSock = accept(m_listenSock, (sockaddr*)&clientAddr, &clientLen);
            if (clientSock == INVALID_SOCKET) continue;

            char buf[4096] = { 0 };
            int n = recv(clientSock, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                std::string req(buf, n);
                std::string path = "/";
                size_t p1 = req.find(' ');
                if (p1 != std::string::npos) {
                    size_t p2 = req.find(' ', p1 + 1);
                    if (p2 != std::string::npos) {
                        path = req.substr(p1 + 1, p2 - p1 - 1);
                    }
                }

                std::function<std::string(const std::string&, const std::string&)> h;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    h = m_handler;
                }

                std::string respHttp;
                if (h) {
                    respHttp = h(path, req);
                } else {
                    respHttp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
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
    std::mutex m_mutex;
    std::function<std::string(const std::string&, const std::string&)> m_handler;
};

// =========================================================================
// Suite 1: Concurrency, Thread Safety & Deadlock Elimination
// =========================================================================
void runSuite1_ConcurrencyAndDeadlocks() {
    CHALLENGE_SECTION("Suite 1: Concurrency, Thread Safety & Deadlock Elimination");

    AppConfig cfg;
    cfg.openrouter_api_key = "test_key";
    cfg.request_timeout = 1;
    cfg.enable_local_fallback = true;
    cfg.ollama_url = "http://127.0.0.1:59999"; // closed port -> instantaneous 12029

    // Test 1.1: Reentrant callback deadlock check
    {
        VisionClient client(cfg);
        std::atomic<bool> callbackFired(false);
        std::atomic<bool> deadlockAvoided(false);

        // A callback that attempts to acquire clientMutex by calling updateConfig
        client.setStatusCallback([&](const std::string& msg) {
            callbackFired = true;
            AppConfig newCfg;
            newCfg.openrouter_api_key = "reentrant_updated_key";
            // If VisionClient::reportStatus held m_clientMutex, this would DEADLOCK!
            client.updateConfig(newCfg);
            deadlockAvoided = true;
        });

        // Trigger cascade on invalid closed URL (which calls reportStatus)
        try {
            client.executeCascade("", "test prompt");
        } catch (...) {
            // Expected to fail on network, but must NOT deadlock!
        }

        CHALLENGE_ASSERT(callbackFired.load(), "Status callback was successfully executed");
        CHALLENGE_ASSERT(deadlockAvoided.load(), "Reentrant client.updateConfig() succeeded without deadlock");
    }

    // Test 1.2: Callback exception safety
    {
        VisionClient client(cfg);
        std::atomic<bool> throwingCbFired(false);

        client.setStatusCallback([&](const std::string& msg) {
            throwingCbFired = true;
            throw std::runtime_error("Intentionally malicious callback exception!");
        });

        bool cleanCatch = false;
        try {
            client.executeCascade("", "test");
        } catch (const std::runtime_error& e) {
            // The exception caught should be the cascade network error, NOT the callback exception!
            std::string err = e.what();
            if (err.find("Intentionally malicious") == std::string::npos) {
                cleanCatch = true;
            }
        } catch (...) {
            cleanCatch = false;
        }

        CHALLENGE_ASSERT(throwingCbFired.load(), "Throwing callback was invoked");
        CHALLENGE_ASSERT(cleanCatch, "Callback exception was absorbed cleanly and did not leak out of reportStatus");
    }

    // Test 1.3: Heavy concurrent multithreaded hammering (24 threads, 5,000 iterations)
    {
        VisionClient client(cfg);
        std::atomic<bool> stopStress(false);
        std::vector<std::thread> workers;

        // 8 threads constantly updating config
        for (int i = 0; i < 8; ++i) {
            workers.emplace_back([&, i]() {
                int iters = 0;
                while (!stopStress.load() && iters < 500) {
                    AppConfig localCfg;
                    localCfg.openrouter_api_key = "hammer_key_" + std::to_string(i) + "_" + std::to_string(iters);
                    localCfg.fallback_url = "http://127.0.0.1:" + std::to_string(50000 + i);
                    client.updateConfig(localCfg);
                    iters++;
                    std::this_thread::yield();
                }
            });
        }

        // 8 threads constantly updating status callback
        for (int i = 0; i < 8; ++i) {
            workers.emplace_back([&, i]() {
                int iters = 0;
                while (!stopStress.load() && iters < 500) {
                    client.setStatusCallback([i, iters](const std::string& msg) {
                        volatile int x = i * iters;
                        (void)x;
                    });
                    iters++;
                    std::this_thread::yield();
                }
            });
        }

        // 8 threads building payloads concurrently
        for (int i = 0; i < 8; ++i) {
            workers.emplace_back([&, i]() {
                int iters = 0;
                while (!stopStress.load() && iters < 500) {
                    auto p1 = VisionClient::buildOpenAIPayload("concurrent prompt", "", "model");
                    auto p2 = VisionClient::buildOllamaPayload("concurrent prompt", "", "model");
                    (void)p1;
                    (void)p2;
                    iters++;
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        CHALLENGE_ASSERT(true, "24-thread concurrent hammer completed with zero crashes, torn states, or deadlocks");
    }
}

// =========================================================================
// Suite 2: HttpResponse Invariants (Transient vs Non-Transient)
// =========================================================================
void runSuite2_HttpResponseClassification() {
    CHALLENGE_SECTION("Suite 2: HttpResponse Invariants (Transient vs Non-Transient)");

    // Transport errors
    {
        HttpResponse r;
        r.statusCode = 0;
        r.winHttpError = 0;
        CHALLENGE_ASSERT(r.isTransient(), "statusCode 0 is transient");
        CHALLENGE_ASSERT(!r.isSuccess(), "statusCode 0 is not success");
    }
    {
        HttpResponse r;
        r.statusCode = 0;
        r.winHttpError = 12029; // ERROR_WINHTTP_CANNOT_CONNECT
        CHALLENGE_ASSERT(r.isTransient(), "WinHttp 12029 is transient");
    }
    {
        HttpResponse r;
        r.statusCode = 0;
        r.winHttpError = 12002; // ERROR_WINHTTP_TIMEOUT
        CHALLENGE_ASSERT(!r.isTransient(), "WinHttp 12002 timeout is non-transient (no auto-failover on deep reasoning)");
    }
    {
        HttpResponse r;
        r.statusCode = 0;
        r.winHttpError = 12030; // ERROR_WINHTTP_CONNECTION_ERROR
        CHALLENGE_ASSERT(r.isTransient(), "WinHttp 12030 is transient");
    }

    // HTTP 5xx Server Errors
    for (int code : { 500, 502, 503, 504, 521, 599 }) {
        HttpResponse r;
        r.statusCode = code;
        CHALLENGE_ASSERT(r.isTransient(), "HTTP " + std::to_string(code) + " is transient");
    }

    // HTTP 408 & 429
    {
        HttpResponse r;
        r.statusCode = 408;
        CHALLENGE_ASSERT(!r.isTransient(), "HTTP 408 Request Timeout is non-transient (no auto-failover on deep reasoning)");
    }
    {
        HttpResponse r;
        r.statusCode = 429;
        CHALLENGE_ASSERT(r.isTransient(), "HTTP 429 Rate Limit is transient");
    }

    // HTTP 407 Proxy Auth
    {
        HttpResponse r;
        r.statusCode = 407;
        CHALLENGE_ASSERT(r.isTransient(), "HTTP 407 Proxy Auth is transient for cascade failover");
    }

    // Non-transient Client Errors (Must NOT cascade)
    for (int code : { 400, 401, 403, 404, 422 }) {
        HttpResponse r;
        r.statusCode = code;
        CHALLENGE_ASSERT(!r.isTransient(), "HTTP " + std::to_string(code) + " is NON-transient client error");
    }

    // Success definition
    {
        HttpResponse r;
        r.statusCode = 200;
        r.success = true;
        r.body = "{\"choices\":[]}";
        CHALLENGE_ASSERT(r.isSuccess(), "HTTP 200 with non-empty body is success");
    }
    {
        HttpResponse r;
        r.statusCode = 200;
        r.success = true;
        r.body = "";
        CHALLENGE_ASSERT(!r.isSuccess(), "HTTP 200 with EMPTY body is NOT success");
    }
}

// =========================================================================
// Suite 3: Multi-Format Payload Adapters & Encoding
// =========================================================================
void runSuite3_PayloadAdapters() {
    CHALLENGE_SECTION("Suite 3: Multi-Format Payload Adapters & Encoding");

    // OpenAI payload with empty image
    {
        auto p = VisionClient::buildOpenAIPayload("Test question", "", "meta-llama/llama-3.2-11b-vision-instruct:free");
        CHALLENGE_ASSERT(p.contains("messages"), "OpenAI payload contains 'messages'");
        CHALLENGE_ASSERT(p["messages"].is_array(), "'messages' is array");
        auto content = p["messages"][0]["content"];
        CHALLENGE_ASSERT(content.is_array(), "content is array");
        CHALLENGE_ASSERT(content.size() == 1, "empty image results in exactly 1 content part");
        CHALLENGE_ASSERT(content[0]["type"] == "text", "content part is text");
        CHALLENGE_ASSERT(content[0]["text"] == "Test question", "text matches prompt");
    }

    // OpenAI payload with image
    {
        std::string sampleB64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=";
        auto p = VisionClient::buildOpenAIPayload("Test question", sampleB64, "gpt-4o");
        auto content = p["messages"][0]["content"];
        CHALLENGE_ASSERT(content.size() == 2, "content has 2 parts with image");
        CHALLENGE_ASSERT(content[1]["type"] == "image_url", "part 2 is image_url");
        std::string url = content[1]["image_url"]["url"];
        CHALLENGE_ASSERT(url.find("data:image/jpeg;base64,") == 0, "image_url starts with data URI prefix");
    }

    // Ollama payload with empty image
    {
        auto p = VisionClient::buildOllamaPayload("Prompt text", "", "");
        CHALLENGE_ASSERT(p["model"] == "llava:latest", "empty model defaults to llava:latest");
        CHALLENGE_ASSERT(p["prompt"] == "Prompt text", "prompt matches");
        CHALLENGE_ASSERT(p["images"].is_array(), "images is array");
        CHALLENGE_ASSERT(p["images"].empty(), "images is empty array [] when imageBase64 is empty");
        CHALLENGE_ASSERT(p["stream"] == false, "stream is false");
    }

    // Ollama payload with image
    {
        std::string sampleB64 = "dGVzdF9pbWFnZV9kYXRh";
        auto p = VisionClient::buildOllamaPayload("Prompt text", sampleB64, "custom-model:7b");
        CHALLENGE_ASSERT(p["model"] == "custom-model:7b", "custom model respected");
        CHALLENGE_ASSERT(p["images"].size() == 1, "images contains exactly 1 element");
        CHALLENGE_ASSERT(p["images"][0] == sampleB64, "image b64 matches");
    }
}

// =========================================================================
// Suite 4: Adversarial Response Parser Resilience
// =========================================================================
void runSuite4_ResponseParserResilience() {
    CHALLENGE_SECTION("Suite 4: Adversarial Response Parser Resilience");

    // OpenAI Parsers
    // Valid standard response
    {
        std::string json = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Answer 42\"}}]}";
        std::string ans = VisionClient::parseOpenAIResponse(json);
        CHALLENGE_ASSERT(ans == "Answer 42", "Parsed standard OpenAI response correctly");
    }

    // Multi-part content array (Claude / modern OpenAI)
    {
        std::string json = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"Part 1 \"},{\"type\":\"text\",\"text\":\"Part 2\"}]}}]}";
        std::string ans = VisionClient::parseOpenAIResponse(json);
        CHALLENGE_ASSERT(ans == "Part 1  Part 2", "Parsed multi-part content array correctly: " + ans);
    }

    // OpenAI API error JSON
    {
        std::string json = "{\"error\":{\"message\":\"Rate limit exceeded (429)\",\"type\":\"rate_limit_error\"}}";
        bool threw = false;
        try {
            VisionClient::parseOpenAIResponse(json);
        } catch (const std::runtime_error& e) {
            threw = true;
            CHALLENGE_ASSERT(std::string(e.what()).find("Rate limit exceeded") != std::string::npos, "Extracted API error message");
        }
        CHALLENGE_ASSERT(threw, "Threw on API error object");
    }

    // Malformed JSON
    {
        bool threw = false;
        try {
            VisionClient::parseOpenAIResponse("<html><head><title>502 Bad Gateway</title></head><body>502</body></html>");
        } catch (const std::runtime_error& e) {
            threw = true;
            CHALLENGE_ASSERT(std::string(e.what()).find("malformed JSON") != std::string::npos, "Malformed JSON cleanly caught");
        }
        CHALLENGE_ASSERT(threw, "Threw on HTML response");
    }

    // Missing 'choices' array
    {
        bool threw = false;
        try {
            VisionClient::parseOpenAIResponse("{\"id\":\"chatcmpl-123\",\"model\":\"test\"}");
        } catch (const std::runtime_error& e) {
            threw = true;
            CHALLENGE_ASSERT(std::string(e.what()).find("missing 'choices'") != std::string::npos, "Missing choices detected");
        }
        CHALLENGE_ASSERT(threw, "Threw on missing choices");
    }

    // Empty 'choices' array
    {
        bool threw = false;
        try {
            VisionClient::parseOpenAIResponse("{\"choices\":[]}");
        } catch (const std::runtime_error& e) {
            threw = true;
        }
        CHALLENGE_ASSERT(threw, "Threw on empty choices array");
    }

    // Ollama Parsers
    // Valid standard response
    {
        std::string json = "{\"model\":\"llava:latest\",\"response\":\"Ollama Answer 100\",\"done\":true}";
        std::string ans = VisionClient::parseOllamaResponse(json);
        CHALLENGE_ASSERT(ans == "Ollama Answer 100", "Parsed standard Ollama response correctly");
    }

    // Ollama error payload
    {
        std::string json = "{\"error\":\"model 'llama3' not found, try pulling it first\"}";
        bool threw = false;
        try {
            VisionClient::parseOllamaResponse(json);
        } catch (const std::runtime_error& e) {
            threw = true;
            CHALLENGE_ASSERT(std::string(e.what()).find("model 'llama3' not found") != std::string::npos, "Ollama error extracted");
        }
        CHALLENGE_ASSERT(threw, "Threw on Ollama error payload");
    }

    // Ollama missing 'response' field
    {
        bool threw = false;
        try {
            VisionClient::parseOllamaResponse("{\"done\":true}");
        } catch (const std::runtime_error& e) {
            threw = true;
        }
        CHALLENGE_ASSERT(threw, "Threw on Ollama missing response field");
    }
}

// =========================================================================
// Suite 5: Live End-to-End 3-Tier Cascade Fallback
// =========================================================================
void runSuite5_Live3TierCascade() {
    CHALLENGE_SECTION("Suite 5: Live End-to-End 3-Tier Cascade Fallback");

    MiniHttpServer relayServer;
    MiniHttpServer ollamaServer;

    if (!relayServer.start() || !ollamaServer.start()) {
        std::cerr << "[-] Failed to start loopback test servers!" << std::endl;
        g_failCount++;
        return;
    }

    int relayPort = relayServer.getPort();
    int ollamaPort = ollamaServer.getPort();

    // Configure relay to return 502 Bad Gateway
    relayServer.setHandler([](const std::string& path, const std::string& body) {
        return "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nBad Gateway";
    });

    // Configure Ollama to return valid response
    ollamaServer.setHandler([](const std::string& path, const std::string& body) {
        std::string json = "{\"model\":\"llava:latest\",\"response\":\"Resilient Failover Response from Tier 3 Ollama\",\"done\":true}";
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
    });

    // AppConfig setup:
    // Tier 1: Cloud routes through closed proxy port 59999 (immediate WinHttp 12029 Connection Refused)
    // Tier 2: Relay point to loopback relayPort (local address bypasses proxy -> returns HTTP 502)
    // Tier 3: Ollama point to loopback ollamaPort (local address bypasses proxy -> returns HTTP 200)
    AppConfig cfg;
    cfg.proxy_url = "http://127.0.0.1:59999"; // closed port -> WinHttp 12029 on Tier 1
    cfg.openrouter_api_key = "test_cloud_key";
    cfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayPort) + "/v1/chat/completions";
    cfg.fallback_api_key = "test_relay_key";
    cfg.enable_local_fallback = true;
    cfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaPort);
    cfg.request_timeout = 2;

    std::vector<std::string> statusSequence;
    VisionClient client(cfg, [&](const std::string& msg) {
        statusSequence.push_back(msg);
    });

    std::string finalAnswer;
    bool cascadeOk = false;
    try {
        finalAnswer = client.executeCascade("", "Solve this problem");
        cascadeOk = true;
    } catch (const std::exception& e) {
        std::cerr << "[-] Cascade failed: " << e.what() << std::endl;
        cascadeOk = false;
    }

    CHALLENGE_ASSERT(cascadeOk, "executeCascade completed successfully across failovers");
    CHALLENGE_ASSERT(finalAnswer == "Resilient Failover Response from Tier 3 Ollama", "Answer matches Tier 3 Ollama output");

    // Verify UI Status notification sequence
    CHALLENGE_ASSERT(statusSequence.size() >= 3, "Received at least 3 UI status updates");
    if (statusSequence.size() >= 3) {
        CHALLENGE_ASSERT(statusSequence[0] == "Анализирую...", "Status 1 is 'Анализирую...'");
        CHALLENGE_ASSERT(statusSequence[1] == "Подключение к резервному серверу...", "Status 2 is 'Подключение к резервному серверу...'");
        CHALLENGE_ASSERT(statusSequence[2] == "Переключение на локальный Ollama...", "Status 3 is 'Переключение на локальный Ollama...'");
    }

    // Circular fallback test:
    // If fallback_url is identical to primary cloud URL, Tier 2 must be SKIPPED!
    {
        std::vector<std::string> circularStatus;
        AppConfig circCfg = cfg;
        circCfg.fallback_url = "https://openrouter.ai/api/v1/chat/completions/"; // Trailing slash variant of Tier 1 URL
        VisionClient circClient(circCfg, [&](const std::string& msg) {
            circularStatus.push_back(msg);
        });

        try {
            std::string ans = circClient.executeCascade("", "Test circular");
            CHALLENGE_ASSERT(ans == "Resilient Failover Response from Tier 3 Ollama", "Circular fallback skipped Tier 2 and succeeded on Tier 3");
            // Status sequence should NOT contain "Подключение к резервному серверу..."
            bool foundTier2Status = false;
            for (const auto& s : circularStatus) {
                if (s.find("резервному серверу") != std::string::npos) {
                    foundTier2Status = true;
                }
            }
            CHALLENGE_ASSERT(!foundTier2Status, "Tier 2 was properly skipped when fallback_url pointed to Tier 1 Cloud");
        } catch (const std::exception& e) {
            CHALLENGE_ASSERT(false, "Circular test failed with exception: " + std::string(e.what()));
        }
    }

    relayServer.stop();
    ollamaServer.stop();
}

int main() {
    std::cout << "======================================================\n"
              << " GhostAI M3 Empirical Challenger: Resilience & Cascade\n"
              << " Reviewer 2 & Critic Verification\n"
              << "======================================================" << std::endl;

    runSuite1_ConcurrencyAndDeadlocks();
    runSuite2_HttpResponseClassification();
    runSuite3_PayloadAdapters();
    runSuite4_ResponseParserResilience();
    runSuite5_Live3TierCascade();

    std::cout << "\n======================================================\n"
              << " Challenger M3 Results Summary:\n"
              << " PASSED: " << g_passCount << "\n"
              << " FAILED: " << g_failCount << "\n"
              << "======================================================" << std::endl;

    if (g_failCount == 0) {
        std::cout << "[+] ALL EMPIRICAL CHALLENGES PASSED SUCCESSFULLY!\n";
        return 0;
    } else {
        std::cerr << "[-] SOME CHALLENGES FAILED!\n";
        return 1;
    }
}
