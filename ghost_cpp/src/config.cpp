#include "config.hpp"
#include "crypto.hpp"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <iostream>

namespace {
    std::wstring getConfigFilePathW() {
        // 1. Проверяем config.json или .ghost_ai_config.json рядом с исполняемым файлом (портативный режим)
        wchar_t exePath[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
            std::wstring dir(exePath);
            size_t slash = dir.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                std::wstring exeDir = dir.substr(0, slash + 1);
                std::wstring localCfg = exeDir + L"config.json";
                if (GetFileAttributesW(localCfg.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    return localCfg;
                }
                std::wstring localDotCfg = exeDir + L".ghost_ai_config.json";
                if (GetFileAttributesW(localDotCfg.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    return localDotCfg;
                }
            }
        }

        // 2. Fallback в профиль пользователя (%USERPROFILE%\.ghost_ai_config.json)
        wchar_t userProfile[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) {
            return std::wstring(userProfile) + L"\\.ghost_ai_config.json";
        }
        return L".ghost_ai_config.json";
    }
}

std::string AppConfig::getConfigFilePath() {
    std::wstring wp = getConfigFilePathW();
    int len = WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 1) return ".ghost_ai_config.json";
    std::string s(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, &s[0], len, NULL, NULL);
    return s;
}

nlohmann::json AppConfig::toJson() const {
    nlohmann::json j;
    j["backend"] = backend;
    j["ollama_url"] = ollama_url;
    j["ollama_model"] = ollama_model;
    j["openrouter_api_key"] = openrouter_api_key;
    j["openrouter_model"] = openrouter_model;
    j["fallback_url"] = fallback_url;
    j["fallback_api_key"] = fallback_api_key;
    j["fallback_model"] = fallback_model;
    j["enable_local_fallback"] = enable_local_fallback;
    j["proxy_url"] = proxy_url;
    j["proxy_user"] = proxy_user;
    j["proxy_pass"] = proxy_pass;
    j["validate_tls"] = validate_tls;
    j["system_prompt"] = system_prompt;
    j["user_prompt"] = user_prompt;
    j["hotkeys"] = hotkeys;
    j["capture_keys"] = capture_keys;
    j["visibility_keys"] = visibility_keys;
    j["kill_key"] = kill_key;
    j["max_context_pairs"] = max_context_pairs;
    j["persist_context"] = persist_context;
    j["include_images_in_context"] = include_images_in_context;
    j["reset_context_key"] = reset_context_key;
    j["alpha_min"] = alpha_min;
    j["alpha_max"] = alpha_max;
    j["alpha_step"] = alpha_step;
    j["alpha_default"] = alpha_default;
    j["screenshot_offset"] = screenshot_offset;
    j["capture_step"] = capture_step;
    j["request_timeout"] = request_timeout;
    j["window_alpha"] = window_alpha;
    return j;
}

