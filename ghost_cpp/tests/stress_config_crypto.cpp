#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <sstream>
#include <filesystem>
#include <random>
#include <windows.h>

#include "config.hpp"
#include "crypto.hpp"
#include "network_logger.hpp"

namespace fs = std::filesystem;

static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            g_failCount++; \
        } else { \
            g_passCount++; \
        } \
    } while (0)

#define TEST_SECTION(name) \
    std::cout << "\n========================================\n" \
              << "RUNNING: " << name << "\n" \
              << "========================================" << std::endl;

// =========================================================================
// Suite 1: Crypto & Base64 Primitives Stress Tests
// =========================================================================
void runCryptoPrimitivesSuite() {
    TEST_SECTION("Suite 1: Crypto & Base64 Primitives");

    // 1.1 Base64 encode / decode roundtrip on variable lengths (0 to 1024 bytes)
    for (size_t len = 0; len <= 1024; ++len) {
        std::vector<uint8_t> original(len);
        for (size_t i = 0; i < len; ++i) {
            original[i] = static_cast<uint8_t>((i * 31 + 17) & 0xFF);
        }
        std::string encoded = Crypto::base64Encode(original);
        std::vector<uint8_t> decoded = Crypto::base64Decode(encoded);
        TEST_ASSERT(original == decoded, "Base64 roundtrip failed at length " + std::to_string(len));
    }

    // 1.2 Base64 with all 256 byte values
    {
        std::vector<uint8_t> allBytes(256);
        for (int i = 0; i < 256; ++i) allBytes[i] = static_cast<uint8_t>(i);
        std::string enc = Crypto::base64Encode(allBytes);
        std::vector<uint8_t> dec = Crypto::base64Decode(enc);
        TEST_ASSERT(allBytes == dec, "Base64 all 256 byte values roundtrip failed");
    }

    // 1.3 URL-safe Base64 decode
    {
        // '-' replaced by '+', '_' replaced by '/'
        std::string stdB64 = "ab+c/d==";
        std::string urlB64 = "ab-c_d";
        auto decStd = Crypto::base64Decode(stdB64);
        auto decUrl = Crypto::urlsafeBase64Decode(urlB64);
        TEST_ASSERT(decStd == decUrl, "URL-safe Base64 decode mismatch");
    }

    // 1.4 SHA256 deterministic verification
    {
        // SHA256 of empty string: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
        std::string emptySha = Crypto::sha256Hex("");
        TEST_ASSERT(emptySha == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA256 empty string hex mismatch: " + emptySha);

        // SHA256 of "GhostAI"
        std::string ghostSha = Crypto::sha256Hex("GhostAI");
        TEST_ASSERT(!ghostSha.empty() && ghostSha.length() == 64, "SHA256 hex length != 64");
    }

    // 1.5 HMAC-SHA256
    {
        std::vector<uint8_t> key = { 's', 'e', 'c', 'r', 'e', 't' };
        std::string hmac = Crypto::hmacSha256Hex(key, "GhostAI HMAC Message");
        TEST_ASSERT(!hmac.empty() && hmac.length() == 64, "HMAC-SHA256 hex length != 64");
    }

    // 1.6 XOR cipher roundtrip
    {
        std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0xAA, 0xBB, 0xCC };
        std::vector<uint8_t> key = { 0x5A, 0xCF, 0x33 };
        auto enc = Crypto::xorCipher(data, key);
        auto dec = Crypto::xorCipher(enc, key);
        TEST_ASSERT(data == dec, "XOR cipher roundtrip failed");

        // Empty key edge case
        auto noKey = Crypto::xorCipher(data, {});
        TEST_ASSERT(noKey == data, "XOR with empty key must return original data");
    }

    // 1.7 Machine key stability
    {
        auto k1 = Crypto::getMachineKey();
        auto k2 = Crypto::getMachineKey();
        TEST_ASSERT(k1 == k2 && k1.size() == 32, "Machine key is not stable or not 32 bytes");
    }

    // 1.8 DPAPI direct roundtrip
    {
        std::string plain = "SensitiveDPAPIPayload_12345!@#$%^&*()";
        std::vector<uint8_t> in(plain.begin(), plain.end());
        std::vector<uint8_t> enc;
        bool encOk = Crypto::dpapiEncrypt(in, enc);
        TEST_ASSERT(encOk, "dpapiEncrypt returned false");
        TEST_ASSERT(enc != in, "Ciphertext equals plaintext in DPAPI!");

        std::vector<uint8_t> dec;
        bool decOk = Crypto::dpapiDecrypt(enc, dec);
        TEST_ASSERT(decOk, "dpapiDecrypt returned false");
        std::string decryptedStr(dec.begin(), dec.end());
        TEST_ASSERT(decryptedStr == plain, "DPAPI decrypted string mismatch");
    }

    // 1.9 DPAPI corrupted ciphertext handling
    {
        std::vector<uint8_t> garbage = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33 };
        std::vector<uint8_t> dec;
        bool decOk = Crypto::dpapiDecrypt(garbage, dec);
        TEST_ASSERT(!decOk, "dpapiDecrypt succeeded on random garbage bytes!");
    }
}

