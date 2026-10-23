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

static std::atomic<int> g_passCount{ 0 };
static std::atomic<int> g_failCount{ 0 };
static std::mutex g_ioMutex;

#define CHALLENGE_ASSERT(cond, msg) do { \
    if (cond) { \
        g_passCount++; \
    } else { \
        g_failCount++; \
        std::lock_guard<std::mutex> lk(g_ioMutex); \
        std::cerr << "[-] FAIL [Line " << __LINE__ << "]: " << msg << std::endl; \
    } \
} while(0)

#define CHALLENGE_SECTION(name) do { \
    std::lock_guard<std::mutex> lk(g_ioMutex); \
    std::cout << "\n======================================================\n" \
              << "  RUNNING: " << name << "\n" \
              << "======================================================" << std::endl; \
} while(0)

// Robust loopback HTTP server with per-connection threads
class HighPerformanceHttpServer {
public:
    HighPerformanceHttpServer() : m_listenSock(INVALID_SOCKET), m_port(0), m_running(false), m_requestCount(0) {}
    ~HighPerformanceHttpServer() { stop(); }

    bool start() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

        m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSock == INVALID_SOCKET) return false;

        // Set SO_REUSEADDR
        int opt = 1;
        setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

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

        if (listen(m_listenSock, 128) != 0) {
            closesocket(m_listenSock);
            return false;
        }

        m_running = true;
        m_serverThread = std::thread(&HighPerformanceHttpServer::acceptLoop, this);
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
        std::vector<std::thread> pool;
        {
            std::lock_guard<std::mutex> lock(m_connMutex);
            pool = std::move(m_connThreads);
        }
        for (auto& t : pool) {
            if (t.joinable()) t.join();
        }
        WSACleanup();
    }

    int getPort() const { return m_port; }
    int getRequestCount() const { return m_requestCount.load(); }
    void resetRequestCount() { m_requestCount = 0; }

    void setHandler(std::function<std::string(const std::string& path, const std::string& req)> handler) {
        std::lock_guard<std::mutex> lock(m_handlerMutex);
        m_handler = handler;
    }

private:
    void acceptLoop() {
        while (m_running) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_listenSock, &fds);
            timeval tv = { 0, 20000 }; // 20ms
            int sel = select(0, &fds, NULL, NULL, &tv);
            if (sel <= 0) continue;

            sockaddr_in clientAddr;
            int clientLen = sizeof(clientAddr);
            SOCKET clientSock = accept(m_listenSock, (sockaddr*)&clientAddr, &clientLen);
            if (clientSock == INVALID_SOCKET) continue;

            m_requestCount++;

            // Handle connection in a dedicated thread
            std::thread t([this, clientSock]() {
                handleClient(clientSock);
            });

            {
                std::lock_guard<std::mutex> lock(m_connMutex);
                m_connThreads.push_back(std::move(t));
                // Clean up joined threads
                m_connThreads.erase(
                    std::remove_if(m_connThreads.begin(), m_connThreads.end(), [](std::thread& th) {
                        if (!th.joinable()) return true;
                        // Cannot easily check joinability without blocking, keep vector bounded
                        return false;
                    }),
                    m_connThreads.end()
                );
            }
        }
    }

    void handleClient(SOCKET s) {
        // Read full request headers
        std::string fullReq;
        char buf[4096];
        int totalRead = 0;

        while (true) {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0) break;
            fullReq.append(buf, n);
            totalRead += n;

            // Check if headers end
            size_t headerEnd = fullReq.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                // Check content length if any
                size_t clPos = fullReq.find("Content-Length:");
                if (clPos != std::string::npos) {
                    size_t clEnd = fullReq.find("\r\n", clPos);
                    std::string clStr = fullReq.substr(clPos + 15, clEnd - clPos - 15);
                    try {
                        int cl = std::stoi(clStr);
                        size_t bodyRead = fullReq.size() - (headerEnd + 4);
                        if (bodyRead >= (size_t)cl) {
                            break;
                        }
                    } catch (...) {
                        break;
                    }
                } else {
                    break;
                }
            }
        }

        if (!fullReq.empty()) {
            std::string path = "/";
            size_t p1 = fullReq.find(' ');
            if (p1 != std::string::npos) {
                size_t p2 = fullReq.find(' ', p1 + 1);
                if (p2 != std::string::npos) {
                    path = fullReq.substr(p1 + 1, p2 - p1 - 1);
                }
            }

            std::function<std::string(const std::string&, const std::string&)> h;
            {
                std::lock_guard<std::mutex> lock(m_handlerMutex);
                h = m_handler;
            }

            std::string resp;
            if (h) {
                resp = h(path, fullReq);
            } else {
                resp = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\nOK";
            }

            send(s, resp.data(), (int)resp.size(), 0);
        }

        shutdown(s, SD_BOTH);
        closesocket(s);
    }

    SOCKET m_listenSock;
    int m_port;
    std::atomic<bool> m_running;
    std::atomic<int> m_requestCount;
    std::thread m_serverThread;
    std::mutex m_connMutex;
    std::vector<std::thread> m_connThreads;
    std::mutex m_handlerMutex;
    std::function<std::string(const std::string&, const std::string&)> m_handler;
};