AppConfig AppConfig::fromJson(const nlohmann::json& j) {
    AppConfig cfg;
    if (j.contains("backend") && j["backend"].is_string()) cfg.backend = j["backend"].get<std::string>();
    if (j.contains("ollama_url") && j["ollama_url"].is_string()) cfg.ollama_url = j["ollama_url"].get<std::string>();
    if (j.contains("ollama_model") && j["ollama_model"].is_string()) cfg.ollama_model = j["ollama_model"].get<std::string>();
    if (j.contains("openrouter_api_key") && j["openrouter_api_key"].is_string()) cfg.openrouter_api_key = j["openrouter_api_key"].get<std::string>();
    if (j.contains("openrouter_model") && j["openrouter_model"].is_string()) cfg.openrouter_model = j["openrouter_model"].get<std::string>();
    if (cfg.openrouter_model.empty() || cfg.openrouter_model == "meta-llama/llama-3.2-11b-vision-instruct:free") {
        cfg.openrouter_model = "nvidia/nemotron-3.5-super-vl:free";
    }
    if (j.contains("fallback_url") && j["fallback_url"].is_string()) cfg.fallback_url = j["fallback_url"].get<std::string>();
    if (cfg.fallback_url.empty()) {
        cfg.fallback_url = "https://ghost-relay.nodirmuhammedov54.workers.dev/";
    }
    if (j.contains("fallback_api_key") && j["fallback_api_key"].is_string()) cfg.fallback_api_key = j["fallback_api_key"].get<std::string>();
    if (j.contains("fallback_model") && j["fallback_model"].is_string()) cfg.fallback_model = j["fallback_model"].get<std::string>();
    if (cfg.fallback_model.empty() || cfg.fallback_model == "meta-llama/llama-3.2-11b-vision-instruct:free") {
        cfg.fallback_model = "nvidia/nemotron-3.5-super-vl:free";
    }
    if (j.contains("enable_local_fallback") && j["enable_local_fallback"].is_boolean()) cfg.enable_local_fallback = j["enable_local_fallback"].get<bool>();
    if (j.contains("proxy_url") && j["proxy_url"].is_string()) cfg.proxy_url = j["proxy_url"].get<std::string>();
    if (j.contains("proxy_user") && j["proxy_user"].is_string()) cfg.proxy_user = j["proxy_user"].get<std::string>();
    if (j.contains("proxy_pass") && j["proxy_pass"].is_string()) cfg.proxy_pass = j["proxy_pass"].get<std::string>();
    if (j.contains("validate_tls") && j["validate_tls"].is_boolean()) cfg.validate_tls = j["validate_tls"].get<bool>();
    if (j.contains("system_prompt") && j["system_prompt"].is_string()) cfg.system_prompt = j["system_prompt"].get<std::string>();
    if (j.contains("user_prompt") && j["user_prompt"].is_string()) cfg.user_prompt = j["user_prompt"].get<std::string>();
    if (j.contains("kill_key") && j["kill_key"].is_string()) cfg.kill_key = j["kill_key"].get<std::string>();
    if (j.contains("max_context_pairs") && j["max_context_pairs"].is_number_integer()) cfg.max_context_pairs = j["max_context_pairs"].get<int>();
    if (j.contains("persist_context") && j["persist_context"].is_boolean()) cfg.persist_context = j["persist_context"].get<bool>();
    if (j.contains("include_images_in_context") && j["include_images_in_context"].is_boolean()) cfg.include_images_in_context = j["include_images_in_context"].get<bool>();
    if (j.contains("reset_context_key") && j["reset_context_key"].is_string()) cfg.reset_context_key = j["reset_context_key"].get<std::string>();
    if (j.contains("alpha_min") && j["alpha_min"].is_number()) cfg.alpha_min = j["alpha_min"].get<double>();
    if (j.contains("alpha_max") && j["alpha_max"].is_number()) cfg.alpha_max = j["alpha_max"].get<double>();
    if (j.contains("alpha_step") && j["alpha_step"].is_number()) cfg.alpha_step = j["alpha_step"].get<double>();
    if (j.contains("alpha_default") && j["alpha_default"].is_number()) cfg.alpha_default = j["alpha_default"].get<double>();
    if (j.contains("screenshot_offset") && j["screenshot_offset"].is_number_integer()) cfg.screenshot_offset = j["screenshot_offset"].get<int>();
    if (j.contains("capture_step") && j["capture_step"].is_number_integer()) cfg.capture_step = j["capture_step"].get<int>();
    if (j.contains("request_timeout") && j["request_timeout"].is_number_integer()) cfg.request_timeout = j["request_timeout"].get<int>();
    if (j.contains("window_alpha") && j["window_alpha"].is_number()) cfg.window_alpha = j["window_alpha"].get<double>();

    if (j.contains("hotkeys") && j["hotkeys"].is_object()) {
        for (auto& [k, v] : j["hotkeys"].items()) {
            if (v.is_string()) cfg.hotkeys[k] = v.get<std::string>();
        }
    }
    if (j.contains("capture_keys") && j["capture_keys"].is_object()) {
        for (auto& [k, v] : j["capture_keys"].items()) {
            if (v.is_string()) cfg.capture_keys[k] = v.get<std::string>();
        }
    }
    if (j.contains("visibility_keys") && j["visibility_keys"].is_object()) {
        for (auto& [k, v] : j["visibility_keys"].items()) {
            if (v.is_string()) cfg.visibility_keys[k] = v.get<std::string>();
        }
    }
    return cfg;
}

bool AppConfig::save() const {
    try {
        nlohmann::json plain = toJson();

        nlohmann::json secrets;
        secrets["openrouter_api_key"] = openrouter_api_key;
        secrets["fallback_api_key"] = fallback_api_key;
        secrets["proxy_pass"] = proxy_pass;
        plain["_secrets"] = Crypto::encryptDict(secrets);

        // Sanitize sensitive plain-text values before persisting
        plain["openrouter_api_key"] = "";
        plain["fallback_api_key"] = "";
        plain["proxy_pass"] = "";

        std::wstring path = getConfigFilePathW();
        std::ofstream ofs(path, std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << plain.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

AppConfig AppConfig::load() {
    std::wstring path = getConfigFilePathW();
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return AppConfig();
    }

    try {
        nlohmann::json j;
        ifs >> j;

        std::string openrouterKey = "";
        std::string fallbackKey = "";
        std::string proxyPassword = "";

        if (j.contains("_secrets") && j["_secrets"].is_string()) {
            std::string token = j["_secrets"].get<std::string>();
            nlohmann::json secrets;
            if (Crypto::decryptDict(token, secrets)) {
                if (secrets.contains("openrouter_api_key") && secrets["openrouter_api_key"].is_string()) {
                    openrouterKey = secrets["openrouter_api_key"].get<std::string>();
                }
                if (secrets.contains("fallback_api_key") && secrets["fallback_api_key"].is_string()) {
                    fallbackKey = secrets["fallback_api_key"].get<std::string>();
                }
                if (secrets.contains("proxy_pass") && secrets["proxy_pass"].is_string()) {
                    proxyPassword = secrets["proxy_pass"].get<std::string>();
                }
            }
        }

        AppConfig cfg = AppConfig::fromJson(j);
        if (!openrouterKey.empty()) {
            cfg.openrouter_api_key = openrouterKey;
        }
        if (!fallbackKey.empty()) {
            cfg.fallback_api_key = fallbackKey;
        }
        if (!proxyPassword.empty()) {
            cfg.proxy_pass = proxyPassword;
        }
        return cfg;
    } catch (...) {
        return AppConfig();
    }
}
