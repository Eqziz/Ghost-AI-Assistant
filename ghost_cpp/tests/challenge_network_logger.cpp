#include "network_logger.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <regex>
#include <fstream>
#include <sstream>
#include <cassert>
#include <windows.h>

struct TestResult {
    std::string testName;
    bool passed;
    std::string details;
};

std::vector<TestResult> g_results;

void recordResult(const std::string& name, bool passed, const std::string& details) {
    g_results.push_back({name, passed, details});
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name;
    if (!details.empty()) {
        std::cout << " (" << details << ")";
    }
    std::cout << std::endl;
}

// Helper to read file lines
std::vector<std::string> readFileLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return lines;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

// Helper to remove directory recursively
void cleanDirectory(const std::string& dir) {
    std::string netLog = dir + "\\network.log";
    std::string startLog = dir + "\\ghost_startup.log";
    DeleteFileA(netLog.c_str());
    DeleteFileA(startLog.c_str());
    RemoveDirectoryA(dir.c_str());
}

// -----------------------------------------------------------------------------
// Test 1: URL Sanitization Matrix
// -----------------------------------------------------------------------------
void testUrlSanitization() {
    std::cout << "\n=== Running Test 1: URL Sanitization Matrix ===" << std::endl;

    struct TestCase {
        std::string input;
        std::string expected;
        std::string description;
    };

    std::vector<TestCase> cases = {
        {
            "http://admin:secret123@proxy:8080",
            "http://admin:***@proxy:8080",
            "Standard HTTP basic auth"
        },
        {
            "https://user:p@ssword!@relay.example.com:443/v1/chat",
            "https://user:***@relay.example.com:443/v1/chat",
            "HTTPS with special chars in password"
        },
        {
            "http://admin@proxy:8080",
            "http://admin@proxy:8080",
            "HTTP user without password (should not mask host:port)"
        },
        {
            "http://:onlypass@proxy:8080",
            "http://:***@proxy:8080",
            "HTTP empty user with password"
        },
        {
            "https://openrouter.ai/api/v1/chat/completions?api_key=sk-or-v1-abcdef123456",
            "https://openrouter.ai/api/v1/chat/completions?api_key=***",
            "Query param api_key masked"
        },
        {
            "http://127.0.0.1:8080/query?token=secret_token&other=123",
            "http://127.0.0.1:8080/query?token=***&other=123",
            "Query param token masked with subsequent param"
        },
        {
            "http://proxy:8080/v1?key=val1&secret=val2&password=val3",
            "http://proxy:8080/v1?key=***&secret=***&password=***",
            "Multiple sensitive query params (key, secret, password)"
        },
        {
            "http://u:p@proxy:8080/v1?api_key=mykey&verbose=true",
            "http://u:***@proxy:8080/v1?api_key=***&verbose=true",
            "Combined basic auth and query param"
        },
        {
            "",
            "",
            "Empty URL handling"
        },
        {
            "http://localhost:11434/api/generate",
            "http://localhost:11434/api/generate",
            "Normal URL without credentials preserved intact"
        }
    };

    int passedCount = 0;
    for (const auto& c : cases) {
        std::string actual = NetworkLogger::sanitizeUrl(c.input);
        bool match = (actual == c.expected);
        if (match) {
            passedCount++;
        } else {
            std::cout << "  Mismatch in [" << c.description << "]:\n    Input:    "
                      << c.input << "\n    Expected: " << c.expected
                      << "\n    Actual:   " << actual << std::endl;
        }
    }

    recordResult("URL Sanitization Matrix (10 test vectors)", passedCount == (int)cases.size(),
                 std::to_string(passedCount) + "/" + std::to_string(cases.size()) + " passed");

    // Adversarial Challenge: What if proxy URL has NO scheme, e.g. "admin:pass@proxy:8080"?
    std::string schemeless = "admin:secret123@proxy:8080";
    std::string sanitizedSchemeless = NetworkLogger::sanitizeUrl(schemeless);
    bool schemelessMasked = (sanitizedSchemeless.find("secret123") == std::string::npos);
    recordResult("Adversarial: Schemeless proxy URL (admin:pass@host:port)", schemelessMasked,
                 schemelessMasked ? "Masked" : ("Leaked plain password: " + sanitizedSchemeless));
}