// =========================================================================
// Suite 1: Multithreaded Concurrency, Race Condition Avoidance & Deadlocks
// =========================================================================
void runSuite1_MultithreadedConcurrencyStress() {
    CHALLENGE_SECTION("Suite 1: Multithreaded Concurrency & Deadlock Stress");

    HighPerformanceHttpServer mockServer;
    if (!mockServer.start()) {
        std::cerr << "[-] Failed to start mockServer" << std::endl;
        g_failCount++;
        return;
    }

    mockServer.setHandler([](const std::string& path, const std::string& req) {
        if (path.find("/api/generate") != std::string::npos) {
            std::string json = "{\"model\":\"llava:latest\",\"response\":\"Mock Ollama Concurrency Answer\",\"done\":true}";
            return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " +
                   std::to_string(json.size()) + "\r\n\r\n" + json;
        } else {
            std::string json = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Mock Cloud Concurrency Answer\"}}]}";
            return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " +
                   std::to_string(json.size()) + "\r\n\r\n" + json;
        }
    });

    AppConfig cfg;
    cfg.backend = "ollama";
    cfg.ollama_url = "http://127.0.0.1:" + std::to_string(mockServer.getPort());
    cfg.ollama_model = "llava:latest";
    cfg.max_context_pairs = 10;
    cfg.persist_context = false;
    cfg.request_timeout = 5;

    // Test 1.1: Concurrent calls to analyze() across 16 threads
    {
        ConversationMemory memory(20, false);
        VisionClient client(cfg, memory);

        const int numThreads = 16;
        const int callsPerThread = 5;
        std::vector<std::thread> threads;
        std::atomic<int> successCount{ 0 };

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&, i]() {
                for (int j = 0; j < callsPerThread; ++j) {
                    try {
                        std::string ans = client.analyze("dGVzdF9pbWFnZQ==", "Thread " + std::to_string(i) + " question " + std::to_string(j));
                        if (ans == "Mock Ollama Concurrency Answer") {
                            successCount++;
                        }
                    } catch (const std::exception& e) {
                        std::lock_guard<std::mutex> lk(g_ioMutex);
                        std::cerr << "[-] Thread " << i << " exception: " << e.what() << std::endl;
                    }
                }
            });
        }

        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

        CHALLENGE_ASSERT(successCount.load() == numThreads * callsPerThread,
                         "All 16 threads completed concurrent analyze() successfully (" +
                         std::to_string(successCount.load()) + "/" + std::to_string(numThreads * callsPerThread) + ")");
        CHALLENGE_ASSERT(elapsed < 15000, "16-thread concurrent execution completed within 15 seconds (took " + std::to_string(elapsed) + "ms)");
        CHALLENGE_ASSERT(memory.size() <= 20, "ConversationMemory size maintained within max limits (" + std::to_string(memory.size()) + ")");
    }

    // Test 1.2: Calling updateConfig() concurrently while analyze() is executing
    {
        ConversationMemory memory(10, false);
        VisionClient client(cfg, memory);

        std::atomic<bool> keepRunning{ true };
        std::atomic<int> analyzeCalls{ 0 };
        std::atomic<int> updateCalls{ 0 };
        std::vector<std::thread> workers;

        // 6 threads executing analyze()
        for (int i = 0; i < 6; ++i) {
            workers.emplace_back([&, i]() {
                while (keepRunning.load()) {
                    try {
                        client.analyze("", "Continuous query " + std::to_string(i));
                        analyzeCalls++;
                    } catch (...) {
                        // Transient errors expected if config changes mid-flight
                    }
                    std::this_thread::yield();
                }
            });
        }

        // 6 threads aggressively modifying config
        for (int i = 0; i < 6; ++i) {
            workers.emplace_back([&, i]() {
                int count = 0;
                while (keepRunning.load()) {
                    AppConfig newCfg = cfg;
                    newCfg.ollama_model = "model_" + std::to_string(i) + "_" + std::to_string(count++);
                    newCfg.request_timeout = (count % 5) + 1;
                    newCfg.system_prompt = "System prompt from thread " + std::to_string(i);
                    client.updateConfig(newCfg);
                    updateCalls++;
                    std::this_thread::yield();
                }
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        keepRunning = false;

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        CHALLENGE_ASSERT(analyzeCalls.load() > 50, "analyze() executed concurrently under continuous updateConfig() hammer (total: " + std::to_string(analyzeCalls.load()) + ")");
        CHALLENGE_ASSERT(updateCalls.load() > 100, "updateConfig() executed concurrently without data race (total: " + std::to_string(updateCalls.load()) + ")");
    }

    // Test 1.3: Calling setStatusCallback() concurrently while status reports are emitted (in executeCascade)
    {
        AppConfig cascadeCfg;
        cascadeCfg.backend = "openrouter";
        cascadeCfg.proxy_url = "http://127.0.0.1:59999"; // Closed port -> rapid 12029 failover
        cascadeCfg.enable_local_fallback = true;
        cascadeCfg.ollama_url = "http://127.0.0.1:" + std::to_string(mockServer.getPort());
        cascadeCfg.request_timeout = 2;

        VisionClient client(cascadeCfg);

        std::atomic<bool> keepRunning{ true };
        std::atomic<int> callbackInvocations{ 0 };
        std::atomic<int> callbackUpdates{ 0 };
        std::vector<std::thread> workers;

        // 4 threads calling executeCascade() which triggers reportStatus
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&, i]() {
                while (keepRunning.load()) {
                    try {
                        client.executeCascade("", "Status check query " + std::to_string(i));
                    } catch (...) {}
                    std::this_thread::yield();
                }
            });
        }

        // 4 threads updating setStatusCallback with varying lambdas
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&, i]() {
                int count = 0;
                while (keepRunning.load()) {
                    if (count % 3 == 0) {
                        // Callback that throws exception
                        client.setStatusCallback([&](const std::string& msg) {
                            callbackInvocations++;
                            throw std::runtime_error("Chaos test callback exception");
                        });
                    } else if (count % 3 == 1) {
                        // Callback that does heap allocations
                        client.setStatusCallback([&, i](const std::string& msg) {
                            callbackInvocations++;
                            std::string copy = msg + " - handled by thread " + std::to_string(i);
                            (void)copy;
                        });
                    } else {
                        // Null callback
                        client.setStatusCallback(nullptr);
                    }
                    callbackUpdates++;
                    count++;
                    std::this_thread::yield();
                }
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        keepRunning = false;

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        CHALLENGE_ASSERT(callbackInvocations.load() > 0, "Status callbacks were invoked during concurrent swapping (total: " + std::to_string(callbackInvocations.load()) + ")");
        CHALLENGE_ASSERT(callbackUpdates.load() > 100, "setStatusCallback() was safely updated concurrently (total: " + std::to_string(callbackUpdates.load()) + ")");
    }

    // Test 1.4: Reentrant updateConfig and setStatusCallback inside callback
    {
        AppConfig cascadeCfg;
        cascadeCfg.backend = "openrouter";
        cascadeCfg.proxy_url = "http://127.0.0.1:59999"; // Closed port -> rapid 12029 failover
        cascadeCfg.enable_local_fallback = true;
        cascadeCfg.ollama_url = "http://127.0.0.1:" + std::to_string(mockServer.getPort());
        cascadeCfg.request_timeout = 2;

        VisionClient client(cascadeCfg);
        std::atomic<bool> reentrantUpdateDone{ false };
        std::atomic<bool> reentrantSetCbDone{ false };

        client.setStatusCallback([&](const std::string& msg) {
            // Reentrantly call updateConfig
            AppConfig localCfg = cascadeCfg;
            localCfg.ollama_model = "reentrant_model";
            client.updateConfig(localCfg);
            reentrantUpdateDone = true;

            // Reentrantly call setStatusCallback
            client.setStatusCallback(nullptr);
            reentrantSetCbDone = true;
        });

        try {
            client.executeCascade("", "test reentrant");
        } catch (...) {}

        CHALLENGE_ASSERT(reentrantUpdateDone.load(), "Reentrant updateConfig() succeeded without ABBA deadlock");
        CHALLENGE_ASSERT(reentrantSetCbDone.load(), "Reentrant setStatusCallback() succeeded without ABBA deadlock");
    }

    mockServer.stop();
}