// =========================================================================
// Suite 2: Crypto Dictionary Token Serialization & Deserialization
// =========================================================================
void runCryptoDictTokenSuite() {
    TEST_SECTION("Suite 2: Crypto Dictionary Tokens");

    // 2.1 Normal encryptDict / decryptDict roundtrip
    {
        nlohmann::json d;
        d["openrouter_api_key"] = "sk-or-v1-abcdef1234567890";
        d["fallback_api_key"] = "fb-relay-token-secret-999";
        d["proxy_pass"] = "SuperP@ssw0rd!#$";

        std::string token = Crypto::encryptDict(d);
        TEST_ASSERT(!token.empty(), "encryptDict returned empty token");
        TEST_ASSERT(token.rfind("v1:dpapi:", 0) == 0 || token.rfind("v1:xor:", 0) == 0,
            "Token does not start with v1:dpapi: or v1:xor: token=" + token.substr(0, 20));

        nlohmann::json out;
        bool ok = Crypto::decryptDict(token, out);
        TEST_ASSERT(ok, "decryptDict returned false for valid token");
        TEST_ASSERT(out["openrouter_api_key"] == "sk-or-v1-abcdef1234567890", "openrouter_api_key mismatch");
        TEST_ASSERT(out["fallback_api_key"] == "fb-relay-token-secret-999", "fallback_api_key mismatch");
        TEST_ASSERT(out["proxy_pass"] == "SuperP@ssw0rd!#$", "proxy_pass mismatch");
    }

    // 2.2 Token with special Unicode / Cyrillic / Emojis
    {
        nlohmann::json d;
        d["openrouter_api_key"] = "Ключ_OpenRouter_Русский_123";
        d["fallback_api_key"] = "🔑Pass_with_emojis_🛡️🚀";
        d["proxy_pass"] = "日本語_パスワード_\"Quotes\"_\\Backslash\\_\nNewline";

        std::string token = Crypto::encryptDict(d);
        nlohmann::json out;
        bool ok = Crypto::decryptDict(token, out);
        TEST_ASSERT(ok, "decryptDict failed on Unicode/Emoji dictionary");
        TEST_ASSERT(out["openrouter_api_key"] == "Ключ_OpenRouter_Русский_123", "Cyrillic key mismatch");
        TEST_ASSERT(out["fallback_api_key"] == "🔑Pass_with_emojis_🛡️🚀", "Emoji key mismatch");
        TEST_ASSERT(out["proxy_pass"] == "日本語_パスワード_\"Quotes\"_\\Backslash\\_\nNewline", "Japanese/escape key mismatch");
    }

    // 2.3 Malformed token string stress
    const std::vector<std::string> malformedTokens = {
        "",
        "invalid_token_no_colons",
        "v1",
        "v1:",
        "v1:dpapi",
        "v1:dpapi:",
        "v1:xor:",
        "v2:dpapi:AQAAANCMnd8BFd...",
        "v1:unknown_algo:AQAAANCMnd8BFd...",
        "v1:dpapi:!@#$%^&*()not_base64",
        "v1:xor:!@#$%^&*()not_base64",
        "v1:dpapi:AQAAANCMnd8BFd(invalid)",
        ":dpapi:payload",
        "::payload"
    };

    for (const auto& mt : malformedTokens) {
        nlohmann::json out;
        bool ok = Crypto::decryptDict(mt, out);
        TEST_ASSERT(!ok, "decryptDict unexpectedly succeeded on malformed token: " + mt);
    }

    // 2.4 Corrupted ciphertext in v1:dpapi token
    {
        nlohmann::json d;
        d["key"] = "value";
        std::string token = Crypto::encryptDict(d);
        if (token.rfind("v1:dpapi:", 0) == 0) {
            std::string payloadOrig = token.substr(9);
            std::string payload = payloadOrig;
            // Flip characters near the middle of payload (where ciphertext lives)
            size_t mid = payload.length() / 2;
            payload[mid] = (payload[mid] == 'A') ? 'B' : 'A';
            payload[mid + 1] = (payload[mid + 1] == 'A') ? 'B' : 'A';
            payload[payload.length() - 5] = (payload[payload.length() - 5] == 'A') ? 'B' : 'A';
            std::string corruptedToken = "v1:dpapi:" + payload;
            nlohmann::json out;
            bool ok = Crypto::decryptDict(corruptedToken, out);
            std::cout << "[DEBUG 2.4] middle-flip: ok=" << (ok ? "true" : "false")
                      << " payload_len=" << payload.length()
                      << " out=" << out.dump() << std::endl;
            TEST_ASSERT(!ok, "decryptDict succeeded on bit-flipped DPAPI payload!");
        }
    }

    // 2.5 Corrupted ciphertext in v1:xor token
    {
        std::vector<uint8_t> garbage = { 0xFF, 0xFE, 0xFD, 0xFC, 0x01, 0x02, 0x03 };
        std::string badB64 = Crypto::base64Encode(garbage);
        std::string xorToken = "v1:xor:" + badB64;
        nlohmann::json out;
        bool ok = Crypto::decryptDict(xorToken, out);
        TEST_ASSERT(!ok, "decryptDict succeeded on invalid JSON bytes in XOR method!");
    }

    // 2.6 Explicit v1:xor: token roundtrip
    {
        nlohmann::json d = {{"fallback_api_key", "xor_secret_123"}, {"proxy_pass", "xor_pass_456"}};
        std::string jsonStr = d.dump();
        std::vector<uint8_t> raw(jsonStr.begin(), jsonStr.end());
        auto xorEnc = Crypto::xorCipher(raw, Crypto::getMachineKey());
        std::string xorToken = "v1:xor:" + Crypto::base64Encode(xorEnc);

        nlohmann::json out;
        bool ok = Crypto::decryptDict(xorToken, out);
        TEST_ASSERT(ok, "decryptDict failed on explicit v1:xor: token");
        TEST_ASSERT(out["fallback_api_key"] == "xor_secret_123", "v1:xor fallback_api_key mismatch");
        TEST_ASSERT(out["proxy_pass"] == "xor_pass_456", "v1:xor proxy_pass mismatch");
    }

    // 2.7 Decrypt real-world token from user profile config
    {
        std::string realToken = "v1:dpapi:AQAAANCMnd8BFdERjHoAwE/Cl+sBAAAAu2PhozpBFUqOb++dyERsrAAAAAACAAAAAAAQZgAAAAEAACAAAAAMKbVQrcuoW8lDjPso25JeuHa2L9gI40LhtIp4oc0IJgAAAAAOgAAAAAIAACAAAADdhea7cTJ68P3eJPp3Svt/rXsWo7cMj5Zz8WSP1JEj2HAAAABrip8GxvbYKGsHZn2yhzVKltG3+Txb8lnAcKYRm+rWO286h2FueClkD946ot0KvtaY4RjoGe+GIWdi5xAY0AfF3/E7GkhbO+ydptiTD4eSLOe2+4G1D99jhs2Y+0uqolWXC/7Uy8n2giJ/el+gRLomQAAAAIfNJREq81eldDbzJVWqXehkXkkAe8CQx4QDOnMLf9JXP9NXcJmCMjaXroQ9BBcrUQsXlw3CPmZenaLoEB0poWU=";
        nlohmann::json out;
        bool ok = Crypto::decryptDict(realToken, out);
        std::cout << "[INFO] Real-world user token decryption: " << (ok ? "SUCCESS (current user profile)" : "DPAPI_DIFFERENT_PROFILE") << std::endl;
        if (ok) {
            TEST_ASSERT(out.contains("openrouter_api_key"), "Real token missing openrouter_api_key");
        }
    }
}

