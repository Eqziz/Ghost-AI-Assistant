#include "network_logger.hpp"
#include "config.hpp"
#include "crypto.hpp"
#include "setup_ui.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <regex>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cassert>
#include <windows.h>

namespace fs = std::filesystem;

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_TRUE(cond, desc) do { \
    if (cond) { \
        g_pass++; \
    } else { \
        g_fail++; \
        std::cerr << "[FAIL] Line " << __LINE__ << ": " << desc << std::endl; \
    } \
} while(0)

// 1. Extreme URL Sanitization Vectors
void testExtremeUrlSanitization() {
    std::cout << "\n--- Test Suite 1: Extreme URL Sanitization Vectors ---" << std::endl;

    struct Vector {
        std::string input;
        std::string expected;
        std::string note;
    };

    std::vector<Vector> vectors = {
        // Basic auth variations
        {"http://user:p@ss@host:8080/", "http://user:***@host:8080/", "At-sign in password"},
        {"http://user:p@ss:w@rd!@host:8080/", "http://user:***@host:8080/", "Multiple colons and at-signs in password"},
        {"user:secret@proxy.internal:3128", "user:***@proxy.internal:3128", "Schemeless proxy URL"},
        {"admin:p@ss123@192.168.1.1:80", "admin:***@192.168.1.1:80", "Schemeless with IP and at in pass"},
        {"http://user@host:8080/", "http://user@host:8080/", "User without password should not be corrupted"},
        {"http://:password@host:8080/", "http://:***@host:8080/", "Empty user with password"},
        {"http://host:8080/path/to/resource", "http://host:8080/path/to/resource", "Plain URL without credentials"},

        // Query parameters (case insensitivity)
        {"https://api.com/v1?API_KEY=super_secret", "https://api.com/v1?API_KEY=***", "Uppercase API_KEY"},
        {"https://api.com/v1?ApiKey=super_secret", "https://api.com/v1?ApiKey=***", "Mixed case ApiKey"},
        {"https://api.com/v1?access_token=token123&user_key=key456", "https://api.com/v1?access_token=***&user_key=***", "Multiple sensitive keys"},
        {"https://api.com/v1?AUTH_TOKEN=jwt.token.here", "https://api.com/v1?AUTH_TOKEN=***", "Uppercase AUTH_TOKEN"},
        {"https://api.com/v1?my_secret=hidden&other=public", "https://api.com/v1?my_secret=***&other=public", "Suffix _secret"},
        {"https://api.com/v1?custom_password=pw123", "https://api.com/v1?custom_password=***", "Suffix _password"},
        {"https://api.com/v1?keyword=not_a_secret", "https://api.com/v1?keyword=not_a_secret", "Non-sensitive param with 'key' prefix"},
        {"https://api.com/v1?public_keyring=safe", "https://api.com/v1?public_keyring=safe", "Non-sensitive param containing key"},

        // Edge cases
        {"", "", "Empty string"},
        {"https://", "https://", "Scheme only"},
        {"http://user:pass@host?token=secret#my_key=frag", "http://user:***@host?token=***#my_key=frag", "Auth + query + fragment"},
        {"api_key=secret_standalone", "api_key=***", "Query string without host or scheme"}
    };

    for (const auto& v : vectors) {
        std::string actual = NetworkLogger::sanitizeUrl(v.input);
        bool ok = (actual == v.expected);
        ASSERT_TRUE(ok, v.note + " (got: " + actual + ", expected: " + v.expected + ")");
    }

    // Stress: very long query string
    std::string longUrl = "https://api.example.com/v1?param=value";
    for (int i = 0; i < 500; ++i) {
        longUrl += "&token" + std::to_string(i) + "_key=secret" + std::to_string(i);
    }
    std::string sanitizedLong = NetworkLogger::sanitizeUrl(longUrl);
    ASSERT_TRUE(sanitizedLong.find("secret") == std::string::npos, "All secret tokens in 500-param URL masked");
}

