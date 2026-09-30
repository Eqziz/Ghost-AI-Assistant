#pragma once

#include "config.hpp"
#include "conversation.hpp"
#include <string>

class VisionClient {
public:
    VisionClient(const AppConfig& config, ConversationMemory& memory);

    std::string analyze(const std::string& imageBase64);

private:
    std::string analyzeOllama(const std::string& imageBase64, const std::string& prompt);
    std::string analyzeOpenRouter(const std::string& imageBase64, const std::string& prompt);
    std::string buildUserPrompt(const std::string& prompt);

    static std::string httpPost(const std::string& url, const std::string& jsonPayload, const std::string& authHeader, int timeoutSec);

    AppConfig m_config;
    ConversationMemory& m_memory;
};