// =========================================================================
// Suite 3: AppConfig Schema & JSON Deserialization Stress Tests
// =========================================================================
void runConfigSchemaSuite() {
    TEST_SECTION("Suite 3: AppConfig Schema & Deserialization");

    // 3.1 Verify struct default constructor values
    {
        AppConfig cfg;
        TEST_ASSERT(cfg.fallback_url == "", "Default fallback_url must be empty");
        TEST_ASSERT(cfg.fallback_api_key == "", "Default fallback_api_key must be empty");
        TEST_ASSERT(cfg.fallback_model == "", "Default fallback_model must be empty");
        TEST_ASSERT(cfg.enable_local_fallback == true, "Default enable_local_fallback must be true");
        TEST_ASSERT(cfg.proxy_url == "", "Default proxy_url must be empty");
        TEST_ASSERT(cfg.proxy_user == "", "Default proxy_user must be empty");
        TEST_ASSERT(cfg.proxy_pass == "", "Default proxy_pass must be empty");
    }

    // 3.2 Legacy v0 JSON compatibility: completely missing all 7 new fields
    {
        nlohmann::json legacyV0 = {
            {"backend", "openrouter"},
            {"ollama_url", "http://localhost:11434"},
            {"openrouter_model", "meta-llama/llama-3.2-11b-vision-instruct:free"},
            {"system_prompt", "Legacy prompt"},
            {"alpha_default", 0.75}
        };

        AppConfig cfg = AppConfig::fromJson(legacyV0);
        TEST_ASSERT(cfg.backend == "openrouter", "Legacy v0 backend mismatch");
        TEST_ASSERT(cfg.system_prompt == "Legacy prompt", "Legacy v0 system_prompt mismatch");
        TEST_ASSERT(cfg.alpha_default == 0.75, "Legacy v0 alpha_default mismatch");

        // Verify resilient defaults remain intact
        TEST_ASSERT(cfg.fallback_url == "", "Legacy v0 missing fallback_url did not default");
        TEST_ASSERT(cfg.fallback_api_key == "", "Legacy v0 missing fallback_api_key did not default");
        TEST_ASSERT(cfg.fallback_model == "", "Legacy v0 missing fallback_model did not default");
        TEST_ASSERT(cfg.enable_local_fallback == true, "Legacy v0 missing enable_local_fallback did not default to true");
        TEST_ASSERT(cfg.proxy_url == "", "Legacy v0 missing proxy_url did not default");
        TEST_ASSERT(cfg.proxy_user == "", "Legacy v0 missing proxy_user did not default");
        TEST_ASSERT(cfg.proxy_pass == "", "Legacy v0 missing proxy_pass did not default");
    }

    // 3.3 Empty JSON object {}
    {
        nlohmann::json emptyJ = nlohmann::json::object();
        AppConfig cfg = AppConfig::fromJson(emptyJ);
        TEST_ASSERT(cfg.backend == "ollama", "Empty JSON backend default mismatch");
        TEST_ASSERT(cfg.enable_local_fallback == true, "Empty JSON enable_local_fallback default mismatch");
        TEST_ASSERT(cfg.fallback_url == "", "Empty JSON fallback_url default mismatch");
    }

    // 3.4 Type confusion / boundary inputs in JSON
    {
        nlohmann::json corruptedFields = {
            {"fallback_url", 12345},                   // int instead of string
            {"fallback_api_key", nlohmann::json::array()}, // array instead of string
            {"enable_local_fallback", "false"},        // string instead of boolean
            {"proxy_url", nullptr},                    // null instead of string
            {"proxy_user", false},                     // bool instead of string
            {"proxy_pass", 99.99},                     // float instead of string
            {"max_context_pairs", "ten"},              // string instead of int
            {"window_alpha", "transparent"},           // string instead of double
            {"hotkeys", "not_an_object"}               // string instead of object
        };

        // fromJson must not throw exception, should safely skip malformed fields and retain defaults
        AppConfig cfg = AppConfig::fromJson(corruptedFields);
        TEST_ASSERT(cfg.fallback_url == "", "corrupted int fallback_url must retain default empty string");
        TEST_ASSERT(cfg.fallback_api_key == "", "corrupted array fallback_api_key must retain default empty string");
        TEST_ASSERT(cfg.enable_local_fallback == true, "corrupted string enable_local_fallback must retain default true");
        TEST_ASSERT(cfg.proxy_url == "", "corrupted null proxy_url must retain default empty string");
        TEST_ASSERT(cfg.proxy_user == "", "corrupted bool proxy_user must retain default empty string");
        TEST_ASSERT(cfg.proxy_pass == "", "corrupted float proxy_pass must retain default empty string");
        TEST_ASSERT(cfg.max_context_pairs == 10, "corrupted max_context_pairs must retain default 10");
        TEST_ASSERT(cfg.window_alpha == 0.5, "corrupted window_alpha must retain default 0.5");
    }

    // 3.5 Full serialization / deserialization roundtrip
    {
        AppConfig orig;
        orig.backend = "openrouter";
        orig.fallback_url = "https://relay.vps.example.com/v1/chat/completions";
        orig.fallback_api_key = "fb-key-123";
        orig.fallback_model = "relay-llama-3";
        orig.enable_local_fallback = false;
        orig.proxy_url = "http://10.1.2.3:8080";
        orig.proxy_user = "admin";
        orig.proxy_pass = "secretPass123";

        nlohmann::json j = orig.toJson();
        AppConfig restored = AppConfig::fromJson(j);

        TEST_ASSERT(restored.fallback_url == orig.fallback_url, "toJson/fromJson fallback_url mismatch");
        TEST_ASSERT(restored.fallback_api_key == orig.fallback_api_key, "toJson/fromJson fallback_api_key mismatch");
        TEST_ASSERT(restored.fallback_model == orig.fallback_model, "toJson/fromJson fallback_model mismatch");
        TEST_ASSERT(restored.enable_local_fallback == false, "toJson/fromJson enable_local_fallback mismatch");
        TEST_ASSERT(restored.proxy_url == orig.proxy_url, "toJson/fromJson proxy_url mismatch");
        TEST_ASSERT(restored.proxy_user == orig.proxy_user, "toJson/fromJson proxy_user mismatch");
        TEST_ASSERT(restored.proxy_pass == orig.proxy_pass, "toJson/fromJson proxy_pass mismatch");
    }
}