// -----------------------------------------------------------------------------
// Test 2: Timestamp Format Precision & Range Validation
// -----------------------------------------------------------------------------
void testTimestampFormat() {
    std::cout << "\n=== Running Test 2: Timestamp Format & Validation ===" << std::endl;
    std::regex tsRegex(R"(^(\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2})\.(\d{3})$)");

    bool allMatch = true;
    for (int i = 0; i < 50; ++i) {
        std::string ts = NetworkLogger::getTimestamp();
        std::smatch m;
        if (!std::regex_match(ts, m, tsRegex)) {
            allMatch = false;
            std::cout << "  Invalid timestamp format: " << ts << std::endl;
            break;
        }
        int year = std::stoi(m[1].str());
        int month = std::stoi(m[2].str());
        int day = std::stoi(m[3].str());
        int hour = std::stoi(m[4].str());
        int min = std::stoi(m[5].str());
        int sec = std::stoi(m[6].str());
        int ms = std::stoi(m[7].str());

        if (year < 2024 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31 ||
            hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59 || ms < 0 || ms > 999) {
            allMatch = false;
            std::cout << "  Timestamp values out of calendar bounds: " << ts << std::endl;
            break;
        }
    }
    recordResult("Timestamp Format & Bounds Validation (50 samples)", allMatch,
                 allMatch ? "Matches [YYYY-MM-DD HH:MM:SS.mmm]" : "Format error");
}

// -----------------------------------------------------------------------------
// Test 3: Dual-File Output Mirroring
// -----------------------------------------------------------------------------
void testDualFileMirroring(const std::string& testDir) {
    std::cout << "\n=== Running Test 3: Dual-File Output Mirroring ===" << std::endl;
    NetworkLogger::setLogDirectory(testDir);

    std::string netPath = NetworkLogger::getNetworkLogPath();
    std::string startPath = NetworkLogger::getStartupLogPath();

    // Clean prior logs
    DeleteFileA(netPath.c_str());
    DeleteFileA(startPath.c_str());

    // 1. Regular log calls should only affect network.log
    NetworkLogger::log("Routine diagnostic message");
    NetworkLogger::logAttempt(1, "PrimaryCloud", "https://api.openai.com/v1");
    NetworkLogger::logError(1, "PrimaryCloud", 12029, 0, "Connection refused");
    NetworkLogger::logSuccess(2, "BackupRelay", 145);

    auto startupLinesBefore = readFileLines(startPath);
    bool nonTransitionsIsolated = startupLinesBefore.empty();
    recordResult("Routine logs isolated from ghost_startup.log", nonTransitionsIsolated,
                 nonTransitionsIsolated ? "ghost_startup.log clean" : "Leaked routine logs to startup log");

    // 2. Failover transition (Tier 1 -> Tier 2)
    NetworkLogger::logTransition(1, 2, "WinHttp error 12029 (ERROR_WINHTTP_CANNOT_CONNECT)");

    // 3. Failover transition (Tier 2 -> Tier 3)
    NetworkLogger::logTransition(2, 3, "HTTP 502 Bad Gateway");

    // 4. Cascade exhaustion (Tier 3 -> Tier 0)
    NetworkLogger::logTransition(3, 0, "Local Ollama service not responding");

    auto netLines = readFileLines(netPath);
    auto startLines = readFileLines(startPath);

    bool failover1Found = false;
    bool failover2Found = false;
    bool failureFound = false;

    for (const auto& line : startLines) {
        if (line.find("[NETWORK_FAILOVER]") != std::string::npos &&
            line.find("TIER-1 -> TIER-2") != std::string::npos) {
            failover1Found = true;
        }
        if (line.find("[NETWORK_FAILOVER]") != std::string::npos &&
            line.find("TIER-2 -> TIER-3") != std::string::npos) {
            failover2Found = true;
        }
        if (line.find("[NETWORK_FAILURE]") != std::string::npos &&
            line.find("All tiers exhausted") != std::string::npos) {
            failureFound = true;
        }
    }

    bool allTransitionsMirrored = (failover1Found && failover2Found && failureFound);
    recordResult("Dual-file mirroring: [NETWORK_FAILOVER] & [NETWORK_FAILURE]", allTransitionsMirrored,
                 "Found all 3 mirrored transitions in ghost_startup.log");

    // Verify network.log also has all corresponding lines
    bool netHasAll = true;
    for (const auto& tag : {"TIER-1 -> TIER-2", "TIER-2 -> TIER-3", "CASCADE_EXHAUSTED"}) {
        bool found = false;
        for (const auto& line : netLines) {
            if (line.find(tag) != std::string::npos) {
                found = true;
                break;
            }
        }
        if (!found) {
            netHasAll = false;
            break;
        }
    }
    recordResult("Dual-file consistency: network.log contains all transitions", netHasAll,
                 netHasAll ? "network.log matches" : "network.log missing transitions");
}

