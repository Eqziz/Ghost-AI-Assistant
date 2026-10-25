#include "network_logger.hpp"
#include <fstream>
#include <sstream>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <winhttp.h>

namespace {
    std::recursive_mutex s_logMutex;
    std::string s_customLogDir;
    std::string s_lastEmittedTimestamp;

    std::wstring toWideUtf8(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        if (size <= 1) return L"";
        std::wstring w(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &w[0], size);
        return w;
    }

    std::string toUtf8Narrow(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (size <= 1) return "";
        std::string s(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &s[0], size, NULL, NULL);
        return s;
    }
}

std::string NetworkLogger::resolveDefaultLogDirectory() {
    wchar_t exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
        std::wstring dir(exePath);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            return toUtf8Narrow(dir.substr(0, slash + 1));
        }
    }
    return "";
}

void NetworkLogger::setLogDirectory(const std::string& dirPath) {
    std::lock_guard<std::recursive_mutex> lock(s_logMutex);
    s_customLogDir = dirPath;
    if (!s_customLogDir.empty() && s_customLogDir.back() != '\\' && s_customLogDir.back() != '/') {
        s_customLogDir += "\\";
    }
}

std::string NetworkLogger::getLogDirectory() {
    std::lock_guard<std::recursive_mutex> lock(s_logMutex);
    if (!s_customLogDir.empty()) {
        return s_customLogDir;
    }
    return resolveDefaultLogDirectory();
}

std::string NetworkLogger::getNetworkLogPath() {
    return getLogDirectory() + "network.log";
}

std::string NetworkLogger::getStartupLogPath() {
    return getLogDirectory() + "ghost_startup.log";
}

void NetworkLogger::appendToFile(const std::string& fullPath, const std::string& formattedLine) {
    std::lock_guard<std::recursive_mutex> lock(s_logMutex);
    std::wstring wPath = toWideUtf8(fullPath);
    std::ofstream ofs(wPath.c_str(), std::ios::app);
    if (ofs.is_open()) {
        ofs << formattedLine << "\n";
        ofs.flush();
    }
}

std::string NetworkLogger::getTimestamp() {
    std::lock_guard<std::recursive_mutex> lock(s_logMutex);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::string currentTs(buf);
    if (!s_lastEmittedTimestamp.empty() && currentTs < s_lastEmittedTimestamp) {
        return s_lastEmittedTimestamp;
    }
    s_lastEmittedTimestamp = currentTs;
    return currentTs;
}

std::string NetworkLogger::getTierName(int tier) {
    switch (tier) {
        case 1:  return "Tier 1: Primary Cloud";
        case 2:  return "Tier 2: Backup VPS Relay";
        case 3:  return "Tier 3: Local Offline Ollama";
        default: return "Tier " + std::to_string(tier) + ": Custom";
    }
}

std::string NetworkLogger::getWinHttpErrorString(DWORD errorCode) {
    switch (errorCode) {
        case 0:
            return "SUCCESS (0)";
        case ERROR_WINHTTP_OUT_OF_HANDLES:
            return "ERROR_WINHTTP_OUT_OF_HANDLES (12001)";
        case ERROR_WINHTTP_TIMEOUT:
            return "ERROR_WINHTTP_TIMEOUT (12002)";
        case ERROR_WINHTTP_INTERNAL_ERROR:
            return "ERROR_WINHTTP_INTERNAL_ERROR (12004)";
        case ERROR_WINHTTP_INVALID_URL:
            return "ERROR_WINHTTP_INVALID_URL (12005)";
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
            return "ERROR_WINHTTP_UNRECOGNIZED_SCHEME (12006)";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return "ERROR_WINHTTP_NAME_NOT_RESOLVED (12007)";
        case ERROR_WINHTTP_SHUTDOWN:
            return "ERROR_WINHTTP_SHUTDOWN (12012)";
        case ERROR_WINHTTP_LOGIN_FAILURE:
            return "ERROR_WINHTTP_LOGIN_FAILURE (12015)";
        case ERROR_WINHTTP_OPERATION_CANCELLED:
            return "ERROR_WINHTTP_OPERATION_CANCELLED (12017)";
        case ERROR_WINHTTP_INCORRECT_HANDLE_TYPE:
            return "ERROR_WINHTTP_INCORRECT_HANDLE_TYPE (12018)";
        case ERROR_WINHTTP_INCORRECT_HANDLE_STATE:
            return "ERROR_WINHTTP_INCORRECT_HANDLE_STATE (12019)";
        case ERROR_WINHTTP_CANNOT_CONNECT:
            return "ERROR_WINHTTP_CANNOT_CONNECT (12029)";
        case ERROR_WINHTTP_CONNECTION_ERROR:
            return "ERROR_WINHTTP_CONNECTION_ERROR (12030)";
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
            return "ERROR_WINHTTP_SECURE_CERT_DATE_INVALID (12037)";
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
            return "ERROR_WINHTTP_SECURE_CERT_CN_INVALID (12038)";
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
            return "ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED (12044)";
        case ERROR_WINHTTP_SECURE_INVALID_CA:
            return "ERROR_WINHTTP_SECURE_INVALID_CA (12045)";
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
            return "ERROR_WINHTTP_SECURE_CERT_REV_FAILED (12057)";
        case ERROR_WINHTTP_HEADER_NOT_FOUND:
            return "ERROR_WINHTTP_HEADER_NOT_FOUND (12150)";
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
            return "ERROR_WINHTTP_INVALID_SERVER_RESPONSE (12152)";
        case ERROR_WINHTTP_INVALID_HEADER:
            return "ERROR_WINHTTP_INVALID_HEADER (12153)";
        case ERROR_WINHTTP_INVALID_QUERY_REQUEST:
            return "ERROR_WINHTTP_INVALID_QUERY_REQUEST (12154)";
        case ERROR_WINHTTP_HEADER_ALREADY_EXISTS:
            return "ERROR_WINHTTP_HEADER_ALREADY_EXISTS (12155)";
        case ERROR_WINHTTP_REDIRECT_FAILED:
            return "ERROR_WINHTTP_REDIRECT_FAILED (12156)";
        case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
            return "ERROR_WINHTTP_SECURE_CHANNEL_ERROR (12157)";
        case ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT:
            return "ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT (12166)";
        case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT:
            return "ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT (12167)";
        case ERROR_WINHTTP_SECURE_FAILURE:
            return "ERROR_WINHTTP_SECURE_FAILURE (12175)";
        case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR:
            return "ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR (12178)";
        default:
            return "WINHTTP_ERROR (" + std::to_string(errorCode) + ")";
    }
}