// =========================================================================
// Suite 4: Persistence, DPAPI Secret Protection & Plaintext Leak Prevention
// =========================================================================
void runPersistenceAndLeakSuite(const fs::path& sandboxDir) {
    TEST_SECTION("Suite 4: Persistence, DPAPI & Plaintext Leak Prevention");

    fs::path sandboxCfg = sandboxDir / "config.json";

    // 4.1 Plaintext leak verification:
    // When saving secrets, sensitive strings must NEVER appear in plain text on disk!
    {
        const std::string canaryOr = "CANARY_OPENROUTER_SECRET_KEY_999888777";
        const std::string canaryFb = "CANARY_FALLBACK_API_KEY_555444333";
        const std::string canaryProxy = "CANARY_PROXY_PASSWORD_111222333";

        AppConfig cfg;
        cfg.backend = "openrouter";
        cfg.openrouter_api_key = canaryOr;
        cfg.fallback_url = "https://backup-relay.example.com/api";
        cfg.fallback_api_key = canaryFb;
        cfg.proxy_url = "http://127.0.0.1:8888";
        cfg.proxy_user = "proxy_test_user";
        cfg.proxy_pass = canaryProxy;
        cfg.enable_local_fallback = true;

        // Perform save (writes to sandboxCfg because config.json exists next to exe)
        bool saved = cfg.save();
        TEST_ASSERT(saved, "AppConfig::save() returned false");

        // Inspect the saved file on disk directly as a raw byte stream / string
        std::ifstream ifs(sandboxCfg, std::ios::binary);
        TEST_ASSERT(ifs.is_open(), "Could not open saved config file for raw inspection");
        std::stringstream buffer;
        buffer << ifs.rdbuf();
        std::string rawContent = buffer.str();
        ifs.close();

        // 1. Canaries must NOT exist anywhere in rawContent as substrings
        TEST_ASSERT(rawContent.find(canaryOr) == std::string::npos,
            "CRITICAL SECURITY LEAK: openrouter_api_key leaked into disk JSON in plaintext!");
        TEST_ASSERT(rawContent.find(canaryFb) == std::string::npos,
            "CRITICAL SECURITY LEAK: fallback_api_key leaked into disk JSON in plaintext!");
        TEST_ASSERT(rawContent.find(canaryProxy) == std::string::npos,
            "CRITICAL SECURITY LEAK: proxy_pass leaked into disk JSON in plaintext!");

        // 2. Parse JSON directly from disk: verify plaintext fields are sanitized to empty strings
        nlohmann::json diskJson = nlohmann::json::parse(rawContent);
        TEST_ASSERT(diskJson.contains("openrouter_api_key") && diskJson["openrouter_api_key"] == "",
            "disk JSON openrouter_api_key must be sanitized to empty string");
        TEST_ASSERT(diskJson.contains("fallback_api_key") && diskJson["fallback_api_key"] == "",
            "disk JSON fallback_api_key must be sanitized to empty string");
        TEST_ASSERT(diskJson.contains("proxy_pass") && diskJson["proxy_pass"] == "",
            "disk JSON proxy_pass must be sanitized to empty string");

        // 3. Verify _secrets token is present and valid
        TEST_ASSERT(diskJson.contains("_secrets") && diskJson["_secrets"].is_string(),
            "disk JSON must contain _secrets token string");
        std::string token = diskJson["_secrets"].get<std::string>();
        TEST_ASSERT(token.rfind("v1:dpapi:", 0) == 0 || token.rfind("v1:xor:", 0) == 0,
            "disk JSON _secrets token does not have valid prefix");

        // 4. Now call AppConfig::load() and verify secrets are successfully recovered!
        AppConfig loaded = AppConfig::load();
        TEST_ASSERT(loaded.openrouter_api_key == canaryOr,
            "Loaded openrouter_api_key does not match canary: " + loaded.openrouter_api_key);
        TEST_ASSERT(loaded.fallback_api_key == canaryFb,
            "Loaded fallback_api_key does not match canary: " + loaded.fallback_api_key);
        TEST_ASSERT(loaded.proxy_pass == canaryProxy,
            "Loaded proxy_pass does not match canary: " + loaded.proxy_pass);
        TEST_ASSERT(loaded.proxy_user == "proxy_test_user", "proxy_user mismatch");
        TEST_ASSERT(loaded.fallback_url == "https://backup-relay.example.com/api", "fallback_url mismatch");
    }

    // 4.2 Repeated save-load cycle stress (10 cycles to check for degradation or double-encryption)
    {
        AppConfig cfg = AppConfig::load();
        for (int cycle = 0; cycle < 10; ++cycle) {
            cfg.proxy_pass = "CyclePassword_" + std::to_string(cycle);
            bool s = cfg.save();
            TEST_ASSERT(s, "Save failed on cycle " + std::to_string(cycle));
            AppConfig reloaded = AppConfig::load();
            TEST_ASSERT(reloaded.proxy_pass == "CyclePassword_" + std::to_string(cycle),
                "Reloaded password mismatch on cycle " + std::to_string(cycle));
            cfg = reloaded;
        }
    }

    // 4.3 Raw unencrypted secrets written directly to disk JSON by user (no _secrets)
    {
        nlohmann::json manualJson = {
            {"backend", "openrouter"},
            {"openrouter_api_key", "manual-plaintext-or-key"},
            {"fallback_api_key", "manual-plaintext-fb-key"},
            {"proxy_pass", "manual-plaintext-proxy-pass"},
            {"fallback_url", "http://127.0.0.1:9999"}
        };
        {
            std::ofstream ofs(sandboxCfg, std::ios::trunc);
            ofs << manualJson.dump(2);
        }

        // AppConfig::load() must read the plaintext keys correctly
        AppConfig loaded = AppConfig::load();
        TEST_ASSERT(loaded.openrouter_api_key == "manual-plaintext-or-key",
            "load() failed to ingest raw plaintext openrouter_api_key");
        TEST_ASSERT(loaded.fallback_api_key == "manual-plaintext-fb-key",
            "load() failed to ingest raw plaintext fallback_api_key");
        TEST_ASSERT(loaded.proxy_pass == "manual-plaintext-proxy-pass",
            "load() failed to ingest raw plaintext proxy_pass");

        // Now save: must automatically encrypt into _secrets and scrub plaintext
        loaded.save();

        std::ifstream ifs(sandboxCfg);
        nlohmann::json scrubbed;
        ifs >> scrubbed;
        TEST_ASSERT(scrubbed["openrouter_api_key"] == "", "save() did not scrub manual plaintext OR key");
        TEST_ASSERT(scrubbed["fallback_api_key"] == "", "save() did not scrub manual plaintext FB key");
        TEST_ASSERT(scrubbed["proxy_pass"] == "", "save() did not scrub manual plaintext proxy pass");
        TEST_ASSERT(scrubbed.contains("_secrets"), "save() did not create _secrets from manual plaintext keys");

        // Load again to verify encrypted keys persist
        AppConfig reloaded = AppConfig::load();
        TEST_ASSERT(reloaded.openrouter_api_key == "manual-plaintext-or-key", "Reload after encrypt failed for OR key");
        TEST_ASSERT(reloaded.fallback_api_key == "manual-plaintext-fb-key", "Reload after encrypt failed for FB key");
        TEST_ASSERT(reloaded.proxy_pass == "manual-plaintext-proxy-pass", "Reload after encrypt failed for proxy pass");
    }

    // 4.4 Corrupted _secrets on disk
    {
        nlohmann::json corruptSecretsJson = {
            {"backend", "ollama"},
            {"fallback_url", "http://127.0.0.1:5000"},
            {"_secrets", "v1:dpapi:CORRUPTED_BASE64_GARBAGE!#$"}
        };
        {
            std::ofstream ofs(sandboxCfg, std::ios::trunc);
            ofs << corruptSecretsJson.dump(2);
        }

        // Must not crash or throw exception; fallback to defaults/empty keys gracefully
        AppConfig loaded = AppConfig::load();
        TEST_ASSERT(loaded.backend == "ollama", "load() failed on corrupt _secrets");
        TEST_ASSERT(loaded.fallback_url == "http://127.0.0.1:5000", "load() failed to retain fallback_url");
        TEST_ASSERT(loaded.openrouter_api_key == "", "Corrupted _secrets openrouter_api_key must be empty");
        TEST_ASSERT(loaded.fallback_api_key == "", "Corrupted _secrets fallback_api_key must be empty");
        TEST_ASSERT(loaded.proxy_pass == "", "Corrupted _secrets proxy_pass must be empty");
    }

    // 4.5 Non-string _secrets on disk
    {
        nlohmann::json nonStringSecrets = {
            {"backend", "ollama"},
            {"_secrets", 123456} // integer instead of string token
        };
        {
            std::ofstream ofs(sandboxCfg, std::ios::trunc);
            ofs << nonStringSecrets.dump(2);
        }

        AppConfig loaded = AppConfig::load();
        TEST_ASSERT(loaded.backend == "ollama", "load() failed on non-string _secrets");
        TEST_ASSERT(loaded.openrouter_api_key == "", "non-string _secrets openrouter_api_key must be empty");
    }
}

