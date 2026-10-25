#pragma once

#include "config.hpp"
#include <optional>

// Win32 Control IDs for SetupUI dialog
enum {
    IDC_RADIO_OLLAMA = 1001,
    IDC_RADIO_OPENROUTER,
    IDC_EDIT_OLLAMA_URL,
    IDC_EDIT_OLLAMA_MODEL,
    IDC_EDIT_OPENROUTER_KEY,
    IDC_EDIT_OPENROUTER_MODEL,
    IDC_EDIT_SYSTEM_PROMPT,
    IDC_EDIT_USER_PROMPT,
    // Basic hotkeys
    IDC_EDIT_HK_SCREENSHOT,
    IDC_EDIT_HK_FOLLOW,
    IDC_EDIT_HK_RESET,
    IDC_EDIT_HK_KILL,
    // Visibility hotkeys
    IDC_EDIT_HK_INCREASE_ALPHA,
    IDC_EDIT_HK_DECREASE_ALPHA,
    IDC_EDIT_HK_TOGGLE_INVISIBLE,
    IDC_EDIT_HK_TOGGLE_CLICKTHROUGH,
    // Capture / size hotkeys
    IDC_EDIT_HK_EXPAND_RIGHT,
    IDC_EDIT_HK_SHRINK_LEFT,
    IDC_EDIT_HK_EXPAND_DOWN,
    IDC_EDIT_HK_SHRINK_UP,
    IDC_EDIT_HK_EXPAND_ALL,
    IDC_EDIT_HK_SHRINK_ALL,
    IDC_EDIT_HK_RESET_CAPTURE,
    // Context
    IDC_EDIT_MAX_PAIRS,
    IDC_CHECK_PERSIST_CTX,
    IDC_CHECK_CTX_IMAGES,
    IDC_EDIT_HK_RESET_CONTEXT,
    IDC_BTN_SAVE_START,
    IDC_BTN_CANCEL,
    // Resilient Network: Cascade Fallback & Proxy
    IDC_EDIT_FALLBACK_URL = 1050,
    IDC_FALLBACK_URL = 1050,
    IDC_EDIT_FALLBACK_KEY = 1051,
    IDC_FALLBACK_KEY = 1051,
    IDC_EDIT_FALLBACK_MODEL = 1052,
    IDC_FALLBACK_MODEL = 1052,
    IDC_CHECK_LOCAL_FALLBACK = 1053,
    IDC_ENABLE_LOCAL_FALLBACK = 1053,
    IDC_EDIT_PROXY_URL = 1054,
    IDC_PROXY_URL = 1054,
    IDC_EDIT_PROXY_USER = 1055,
    IDC_PROXY_USER = 1055,
    IDC_EDIT_PROXY_PASS = 1056,
    IDC_PROXY_PASS = 1056
};

class SetupUI {
public:
    // Показывает окно настроек Ghost-AI и возвращает обновленный конфиг при нажатии "Сохранить и запустить"
    static std::optional<AppConfig> showSetupDialog(const AppConfig& initialConfig);
};

