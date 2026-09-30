#include "vision_client.hpp"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <iostream>
#include <stdexcept>

#pragma comment(lib, "winhttp.lib")

namespace {
    std::wstring toWide(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        std::wstring w(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &w[0], size);
        return w;
    }
}

VisionClient::VisionClient(const AppConfig& config, ConversationMemory& memory)
    : m_config(config), m_memory(memory)
{}

std::string VisionClient::httpPost(const std::string& url, const std::string& jsonPayload, const std::string& authHeader, int timeoutSec) {
    std::wstring wUrl = toWide(url);

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        throw std::runtime_error("WinHttpCrackUrl failed");
    }

    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath = (urlComp.dwUrlPathLength > 0) ? std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength) : L"/";
    if (urlComp.dwExtraInfoLength > 0) {
        urlPath.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = urlComp.nPort;

    HINTERNET hSession = WinHttpOpen(L"Ghost-AI/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        throw std::runtime_error("WinHttpOpen failed");
    }

    DWORD timeoutMs = static_cast<DWORD>(timeoutSec * 1000);
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        throw std::runtime_error("WinHttpConnect failed");
    }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        throw std::runtime_error("WinHttpOpenRequest failed");
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!authHeader.empty()) {
        headers += toWide(authHeader) + L"\r\n";
    }

    BOOL sendOk = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
                                     (LPVOID)jsonPayload.c_str(), (DWORD)jsonPayload.length(),
                                     (DWORD)jsonPayload.length(), 0);

    if (!sendOk) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (err == ERROR_WINHTTP_TIMEOUT) {
            throw std::runtime_error("Timeout");
        }
        throw std::runtime_error("WinHttpSendRequest error: " + std::to_string(err));
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        throw std::runtime_error("WinHttpReceiveResponse failed");
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1, 0);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
            responseBody.append(buffer.data(), bytesRead);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (statusCode != 200) {
        throw std::runtime_error("HTTP " + std::to_string(statusCode) + ": " + responseBody.substr(0, 200));
    }

    return responseBody;
}

std::string VisionClient::buildUserPrompt(const std::string& prompt) {
    std::string sp = m_config.system_prompt;
    std::string historyText;
    if (m_memory.isEnabled()) {
        auto turns = m_memory.getTurns(false);
        if (!turns.empty()) {
            historyText = "\n\nПРЕДЫДУЩИЙ ДИАЛОГ:\n";
            for (size_t i = 0; i < turns.size(); ++i) {
                historyText += "[" + std::to_string(i + 1) + "] Пользователь: " + turns[i].user_text + "\n";
                historyText += "[" + std::to_string(i + 1) + "] Ассистент: " + turns[i].assistant_text + "\n";
            }
        }
    }

    std::string res;
    if (!sp.empty()) {
        res += sp + "\n\n";
    }
    if (!historyText.empty()) {
        res += historyText + "\n\n";
    }
    res += "ЗАДАНИЕ:\n" + prompt;
    return res;
}

std::string VisionClient::analyzeOllama(const std::string& imageBase64, const std::string& prompt) {
    std::string baseUrl = m_config.ollama_url;
    while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
    std::string url = baseUrl + "/api/generate";

    std::string fullPrompt = buildUserPrompt(prompt);

    nlohmann::json payload;
    payload["model"] = m_config.ollama_model;
    payload["prompt"] = fullPrompt;
    payload["images"] = nlohmann::json::array({ imageBase64 });
    payload["stream"] = false;

    std::string respStr = httpPost(url, payload.dump(), "", m_config.request_timeout);
    auto j = nlohmann::json::parse(respStr);
    std::string answer = j.value("response", "");
    if (answer.empty()) {
        throw std::runtime_error("Ollama: пустой ответ");
    }
    return answer;
}

std::string VisionClient::analyzeOpenRouter(const std::string& imageBase64, const std::string& prompt) {
    std::string url = "https://openrouter.ai/api/v1/chat/completions";
    std::string authHeader = "Authorization: Bearer " + m_config.openrouter_api_key;

    nlohmann::json messages = nlohmann::json::array();
    if (!m_config.system_prompt.empty()) {
        messages.push_back({
            {"role", "system"},
            {"content", m_config.system_prompt}
        });
    }

    if (m_memory.isEnabled()) {
        auto turns = m_memory.getTurns(m_config.include_images_in_context);
        for (const auto& t : turns) {
            messages.push_back({{"role", "user"}, {"content", t.user_text}});
            messages.push_back({{"role", "assistant"}, {"content", t.assistant_text}});
        }
    }

    nlohmann::json userContent = nlohmann::json::array();
    userContent.push_back({{"type", "text"}, {"text", prompt}});
    userContent.push_back({
        {"type", "image_url"},
        {"image_url", {{"url", "data:image/jpeg;base64," + imageBase64}}}
    });

    messages.push_back({
        {"role", "user"},
        {"content", userContent}
    });

    nlohmann::json payload;
    payload["model"] = m_config.openrouter_model;
    payload["messages"] = messages;
    payload["max_tokens"] = 500;

    std::string respStr = httpPost(url, payload.dump(), authHeader, m_config.request_timeout);
    auto j = nlohmann::json::parse(respStr);

    std::string answer;
    try {
        auto content = j["choices"][0]["message"]["content"];
        if (content.is_string()) {
            answer = content.get<std::string>();
        } else if (content.is_array()) {
            for (const auto& part : content) {
                if (part.is_object() && part.value("type", "") == "text") {
                    answer += part.value("text", "") + " ";
                }
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("OpenRouter: формат ответа: " + std::string(e.what()));
    }

    // Trim
    while (!answer.empty() && (answer.front() == ' ' || answer.front() == '\n' || answer.front() == '\r')) answer.erase(answer.begin());
    while (!answer.empty() && (answer.back() == ' ' || answer.back() == '\n' || answer.back() == '\r')) answer.pop_back();

    if (answer.empty()) {
        throw std::runtime_error("OpenRouter: пустой ответ");
    }
    return answer;
}

std::string VisionClient::analyze(const std::string& imageBase64) {
    std::string userPrompt = m_config.user_prompt;
    std::string answer;

    if (m_config.backend == "ollama") {
        answer = analyzeOllama(imageBase64, userPrompt);
    } else if (m_config.backend == "openrouter") {
        answer = analyzeOpenRouter(imageBase64, userPrompt);
    } else {
        throw std::runtime_error("Неизвестный backend: " + m_config.backend);
    }

    m_memory.addTurn(userPrompt, imageBase64, answer);
    return answer;
}