// =========================================================================
// Suite 5: Special Characters, Unicode, and Extreme Boundaries Stress Tests
// =========================================================================
void runSpecialCharactersSuite(const fs::path& sandboxDir) {
    TEST_SECTION("Suite 5: Special Characters & Boundary Stress");

    fs::path sandboxCfg = sandboxDir / "config.json";

    // 5.1 Unicode, Cyrillic, Asian characters, and Emojis
    {
        AppConfig cfg;
        cfg.openrouter_api_key = "sk-секретный-ключ-12345-абвгдеёжзийклмнопрстуфхцчшщъыьэюя";
        cfg.fallback_api_key = "fb-🔑🛡️🚀-パスワード-المفتاح-中文密码";
        cfg.proxy_user = "пользователь_тест";
        cfg.proxy_pass = "P@$$w0rd!#%^&*()_+-=[]{}|;':\",./<>?`~ \t\r\n";
        cfg.fallback_url = "https://резервный-шлюз.рф:8443/v1/chat/completions?auth=token&lang=ru#anchor";
        cfg.proxy_url = "http://тест-прокси.локально:8080";

        bool ok = cfg.save();
        TEST_ASSERT(ok, "Failed to save config with Unicode/special characters");

        AppConfig loaded = AppConfig::load();
        TEST_ASSERT(loaded.openrouter_api_key == cfg.openrouter_api_key, "Cyrillic openrouter_api_key mismatch");
        TEST_ASSERT(loaded.fallback_api_key == cfg.fallback_api_key, "Multilingual/Emoji fallback_api_key mismatch");
        TEST_ASSERT(loaded.proxy_user == cfg.proxy_user, "Unicode proxy_user mismatch");
        TEST_ASSERT(loaded.proxy_pass == cfg.proxy_pass, "Complex special chars proxy_pass mismatch");
        TEST_ASSERT(loaded.fallback_url == cfg.fallback_url, "Unicode fallback_url mismatch");
        TEST_ASSERT(loaded.proxy_url == cfg.proxy_url, "Unicode proxy_url mismatch");
    }

    // 5.2 Very large secrets (64 KB)
    {
        std::string largeSecret(65536, 'X');
        for (size_t i = 0; i < largeSecret.size(); i += 16) {
            largeSecret[i] = 'A' + (i % 26);
        }

        AppConfig cfg;
        cfg.fallback_api_key = largeSecret;
        bool ok = cfg.save();
        TEST_ASSERT(ok, "Failed to save 64KB fallback_api_key");

        AppConfig loaded = AppConfig::load();
        TEST_ASSERT(loaded.fallback_api_key == largeSecret, "64KB fallback_api_key roundtrip mismatch");
    }

    // 5.3 Empty secrets roundtrip
    {
        AppConfig cfg;
        cfg.openrouter_api_key = "";
        cfg.fallback_api_key = "";
        cfg.proxy_pass = "";
        bool ok = cfg.save();
        TEST_ASSERT(ok, "Failed to save with empty secrets");

        AppConfig loaded = AppConfig::load();
        TEST_ASSERT(loaded.openrouter_api_key == "", "openrouter_api_key not empty");
        TEST_ASSERT(loaded.fallback_api_key == "", "fallback_api_key not empty");
        TEST_ASSERT(loaded.proxy_pass == "", "proxy_pass not empty");
    }
}