// -----------------------------------------------------------------------------
// Test 4: Multithreaded Concurrency Stress & Monotonicity
// -----------------------------------------------------------------------------
void testMultithreadedConcurrencyStress(const std::string& testDir) {
    std::cout << "\n=== Running Test 4: Multithreaded Concurrency Stress ===" << std::endl;
    NetworkLogger::setLogDirectory(testDir);

    std::string netPath = NetworkLogger::getNetworkLogPath();
    std::string startPath = NetworkLogger::getStartupLogPath();

    DeleteFileA(netPath.c_str());
    DeleteFileA(startPath.c_str());

    const int NUM_THREADS = 16;
    const int WRITES_PER_THREAD = 1000;
    const int EXPECTED_TOTAL_WRITES = NUM_THREADS * WRITES_PER_THREAD;

    std::cout << "  Spawning " << NUM_THREADS << " threads, " << WRITES_PER_THREAD
              << " writes each (total " << EXPECTED_TOTAL_WRITES << " writes)..." << std::endl;

    std::atomic<bool> startFlag(false);
    std::atomic<int> completedThreads(0);
    std::atomic<int> transitionCount(0);

    auto workerFunc = [&](int threadId) {
        while (!startFlag.load()) {
            std::this_thread::yield();
        }

        for (int i = 0; i < WRITES_PER_THREAD; ++i) {
            int op = (threadId + i) % 5;
            switch (op) {
                case 0:
                    NetworkLogger::log("Thread " + std::to_string(threadId) + " event " + std::to_string(i));
                    break;
                case 1:
                    NetworkLogger::logAttempt(1, "Cloud", "http://user:pass@127.0.0.1:8080/v1/chat?k=v");
                    break;
                case 2:
                    NetworkLogger::logError(2, "Relay", 12029, 502, "Detail " + std::to_string(i));
                    break;
                case 3:
                    NetworkLogger::logSuccess(3, "Ollama", 42);
                    break;
                case 4:
                    NetworkLogger::logTransition(1, 2, "Failover test " + std::to_string(i));
                    transitionCount++;
                    break;
            }
        }
        completedThreads++;
    };

    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back(workerFunc, t);
    }

    // Release threads simultaneously
    startFlag.store(true);

    for (auto& t : threads) {
        t.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double elapsedSec = std::chrono::duration<double>(endTime - startTime).count();

    std::cout << "  Stress completed in " << elapsedSec << " seconds ("
              << (EXPECTED_TOTAL_WRITES / elapsedSec) << " writes/sec)." << std::endl;

    // Verify file contents
    auto netLines = readFileLines(netPath);
    auto startLines = readFileLines(startPath);

    int actualNetLines = (int)netLines.size();
    int actualStartLines = (int)startLines.size();
    int expectedTransitions = transitionCount.load();

    std::cout << "  Actual network.log lines: " << actualNetLines
              << " (expected " << EXPECTED_TOTAL_WRITES << ")" << std::endl;
    std::cout << "  Actual ghost_startup.log lines: " << actualStartLines
              << " (expected " << expectedTransitions << ")" << std::endl;

    bool zeroDroppedNet = (actualNetLines == EXPECTED_TOTAL_WRITES);
    recordResult("Concurrency Stress: Zero dropped lines in network.log", zeroDroppedNet,
                 std::to_string(actualNetLines) + "/" + std::to_string(EXPECTED_TOTAL_WRITES));

    bool zeroDroppedStartup = (actualStartLines == expectedTransitions);
    recordResult("Concurrency Stress: Exact mirroring count in ghost_startup.log", zeroDroppedStartup,
                 std::to_string(actualStartLines) + "/" + std::to_string(expectedTransitions));

    // Verify integrity of every line in network.log (no tearing, no nulls, proper timestamp prefix)
    std::regex logLineRegex(R"(^\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] .+$)");
    int corruptLines = 0;
    for (const auto& line : netLines) {
        if (!std::regex_match(line, logLineRegex) || line.find('\0') != std::string::npos) {
            corruptLines++;
        }
    }
    recordResult("Concurrency Stress: Line integrity (zero torn or corrupted lines)", corruptLines == 0,
                 corruptLines == 0 ? "100% integrity" : (std::to_string(corruptLines) + " corrupted lines"));

    // Check Timestamp Monotonicity across network.log
    // We check if timestamps ever invert (i.e. line[k] timestamp < line[k-1] timestamp)
    int inversions = 0;
    std::string prevTs = "";
    for (size_t i = 0; i < netLines.size(); ++i) {
        if (netLines[i].size() >= 24 && netLines[i][0] == '[') {
            std::string currTs = netLines[i].substr(1, 23); // "YYYY-MM-DD HH:MM:SS.mmm"
            if (!prevTs.empty()) {
                if (currTs < prevTs) {
                    inversions++;
                }
            }
            prevTs = currTs;
        }
    }
    std::cout << "  Timestamp inversions observed under concurrent logging: " << inversions << std::endl;
    recordResult("Timestamp Monotonicity check under concurrency", inversions == 0,
                 std::to_string(inversions) + " inversions out of " + std::to_string(netLines.size()) + " entries");
}

