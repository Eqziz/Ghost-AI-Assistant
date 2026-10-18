#include "vision_client.hpp"
#include "obfuscator.hpp"
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
}

VisionClient::VisionClient(const AppConfig& config, ConversationMemory& memory)
    : m_config(config), m_memory(memory)
{}

namespace {
    struct WinHttpHandle {
        HINTERNET handle = NULL;
        WinHttpHandle(HINTERNET h = NULL) : handle(h) {}
        ~WinHttpHandle() { if (handle) WinHttpCloseHandle(handle); }
        operator HINTERNET() const { return handle; }
        WinHttpHandle& operator=(HINTERNET h) {
            if (handle) WinHttpCloseHandle(handle);
            handle = h;
            return *this;
        }
    };
}

std::string VisionClient::httpPost(const std::string& url, const std::string& jsonPayload, const std::string& authHeader, int timeoutSec) {
    std::string fullUrl = url;
    std::string httpPrefix = _X("http://");
    std::string httpsPrefix = _X("https://");
    if (fullUrl.rfind(httpPrefix, 0) != 0 && fullUrl.rfind(httpsPrefix, 0) != 0) {
        fullUrl = httpPrefix + fullUrl;
    }
    std::wstring wUrl = toWide(fullUrl);

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
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
    INTERNET_PORT port = urlComp.nPort;
    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    WinHttpHandle hSession = WinHttpOpen(
        _XWC(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)"),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) {
        throw std::runtime_error("WinHttpOpen failed");
    }

    int timeoutMs = timeoutSec * 1000;
    WinHttpSetTimeouts(hSession, 10000, 10000, timeoutMs, timeoutMs);

    WinHttpHandle hConnect = WinHttpConnect(hSession, hostName.c_str(), port, 0);
    if (!hConnect) {
        throw std::runtime_error("WinHttpConnect failed");
    }

    DWORD reqFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle hRequest = WinHttpOpenRequest(
        hConnect,
        _XWC(L"POST"),
        urlPath.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        reqFlags
    );
    if (!hRequest) {
        throw std::runtime_error("WinHttpOpenRequest failed");
    }

    if (isHttps) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    std::wstring headers = _XW(L"Content-Type: application/json; charset=utf-8\r\n");
    if (!authHeader.empty()) {
        headers += toWide(authHeader) + L"\r\n";
    }

    BOOL sendOk = WinHttpSendRequest(
        hRequest,
        headers.c_str(), (DWORD)headers.length(),
        (LPVOID)jsonPayload.data(), (DWORD)jsonPayload.length(),
        (DWORD)jsonPayload.length(),
        0
    );

    if (!sendOk) {
        DWORD err = GetLastError();
        if (err == ERROR_WINHTTP_TIMEOUT) {
            throw std::runtime_error("Network timeout");
        }
        throw std::runtime_error("Network error: " + std::to_string(err));
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        throw std::runtime_error("No response from server");
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
            historyText = "\n\n" + _X("PREVIOUS:") + "\n";
            for (size_t i = 0; i < turns.size(); ++i) {
                historyText += "[" + std::to_string(i + 1) + "] User: " + turns[i].user_text + "\n";
                historyText += "[" + std::to_string(i + 1) + "] Assistant: " + turns[i].assistant_text + "\n";
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
    res += _X("PROMPT:\n") + prompt;
    return res;
}

std::string VisionClient::analyzeOllama(const std::string& imageBase64, const std::string& prompt) {
    std::string baseUrl = m_config.ollama_url;
    while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
    std::string url = baseUrl + _X("/api/generate");

    std::string fullPrompt = buildUserPrompt(prompt);

    nlohmann::json payload;
    payload[_XC("model")] = m_config.ollama_model;
    payload[_XC("prompt")] = fullPrompt;
    payload[_XC("images")] = nlohmann::json::array({ imageBase64 });
    payload[_XC("stream")] = false;

    std::string respStr = httpPost(url, payload.dump(), "", m_config.request_timeout);
    auto j = nlohmann::json::parse(respStr);
    if (j.contains(_XC("error"))) {
        std::string err = j[_XC("error")].is_string() ? j[_XC("error")].get<std::string>() : j[_XC("error")].dump();
        throw std::runtime_error("Ollama API: " + err);
    }
    std::string answer = j.value(_XC("response"), "");
    if (answer.empty()) {
        throw std::runtime_error("Ollama: empty response");
    }
    return answer;
}

std::string VisionClient::analyzeOpenRouter(const std::string& imageBase64, const std::string& prompt) {
    std::string url = _X("https://openrouter.ai/api/v1/chat/completions");
    std::string authHeader = _X("Authorization: Bearer ") + m_config.openrouter_api_key;

    nlohmann::json messages = nlohmann::json::array();
    if (!m_config.system_prompt.empty()) {
        messages.push_back({
            {_XC("role"), _XC("system")},
            {_XC("content"), m_config.system_prompt}
        });
    }

    if (m_memory.isEnabled()) {
        auto turns = m_memory.getTurns(m_config.include_images_in_context);
        for (const auto& t : turns) {
            messages.push_back({{_XC("role"), _XC("user")}, {_XC("content"), t.user_text}});
            messages.push_back({{_XC("role"), _XC("assistant")}, {_XC("content"), t.assistant_text}});
        }
    }

    nlohmann::json userContent = nlohmann::json::array();
    userContent.push_back({{_XC("type"), _XC("text")}, {_XC("text"), prompt}});
    userContent.push_back({
        {_XC("type"), _XC("image_url")},
        {_XC("image_url"), {{_XC("url"), _X("data:image/jpeg;base64,") + imageBase64}}}
    });

    messages.push_back({
        {_XC("role"), _XC("user")},
        {_XC("content"), userContent}
    });

    nlohmann::json payload;
    payload[_XC("model")] = m_config.openrouter_model;
    payload[_XC("messages")] = messages;
    payload[_XC("max_tokens")] = 500;

    std::string respStr = httpPost(url, payload.dump(), authHeader, m_config.request_timeout);
    auto j = nlohmann::json::parse(respStr);

    if (j.contains(_XC("error"))) {
        if (j[_XC("error")].is_object()) {
            throw std::runtime_error("OpenRouter API: " + j[_XC("error")].value(_XC("message"), "Error"));
        } else if (j[_XC("error")].is_string()) {
            throw std::runtime_error("OpenRouter API: " + j[_XC("error")].get<std::string>());
        }
    }

    std::string answer;
    try {
        auto content = j[_XC("choices")][0][_XC("message")][_XC("content")];
        if (content.is_string()) {
            answer = content.get<std::string>();
        } else if (content.is_array()) {
            for (const auto& part : content) {
                if (part.is_object() && part.value(_XC("type"), "") == _X("text")) {
                    answer += part.value(_XC("text"), "") + " ";
                }
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("OpenRouter parse error: " + std::string(e.what()));
    }

    // Trim
    while (!answer.empty() && (answer.front() == ' ' || answer.front() == '\n' || answer.front() == '\r')) answer.erase(answer.begin());
    while (!answer.empty() && (answer.back() == ' ' || answer.back() == '\n' || answer.back() == '\r')) answer.pop_back();

    if (answer.empty()) {
        throw std::runtime_error("OpenRouter: empty response");
    }
    return answer;
}

std::string VisionClient::analyze(const std::string& imageBase64) {
    std::string userPrompt = m_config.user_prompt;
    std::string answer;

    if (m_config.backend == _X("ollama")) {
        answer = analyzeOllama(imageBase64, userPrompt);
    } else if (m_config.backend == _X("openrouter")) {
        answer = analyzeOpenRouter(imageBase64, userPrompt);
    } else {
        throw std::runtime_error("Unknown backend: " + m_config.backend);
    }

    m_memory.addTurn(userPrompt, imageBase64, answer);
    return answer;
}