// =========================================================================
// Suite 6: NetworkLogger URL Sanitization Stress Tests
// =========================================================================
void runNetworkLoggerSanitizeSuite() {
    TEST_SECTION("Suite 6: NetworkLogger URL Sanitization");

    // 6.1 Basic auth masking
    {
        std::string raw = "http://admin:SecretPassword123@127.0.0.1:8080/v1/chat";
        std::string sanitized = NetworkLogger::sanitizeUrl(raw);
        TEST_ASSERT(sanitized == "http://admin:***@127.0.0.1:8080/v1/chat",
            "Basic auth sanitize failed: " + sanitized);
        TEST_ASSERT(sanitized.find("SecretPassword123") == std::string::npos,
            "Sanitized URL contains plain password!");
    }

    // 6.2 Basic auth with complex characters in password
    {
        std::string raw = "https://user_1:p%40ss%20w!rd@backup-relay.example.com:443/";
        std::string sanitized = NetworkLogger::sanitizeUrl(raw);
        TEST_ASSERT(sanitized == "https://user_1:***@backup-relay.example.com:443/",
            "Complex basic auth sanitize failed: " + sanitized);
    }

    // 6.3 Query parameter masking: api_key=, key=, token=, secret=, password=
    {
        std::string raw = "https://api.openai.com/v1/chat?api_key=sk-123456&other=param&token=my_secret_token";
        std::string sanitized = NetworkLogger::sanitizeUrl(raw);
        TEST_ASSERT(sanitized.find("sk-123456") == std::string::npos, "api_key query param not masked!");
        TEST_ASSERT(sanitized.find("my_secret_token") == std::string::npos, "token query param not masked!");
        TEST_ASSERT(sanitized.find("other=param") != std::string::npos, "Non-sensitive param lost!");
    }

    // 6.4 URL with no secrets
    {
        std::string raw = "http://127.0.0.1:11434/api/generate";
        std::string sanitized = NetworkLogger::sanitizeUrl(raw);
        TEST_ASSERT(sanitized == raw, "Safe URL was improperly modified: " + sanitized);
    }

    // 6.5 Empty URL
    {
        std::string sanitized = NetworkLogger::sanitizeUrl("");
        TEST_ASSERT(sanitized == "", "Empty URL returned non-empty: " + sanitized);
    }

    // 6.6 SOCKS5 proxy URL masking
    {
        std::string raw = "socks5://socksuser:sockspass123@10.0.0.1:1080";
        std::string sanitized = NetworkLogger::sanitizeUrl(raw);
        TEST_ASSERT(sanitized == "socks5://socksuser:***@10.0.0.1:1080", "socks5 sanitize failed: " + sanitized);
    }

    // 6.7 Proxy URL without scheme (user:pass@host:port)
    {
        std::string raw = "myuser:secret123@proxy.lan:8080";
        std::string sanitized = NetworkLogger::sanitizeUrl(raw);
        bool masked = (sanitized.find("secret123") == std::string::npos);
        std::cout << "[OBSERVATION 6.7] sanitizeUrl on scheme-less URL masked: "
                  << (masked ? "YES" : "NO (requires scheme ://)") << " result=" << sanitized << std::endl;
    }
}

