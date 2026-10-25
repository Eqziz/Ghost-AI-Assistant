#include "vision_client.hpp"
#include "obfuscator.hpp"
#include "proxy_utils.hpp"
#include "network_logger.hpp"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

#ifndef WINHTTP_OPTION_PROXY_USERNAME
#define WINHTTP_OPTION_PROXY_USERNAME 0x1002
#endif
#ifndef WINHTTP_OPTION_PROXY_PASSWORD
#define WINHTTP_OPTION_PROXY_PASSWORD 0x1003
#endif

#ifndef WINHTTP_AUTH_TARGET_PROXY
#define WINHTTP_AUTH_TARGET_PROXY 0x00000001
#endif
#ifndef WINHTTP_AUTH_SCHEME_BASIC
#define WINHTTP_AUTH_SCHEME_BASIC 0x00000001
#endif
#ifndef HTTP_STATUS_PROXY_AUTH_REQ
#define HTTP_STATUS_PROXY_AUTH_REQ 407
#endif

std::string HttpResponse::getReasonString() const {
    if (isTimeout()) {
        return "Таймаут ожидания: нейросеть долго анализирует сложный вопрос (авто-переключение отключено)";
    }
    if (winHttpError != 0) {
        return "WinHttp error " + std::to_string(winHttpError) + " (" + NetworkLogger::getWinHttpErrorString(winHttpError) + ")";
    }
    if (statusCode != 0) {
        return "HTTP " + std::to_string(statusCode) + " (" + NetworkLogger::getHttpStatusString(statusCode) + ")";
    }
    if (!errorMessage.empty()) {
        return errorMessage;
    }
    return "Unknown network error";
}

VisionClient::VisionClient(const AppConfig& config, ConversationMemory& memory, StatusCallback statusCb)
    : m_config(config), m_pMemory(&memory), m_statusCallback(statusCb)
{}

VisionClient::VisionClient(const AppConfig& config, StatusCallback statusCb)
    : m_config(config), m_statusCallback(statusCb)
{
    m_ownedMemory = std::make_unique<ConversationMemory>(config.max_context_pairs, config.persist_context);
    m_pMemory = m_ownedMemory.get();
}

void VisionClient::setStatusCallback(StatusCallback cb) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    m_statusCallback = std::move(cb);
}

void VisionClient::updateConfig(const AppConfig& config) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    m_config = config;
}

void VisionClient::reportStatus(const std::string& message) {
    StatusCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        cb = m_statusCallback;
    }
    if (cb) {
        try {
            cb(message);
        } catch (...) {
            // Callback must never leak exception
        }
    }
}

bool VisionClient::isSameUrl(const std::string& u1, const std::string& u2) {
    auto normalize = [](std::string s) {
        while (!s.empty() && (s.back() == '/' || s.back() == ' ')) s.pop_back();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        return s;
    };
    return normalize(u1) == normalize(u2);
}

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