// =========================================================================
// Suite 2: Circular & Edge-case Network Configurations
// =========================================================================
void runSuite2_CircularAndEdgeCaseConfigs() {
    CHALLENGE_SECTION("Suite 2: Circular & Edge-case Network Configurations");

    HighPerformanceHttpServer mockRelay;
    HighPerformanceHttpServer mockOllama;

    if (!mockRelay.start() || !mockOllama.start()) {
        std::cerr << "[-] Failed to start test servers for Suite 2" << std::endl;
        g_failCount++;
        return;
    }

    int relayPort = mockRelay.getPort();
    int ollamaPort = mockOllama.getPort();

    mockRelay.setHandler([](const std::string& path, const std::string& req) {
        std::string json = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Mock Relay Answer\"}}]}";
        return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " +
               std::to_string(json.size()) + "\r\n\r\n" + json;
    });

    mockOllama.setHandler([](const std::string& path, const std::string& req) {
        std::string json = "{\"model\":\"llava:latest\",\"response\":\"Mock Ollama Fallback Answer\",\"done\":true}";
        return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " +
               std::to_string(json.size()) + "\r\n\r\n" + json;
    });

    // Base config where Tier 1 fails via closed proxy port (12029)
    AppConfig baseCfg;
    baseCfg.backend = "openrouter";
    baseCfg.proxy_url = "http://127.0.0.1:59998"; // Closed port -> WinHttp 12029
    baseCfg.openrouter_api_key = "test_key";
    baseCfg.enable_local_fallback = true;
    baseCfg.ollama_url = "http://127.0.0.1:" + std::to_string(ollamaPort);
    baseCfg.request_timeout = 2;

    // Test 2.1: Circular Fallback URL Permutations
    // All variants pointing back to Tier 1 Cloud URL must bypass Tier 2
    std::vector<std::string> circularUrls = {
        "https://openrouter.ai/api/v1/chat/completions",
        "https://openrouter.ai/api/v1/chat/completions/",
        "https://openrouter.ai/api/v1/chat/completions///",
        "https://openrouter.ai/api/v1/chat/completions/   ",
        "HTTPS://OPENROUTER.AI/api/v1/chat/completions",
        "Https://OpenRouter.AI/api/v1/chat/completions/",
        "HTTPS://OPENROUTER.AI/API/V1/CHAT/COMPLETIONS"
    };

    for (const auto& circUrl : circularUrls) {
        AppConfig testCfg = baseCfg;
        testCfg.fallback_url = circUrl;

        std::vector<std::string> statuses;
        VisionClient client(testCfg, [&](const std::string& msg) {
            statuses.push_back(msg);
        });

        std::string answer;
        try {
            answer = client.executeCascade("", "Prompt");
        } catch (const std::exception& e) {
            std::cerr << "[-] Error on circUrl (" << circUrl << "): " << e.what() << std::endl;
        }

        CHALLENGE_ASSERT(answer == "Mock Ollama Fallback Answer",
                         "Circular URL correctly bypassed Tier 2 and succeeded on Tier 3 for: " + circUrl);

        bool sawTier2Status = false;
        for (const auto& st : statuses) {
            if (st.find("резервному серверу") != std::string::npos) {
                sawTier2Status = true;
            }
        }
        CHALLENGE_ASSERT(!sawTier2Status, "Tier 2 status update was NOT emitted for circular URL: " + circUrl);
    }

    // Test 2.2: Dual-tier Failover (Tier 1 fails with 12029 -> Tier 2 fails with 503 -> Tier 3 succeeds with 200)
    {
        mockRelay.setHandler([](const std::string& path, const std::string& req) {
            return "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: 19\r\n\r\nService Unavailable";
        });

        AppConfig cascadeCfg = baseCfg;
        cascadeCfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayPort) + "/v1/chat/completions";

        std::vector<std::string> statuses;
        VisionClient client(cascadeCfg, [&](const std::string& msg) {
            statuses.push_back(msg);
        });

        std::string answer = client.executeCascade("", "Dual failover query");
        CHALLENGE_ASSERT(answer == "Mock Ollama Fallback Answer", "Cascade recovered on Tier 3 after Tier 1 (12029) and Tier 2 (503)");
        CHALLENGE_ASSERT(statuses.size() >= 3, "Received all 3 status updates during dual failover");
        if (statuses.size() >= 3) {
            CHALLENGE_ASSERT(statuses[0] == "Анализирую...", "Status 1 is 'Анализирую...'");
            CHALLENGE_ASSERT(statuses[1] == "Подключение к резервному серверу...", "Status 2 is 'Подключение к резервному серверу...'");
            CHALLENGE_ASSERT(statuses[2] == "Переключение на локальный Ollama...", "Status 3 is 'Переключение на локальный Ollama...'");
        }
    }

    // Test 2.3: Non-transient client error cutoff on Tier 2
    // Tier 1 fails (12029) -> Tier 2 returns 401 Unauthorized -> Cascade MUST ABORT immediately and NOT call Tier 3!
    {
        mockRelay.setHandler([](const std::string& path, const std::string& req) {
            return "HTTP/1.1 401 Unauthorized\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: 42\r\n\r\n{\"error\":{\"message\":\"Invalid API token\"}}";
        });
        mockOllama.resetRequestCount();

        AppConfig nonTransientCfg = baseCfg;
        nonTransientCfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayPort) + "/v1/chat/completions";

        VisionClient client(nonTransientCfg);
        bool cascadeAborted = false;
        try {
            client.executeCascade("", "Non-transient test");
        } catch (const std::runtime_error& e) {
            std::string err = e.what();
            if (err.find("Tier 2 VPS Relay Client Error") != std::string::npos && err.find("401") != std::string::npos) {
                cascadeAborted = true;
            }
        }

        CHALLENGE_ASSERT(cascadeAborted, "Cascade aborted immediately on Tier 2 HTTP 401 Unauthorized without trying Tier 3");
        CHALLENGE_ASSERT(mockOllama.getRequestCount() == 0, "Tier 3 Ollama was NOT invoked after Tier 2 client error (0 requests)");
    }

    // Test 2.4: Non-transient client error cutoff on Tier 2 with HTTP 400 Bad Request
    {
        mockRelay.setHandler([](const std::string& path, const std::string& req) {
            return "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: 36\r\n\r\n{\"error\":{\"message\":\"Bad Request\"}}";
        });
        mockOllama.resetRequestCount();

        AppConfig nonTransientCfg = baseCfg;
        nonTransientCfg.fallback_url = "http://127.0.0.1:" + std::to_string(relayPort) + "/v1/chat/completions";

        VisionClient client(nonTransientCfg);
        bool cascadeAborted = false;
        try {
            client.executeCascade("", "Non-transient 400 test");
        } catch (const std::runtime_error& e) {
            std::string err = e.what();
            if (err.find("Tier 2 VPS Relay Client Error") != std::string::npos && err.find("400") != std::string::npos) {
                cascadeAborted = true;
            }
        }

        CHALLENGE_ASSERT(cascadeAborted, "Cascade aborted immediately on Tier 2 HTTP 400 Bad Request without trying Tier 3");
        CHALLENGE_ASSERT(mockOllama.getRequestCount() == 0, "Tier 3 Ollama was NOT invoked after Tier 2 400 error (0 requests)");
    }

    mockRelay.stop();
    mockOllama.stop();
}

