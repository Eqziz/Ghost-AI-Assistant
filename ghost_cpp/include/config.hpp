#pragma once

#include <string>
#include <map>
#include "json.hpp"

struct AppConfig {
    // Backend
    std::string backend = "ollama";
    std::string ollama_url = "http://localhost:11434";
    std::string ollama_model = "";
    std::string openrouter_api_key = "";
    std::string openrouter_model = "meta-llama/llama-3.2-11b-vision-instruct:free";

    // Промпты
    std::string system_prompt = "Только правильный ответ без никаких слов";
    std::string user_prompt = "Отвечай максимально коротко и без ошибок";

    // Хоткеи
    std::map<std::string, std::string> hotkeys = {
        {"screenshot",    "esc"},
        {"toggle_follow", "right ctrl"},
        {"reset",         "ctrl+shift+r"}
    };

    std::map<std::string, std::string> capture_keys = {
        {"expand_right",  "right"},
        {"shrink_left",   "left"},
        {"expand_down",   "down"},
        {"shrink_up",     "up"},
        {"expand_all",    "mouse wheel up"},
        {"shrink_all",    "mouse wheel down"},
        {"reset_capture", "ctrl+0"}
    };

    std::map<std::string, std::string> visibility_keys = {
        {"increase_alpha",      "ctrl+up"},
        {"decrease_alpha",      "ctrl+down"},
        {"toggle_clickthrough", "ctrl+shift+c"},
        {"toggle_invisible",    "ctrl+shift+h"}
    };

    std::string kill_key = "ctrl+shift+q";

    // Контекст
    int max_context_pairs = 10;
    bool persist_context = true;
    bool include_images_in_context = false;
    std::string reset_context_key = "ctrl+shift+x";

    // Параметры окна и захвата
    double alpha_min = 0.05;
    double alpha_max = 1.0;
    double alpha_step = 0.1;
    double alpha_default = 0.5;
    int screenshot_offset = 50;
    int capture_step = 25;
    int request_timeout = 30;
    double window_alpha = 0.5;

    nlohmann::json toJson() const;
    static AppConfig fromJson(const nlohmann::json& j);

    bool save() const;
    static AppConfig load();

    static std::string getConfigFilePath();
};
