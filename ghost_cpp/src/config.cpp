#include "config.hpp"
#include "crypto.hpp"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <iostream>

namespace {
    std::wstring getConfigFilePathW() {
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
    if (j.contains("backend")) cfg.backend = j["backend"].get<std::string>();
    if (j.contains("ollama_url")) cfg.ollama_url = j["ollama_url"].get<std::string>();
    if (j.contains("ollama_model")) cfg.ollama_model = j["ollama_model"].get<std::string>();
    if (j.contains("openrouter_api_key")) cfg.openrouter_api_key = j["openrouter_api_key"].get<std::string>();
    if (j.contains("openrouter_model")) cfg.openrouter_model = j["openrouter_model"].get<std::string>();
    if (j.contains("system_prompt")) cfg.system_prompt = j["system_prompt"].get<std::string>();
    if (j.contains("user_prompt")) cfg.user_prompt = j["user_prompt"].get<std::string>();
    if (j.contains("kill_key")) cfg.kill_key = j["kill_key"].get<std::string>();
    if (j.contains("max_context_pairs")) cfg.max_context_pairs = j["max_context_pairs"].get<int>();
    if (j.contains("persist_context")) cfg.persist_context = j["persist_context"].get<bool>();
    if (j.contains("include_images_in_context")) cfg.include_images_in_context = j["include_images_in_context"].get<bool>();
    if (j.contains("reset_context_key")) cfg.reset_context_key = j["reset_context_key"].get<std::string>();
    if (j.contains("alpha_min")) cfg.alpha_min = j["alpha_min"].get<double>();
    if (j.contains("alpha_max")) cfg.alpha_max = j["alpha_max"].get<double>();
    if (j.contains("alpha_step")) cfg.alpha_step = j["alpha_step"].get<double>();
    if (j.contains("alpha_default")) cfg.alpha_default = j["alpha_default"].get<double>();
    if (j.contains("screenshot_offset")) cfg.screenshot_offset = j["screenshot_offset"].get<int>();
    if (j.contains("capture_step")) cfg.capture_step = j["capture_step"].get<int>();
    if (j.contains("request_timeout")) cfg.request_timeout = j["request_timeout"].get<int>();
    if (j.contains("window_alpha")) cfg.window_alpha = j["window_alpha"].get<double>();

    if (j.contains("hotkeys") && j["hotkeys"].is_object()) {
        for (auto& [k, v] : j["hotkeys"].items()) {
            cfg.hotkeys[k] = v.get<std::string>();
        }
    }
    if (j.contains("capture_keys") && j["capture_keys"].is_object()) {
        for (auto& [k, v] : j["capture_keys"].items()) {
            cfg.capture_keys[k] = v.get<std::string>();
        }
    }
    if (j.contains("visibility_keys") && j["visibility_keys"].is_object()) {
        for (auto& [k, v] : j["visibility_keys"].items()) {
            cfg.visibility_keys[k] = v.get<std::string>();
        }
    }
    return cfg;
}

bool AppConfig::save() const {
    try {
        nlohmann::json plain = toJson();

        nlohmann::json secrets;
        secrets["openrouter_api_key"] = openrouter_api_key;
        plain["_secrets"] = Crypto::encryptDict(secrets);
        plain["openrouter_api_key"] = "";

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
        if (j.contains("_secrets")) {
            std::string token = j["_secrets"].get<std::string>();
            nlohmann::json secrets;
            if (Crypto::decryptDict(token, secrets)) {
                if (secrets.contains("openrouter_api_key")) {
                    openrouterKey = secrets["openrouter_api_key"].get<std::string>();
                }
            }
        }

        AppConfig cfg = AppConfig::fromJson(j);
        if (!openrouterKey.empty()) {
            cfg.openrouter_api_key = openrouterKey;
        }
        return cfg;
    } catch (...) {
        return AppConfig();
    }
}