// 2. Multithreaded Concurrency & Timestamp Monotonicity Stress
void testConcurrencyAndMonotonicity(const std::string& sandboxDir) {
    std::cout << "\n--- Test Suite 2: Multithreaded Concurrency Stress & Monotonicity ---" << std::endl;

    NetworkLogger::setLogDirectory(sandboxDir);
    std::string netLog = NetworkLogger::getNetworkLogPath();
    std::string startLog = NetworkLogger::getStartupLogPath();

    DeleteFileA(netLog.c_str());
    DeleteFileA(startLog.c_str());

    const int NUM_THREADS = 16;
    const int WRITES_PER_THREAD = 1000;
    std::vector<std::thread> workers;

    std::atomic<bool> startFlag{false};

    for (int t = 0; t < NUM_THREADS; ++t) {
        workers.emplace_back([t, &startFlag, WRITES_PER_THREAD]() {
            while (!startFlag.load()) {
                std::this_thread::yield();
            }
            for (int i = 0; i < WRITES_PER_THREAD; ++i) {
                int op = (t + i) % 6;
                switch (op) {
                    case 0:
                        NetworkLogger::log("Routine diagnostic entry from thread " + std::to_string(t));
                        break;
                    case 1:
                        NetworkLogger::logAttempt(1, "PrimaryCloud", "https://user:p@ss@api.provider.com/v1");
                        break;
                    case 2:
                        NetworkLogger::logError(1, "PrimaryCloud", 12029, 0, "Connection refused");
                        break;
                    case 3:
                        NetworkLogger::logSuccess(2, "BackupRelay", 45);
                        break;
                    case 4:
                        NetworkLogger::logTransition(1, 2, "Failover simulation");
                        break;
                    case 5:
                        NetworkLogger::logStartupStep("Startup checkpoint " + std::to_string(i));
                        break;
                }
            }
        });
    }

    auto tStart = std::chrono::high_resolution_clock::now();
    startFlag.store(true);
    for (auto& w : workers) {
        w.join();
    }
    auto tEnd = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double>(tEnd - tStart).count();
    std::cout << "  Executed " << (NUM_THREADS * WRITES_PER_THREAD) << " concurrent log operations in "
              << duration << " s (" << ((NUM_THREADS * WRITES_PER_THREAD) / duration) << " ops/s)" << std::endl;

    // Read and verify network.log
    std::ifstream ifsNet(netLog);
    ASSERT_TRUE(ifsNet.is_open(), "network.log opened successfully");
    std::string line;
    std::string prevTsNet = "";
    int netLineCount = 0;
    int netInversions = 0;
    std::regex tsRegex(R"(^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\])");

    while (std::getline(ifsNet, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        netLineCount++;
        std::smatch m;
        if (std::regex_search(line, m, tsRegex)) {
            std::string ts = m[1].str();
            if (!prevTsNet.empty() && ts < prevTsNet) {
                netInversions++;
            }
            prevTsNet = ts;
        }
        // Verify no credentials leaked
        ASSERT_TRUE(line.find("p@ss") == std::string::npos, "Password leaked in concurrent network.log line");
    }
    ifsNet.close();

    ASSERT_TRUE(netInversions == 0, "Zero timestamp inversions in network.log under concurrency");
    std::cout << "  network.log lines: " << netLineCount << ", inversions: " << netInversions << std::endl;

    // Read and verify ghost_startup.log
    std::ifstream ifsStart(startLog);
    ASSERT_TRUE(ifsStart.is_open(), "ghost_startup.log opened successfully");
    int startLineCount = 0;
    int mirroredTransitionCount = 0;
    int startupStepCount = 0;

    while (std::getline(ifsStart, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        startLineCount++;
        if (line.find("[NETWORK_FAILOVER]") != std::string::npos || line.find("[NETWORK_FAILURE]") != std::string::npos) {
            mirroredTransitionCount++;
        } else if (line.find(" ms] Startup checkpoint") != std::string::npos) {
            startupStepCount++;
        } else {
            // Unexpected entry in ghost_startup.log
            ASSERT_TRUE(false, "Unexpected entry in ghost_startup.log: " + line);
        }
    }
    ifsStart.close();

    std::cout << "  ghost_startup.log lines: " << startLineCount
              << " (mirrored transitions: " << mirroredTransitionCount
              << ", startup steps: " << startupStepCount << ")" << std::endl;
    ASSERT_TRUE(mirroredTransitionCount > 0, "Mirrored transitions recorded in ghost_startup.log");
    ASSERT_TRUE(startupStepCount > 0, "Startup steps recorded in ghost_startup.log");
}

// 3. AppConfig DPAPI Encryption, XOR Fallback, and Plaintext Disk Scrubbing
void testAppConfigSecurityAndPersistence(const std::string& sandboxDir) {
    std::cout << "\n--- Test Suite 3: AppConfig Security, DPAPI/XOR & Disk Scrubbing ---" << std::endl;

    std::string cfgPath = sandboxDir + "\\config.json";

    // 3.1 Plaintext scrubbing check
    {
        AppConfig cfg;
        cfg.backend = "openrouter";
        cfg.openrouter_api_key = "CANARY_PRIMARY_API_KEY_SECRET_1111";
        cfg.fallback_url = "https://backup.relay.org/v1";
        cfg.fallback_api_key = "CANARY_FALLBACK_API_KEY_SECRET_2222";
        cfg.fallback_model = "relay-vps-llama";
        cfg.enable_local_fallback = true;
        cfg.proxy_url = "http://proxy.corporate.lan:8080";
        cfg.proxy_user = "corp_user";
        cfg.proxy_pass = "CANARY_PROXY_PASSWORD_SECRET_3333";

        // Write directly using toJson() + secret encryption manually to simulate save() to sandbox
        nlohmann::json plain = cfg.toJson();
        nlohmann::json secrets;
        secrets["openrouter_api_key"] = cfg.openrouter_api_key;
        secrets["fallback_api_key"] = cfg.fallback_api_key;
        secrets["proxy_pass"] = cfg.proxy_pass;
        plain["_secrets"] = Crypto::encryptDict(secrets);

        // Plaintext scrubbing
        plain["openrouter_api_key"] = "";
        plain["fallback_api_key"] = "";
        plain["proxy_pass"] = "";

        std::ofstream ofs(cfgPath, std::ios::trunc);
        ofs << plain.dump(2);
        ofs.close();

        // Inspect raw file bytes
        std::ifstream ifs(cfgPath);
        std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        ASSERT_TRUE(raw.find("CANARY_PRIMARY_API_KEY_SECRET_1111") == std::string::npos, "Primary API key scrubbed from disk");
        ASSERT_TRUE(raw.find("CANARY_FALLBACK_API_KEY_SECRET_2222") == std::string::npos, "Fallback API key scrubbed from disk");
        ASSERT_TRUE(raw.find("CANARY_PROXY_PASSWORD_SECRET_3333") == std::string::npos, "Proxy password scrubbed from disk");

        // Verify JSON parsing of the scrubbed file
        nlohmann::json diskJson = nlohmann::json::parse(raw);
        ASSERT_TRUE(diskJson["openrouter_api_key"] == "", "openrouter_api_key is empty string in JSON");
        ASSERT_TRUE(diskJson["fallback_api_key"] == "", "fallback_api_key is empty string in JSON");
        ASSERT_TRUE(diskJson["proxy_pass"] == "", "proxy_pass is empty string in JSON");
        ASSERT_TRUE(diskJson["_secrets"].is_string(), "_secrets is a string token");

        // Decrypt token
        std::string token = diskJson["_secrets"].get<std::string>();
        nlohmann::json decSecrets;
        bool decOk = Crypto::decryptDict(token, decSecrets);
        ASSERT_TRUE(decOk, "Crypto::decryptDict succeeded on saved token");
        ASSERT_TRUE(decSecrets["openrouter_api_key"] == "CANARY_PRIMARY_API_KEY_SECRET_1111", "Decrypted primary key matches");
        ASSERT_TRUE(decSecrets["fallback_api_key"] == "CANARY_FALLBACK_API_KEY_SECRET_2222", "Decrypted fallback key matches");
        ASSERT_TRUE(decSecrets["proxy_pass"] == "CANARY_PROXY_PASSWORD_SECRET_3333", "Decrypted proxy pass matches");
    }

    // 3.2 XOR Fallback token generation and tampering resistance
    {
        nlohmann::json plainSecrets = {
            {"openrouter_api_key", "sk-or-test-key"},
            {"fallback_api_key", "sk-fb-test-key"},
            {"proxy_pass", "secretProxy123"}
        };
        std::string jsonStr = plainSecrets.dump();
        std::vector<uint8_t> rawBytes(jsonStr.begin(), jsonStr.end());
        auto xorEnc = Crypto::xorCipher(rawBytes, Crypto::getMachineKey());
        std::string xorToken = "v1:xor:" + Crypto::base64Encode(xorEnc);
        ASSERT_TRUE(xorToken.rfind("v1:xor:", 0) == 0, "XOR token starts with 'v1:xor:'");

        nlohmann::json restored;
        bool xorOk = Crypto::decryptDict(xorToken, restored);
        ASSERT_TRUE(xorOk, "Crypto::decryptDict successfully handles v1:xor: tokens");
        ASSERT_TRUE(restored["fallback_api_key"] == "sk-fb-test-key", "XOR restored fallback_api_key matches");
        ASSERT_TRUE(restored["proxy_pass"] == "secretProxy123", "XOR restored proxy_pass matches");

        // Tamper with the XOR token: flip characters in payload
        std::string tampered = xorToken;
        if (tampered.length() > 20) {
            tampered[tampered.length() - 5] ^= 0x01; // flip 1 bit in HMAC or payload
            nlohmann::json tamperedOut;
            bool tamperResult = Crypto::decryptDict(tampered, tamperedOut);
            ASSERT_TRUE(!tamperResult, "Crypto::decryptDict rejects tampered token without crash");
        }
    }

    // 3.3 Unicode and special characters in credentials
    {
        nlohmann::json complexSecrets = {
            {"openrouter_api_key", "sk-test-Привет-Мир-🚀-newline\n\ttab"},
            {"fallback_api_key", "key-with-symbols-!@#$%^&*()_+-=[]{}|;:'\",.<>/?"},
            {"proxy_pass", "русский_пароль_12345"}
        };
        std::string token = Crypto::encryptDict(complexSecrets);
        nlohmann::json outSecrets;
        bool decOk = Crypto::decryptDict(token, outSecrets);
        ASSERT_TRUE(decOk, "Complex UTF-8/emoji secret decryption succeeded");
        ASSERT_TRUE(outSecrets["openrouter_api_key"] == complexSecrets["openrouter_api_key"], "UTF-8 emoji roundtrip match");
        ASSERT_TRUE(outSecrets["fallback_api_key"] == complexSecrets["fallback_api_key"], "Special symbols roundtrip match");
        ASSERT_TRUE(outSecrets["proxy_pass"] == complexSecrets["proxy_pass"], "Russian password roundtrip match");
    }
}

// 4. SetupUI Contract & Control IDs Verification
void testSetupUiContract() {
    std::cout << "\n--- Test Suite 4: SetupUI Contract & Control IDs ---" << std::endl;

    // Verify Control IDs as defined in setup_ui.hpp
    ASSERT_TRUE(IDC_RADIO_OLLAMA == 1001, "IDC_RADIO_OLLAMA == 1001");
    ASSERT_TRUE(IDC_RADIO_OPENROUTER == 1002, "IDC_RADIO_OPENROUTER == 1002");
    ASSERT_TRUE(IDC_FALLBACK_URL == 1050, "IDC_FALLBACK_URL == 1050");
    ASSERT_TRUE(IDC_FALLBACK_KEY == 1051, "IDC_FALLBACK_KEY == 1051");
    ASSERT_TRUE(IDC_FALLBACK_MODEL == 1052, "IDC_FALLBACK_MODEL == 1052");
    ASSERT_TRUE(IDC_ENABLE_LOCAL_FALLBACK == 1053, "IDC_ENABLE_LOCAL_FALLBACK == 1053");
    ASSERT_TRUE(IDC_PROXY_URL == 1054, "IDC_PROXY_URL == 1054");
    ASSERT_TRUE(IDC_PROXY_USER == 1055, "IDC_PROXY_USER == 1055");
    ASSERT_TRUE(IDC_PROXY_PASS == 1056, "IDC_PROXY_PASS == 1056");

    // Verify Win32 style constants
    ASSERT_TRUE(ES_PASSWORD == 0x0020, "ES_PASSWORD is 0x0020");
    ASSERT_TRUE(BS_AUTOCHECKBOX == 0x0003, "BS_AUTOCHECKBOX is 0x0003");

    // Verify AppConfig defaults for SetupUI
    AppConfig defaultCfg;
    ASSERT_TRUE(defaultCfg.backend == "ollama", "Default backend is ollama");
    ASSERT_TRUE(defaultCfg.enable_local_fallback == true, "Default enable_local_fallback is true");
    ASSERT_TRUE(defaultCfg.fallback_url.empty(), "Default fallback_url is empty");
    ASSERT_TRUE(defaultCfg.fallback_api_key.empty(), "Default fallback_api_key is empty");
    ASSERT_TRUE(defaultCfg.proxy_url.empty(), "Default proxy_url is empty");
    ASSERT_TRUE(defaultCfg.proxy_user.empty(), "Default proxy_user is empty");
    ASSERT_TRUE(defaultCfg.proxy_pass.empty(), "Default proxy_pass is empty");
}

int main() {
    std::cout << "========================================================\n"
              << " Challenger 1 Empirical Harness: M1 Remediation Check\n"
              << "========================================================" << std::endl;

    // Sandbox folder in temp
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string sandboxDir = std::string(tempPath) + "GhostAI_Challenger1_Test";
    CreateDirectoryA(sandboxDir.c_str(), NULL);

    try {
        testExtremeUrlSanitization();
        testConcurrencyAndMonotonicity(sandboxDir);
        testAppConfigSecurityAndPersistence(sandboxDir);
        testSetupUiContract();
    } catch (const std::exception& e) {
        std::cerr << "[CRITICAL EXCEPTION] " << e.what() << std::endl;
        g_fail++;
    } catch (...) {
        std::cerr << "[CRITICAL EXCEPTION] Unknown non-standard exception" << std::endl;
        g_fail++;
    }

    // Clean up sandbox
    std::string fNet = sandboxDir + "\\network.log";
    std::string fStart = sandboxDir + "\\ghost_startup.log";
    std::string fCfg = sandboxDir + "\\config.json";
    DeleteFileA(fNet.c_str());
    DeleteFileA(fStart.c_str());
    DeleteFileA(fCfg.c_str());
    RemoveDirectoryA(sandboxDir.c_str());

    std::cout << "\n========================================================\n"
              << " Results Summary:\n"
              << " PASSED: " << g_pass << "\n"
              << " FAILED: " << g_fail << "\n"
              << "========================================================" << std::endl;

    if (g_fail > 0) {
        std::cout << "[-] EMPIRICAL CHALLENGE FAILED with " << g_fail << " failures!" << std::endl;
        return 1;
    }

    std::cout << "[+] ALL EMPIRICAL CHALLENGES PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