HttpResponse VisionClient::executeHttpRequest(
    const std::string& url,
    const std::string& jsonPayload,
    const std::string& authHeader,
    int timeoutSec,
    int tier,
    const std::string& endpointName
) {
    HttpResponse resp;
    auto startTime = std::chrono::steady_clock::now();
    NetworkLogger::logAttempt(tier, endpointName, url);

    std::string fullUrl = url;
    std::string httpPrefix = _X("http://");
    std::string httpsPrefix = _X("https://");
    if (fullUrl.rfind(httpPrefix, 0) != 0 && fullUrl.rfind(httpsPrefix, 0) != 0) {
        fullUrl = httpPrefix + fullUrl;
    }
    std::wstring wUrl = ProxyUtils::toWide(fullUrl);

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        DWORD err = GetLastError();
        NetworkLogger::logError(tier, endpointName, err, 0, "WinHttpCrackUrl failed for URL: " + NetworkLogger::sanitizeUrl(url));
        resp.winHttpError = err;
        resp.statusCode = 0;
        resp.errorMessage = "WinHttpCrackUrl failed: " + NetworkLogger::getWinHttpErrorString(err);
        return resp;
    }

    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
    INTERNET_PORT port = urlComp.nPort;
    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    std::string hostNameStr = ProxyUtils::toNarrow(hostName);

    // 1. Resolve proxy configuration under mutex
    ProxyUtils::ProxyConfig proxyCfg;
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        proxyCfg = ProxyUtils::parseFromConfig(m_config);
    }

    DWORD accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    LPCWSTR proxyName = WINHTTP_NO_PROXY_NAME;
    LPCWSTR proxyBypass = WINHTTP_NO_PROXY_BYPASS;
    std::wstring wProxyName;
    std::wstring wProxyBypass = L"<local>;127.0.0.1;localhost";

    bool targetIsLocal = ProxyUtils::isLocalAddress(hostNameStr);

    if (proxyCfg.is_valid && !proxyCfg.host.empty() && !targetIsLocal) {
        accessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        wProxyName = proxyCfg.toWinHttpProxyName();
        proxyName = wProxyName.c_str();
        proxyBypass = wProxyBypass.c_str();
        NetworkLogger::log("Routing via proxy: " + proxyCfg.getSanitizedUrl() +
                           (proxyCfg.is_authenticated ? " (authenticated)" : " (anonymous)"));
    }

    // 2. Open WinHttp Session with Chrome 131 User-Agent
    const wchar_t* const CHROME_UA = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
    WinHttpHandle hSession = WinHttpOpen(
        CHROME_UA,
        accessType,
        proxyName,
        proxyBypass,
        0
    );
    if (!hSession) {
        DWORD err = GetLastError();
        NetworkLogger::logError(tier, endpointName, err, 0, "WinHttpOpen failed: " + NetworkLogger::getWinHttpErrorString(err));
        resp.winHttpError = err;
        resp.statusCode = 0;
        resp.errorMessage = "WinHttpOpen failed: " + NetworkLogger::getWinHttpErrorString(err);
        return resp;
    }

    // Modern TLS protocol negotiation
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    int timeoutMs = timeoutSec * 1000;
    DWORD connectTimeout = (std::min)(10000, timeoutMs);
    WinHttpSetTimeouts(hSession, connectTimeout, connectTimeout, timeoutMs, timeoutMs);

    // 3. Connect to Target Host
    WinHttpHandle hConnect = WinHttpConnect(hSession, hostName.c_str(), port, 0);
    if (!hConnect) {
        DWORD err = GetLastError();
        NetworkLogger::logError(tier, endpointName, err, 0, "WinHttpConnect failed: " + NetworkLogger::getWinHttpErrorString(err));
        resp.winHttpError = err;
        resp.statusCode = 0;
        resp.errorMessage = "WinHttpConnect failed: " + NetworkLogger::getWinHttpErrorString(err);
        return resp;
    }

    // 4. Open Request Handle
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
        DWORD err = GetLastError();
        NetworkLogger::logError(tier, endpointName, err, 0, "WinHttpOpenRequest failed: " + NetworkLogger::getWinHttpErrorString(err));
        resp.winHttpError = err;
        resp.statusCode = 0;
        resp.errorMessage = "WinHttpOpenRequest failed: " + NetworkLogger::getWinHttpErrorString(err);
        return resp;
    }

    // 5. SSL Security Flags for Corporate Middlebox / MITM Compatibility
    if (isHttps) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    // 6. Proactively set proxy credentials options on hRequest
    std::wstring wProxyUser;
    std::wstring wProxyPass;
    if (proxyCfg.is_valid && proxyCfg.is_authenticated && accessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY) {
        wProxyUser = proxyCfg.toWinHttpUsername();
        wProxyPass = proxyCfg.toWinHttpPassword();

        if (!wProxyUser.empty()) {
            WinHttpSetOption(hRequest, WINHTTP_OPTION_PROXY_USERNAME,
                             (LPVOID)wProxyUser.c_str(),
                             (DWORD)((wProxyUser.length() + 1) * sizeof(wchar_t)));
        }
        if (!wProxyPass.empty()) {
            WinHttpSetOption(hRequest, WINHTTP_OPTION_PROXY_PASSWORD,
                             (LPVOID)wProxyPass.c_str(),
                             (DWORD)((wProxyPass.length() + 1) * sizeof(wchar_t)));
        }
    }

    // 7. Inject Stealth Browser Masking Headers (F8)
    std::wstring headers = ProxyUtils::buildBrowserHeadersW(authHeader);
    BOOL addOk = WinHttpAddRequestHeaders(
        hRequest,
        headers.c_str(),
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE
    );
    if (!addOk) {
        DWORD err = GetLastError();
        NetworkLogger::logError(tier, endpointName, err, 0, "WinHttpAddRequestHeaders failed: " + NetworkLogger::getWinHttpErrorString(err));
    }

    // 8. Request Execution & HTTP 407 Challenge-Response State Machine
    int retryCount = 0;
    const int maxRetries = 1; // Strictly cap retry count to 1 to prevent infinite loops
    DWORD statusCode = 0;

    while (true) {
        BOOL sendOk = WinHttpSendRequest(
            hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)jsonPayload.data(), (DWORD)jsonPayload.length(),
            (DWORD)jsonPayload.length(),
            0
        );

        if (!sendOk) {
            DWORD err = GetLastError();
            NetworkLogger::logError(tier, endpointName, err, 0, "WinHttpSendRequest failed: " + NetworkLogger::getWinHttpErrorString(err));
            resp.winHttpError = err;
            resp.statusCode = 0;
            resp.errorMessage = "WinHttpSendRequest failed: " + NetworkLogger::getWinHttpErrorString(err);
            return resp;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD err = GetLastError();
            NetworkLogger::logError(tier, endpointName, err, 0, "WinHttpReceiveResponse failed: " + NetworkLogger::getWinHttpErrorString(err));
            resp.winHttpError = err;
            resp.statusCode = 0;
            resp.errorMessage = "WinHttpReceiveResponse failed: " + NetworkLogger::getWinHttpErrorString(err);
            return resp;
        }

        statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

        // HTTP 407 Proxy Authentication Required challenge
        if (statusCode == HTTP_STATUS_PROXY_AUTH_REQ && retryCount < maxRetries && proxyCfg.is_valid && proxyCfg.is_authenticated) {
            NetworkLogger::log("Received HTTP 407 (Proxy Authentication Required). Submitting credentials for user: " + proxyCfg.username);

            BOOL credOk = WinHttpSetCredentials(
                hRequest,
                WINHTTP_AUTH_TARGET_PROXY,
                WINHTTP_AUTH_SCHEME_BASIC,
                wProxyUser.c_str(),
                wProxyPass.c_str(),
                NULL
            );

            if (!credOk) {
                DWORD err = GetLastError();
                NetworkLogger::logError(tier, endpointName, err, 407, "WinHttpSetCredentials failed");
                resp.winHttpError = err;
                resp.statusCode = 407;
                resp.errorMessage = "WinHttpSetCredentials failed";
                return resp;
            }

            retryCount++;
            continue;
        }

        break;
    }

    // 9. Read Response Body
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1, 0);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
            responseBody.append(buffer.data(), bytesRead);
        } else {
            DWORD err = GetLastError();
            if (err != 0) {
                NetworkLogger::logError(tier, endpointName, err, statusCode, "WinHttpReadData failed");
                resp.winHttpError = err;
                resp.statusCode = statusCode;
                resp.errorMessage = "WinHttpReadData failed: " + NetworkLogger::getWinHttpErrorString(err);
                return resp;
            }
        }
    }

    resp.statusCode = statusCode;
    resp.body = responseBody;

    // 10. Status Code Evaluation & Diagnostic Logging
    if (statusCode != 200) {
        std::string detail = "HTTP " + std::to_string(statusCode) + " (" + NetworkLogger::getHttpStatusString(statusCode) + ")";
        if (statusCode == 407) {
            detail += " — Proxy credentials rejected or missing";
        } else if (statusCode == 403) {
            detail += " — Proxy or origin forbidden";
        }
        NetworkLogger::logError(tier, endpointName, 0, statusCode, detail);
        resp.errorMessage = detail;
        resp.success = false;
        return resp;
    }

    auto endTime = std::chrono::steady_clock::now();
    resp.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    NetworkLogger::logSuccess(tier, endpointName, resp.elapsedMs);
    resp.success = true;
    return resp;
}

