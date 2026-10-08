#pragma once

#include <string>
#include <windows.h>

/**
 * @brief Thread-safe diagnostic logging engine for GhostAI network operations.
 *
 * Records connection attempts, WinHttp error codes, HTTP statuses, and gateway
 * failover transitions to network.log, and mirrors critical failovers to ghost_startup.log.
 */
class NetworkLogger {
public:
    // =========================================================================
    // Core Interface Contract (PROJECT.md)
    // =========================================================================

    /**
     * @brief Writes a general diagnostic message to network.log.
     */
    static void log(const std::string& message);

    /**
     * @brief Logs the start of a network connection attempt.
     * @param tier 1 = Primary Cloud, 2 = Backup VPS Relay, 3 = Local Offline Ollama
     * @param endpointName Descriptive name of the endpoint
     * @param url Target endpoint URL (automatically sanitized of credentials)
     */
    static void logAttempt(int tier, const std::string& endpointName, const std::string& url);

    /**
     * @brief Logs an error encountered during request execution.
     * @param tier 1 = Primary Cloud, 2 = Backup VPS Relay, 3 = Local Offline Ollama
     * @param endpointName Descriptive name of the endpoint
     * @param winHttpError GetLastError() WinHttp error code (e.g. 12029, 12002), or 0 if none
     * @param httpStatusCode HTTP status code from server response, or 0 if transport failed
     * @param detail Additional diagnostic context or error message
     */
    static void logError(int tier, const std::string& endpointName, DWORD winHttpError, int httpStatusCode, const std::string& detail);

    /**
     * @brief Logs a successful request resolution.
     * @param tier 1 = Primary Cloud, 2 = Backup VPS Relay, 3 = Local Offline Ollama
     * @param endpointName Descriptive name of the endpoint
     * @param elapsedMs Request duration in milliseconds
     */
    static void logSuccess(int tier, const std::string& endpointName, long long elapsedMs);

    /**
     * @brief Logs a failover transition between tiers.
     *        Mirrors this event to both network.log and ghost_startup.log.
     * @param fromTier Origin tier (e.g. 1)
     * @param toTier Destination tier (e.g. 2, or 0 if cascade exhausted)
     * @param reason Triggering cause (e.g. "WinHttp error 12029 (ERROR_WINHTTP_CANNOT_CONNECT)")
     */
    static void logTransition(int fromTier, int toTier, const std::string& reason);

    // =========================================================================
    // Configuration & Path Resolution
    // =========================================================================

    /**
     * @brief Sets custom directory where log files will be created.
     *        If empty, defaults to executable directory (portable mode) or current directory.
     */
    static void setLogDirectory(const std::string& dirPath);

    /**
     * @brief Returns currently configured log directory.
     */
    static std::string getLogDirectory();

    /**
     * @brief Returns full absolute path to network.log.
     */
    static std::string getNetworkLogPath();

    /**
     * @brief Returns full absolute path to ghost_startup.log.
     */
    static std::string getStartupLogPath();

    // =========================================================================
    // Diagnostic Formatting Helpers
    // =========================================================================

    /**
     * @brief Generates sub-millisecond timestamp string formatted as YYYY-MM-DD HH:MM:SS.mmm.
     */
    static std::string getTimestamp();

    /**
     * @brief Returns human-readable label for a tier index (e.g. "Tier 1: Primary Cloud").
     */
    static std::string getTierName(int tier);

    /**
     * @brief Translates WinHttp DWORD error code to symbolic name and description.
     */
    static std::string getWinHttpErrorString(DWORD errorCode);

    /**
     * @brief Translates HTTP status code to RFC description.
     */
    static std::string getHttpStatusString(int statusCode);

    /**
     * @brief Sanitizes URL by masking passwords in basic auth and sensitive query parameters.
     */
    static std::string sanitizeUrl(const std::string& url);

    /**
     * @brief Thread-safe startup step logger for main.cpp integration.
     */
    static void logStartupStep(const std::string& step);

private:
    static void appendToFile(const std::string& fullPath, const std::string& formattedLine);
    static std::string resolveDefaultLogDirectory();
};