// =========================================================================
// Suite 3: Boundary Stresses, Huge Payloads & Malformed Resiliency
// =========================================================================
void runSuite3_BoundaryStressAndPayloadResilience() {
    CHALLENGE_SECTION("Suite 3: Boundary Stresses, Huge Payloads & Malformed Resilience");

    // Test 3.1: Empty image inputs schema verification
    {
        // OpenAI payload
        auto openAiEmpty = VisionClient::buildOpenAIPayload("Hello prompt", "", "model-xyz");
        auto parts = openAiEmpty["messages"][0]["content"];
        CHALLENGE_ASSERT(parts.size() == 1, "OpenAI payload with empty image has exactly 1 part in user content");
        CHALLENGE_ASSERT(parts[0]["type"] == "text", "The single part is of type 'text'");
        CHALLENGE_ASSERT(parts[0]["text"] == "Hello prompt", "Text matches prompt");

        bool hasImageUrl = false;
        for (const auto& p : parts) {
            if (p.value("type", "") == "image_url") {
                hasImageUrl = true;
            }
        }
        CHALLENGE_ASSERT(!hasImageUrl, "OpenAI payload omits 'image_url' object completely when imageBase64 is empty");

        // Ollama payload
        auto ollamaEmpty = VisionClient::buildOllamaPayload("Hello prompt", "", "model-xyz");
        CHALLENGE_ASSERT(ollamaEmpty["images"].is_array(), "Ollama payload 'images' field is a JSON array");
        CHALLENGE_ASSERT(ollamaEmpty["images"].empty(), "Ollama payload 'images' field is EMPTY array [] (size 0)");
        CHALLENGE_ASSERT(ollamaEmpty["images"].size() == 0, "Ollama payload 'images' length is strictly 0");
    }

    // Test 3.2: Huge payloads (10 MB base64 image + 100 KB text prompt)
    {
        std::string hugeB64(10 * 1024 * 1024, 'A'); // 10MB base64 string
        std::string hugePrompt(100 * 1024, 'Z');    // 100KB prompt string

        auto start = std::chrono::steady_clock::now();

        auto openAiHuge = VisionClient::buildOpenAIPayload(hugePrompt, hugeB64, "huge-model");
        auto ollamaHuge = VisionClient::buildOllamaPayload(hugePrompt, hugeB64, "huge-model");

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

        CHALLENGE_ASSERT(openAiHuge["messages"][0]["content"].size() == 2, "OpenAI huge payload generated 2 content parts");
        CHALLENGE_ASSERT(ollamaHuge["images"].size() == 1, "Ollama huge payload generated 1 image element");
        CHALLENGE_ASSERT(ollamaHuge["images"][0].get<std::string>().size() == 10 * 1024 * 1024, "Ollama image size matches 10MB");
        CHALLENGE_ASSERT(elapsed < 2000, "10MB payload generation took under 2000ms (took " + std::to_string(elapsed) + "ms)");
    }

    // Test 3.3: Malformed JSON responses and HTML error pages
    {
        std::vector<std::pair<std::string, std::string>> adversarialInputs = {
            {"Truncated JSON 1", "{\"choices\": [{\"message\": {\"content\": \"He"},
            {"Truncated JSON 2", "{\"response\": \"Ollama answer..."},
            {"Unquoted token", "{\"choices\": [unquoted_value]}"},
            {"HTML 500 Page", "<!DOCTYPE html><html><head><title>500 Error</title></head><body>Internal Error</body></html>"},
            {"HTML 502 Page", "<html><body>502 Bad Gateway: nginx/1.24.0</body></html>"},
            {"HTML 503 Page", "<html><body><h1>503 Service Temporarily Unavailable</h1></body></html>"},
            {"Empty string", ""},
            {"Whitespace only", "   \r\n\t   \n  "},
            {"Integer scalar", "12345678"},
            {"Boolean scalar", "true"},
            {"JSON array", "[1, 2, 3, 4]"},
            {"Missing content key", "{\"choices\": [{\"message\": {\"role\": \"assistant\"}}]}"},
            {"Empty choices array", "{\"choices\": []}"},
            {"Null message", "{\"choices\": [{\"message\": null}]}"},
            {"Empty object", "{}"},
            {"Ollama missing response", "{\"model\": \"test\", \"done\": true}"}
        };

        for (const auto& item : adversarialInputs) {
            bool openAiCaught = false;
            try {
                VisionClient::parseOpenAIResponse(item.second);
            } catch (const std::runtime_error&) {
                openAiCaught = true;
            } catch (...) {
                openAiCaught = false;
            }

            bool ollamaCaught = false;
            try {
                VisionClient::parseOllamaResponse(item.second);
            } catch (const std::runtime_error&) {
                ollamaCaught = true;
            } catch (...) {
                ollamaCaught = false;
            }

            CHALLENGE_ASSERT(openAiCaught, "parseOpenAIResponse safely caught malformed input: " + item.first);
            CHALLENGE_ASSERT(ollamaCaught, "parseOllamaResponse safely caught malformed input: " + item.first);
        }

        // Ollama null response dumps "null" string rather than crashing
        {
            std::string ans = VisionClient::parseOllamaResponse("{\"response\": null}");
            CHALLENGE_ASSERT(ans == "null", "parseOllamaResponse handles null response by dumping 'null' string without crash");
        }
    }

    // Test 3.4: Unicode & Multi-byte Characters
    {
        std::string ruText = "Ответ ассистента: 42. Вариант [B] правильный! 👻 🎉";
        std::string openAiRu = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"" + ruText + "\"}}]}";
        std::string parsedOpenAi = VisionClient::parseOpenAIResponse(openAiRu);
        CHALLENGE_ASSERT(parsedOpenAi == ruText, "Parsed Unicode/Cyrillic OpenAI response correctly: " + parsedOpenAi);

        std::string ollamaRu = "{\"model\":\"llava\",\"response\":\"" + ruText + "\",\"done\":true}";
        std::string parsedOllama = VisionClient::parseOllamaResponse(ollamaRu);
        CHALLENGE_ASSERT(parsedOllama == ruText, "Parsed Unicode/Cyrillic Ollama response correctly: " + parsedOllama);
    }

    // Test 3.5: Live Captive Portal / HTML on HTTP 200 Cascade Failover
    // Tier 1 gets 12029 -> Tier 2 returns HTTP 200 with HTML login portal -> parseOpenAIResponse throws -> cascades to Tier 3!
    {
        HighPerformanceHttpServer portalServer;
        HighPerformanceHttpServer relayServer;

        if (!portalServer.start() || !relayServer.start()) {
            std::cerr << "[-] Failed to start servers for captive portal test" << std::endl;
            g_failCount++;
            return;
        }

        // Portal returns HTTP 200 with WiFi Login HTML
        portalServer.setHandler([](const std::string& path, const std::string& req) {
            std::string html = "<html><head><title>WiFi Captive Portal Login</title></head><body>Please login</body></html>";
            return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/html\r\nContent-Length: " +
                   std::to_string(html.size()) + "\r\n\r\n" + html;
        });

        // Relay/Ollama returns valid response
        relayServer.setHandler([](const std::string& path, const std::string& req) {
            std::string json = "{\"model\":\"llava\",\"response\":\"Tier 3 Rescued Answer from Tier 2 Captive Portal\",\"done\":true}";
            return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " +
                   std::to_string(json.size()) + "\r\n\r\n" + json;
        });

        AppConfig portalCfg;
        portalCfg.backend = "openrouter";
        portalCfg.proxy_url = "http://127.0.0.1:59997"; // Closed port -> Tier 1 gets 12029
        portalCfg.fallback_url = "http://127.0.0.1:" + std::to_string(portalServer.getPort()) + "/v1/chat/completions";
        portalCfg.enable_local_fallback = true;
        portalCfg.ollama_url = "http://127.0.0.1:" + std::to_string(relayServer.getPort()); // ollama will act as Tier 3
        portalCfg.request_timeout = 2;

        VisionClient client(portalCfg);
        std::string answer;
        try {
            answer = client.executeCascade("", "Captive portal test prompt");
        } catch (const std::exception& e) {
            std::cerr << "[-] Error on portal test: " << e.what() << std::endl;
        }

        CHALLENGE_ASSERT(answer == "Tier 3 Rescued Answer from Tier 2 Captive Portal",
                         "Captive portal HTTP 200 HTML on Tier 2 was safely handled and cascaded to Tier 3!");

        portalServer.stop();
        relayServer.stop();
    }
}

int main() {
    std::cout << "======================================================\n"
              << " GhostAI M3 Challenger 2: Concurrency & Boundary Stress\n"
              << " Adversarial Verification Suite\n"
              << "======================================================" << std::endl;

    runSuite1_MultithreadedConcurrencyStress();
    runSuite2_CircularAndEdgeCaseConfigs();
    runSuite3_BoundaryStressAndPayloadResilience();

    std::cout << "\n======================================================\n"
              << " Challenger 2 Results Summary:\n"
              << " PASSED: " << g_passCount.load() << "\n"
              << " FAILED: " << g_failCount.load() << "\n"
              << "======================================================" << std::endl;

    if (g_failCount.load() == 0) {
        std::cout << "[+] ALL EMPIRICAL CHALLENGES PASSED SUCCESSFULLY!\n";
        return 0;
    } else {
        std::cerr << "[-] SOME CHALLENGES FAILED!\n";
        return 1;
    }
}
