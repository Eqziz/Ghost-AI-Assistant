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

struct TestCheck {
    std::string name;
    bool passed;
    std::string details;
};

std::vector<TestCheck> g_checks;

void recordCheck(const std::string& name, bool passed, const std::string& details = "") {
    g_checks.push_back({name, passed, details});
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name;
    if (!details.empty()) {
        std::cout << " (" << details << ")";
    }
    std::cout << std::endl;
}

std::vector<std::string> readLines(const std::string& path) {
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

void cleanDir(const std::string& dir) {
    DeleteFileA((dir + "\\network.log").c_str());
    DeleteFileA((dir + "\\ghost_startup.log").c_str());
    RemoveDirectoryA(dir.c_str());
}

// -----------------------------------------------------------------------------
// Test A: Exhaustive URL Sanitization Matrix (40+ vectors)
// -----------------------------------------------------------------------------
void testExhaustiveUrlSanitization() {
    std::cout << "\n=== Test A: Exhaustive URL Sanitization Matrix ===" << std::endl;

    struct Vector {
        std::string input;
        std::string expected;
        std::string label;
    };

    std::vector<Vector> vectors = {
        // 1. Schemeless URLs
        {"admin:secret123@proxy:8080", "admin:***@proxy:8080", "Schemeless basic auth with port"},
        {"user:pass@host", "user:***@host", "Schemeless auth no port"},
        {"user:pass@proxy.example.com:8080/v1/chat", "user:***@proxy.example.com:8080/v1/chat", "Schemeless auth with path"},
        {":pass@host:8080", ":***@host:8080", "Schemeless empty username"},
        {"user:@host:8080", "user:***@host:8080", "Schemeless empty password"},
        {"proxy.lan:8080", "proxy.lan:8080", "Schemeless host:port no auth (must not mask host)"},
        {"user@proxy.lan:8080", "user@proxy.lan:8080", "Schemeless user-only no pass (must not mask port)"},
        {"proxy.lan:8080/path?query=val", "proxy.lan:8080/path?query=val", "Schemeless path and query no auth"},

        // 2. Standard Scheme URLs
        {"http://admin:secret123@proxy:8080", "http://admin:***@proxy:8080", "HTTP basic auth"},
        {"https://user:secret@proxy.org:443/chat", "https://user:***@proxy.org:443/chat", "HTTPS basic auth with path"},
        {"http://admin@proxy:8080", "http://admin@proxy:8080", "HTTP user without password"},
        {"http://:onlypass@proxy:8080", "http://:***@proxy:8080", "HTTP empty user with password"},
        {"http://user:@proxy:8080", "http://user:***@proxy:8080", "HTTP user with empty password"},
        {"http://localhost:11434/api/generate", "http://localhost:11434/api/generate", "Clean Ollama URL"},

        // 3. Passwords with '@' and special characters
        {"https://user:p@ssword!@relay:443", "https://user:***@relay:443", "Password with single @"},
        {"https://user:p@ss@word@@relay:443/v1", "https://user:***@relay:443/v1", "Password with multiple @ chars"},
        {"user:p@ssword!@relay:443", "user:***@relay:443", "Schemeless password with single @"},
        {"http://admin:p:a:s:s@host:8080", "http://admin:***@host:8080", "Password containing colons"},
        {"http://user:pass$word%^&*@host:8080", "http://user:***@host:8080", "Password with punctuation symbols"},

        // 4. Case-Insensitive Query Parameters
        {"https://api.openai.com/v1/chat?API_KEY=sk-upper12345&Token=myToken",
         "https://api.openai.com/v1/chat?API_KEY=***&Token=***", "Uppercase ?API_KEY= and ?Token="},
        {"https://api.openai.com/v1/chat?apiKey=123&Secret=456&PASSWORD=abc",
         "https://api.openai.com/v1/chat?apiKey=***&Secret=***&PASSWORD=***", "Mixed case apiKey, Secret, PASSWORD"},
        {"https://api.openai.com/v1/chat?access_token=foo&AUTH_TOKEN=bar",
         "https://api.openai.com/v1/chat?access_token=***&AUTH_TOKEN=***", "access_token and AUTH_TOKEN"},
        {"http://relay:443/v1?custom_key=secret1&my_secret=secret2&db_password=secret3",
         "http://relay:443/v1?custom_key=***&my_secret=***&db_password=***", "Key suffixes _key, _secret, _password"},
        {"http://example.com/v1?safe=hello&API_KEY=secret&other=world",
         "http://example.com/v1?safe=hello&API_KEY=***&other=world", "Safe parameters preserved around uppercase API_KEY"},
        {"http://example.com/v1?KEY=123#my-frag",
         "http://example.com/v1?KEY=***#my-frag", "Uppercase KEY with URI fragment"},
        {"http://example.com/v1?TOKEN=",
         "http://example.com/v1?TOKEN=***", "Empty sensitive token parameter value"},
        {"api_key=my_secret_token&verbose=1",
         "api_key=***&verbose=1", "Query-only string without question mark"},
        {"API_KEY=my_secret_token&SAFE=1",
         "API_KEY=***&SAFE=1", "Uppercase query-only string"},

        // 5. Combined Basic Auth and Query Parameters
        {"http://u:p@proxy:8080/v1?API_KEY=mykey&verbose=true",
         "http://u:***@proxy:8080/v1?API_KEY=***&verbose=true", "Basic auth + uppercase query param"},
        {"user:pass@proxy:8080/v1?token=xyz",
         "user:***@proxy:8080/v1?token=***", "Schemeless auth + query param"},

        // 6. IPv6 Addresses
        {"http://user:password@[::1]:8080/v1", "http://user:***@[::1]:8080/v1", "HTTP IPv6 loopback with credentials"},
        {"user:password@[2001:db8::1]:8080", "user:***@[2001:db8::1]:8080", "Schemeless IPv6 with credentials"},
        {"http://[::1]:8080/api", "http://[::1]:8080/api", "HTTP IPv6 loopback without credentials"},
        {"[2001:db8::1]:8080", "[2001:db8::1]:8080", "Schemeless IPv6 without credentials"},

        // 7. Edge Cases and Malformed Strings
        {"", "", "Empty string"},
        {"http://", "http://", "Scheme only"},
        {"://", "://", "Empty scheme delimiter"},
        {"http://user:pass", "http://user:pass", "No @ delimiter in scheme URL"},
        {"user:pass", "user:pass", "No @ delimiter in schemeless string"}
    };

    int passedCount = 0;
    for (const auto& v : vectors) {
        std::string actual = NetworkLogger::sanitizeUrl(v.input);
        if (actual == v.expected) {
            passedCount++;
        } else {
            std::cout << "  Mismatch in [" << v.label << "]:\n"
                      << "    Input:    " << v.input << "\n"
                      << "    Expected: " << v.expected << "\n"
                      << "    Actual:   " << actual << std::endl;
        }
    }

    recordCheck("URL Sanitization Exhaustive Matrix (" + std::to_string(vectors.size()) + " vectors)",
                passedCount == (int)vectors.size(),
                std::to_string(passedCount) + "/" + std::to_string(vectors.size()) + " passed");
}

// -----------------------------------------------------------------------------
// Test B: Ultra-High Concurrency Stress (32 threads, 32,000 entries) & Monotonicity
// -----------------------------------------------------------------------------
void testUltraHighConcurrencyStress(const std::string& testDir) {
    std::cout << "\n=== Test B: Ultra-High Concurrency Stress (32 Threads, 32,000 Operations) ===" << std::endl;
    NetworkLogger::setLogDirectory(testDir);

    std::string netPath = NetworkLogger::getNetworkLogPath();
    std::string startPath = NetworkLogger::getStartupLogPath();

    DeleteFileA(netPath.c_str());
    DeleteFileA(startPath.c_str());

    const int NUM_THREADS = 32;
    const int WRITES_PER_THREAD = 1000;
    const int TOTAL_WRITES = NUM_THREADS * WRITES_PER_THREAD;

    std::cout << "  Spawning " << NUM_THREADS << " threads, " << WRITES_PER_THREAD
              << " writes each (total " << TOTAL_WRITES << " writes)..." << std::endl;

    std::atomic<bool> startGate(false);
    std::atomic<int> completedThreads(0);
    std::atomic<int> expectedTransitions(0);

    auto worker = [&](int tid) {
        while (!startGate.load()) {
            std::this_thread::yield();
        }

        for (int i = 0; i < WRITES_PER_THREAD; ++i) {
            int op = (tid + i) % 5;
            switch (op) {
                case 0:
                    NetworkLogger::log("Worker " + std::to_string(tid) + " op " + std::to_string(i));
                    break;
                case 1:
                    NetworkLogger::logAttempt(1, "PrimaryCloud", "https://api.openai.com/v1/chat?API_KEY=sk-test" + std::to_string(i));
                    break;
                case 2:
                    NetworkLogger::logError(2, "BackupRelay", 12029, 502, "Error details " + std::to_string(i));
                    break;
                case 3:
                    NetworkLogger::logSuccess(3, "LocalOllama", 35);
                    break;
                case 4:
                    NetworkLogger::logTransition(1, 2, "Failover event " + std::to_string(tid) + ":" + std::to_string(i));
                    expectedTransitions++;
                    break;
            }
        }
        completedThreads++;
    };

    auto tStart = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back(worker, t);
    }

    startGate.store(true);

    for (auto& th : threads) {
        th.join();
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    double durationSec = std::chrono::duration<double>(tEnd - tStart).count();

    std::cout << "  Concurrent workload completed in " << durationSec << " s ("
              << (TOTAL_WRITES / durationSec) << " writes/sec)." << std::endl;

    auto netLines = readLines(netPath);
    auto startLines = readLines(startPath);

    int netCount = (int)netLines.size();
    int startCount = (int)startLines.size();
    int transExp = expectedTransitions.load();

    std::cout << "  Actual network.log lines: " << netCount << " (expected " << TOTAL_WRITES << ")" << std::endl;
    std::cout << "  Actual ghost_startup.log lines: " << startCount << " (expected " << transExp << ")" << std::endl;

    recordCheck("Zero dropped lines in network.log (32 threads)", netCount == TOTAL_WRITES,
                std::to_string(netCount) + "/" + std::to_string(TOTAL_WRITES));

    recordCheck("Exact mirrored transition count in ghost_startup.log", startCount == transExp,
                std::to_string(startCount) + "/" + std::to_string(transExp));

    // Verify line integrity
    std::regex linePattern(R"(^\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] .+$)");
    int corruptCount = 0;
    for (const auto& l : netLines) {
        if (!std::regex_match(l, linePattern) || l.find('\0') != std::string::npos) {
            corruptCount++;
        }
    }
    recordCheck("Zero torn or corrupted lines across 32,000 entries", corruptCount == 0,
                corruptCount == 0 ? "100% integrity" : std::to_string(corruptCount) + " corrupted");

    // Monotonicity verification
    int inversions = 0;
    std::string prevTs = "";
    for (const auto& l : netLines) {
        if (l.size() >= 24 && l[0] == '[') {
            std::string currTs = l.substr(1, 23);
            if (!prevTs.empty() && currTs < prevTs) {
                inversions++;
            }
            prevTs = currTs;
        }
    }
    std::cout << "  Observed timestamp inversions: " << inversions << std::endl;
    recordCheck("Timestamp monotonicity check (0 inversions out of 32,000 entries)", inversions == 0,
                std::to_string(inversions) + " inversions");

    // Verify zero plain secrets leaked in network.log
    bool secretLeaked = false;
    for (const auto& l : netLines) {
        if (l.find("sk-test") != std::string::npos) {
            secretLeaked = true;
            break;
        }
    }
    recordCheck("Zero unmasked query secrets leaked during concurrent logging", !secretLeaked,
                secretLeaked ? "LEAKED" : "All masked");
}

// -----------------------------------------------------------------------------
// Test C: Dual-File Atomic Consistency & Timestamp Matching
// -----------------------------------------------------------------------------
void testDualFileAtomicConsistency(const std::string& testDir) {
    std::cout << "\n=== Test C: Dual-File Atomic Consistency & Timestamp Matching ===" << std::endl;
    NetworkLogger::setLogDirectory(testDir);

    std::string netPath = NetworkLogger::getNetworkLogPath();
    std::string startPath = NetworkLogger::getStartupLogPath();

    DeleteFileA(netPath.c_str());
    DeleteFileA(startPath.c_str());

    // 1. Routine log should not touch ghost_startup.log
    NetworkLogger::log("Routine diagnostic entry");
    auto sLines = readLines(startPath);
    recordCheck("Routine log does not touch ghost_startup.log", sLines.empty(),
                sLines.empty() ? "Clean" : "Leaked");

    // 2. Failover transition
    std::string reason1 = "Primary Cloud HTTP 500 error";
    NetworkLogger::logTransition(1, 2, reason1);

    // 3. Cascade exhaustion
    std::string reason2 = "Local Ollama 127.0.0.1:11434 connection refused";
    NetworkLogger::logTransition(3, 0, reason2);

    auto netLines = readLines(netPath);
    auto startLines = readLines(startPath);

    recordCheck("ghost_startup.log contains exactly 2 transitions", startLines.size() == 2,
                std::to_string(startLines.size()) + " lines");

    // Verify exact timestamp match between network.log and ghost_startup.log for transition 1
    bool ts1Match = false;
    bool ts2Match = false;

    if (startLines.size() == 2 && netLines.size() >= 3) {
        // Find transition lines in netLines
        std::string netTrans1Ts, netTrans2Ts;
        for (const auto& nl : netLines) {
            if (nl.find("TIER-1 -> TIER-2") != std::string::npos && nl.size() >= 24) {
                netTrans1Ts = nl.substr(1, 23);
            }
            if (nl.find("CASCADE_EXHAUSTED") != std::string::npos && nl.size() >= 24) {
                netTrans2Ts = nl.substr(1, 23);
            }
        }

        std::string startTrans1Ts = (startLines[0].size() >= 24) ? startLines[0].substr(1, 23) : "";
        std::string startTrans2Ts = (startLines[1].size() >= 24) ? startLines[1].substr(1, 23) : "";

        ts1Match = (!netTrans1Ts.empty() && netTrans1Ts == startTrans1Ts);
        ts2Match = (!netTrans2Ts.empty() && netTrans2Ts == startTrans2Ts);
    }

    recordCheck("Transition 1 timestamp identical across both files", ts1Match,
                ts1Match ? "Exact match" : "Mismatch");
    recordCheck("Transition 2 timestamp identical across both files", ts2Match,
                ts2Match ? "Exact match" : "Mismatch");
    recordCheck("Failover tag [NETWORK_FAILOVER] in startup log",
                startLines.size() >= 1 && startLines[0].find("[NETWORK_FAILOVER]") != std::string::npos);
    recordCheck("Exhaustion tag [NETWORK_FAILURE] in startup log",
                startLines.size() >= 2 && startLines[1].find("[NETWORK_FAILURE]") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Test D: Error and Status Code Translation Helpers
// -----------------------------------------------------------------------------
void testTranslations() {
    std::cout << "\n=== Test D: Translation Helpers ===" << std::endl;

    // WinHttp Error codes
    bool winHttpValid = true;
    winHttpValid &= (NetworkLogger::getWinHttpErrorString(0) == "SUCCESS (0)");
    winHttpValid &= (NetworkLogger::getWinHttpErrorString(12001).find("12001") != std::string::npos);
    winHttpValid &= (NetworkLogger::getWinHttpErrorString(12002).find("12002") != std::string::npos);
    winHttpValid &= (NetworkLogger::getWinHttpErrorString(12029).find("12029") != std::string::npos);
    winHttpValid &= (NetworkLogger::getWinHttpErrorString(12030).find("12030") != std::string::npos);
    winHttpValid &= (NetworkLogger::getWinHttpErrorString(12038).find("12038") != std::string::npos);
    winHttpValid &= (NetworkLogger::getWinHttpErrorString(88888) == "WINHTTP_ERROR (88888)");
    recordCheck("WinHttp error code translation completeness", winHttpValid);

    // HTTP Status codes
    bool httpValid = true;
    httpValid &= (NetworkLogger::getHttpStatusString(0) == "None / Transport Failure");
    httpValid &= (NetworkLogger::getHttpStatusString(200) == "200 OK");
    httpValid &= (NetworkLogger::getHttpStatusString(401) == "401 Unauthorized");
    httpValid &= (NetworkLogger::getHttpStatusString(407) == "407 Proxy Authentication Required");
    httpValid &= (NetworkLogger::getHttpStatusString(500) == "500 Internal Server Error");
    httpValid &= (NetworkLogger::getHttpStatusString(502) == "502 Bad Gateway");
    httpValid &= (NetworkLogger::getHttpStatusString(504) == "504 Gateway Timeout");
    httpValid &= (NetworkLogger::getHttpStatusString(999) == "HTTP 999");
    recordCheck("HTTP status code translation completeness", httpValid);

    // Tier Names
    bool tierValid = true;
    tierValid &= (NetworkLogger::getTierName(1) == "Tier 1: Primary Cloud");
    tierValid &= (NetworkLogger::getTierName(2) == "Tier 2: Backup VPS Relay");
    tierValid &= (NetworkLogger::getTierName(3) == "Tier 3: Local Offline Ollama");
    tierValid &= (NetworkLogger::getTierName(4) == "Tier 4: Custom");
    recordCheck("Tier name mapping completeness", tierValid);
}

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "  Adversarial Challenge Suite: Milestone 1 Remediation  " << std::endl;
    std::cout << "=========================================================" << std::endl;

    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string testDir = std::string(tempPath) + "ghost_adv_test_" + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(testDir.c_str(), NULL);

    testExhaustiveUrlSanitization();
    testUltraHighConcurrencyStress(testDir);
    testDualFileAtomicConsistency(testDir);
    testTranslations();

    cleanDir(testDir);

    std::cout << "\n=========================================================" << std::endl;
    std::cout << "  Adversarial Challenge Results Summary" << std::endl;
    std::cout << "=========================================================" << std::endl;

    int passed = 0;
    int failed = 0;
    for (const auto& c : g_checks) {
        if (c.passed) passed++;
        else failed++;
    }

    std::cout << "Total Checks: " << g_checks.size()
              << " | Passed: " << passed
              << " | Failed: " << failed << std::endl;

    return failed == 0 ? 0 : 1;
}