std::string VisionClient::httpPost(
    const std::string& url,
    const std::string& jsonPayload,
    const std::string& authHeader,
    int timeoutSec,
    int tier,
    const std::string& endpointName
) {
    HttpResponse resp = executeHttpRequest(url, jsonPayload, authHeader, timeoutSec, tier, endpointName);
    if (resp.isSuccess()) {
        return resp.body;
    }
    if (resp.winHttpError != 0) {
        if (resp.winHttpError == ERROR_WINHTTP_TIMEOUT) {
            throw std::runtime_error("Network timeout");
        }
        if (resp.winHttpError == 12029) {
            throw std::runtime_error("Сетевая ошибка 12029 (соединение заблокировано брандмауэром/проктором). Переключитесь на локальный Ollama (127.0.0.1:11434) или используйте прокси.");
        }
        throw std::runtime_error("Network error: " + std::to_string(resp.winHttpError));
    }
    throw std::runtime_error("HTTP " + std::to_string(resp.statusCode) + ": " + resp.body.substr(0, 200));
}

nlohmann::json VisionClient::buildOpenAIPayload(
    const std::string& prompt,
    const std::string& imageBase64,
    const std::string& model,
    const std::string& systemPrompt,
    const std::vector<ConversationTurn>& history,
    int maxTokens
) {
    nlohmann::json messages = nlohmann::json::array();

    if (!systemPrompt.empty()) {
        messages.push_back({
            {_XC("role"), _XC("system")},
            {_XC("content"), systemPrompt}
        });
    }

    for (const auto& turn : history) {
        messages.push_back({{_XC("role"), _XC("user")}, {_XC("content"), turn.user_text}});
        messages.push_back({{_XC("role"), _XC("assistant")}, {_XC("content"), turn.assistant_text}});
    }

    nlohmann::json userContent = nlohmann::json::array();
    userContent.push_back({{_XC("type"), _XC("text")}, {_XC("text"), prompt}});

    if (!imageBase64.empty()) {
        userContent.push_back({
            {_XC("type"), _XC("image_url")},
            {_XC("image_url"), {{_XC("url"), _X("data:image/jpeg;base64,") + imageBase64}}}
        });
    }

    messages.push_back({
        {_XC("role"), _XC("user")},
        {_XC("content"), userContent}
    });

    nlohmann::json payload;
    payload[_XC("model")] = model;
    payload[_XC("messages")] = messages;
    payload[_XC("max_tokens")] = maxTokens;
    return payload;
}