std::string NetworkLogger::getHttpStatusString(int statusCode) {
    switch (statusCode) {
        case 0:   return "None / Transport Failure";
        case 200: return "200 OK";
        case 201: return "201 Created";
        case 204: return "204 No Content";
        case 301: return "301 Moved Permanently";
        case 302: return "302 Found / Redirect";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 407: return "407 Proxy Authentication Required";
        case 408: return "408 Request Timeout";
        case 429: return "429 Too Many Requests";
        case 500: return "500 Internal Server Error";
        case 502: return "502 Bad Gateway";
        case 503: return "503 Service Unavailable";
        case 504: return "504 Gateway Timeout";
        case 520: return "520 Origin Unknown Error";
        case 521: return "521 Origin Down";
        case 522: return "522 Origin Connection Timed Out";
        case 523: return "523 Origin Unreachable";
        case 524: return "524 Origin Timeout Occurred";
        default:  return "HTTP " + std::to_string(statusCode);
    }
}

std::string NetworkLogger::sanitizeUrl(const std::string& url) {
    if (url.empty()) return "";
    std::string result = url;

    // 1. Mask basic auth credentials: [scheme://][user:password@]host[:port][/path]
    size_t schemePos = result.find("://");
    size_t authStart = (schemePos != std::string::npos) ? schemePos + 3 : 0;

    size_t authEnd = result.find_first_of("/?#", authStart);
    if (authEnd == std::string::npos) {
        authEnd = result.length();
    }

    if (authEnd > authStart) {
        size_t atPos = result.rfind('@', authEnd - 1);
        if (atPos != std::string::npos && atPos >= authStart) {
            size_t colonPos = result.find(':', authStart);
            if (colonPos != std::string::npos && colonPos < atPos) {
                result.replace(colonPos + 1, atPos - (colonPos + 1), "***");
            }
        }
    }

    // 2. Mask sensitive query parameters (case-insensitive)
    size_t qPos = result.find('?');
    size_t paramStart = std::string::npos;
    if (qPos != std::string::npos) {
        paramStart = qPos + 1;
    } else if (result.find('=') != std::string::npos &&
               result.find('/') == std::string::npos &&
               result.find('@') == std::string::npos) {
        paramStart = 0;
    }

    if (paramStart != std::string::npos) {
        auto isSensitiveKey = [](const std::string& keyLower) -> bool {
            static const char* const s_keys[] = {
                "api_key", "apikey", "key", "token", "secret", "password",
                "access_token", "auth_token"
            };
            for (const char* k : s_keys) {
                if (keyLower == k) return true;
            }
            if (keyLower.length() > 4 && keyLower.compare(keyLower.length() - 4, 4, "_key") == 0) return true;
            if (keyLower.length() > 6 && keyLower.compare(keyLower.length() - 6, 6, "_token") == 0) return true;
            if (keyLower.length() > 7 && keyLower.compare(keyLower.length() - 7, 7, "_secret") == 0) return true;
            if (keyLower.length() > 9 && keyLower.compare(keyLower.length() - 9, 9, "_password") == 0) return true;
            return false;
        };

        while (paramStart < result.length() && result[paramStart] != '#') {
            size_t nextDelim = result.find_first_of("&#", paramStart);
            size_t paramEnd = (nextDelim != std::string::npos) ? nextDelim : result.length();

            size_t eqPos = result.find('=', paramStart);
            if (eqPos != std::string::npos && eqPos < paramEnd) {
                std::string paramName = result.substr(paramStart, eqPos - paramStart);
                std::string nameLower = paramName;
                for (char& c : nameLower) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }

                if (isSensitiveKey(nameLower)) {
                    size_t valStart = eqPos + 1;
                    size_t valLen = paramEnd - valStart;
                    result.replace(valStart, valLen, "***");
                    paramEnd = valStart + 3;
                }
            }

            if (paramEnd < result.length() && result[paramEnd] == '&') {
                paramStart = paramEnd + 1;
            } else {
                break;
            }
        }
    }

    return result;
}