// =========================================================================
// Main Runner
// =========================================================================
int main(int argc, char* argv[]) {
    std::cout << "======================================================\n"
              << " GhostAI M1 Empirical Stress Harness (Challenger 1)\n"
              << "======================================================" << std::endl;

    // Determine sandbox directory next to the executable
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path sandboxDir = exeDir;

    // Ensure a local config.json exists in sandboxDir so getConfigFilePathW() targets it!
    fs::path sandboxCfg = sandboxDir / "config.json";
    if (!fs::exists(sandboxCfg)) {
        std::ofstream ofs(sandboxCfg);
        ofs << "{}";
    }

    std::cout << "[INFO] Test Executable: " << fs::path(exePath).string() << "\n";
    std::cout << "[INFO] Sandbox Config:  " << sandboxCfg.string() << "\n";

    // Run all test suites
    runCryptoPrimitivesSuite();
    runCryptoDictTokenSuite();
    runConfigSchemaSuite();
    runPersistenceAndLeakSuite(sandboxDir);
    runSpecialCharactersSuite(sandboxDir);
    runNetworkLoggerSanitizeSuite();

    // Clean up sandbox config
    std::error_code ec;
    fs::remove(sandboxCfg, ec);

    std::cout << "\n======================================================\n"
              << " Stress Test Results Summary:\n"
              << " PASSED: " << g_passCount << "\n"
              << " FAILED: " << g_failCount << "\n"
              << "======================================================" << std::endl;

    if (g_failCount > 0) {
        std::cerr << "[!] STRESS SUITE DETECTED " << g_failCount << " FAILURES!\n";
        return 1;
    }

    std::cout << "[+] ALL EMPIRICAL CHALLENGES PASSED SUCCESSFULLY!\n";
    return 0;
}