nlohmann::json VisionClient::buildOllamaPayload(
    const std::string& prompt,
    const std::string& imageBase64,
    const std::string& model
) {
    nlohmann::json payload;
    payload[_XC("model")] = !model.empty() ? model : "llava:latest";
    payload[_XC("prompt")] = prompt;
    if (!imageBase64.empty()) {
        payload[_XC("images")] = nlohmann::json::array({ imageBase64 });
    } else {
        payload[_XC("images")] = nlohmann::json::array();
    }
    payload[_XC("stream")] = false;
    return payload;
}

std::string VisionClient::parseOpenAIResponse(const std::string& jsonStr) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonStr);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("OpenAI malformed JSON response: ") + e.what());
    }

    if (j.contains(_XC("error"))) {
        if (j[_XC("error")].is_object()) {
            throw std::runtime_error("OpenAI API error: " + j[_XC("error")].value(_XC("message"), "Error"));
        } else if (j[_XC("error")].is_string()) {
            throw std::runtime_error("OpenAI API error: " + j[_XC("error")].get<std::string>());
        }
    }

    if (!j.contains(_XC("choices")) || !j[_XC("choices")].is_array() || j[_XC("choices")].empty()) {
        throw std::runtime_error("OpenAI response missing 'choices' array");
    }

    std::string answer;
    try {
        const auto& choice = j[_XC("choices")][0];
        if (!choice.contains(_XC("message")) || !choice[_XC("message")].contains(_XC("content"))) {
            throw std::runtime_error("OpenAI response missing 'message.content'");
        }
        const auto& content = choice[_XC("message")][_XC("content")];
        if (content.is_string()) {
            answer = content.get<std::string>();
        } else if (content.is_array()) {
            for (const auto& part : content) {
                if (part.is_object() && part.value(_XC("type"), "") == _X("text")) {
                    answer += part.value(_XC("text"), "") + " ";
                } else if (part.is_string()) {
                    answer += part.get<std::string>() + " ";
                }
            }
        }
    } catch (const std::runtime_error&) {
        throw;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("OpenAI parse error: ") + e.what());
    }

    // Trim
    while (!answer.empty() && (answer.front() == ' ' || answer.front() == '\n' || answer.front() == '\r')) answer.erase(answer.begin());
    while (!answer.empty() && (answer.back() == ' ' || answer.back() == '\n' || answer.back() == '\r')) answer.pop_back();

    if (answer.empty()) {
        throw std::runtime_error("OpenAI: empty response");
    }
    return answer;
}