// -----------------------------------------------------------------------------
// Test 5: Error and Status Code Translation Helpers
// -----------------------------------------------------------------------------
void testHelperTranslations() {
    std::cout << "\n=== Running Test 5: Helper Translations ===" << std::endl;

    // WinHttp errors
    bool winHttpOk = true;
    winHttpOk &= (NetworkLogger::getWinHttpErrorString(12029).find("12029") != std::string::npos);
    winHttpOk &= (NetworkLogger::getWinHttpErrorString(12002).find("12002") != std::string::npos);
    winHttpOk &= (NetworkLogger::getWinHttpErrorString(12030).find("12030") != std::string::npos);
    winHttpOk &= (NetworkLogger::getWinHttpErrorString(0).find("SUCCESS") != std::string::npos);
    winHttpOk &= (NetworkLogger::getWinHttpErrorString(99999).find("99999") != std::string::npos);
    recordResult("WinHttp error code translation", winHttpOk, "12029, 12002, 12030, 0, 99999 verified");

    // HTTP status codes
    bool httpOk = true;
    httpOk &= (NetworkLogger::getHttpStatusString(200).find("200 OK") != std::string::npos);
    httpOk &= (NetworkLogger::getHttpStatusString(407).find("Proxy Authentication") != std::string::npos);
    httpOk &= (NetworkLogger::getHttpStatusString(502).find("502 Bad Gateway") != std::string::npos);
    httpOk &= (NetworkLogger::getHttpStatusString(0).find("Transport Failure") != std::string::npos);
    recordResult("HTTP status code translation", httpOk, "200, 407, 502, 0 verified");

    // Tier names
    bool tierOk = true;
    tierOk &= (NetworkLogger::getTierName(1).find("Tier 1: Primary Cloud") != std::string::npos);
    tierOk &= (NetworkLogger::getTierName(2).find("Tier 2: Backup VPS Relay") != std::string::npos);
    tierOk &= (NetworkLogger::getTierName(3).find("Tier 3: Local Offline Ollama") != std::string::npos);
    tierOk &= (NetworkLogger::getTierName(4).find("Tier 4: Custom") != std::string::npos);
    recordResult("Tier name mapping", tierOk, "Tiers 1, 2, 3, 4 verified");
}

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "  Challenger 2 Empirical Verification: NetworkLogger" << std::endl;
    std::cout << "=========================================================" << std::endl;

    // Use a test directory inside the build folder
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string testDir = std::string(tempPath) + "ghost_test_logs_" + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(testDir.c_str(), NULL);

    testUrlSanitization();
    testTimestampFormat();
    testDualFileMirroring(testDir);
    testMultithreadedConcurrencyStress(testDir);
    testHelperTranslations();

    cleanDirectory(testDir);

    std::cout << "\n=========================================================" << std::endl;
    std::cout << "  Summary of Empirical Test Results" << std::endl;
    std::cout << "=========================================================" << std::endl;
    int passCount = 0;
    int failCount = 0;
    for (const auto& r : g_results) {
        if (r.passed) passCount++;
        else failCount++;
    }
    std::cout << "Total Tests: " << g_results.size()
              << " | Passed: " << passCount
              << " | Failed: " << failCount << std::endl;

    return failCount == 0 ? 0 : 1;
}