void NetworkLogger::log(const std::string& message) {
    std::lock_guard<std::recursive_mutex> lock(s_logMutex);
    std::string line = "[" + getTimestamp() + "] [NET] " + message;
    appendToFile(getNetworkLogPath(), line);
}

void NetworkLogger::logAttempt(int tier, const std::string& endpointName, const std::string& url) {
    std::string sanitized = sanitizeUrl(url);
    std::string tag = "TIER-" + std::to_string(tier) + (!endpointName.empty() ? ": " + endpointName : "");

    std::lock_guard<std::recursive_mutex> lock(s_logMutex);
    std::string line = "[" + getTimestamp() + "] [" + tag + "] Connecting to " + sanitized;
    appendToFile(getNetworkLogPath(), line);
}

void NetworkLogger::logError(int tier, const std::string& endpointName, DWORD winHttpError, int httpStatusCode, const std::string& detail) {
    std::string errStr = (winHttpError != 0) ? getWinHttpErrorString(winHttpError) : "";
    std::string statusStr = (httpStatusCode != 0) ? getHttpStatusString(httpStatusCode) : "";
    std::ostringstream ss;
    ss << " [ERROR] [TIER-" << tier;
    if (!endpointName.empty()) {
        ss << ": " << endpointName;
    }
    ss << "] WinHttpError=" << winHttpError;
    if (winHttpError != 0) {
        ss << " (" << errStr << ")";
    }
    ss << " HTTP=" << httpStatusCode;
    if (httpStatusCode != 0) {
        ss << " (" << statusStr << ")";
    }
    if (!detail.empty()) {
        ss << " Detail: " << detail;
    }

    std::string body = ss.str();
    std::lock_guard<std::recursive_mutex> lock(s_logMutex);
    std::string line = "[" + getTimestamp() + "]" + body;
    appendToFile(getNetworkLogPath(), line);
}

void NetworkLogger::logSuccess(int tier, const std::string& endpointName, long long elapsedMs) {
    std::string tag = "TIER-" + std::to_string(tier) + (!endpointName.empty() ? ": " + endpointName : "");
    std::string body = " [SUCCESS] [" + tag + "] Request resolved successfully in " +
                       std::to_string(elapsedMs) + " ms";
    std::lock_guard<std::recursive_mutex> lock(s_logMutex);
    std::string line = "[" + getTimestamp() + "]" + body;
    appendToFile(getNetworkLogPath(), line);
}

void NetworkLogger::logTransition(int fromTier, int toTier, const std::string& reason) {
    std::string fromName = getTierName(fromTier);
    std::string toName = getTierName(toTier);
    std::lock_guard<std::recursive_mutex> lock(s_logMutex);
    std::string timestamp = getTimestamp();
    std::string netLine;
    std::string startupLine;

    if (toTier > 0) {
        std::string transTag = "TIER-" + std::to_string(fromTier) + " -> TIER-" + std::to_string(toTier);
        netLine = "[" + timestamp + "] [" + transTag + "] Failover from " + fromName +
                  " to " + toName + " - Reason: " + reason;
        startupLine = "[" + timestamp + "] [NETWORK_FAILOVER] [" + transTag + "] Failover from " +
                      fromName + " to " + toName + " - Reason: " + reason;
    } else {
        netLine = "[" + timestamp + "] [CASCADE_EXHAUSTED] [TIER-" + std::to_string(fromTier) + " -> EXHAUSTED] All tiers exhausted after " +
                  fromName + " - Reason: " + reason;
        startupLine = "[" + timestamp + "] [NETWORK_FAILURE] All tiers exhausted after " +
                      fromName + " - Reason: " + reason;
    }

    appendToFile(getNetworkLogPath(), netLine);
    appendToFile(getStartupLogPath(), startupLine);
}

void NetworkLogger::logStartupStep(const std::string& step) {
    std::string line = "[" + std::to_string(GetTickCount64()) + " ms] " + step;
    appendToFile(getStartupLogPath(), line);
}