std::string VisionClient::parseOllamaResponse(const std::string& jsonStr) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonStr);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Ollama malformed JSON response: ") + e.what());
    }

    if (j.contains(_XC("error"))) {
        std::string err = j[_XC("error")].is_string() ? j[_XC("error")].get<std::string>() : j[_XC("error")].dump();
        throw std::runtime_error("Ollama API: " + err);
    }

    if (!j.contains(_XC("response"))) {
        throw std::runtime_error("Ollama response missing 'response' field");
    }

    std::string answer = j[_XC("response")].is_string() ? j[_XC("response")].get<std::string>() : j[_XC("response")].dump();

    // Trim
    while (!answer.empty() && (answer.front() == ' ' || answer.front() == '\n' || answer.front() == '\r')) answer.erase(answer.begin());
    while (!answer.empty() && (answer.back() == ' ' || answer.back() == '\n' || answer.back() == '\r')) answer.pop_back();

    if (answer.empty()) {
        throw std::runtime_error("Ollama: empty response");
    }
    return answer;
}

std::string VisionClient::buildUserPrompt(const std::string& prompt) {
    std::string sp;
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        sp = m_config.system_prompt;
    }
    std::string historyText;
    if (m_pMemory && m_pMemory->isEnabled()) {
        auto turns = m_pMemory->getTurns(false);
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
    std::string baseUrl;
    std::string model;
    int timeoutSec;
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        baseUrl = m_config.ollama_url;
        model = m_config.ollama_model;
        timeoutSec = m_config.request_timeout;
    }
    while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
    std::string url = (baseUrl.empty() ? "http://127.0.0.1:11434" : baseUrl) + _X("/api/generate");

    std::string fullPrompt = buildUserPrompt(prompt);
    nlohmann::json payload = buildOllamaPayload(fullPrompt, imageBase64, model);

    HttpResponse resp = executeHttpRequest(url, payload.dump(), "", timeoutSec, 3, "Local Offline Ollama");
    if (!resp.isSuccess()) {
        throw std::runtime_error("Ollama API: " + resp.getReasonString());
    }
    return parseOllamaResponse(resp.body);
}

