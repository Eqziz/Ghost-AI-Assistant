#pragma once

#include "config.hpp"
#include "conversation.hpp"
#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>
#include <windows.h>

// Status callback for UI reporting (PROJECT.md contract)
using StatusCallback = std::function<void(const std::string& statusMessage)>;
using ConversationTurn = Turn;

/**
 * @brief Structured HTTP response with status codes, error interception, and latency.
 */
struct HttpResponse {
    bool success = false;
    int statusCode = 0;             // HTTP status code (200, 500, 502, 407, etc., or 0 on transport error)
    DWORD winHttpError = 0;         // WinHTTP GetLastError() code (12029, 12002, 12030, or 0)
    std::string body;               // Server response body
    std::string errorMessage;       // Error description/detail
    long long elapsedMs = 0;        // Duration in ms

    bool isSuccess() const {
        return success && statusCode == 200 && !body.empty();
    }

    bool isTimeout() const {
        return winHttpError == 12002 || statusCode == 408;
    }

    bool isTransient() const {
        // Долгий анализ сложного вопроса (таймаут) НЕ должен вызывать авто-переключение узлов
        if (isTimeout()) {
            return false;
        }

        // Transport error (WinHttp system error or status code 0, e.g. 12029 connection refused / blocked)
        if (statusCode == 0 && winHttpError != 0) return true;
        if (winHttpError != 0) return true;

        // HTTP 5xx server errors
        if (statusCode >= 500 && statusCode <= 599) return true;
        // HTTP 429 Rate Limit
        if (statusCode == 429) return true;
        // Unresolved HTTP 407 Proxy Auth
        if (statusCode == 407) return true;
        // Non-transient client errors: 400, 401, 403, 404
        return false;
    }

    std::string getReasonString() const;
};

class VisionClient {
public:
    VisionClient(const AppConfig& config, ConversationMemory& memory, StatusCallback statusCb = nullptr);
    explicit VisionClient(const AppConfig& config, StatusCallback statusCb = nullptr);

    void setStatusCallback(StatusCallback cb);
    void updateConfig(const AppConfig& config);

    std::string analyze(const std::string& imageBase64, const std::string& userPrompt = "");

    // 3-tier cascade fallback engine (Tier 1 Cloud -> Tier 2 VPS Relay -> Tier 3 Local Ollama)
    std::string executeCascade(const std::string& imageBase64, const std::string& userPrompt);

    // Multi-format payload adapters
    static nlohmann::json buildOpenAIPayload(
        const std::string& prompt,
        const std::string& imageBase64,
        const std::string& model,
        const std::string& systemPrompt = "",
        const std::vector<ConversationTurn>& history = {},
        int maxTokens = 500
    );

    static nlohmann::json buildOllamaPayload(
        const std::string& prompt,
        const std::string& imageBase64,
        const std::string& model
    );

    static std::string parseOpenAIResponse(const std::string& jsonStr);
    static std::string parseOllamaResponse(const std::string& jsonStr);

    // Internal HTTP request executor returning HttpResponse without throwing on transient errors
    HttpResponse executeHttpRequest(
        const std::string& url,
        const std::string& jsonPayload,
        const std::string& authHeader,
        int timeoutSec,
        int tier = 1,
        const std::string& endpointName = "Primary Cloud"
    );

    // Legacy httpPost wrapper for callers expecting string response and exception on error
    std::string httpPost(
        const std::string& url,
        const std::string& jsonPayload,
        const std::string& authHeader,
        int timeoutSec,
        int tier = 1,
        const std::string& endpointName = "Primary Cloud"
    );

private:
    std::string analyzeOllama(const std::string& imageBase64, const std::string& prompt);
    std::string analyzeOpenRouter(const std::string& imageBase64, const std::string& prompt);
    std::string buildUserPrompt(const std::string& prompt);
    void reportStatus(const std::string& message);
    static bool isSameUrl(const std::string& u1, const std::string& u2);

    mutable std::mutex m_clientMutex;
    AppConfig m_config;
    std::unique_ptr<ConversationMemory> m_ownedMemory;
    ConversationMemory* m_pMemory = nullptr;
    StatusCallback m_statusCallback;
};