std::string VisionClient::analyzeOpenRouter(const std::string& imageBase64, const std::string& prompt) {
    AppConfig cfg;
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        cfg = m_config;
    }
    std::string url = _X("https://openrouter.ai/api/v1/chat/completions");
    std::string authHeader = _X("Authorization: Bearer ") + cfg.openrouter_api_key;
    std::string model = !cfg.openrouter_model.empty() ? cfg.openrouter_model : "meta-llama/llama-3.2-11b-vision-instruct:free";

    std::vector<ConversationTurn> history;
    if (m_pMemory && m_pMemory->isEnabled()) {
        history = m_pMemory->getTurns(cfg.include_images_in_context);
    }

    nlohmann::json payload = buildOpenAIPayload(prompt, imageBase64, model, cfg.system_prompt, history, 500);

    HttpResponse resp = executeHttpRequest(url, payload.dump(), authHeader, cfg.request_timeout, 1, "Primary Cloud");
    if (!resp.isSuccess()) {
        throw std::runtime_error("OpenRouter API: " + resp.getReasonString());
    }
    return parseOpenAIResponse(resp.body);
}

std::string VisionClient::executeCascade(const std::string& imageBase64, const std::string& userPrompt) {
    reportStatus("Анализирую...");

    AppConfig cfg;
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        cfg = m_config;
    }

    std::string prompt = !userPrompt.empty() ? userPrompt : cfg.user_prompt;
    std::string lastErrorReason = "No tiers attempted";

    std::vector<ConversationTurn> history;
    if (m_pMemory && m_pMemory->isEnabled()) {
        history = m_pMemory->getTurns(cfg.include_images_in_context);
    }

    // =========================================================================
    // Tier 1: Primary Cloud API (OpenRouter / OpenAI)
    // =========================================================================
    std::string tier1Url = _X("https://openrouter.ai/api/v1/chat/completions");
    std::string tier1Auth = !cfg.openrouter_api_key.empty() ? (_X("Authorization: Bearer ") + cfg.openrouter_api_key) : "";
    std::string tier1Model = !cfg.openrouter_model.empty() ? cfg.openrouter_model : "nvidia/nemotron-3.5-super-vl:free";

    nlohmann::json tier1Payload = buildOpenAIPayload(prompt, imageBase64, tier1Model, cfg.system_prompt, history, 500);

    bool shouldSkipTier1 = cfg.openrouter_api_key.empty() && !cfg.fallback_url.empty();
    HttpResponse resp1;

    if (!shouldSkipTier1) {
        resp1 = executeHttpRequest(tier1Url, tier1Payload.dump(), tier1Auth, cfg.request_timeout, 1, "Primary Cloud");
        if (resp1.isSuccess()) {
            try {
                return parseOpenAIResponse(resp1.body);
            } catch (const std::exception& e) {
                NetworkLogger::logError(1, "Primary Cloud", 0, 200, std::string("Payload parse error: ") + e.what());
                lastErrorReason = std::string("Payload parse error: ") + e.what();
            }
        } else {
            lastErrorReason = resp1.getReasonString();
            if (!resp1.isTransient()) {
                // Non-transient client error (400, 401, 403, 404): abort immediately
                throw std::runtime_error("Tier 1 Cloud Client Error: " + resp1.getReasonString() + " - " + resp1.body.substr(0, 200));
            }
        }
    } else {
        lastErrorReason = "No local API key configured; delegating directly to Cloudflare CDN Relay";
    }

    // =========================================================================
    // Failover Transition (Tier 1 -> Tier 2 or Tier 3)
    // =========================================================================
    bool hasTier2 = !cfg.fallback_url.empty() && !isSameUrl(cfg.fallback_url, tier1Url);

    if (hasTier2) {
        NetworkLogger::logTransition(1, 2, lastErrorReason);
        reportStatus("Подключение к резервному серверу...");

        // =====================================================================
        // Tier 2: Backup VPS Relay (OpenAI-compatible)
        // =====================================================================
        std::string tier2Url = cfg.fallback_url;
        std::string tier2Key = !cfg.fallback_api_key.empty() ? cfg.fallback_api_key : cfg.openrouter_api_key;
        std::string tier2Auth = !tier2Key.empty() ? (_X("Authorization: Bearer ") + tier2Key) : "";
        std::string tier2Model = !cfg.fallback_model.empty() ? cfg.fallback_model : tier1Model;

        nlohmann::json tier2Payload = buildOpenAIPayload(prompt, imageBase64, tier2Model, cfg.system_prompt, history, 500);

        HttpResponse resp2 = executeHttpRequest(tier2Url, tier2Payload.dump(), tier2Auth, cfg.request_timeout, 2, "Backup VPS Relay");
        if (resp2.isSuccess()) {
            try {
                return parseOpenAIResponse(resp2.body);
            } catch (const std::exception& e) {
                NetworkLogger::logError(2, "Backup VPS Relay", 0, 200, std::string("Payload parse error: ") + e.what());
                lastErrorReason = std::string("Payload parse error: ") + e.what();
            }
        } else {
            lastErrorReason = resp2.getReasonString();
            if (!resp2.isTransient()) {
                throw std::runtime_error("Tier 2 VPS Relay Client Error: " + resp2.getReasonString() + " - " + resp2.body.substr(0, 200));
            }
        }
    }

    // =========================================================================
    // Failover Transition (Tier 2/1 -> Tier 3 or Exhausted)
    // =========================================================================
    int fromTier = hasTier2 ? 2 : 1;

    if (cfg.enable_local_fallback) {
        NetworkLogger::logTransition(fromTier, 3, lastErrorReason + (!hasTier2 ? " (Tier 2 skipped)" : ""));
        reportStatus("Переключение на локальный Ollama...");

        std::string baseUrl = cfg.ollama_url.empty() ? "http://127.0.0.1:11434" : cfg.ollama_url;
        while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
        std::string tier3Url = baseUrl + _X("/api/generate");

        std::string fullPrompt = buildUserPrompt(prompt);
        std::string tier3Model = !cfg.ollama_model.empty() ? cfg.ollama_model : "llava:latest";

        nlohmann::json tier3Payload = buildOllamaPayload(fullPrompt, imageBase64, tier3Model);

        HttpResponse resp3 = executeHttpRequest(tier3Url, tier3Payload.dump(), "", cfg.request_timeout, 3, "Local Offline Ollama");
        if (resp3.isSuccess()) {
            try {
                return parseOllamaResponse(resp3.body);
            } catch (const std::exception& e) {
                NetworkLogger::logError(3, "Local Offline Ollama", 0, 200, std::string("Payload parse error: ") + e.what());
                lastErrorReason = std::string("Payload parse error: ") + e.what();
            }
        } else {
            lastErrorReason = resp3.getReasonString();
        }

        // Tier 3 also failed: all tiers exhausted
        NetworkLogger::logTransition(3, 0, lastErrorReason + " (All cascade tiers exhausted)");
        throw std::runtime_error("All cascade tiers failed (Tier 1 Cloud, Tier 2 Relay, Tier 3 Ollama): " + lastErrorReason);
    } else {
        // Tier 3 disabled
        NetworkLogger::logTransition(fromTier, 0, lastErrorReason + " (Tier 3 disabled by enable_local_fallback=false)");
        throw std::runtime_error("All cascade tiers failed: Tier " + std::to_string(fromTier) + " failed (" + lastErrorReason + ") and local fallback is disabled");
    }
}

std::string VisionClient::analyze(const std::string& imageBase64, const std::string& userPrompt) {
    std::string prompt;
    std::string backend;
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        prompt = !userPrompt.empty() ? userPrompt : m_config.user_prompt;
        backend = m_config.backend;
    }

    std::string answer;
    if (backend == _X("ollama")) {
        answer = analyzeOllama(imageBase64, prompt);
    } else if (backend == _X("openrouter") || backend.empty()) {
        answer = executeCascade(imageBase64, prompt);
    } else {
        throw std::runtime_error("Unknown backend: " + backend);
    }

    if (m_pMemory) {
        m_pMemory->addTurn(prompt, imageBase64, answer);
    }
    return answer;
}

