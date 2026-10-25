#include "setup_ui.hpp"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

namespace {
    AppConfig s_currentConfig;
    std::optional<AppConfig> s_resultConfig = std::nullopt;

    // Aliases to IDC_* for backward compatibility
    enum {
        ID_RADIO_OLLAMA = IDC_RADIO_OLLAMA,
        ID_RADIO_OPENROUTER = IDC_RADIO_OPENROUTER,
        ID_EDIT_OLLAMA_URL = IDC_EDIT_OLLAMA_URL,
        ID_EDIT_OLLAMA_MODEL = IDC_EDIT_OLLAMA_MODEL,
        ID_EDIT_OPENROUTER_KEY = IDC_EDIT_OPENROUTER_KEY,
        ID_EDIT_OPENROUTER_MODEL = IDC_EDIT_OPENROUTER_MODEL,
        ID_EDIT_SYSTEM_PROMPT = IDC_EDIT_SYSTEM_PROMPT,
        ID_EDIT_USER_PROMPT = IDC_EDIT_USER_PROMPT,
        // Basic hotkeys
        ID_EDIT_HK_SCREENSHOT = IDC_EDIT_HK_SCREENSHOT,
        ID_EDIT_HK_FOLLOW = IDC_EDIT_HK_FOLLOW,
        ID_EDIT_HK_RESET = IDC_EDIT_HK_RESET,
        ID_EDIT_HK_KILL = IDC_EDIT_HK_KILL,
        // Visibility hotkeys
        ID_EDIT_HK_INCREASE_ALPHA = IDC_EDIT_HK_INCREASE_ALPHA,
        ID_EDIT_HK_DECREASE_ALPHA = IDC_EDIT_HK_DECREASE_ALPHA,
        ID_EDIT_HK_TOGGLE_INVISIBLE = IDC_EDIT_HK_TOGGLE_INVISIBLE,
        ID_EDIT_HK_TOGGLE_CLICKTHROUGH = IDC_EDIT_HK_TOGGLE_CLICKTHROUGH,
        // Capture / size hotkeys
        ID_EDIT_HK_EXPAND_RIGHT = IDC_EDIT_HK_EXPAND_RIGHT,
        ID_EDIT_HK_SHRINK_LEFT = IDC_EDIT_HK_SHRINK_LEFT,
        ID_EDIT_HK_EXPAND_DOWN = IDC_EDIT_HK_EXPAND_DOWN,
        ID_EDIT_HK_SHRINK_UP = IDC_EDIT_HK_SHRINK_UP,
        ID_EDIT_HK_EXPAND_ALL = IDC_EDIT_HK_EXPAND_ALL,
        ID_EDIT_HK_SHRINK_ALL = IDC_EDIT_HK_SHRINK_ALL,
        ID_EDIT_HK_RESET_CAPTURE = IDC_EDIT_HK_RESET_CAPTURE,
        // Context
        ID_EDIT_MAX_PAIRS = IDC_EDIT_MAX_PAIRS,
        ID_CHECK_PERSIST_CTX = IDC_CHECK_PERSIST_CTX,
        ID_CHECK_CTX_IMAGES = IDC_CHECK_CTX_IMAGES,
        ID_EDIT_HK_RESET_CONTEXT = IDC_EDIT_HK_RESET_CONTEXT,
        ID_BTN_SAVE_START = IDC_BTN_SAVE_START,
        ID_BTN_CANCEL = IDC_BTN_CANCEL,
        // Cascade Fallback & Proxy
        ID_EDIT_FALLBACK_URL = IDC_EDIT_FALLBACK_URL,
        ID_EDIT_FALLBACK_KEY = IDC_EDIT_FALLBACK_KEY,
        ID_CHECK_LOCAL_FALLBACK = IDC_CHECK_LOCAL_FALLBACK,
        ID_EDIT_PROXY_URL = IDC_EDIT_PROXY_URL,
        ID_EDIT_PROXY_USER = IDC_EDIT_PROXY_USER,
        ID_EDIT_PROXY_PASS = IDC_EDIT_PROXY_PASS
    };

    HWND s_rdoOllama = NULL;
    HWND s_rdoOpenRouter = NULL;
    HWND s_edOllamaUrl = NULL;
    HWND s_edOllamaModel = NULL;
    HWND s_edOpenRouterKey = NULL;
    HWND s_edOpenRouterModel = NULL;

    // Resilient Network & Proxy controls
    HWND s_edFallbackUrl = NULL;
    HWND s_edFallbackKey = NULL;
    HWND s_chkLocalFallback = NULL;
    HWND s_edProxyUrl = NULL;
    HWND s_edProxyUser = NULL;
    HWND s_edProxyPass = NULL;

    HWND s_edSystemPrompt = NULL;
    HWND s_edUserPrompt = NULL;

    // Hotkey edit controls
    HWND s_edHkScreenshot = NULL;
    HWND s_edHkFollow = NULL;
    HWND s_edHkReset = NULL;
    HWND s_edHkKill = NULL;

    HWND s_edHkIncreaseAlpha = NULL;
    HWND s_edHkDecreaseAlpha = NULL;
    HWND s_edHkToggleInvisible = NULL;
    HWND s_edHkToggleClickthrough = NULL;

    HWND s_edHkExpandRight = NULL;
    HWND s_edHkShrinkLeft = NULL;
    HWND s_edHkExpandDown = NULL;
    HWND s_edHkShrinkUp = NULL;
    HWND s_edHkExpandAll = NULL;
    HWND s_edHkShrinkAll = NULL;
    HWND s_edHkResetCapture = NULL;

    // Context controls
    HWND s_edMaxPairs = NULL;
    HWND s_chkPersistCtx = NULL;
    HWND s_chkCtxImages = NULL;
    HWND s_edHkResetContext = NULL;

    HFONT s_fontBold = NULL;
    HFONT s_fontNormal = NULL;
    HBRUSH s_darkBgBrush = NULL;
    HBRUSH s_editBgBrush = NULL;

    std::wstring toWide(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
        if (size <= 0) {
            size = MultiByteToWideChar(CP_ACP, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
            if (size <= 0) return L"";
            std::wstring w(size, 0);
            MultiByteToWideChar(CP_ACP, 0, str.c_str(), static_cast<int>(str.size()), &w[0], size);
            return w;
        }
        std::wstring w(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &w[0], size);
        return w;
    }

    std::string toUtf8(const wchar_t* wstr) {
        if (!wstr || !*wstr) return "";
        int len = static_cast<int>(wcslen(wstr));
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr, len, NULL, 0, NULL, NULL);
        if (size <= 0) return "";
        std::string s(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, len, &s[0], size, NULL, NULL);
        return s;
    }

    std::string getControlText(HWND hwnd) {
        int len = GetWindowTextLengthW(hwnd);
        if (len <= 0) {
            wchar_t fallbackBuf[512] = { 0 };
            len = GetWindowTextW(hwnd, fallbackBuf, 512);
            if (len <= 0) return "";
            std::string res = toUtf8(fallbackBuf);
            while (!res.empty() && (res.front() == ' ' || res.front() == '\t')) res.erase(res.begin());
            while (!res.empty() && (res.back() == ' ' || res.back() == '\t')) res.pop_back();
            return res;
        }
        std::vector<wchar_t> buf(len + 1, 0);
        GetWindowTextW(hwnd, buf.data(), len + 1);
        std::string res = toUtf8(buf.data());
        while (!res.empty() && (res.front() == ' ' || res.front() == '\t')) res.erase(res.begin());
        while (!res.empty() && (res.back() == ' ' || res.back() == '\t')) res.pop_back();
        return res;
    }

    std::string getMapKey(const std::map<std::string, std::string>& m, const std::string& key, const std::string& def) {
        auto it = m.find(key);
        if (it != m.end() && !it->second.empty()) return it->second;
        return def;
    }

    void updateBackendControlsState() {
        bool isOllama = (SendMessage(s_rdoOllama, BM_GETCHECK, 0, 0) == BST_CHECKED);
        EnableWindow(s_edOllamaUrl, isOllama);
        EnableWindow(s_edOllamaModel, isOllama);
        EnableWindow(s_edOpenRouterKey, !isOllama);
        EnableWindow(s_edOpenRouterModel, !isOllama);
    }

    LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_CREATE: {
            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
            s_fontBold = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            s_fontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            s_darkBgBrush = CreateSolidBrush(RGB(30, 30, 30));
            s_editBgBrush = CreateSolidBrush(RGB(42, 42, 42));

            // Заголовок
            HWND lblTitle = CreateWindowW(L"STATIC", L"👻 Ghost-AI — Настройки и горячие клавиши",
                                          WS_CHILD | WS_VISIBLE, 20, 15, 600, 24, hwnd, NULL, hInst, NULL);
            SendMessage(lblTitle, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            // =========================================================================
            //  ЛЕВАЯ КОЛОНКА (X = 20, W = 470): Backend, Fallback & Proxy, Промпты, Контекст
            // =========================================================================

            // --- Блок 1: Backend ---
            HWND grpBackend = CreateWindowW(L"BUTTON", L"Режим работы (Backend)",
                                            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                            20, 45, 470, 150, hwnd, NULL, hInst, NULL);
            SendMessage(grpBackend, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            s_rdoOllama = CreateWindowW(L"BUTTON", L"🦙 Ollama (локально)",
                                        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                                        35, 66, 190, 20, hwnd, (HMENU)ID_RADIO_OLLAMA, hInst, NULL);
            SendMessage(s_rdoOllama, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            s_rdoOpenRouter = CreateWindowW(L"BUTTON", L"☁️ OpenRouter (облако)",
                                            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
                                            235, 66, 200, 20, hwnd, (HMENU)ID_RADIO_OPENROUTER, hInst, NULL);
            SendMessage(s_rdoOpenRouter, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // Ollama URL & Model
            CreateWindowW(L"STATIC", L"Ollama URL:", WS_CHILD | WS_VISIBLE, 35, 91, 110, 20, hwnd, NULL, hInst, NULL);
            s_edOllamaUrl = CreateWindowW(L"EDIT", toWide(s_currentConfig.ollama_url).c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                          150, 89, 325, 22, hwnd, (HMENU)ID_EDIT_OLLAMA_URL, hInst, NULL);
            SendMessage(s_edOllamaUrl, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"Ollama Model:", WS_CHILD | WS_VISIBLE, 35, 116, 110, 20, hwnd, NULL, hInst, NULL);
            s_edOllamaModel = CreateWindowW(L"EDIT", toWide(s_currentConfig.ollama_model).c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                            150, 114, 325, 22, hwnd, (HMENU)ID_EDIT_OLLAMA_MODEL, hInst, NULL);
            SendMessage(s_edOllamaModel, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // OpenRouter Key & Model
            CreateWindowW(L"STATIC", L"OpenRouter Key:", WS_CHILD | WS_VISIBLE, 35, 141, 110, 20, hwnd, NULL, hInst, NULL);
            s_edOpenRouterKey = CreateWindowW(L"EDIT", toWide(s_currentConfig.openrouter_api_key).c_str(),
                                              WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
                                              150, 139, 325, 22, hwnd, (HMENU)ID_EDIT_OPENROUTER_KEY, hInst, NULL);
            SendMessage(s_edOpenRouterKey, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"OpenRouter Model:", WS_CHILD | WS_VISIBLE, 35, 166, 115, 20, hwnd, NULL, hInst, NULL);
            s_edOpenRouterModel = CreateWindowW(L"COMBOBOX", L"",
                                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP,
                                                150, 164, 325, 220, hwnd, (HMENU)ID_EDIT_OPENROUTER_MODEL, hInst, NULL);
            SendMessage(s_edOpenRouterModel, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            const wchar_t* PRESET_MODELS[] = {
                L"nvidia/nemotron-3.5-super-vl:free",
                L"google/gemini-2.0-flash-001",
                L"anthropic/claude-3.5-sonnet",
                L"anthropic/claude-3.5-haiku",
                L"openai/gpt-4o",
                L"openai/gpt-4o-mini",
                L"qwen/qwen-2.5-vl-72b-instruct",
                L"deepseek/deepseek-chat",
                L"meta-llama/llama-3.2-90b-vision-instruct"
            };
            for (const auto* m : PRESET_MODELS) {
                SendMessageW(s_edOpenRouterModel, CB_ADDSTRING, 0, (LPARAM)m);
            }

            std::wstring initialModel = toWide(s_currentConfig.openrouter_model);
            if (initialModel.empty()) {
                initialModel = L"nvidia/nemotron-3.5-super-vl:free";
            }
            SetWindowTextW(s_edOpenRouterModel, initialModel.c_str());

            if (s_currentConfig.backend == "openrouter") {
                SendMessage(s_rdoOpenRouter, BM_SETCHECK, BST_CHECKED, 0);
            } else {
                SendMessage(s_rdoOllama, BM_SETCHECK, BST_CHECKED, 0);
            }

            // --- Блок 2: Резервный шлюз и Прокси (Cascade Fallback & Proxy) ---
            HWND grpFallback = CreateWindowW(L"BUTTON", L"🌐 Обход фильтра OES / Шлюз Cloudflare (Relay)",
                                             WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                             20, 205, 470, 165, hwnd, NULL, hInst, NULL);
            SendMessage(grpFallback, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            std::wstring fallbackDisplay = s_currentConfig.fallback_url.empty() ? 
                L"https://ghost-relay.nodirmuhammedov54.workers.dev/" : toWide(s_currentConfig.fallback_url);

            // Tier 2 Fallback URL
            CreateWindowW(L"STATIC", L"Шлюз Cloudflare:", WS_CHILD | WS_VISIBLE, 35, 227, 145, 20, hwnd, NULL, hInst, NULL);
            s_edFallbackUrl = CreateWindowW(L"EDIT", fallbackDisplay.c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                           185, 225, 290, 22, hwnd, (HMENU)ID_EDIT_FALLBACK_URL, hInst, NULL);
            SendMessage(s_edFallbackUrl, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // Tier 2 Fallback API Key
            CreateWindowW(L"STATIC", L"Резервный API ключ:", WS_CHILD | WS_VISIBLE, 35, 252, 145, 20, hwnd, NULL, hInst, NULL);
            s_edFallbackKey = CreateWindowW(L"EDIT", toWide(s_currentConfig.fallback_api_key).c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
                                           185, 250, 290, 22, hwnd, (HMENU)ID_EDIT_FALLBACK_KEY, hInst, NULL);
            SendMessage(s_edFallbackKey, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // Tier 3 Local Ollama Fallback Toggle
            s_chkLocalFallback = CreateWindowW(L"BUTTON", L"🦙 Авто-переход на локальный Ollama (127.0.0.1:11434) при сбое",
                                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                              35, 277, 440, 20, hwnd, (HMENU)ID_CHECK_LOCAL_FALLBACK, hInst, NULL);
            SendMessage(s_chkLocalFallback, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);
            if (s_currentConfig.enable_local_fallback) {
                SendMessage(s_chkLocalFallback, BM_SETCHECK, BST_CHECKED, 0);
            }

            // Authenticated Proxy: URL
            CreateWindowW(L"STATIC", L"Прокси (Host:Port):", WS_CHILD | WS_VISIBLE, 35, 304, 145, 20, hwnd, NULL, hInst, NULL);
            s_edProxyUrl = CreateWindowW(L"EDIT", toWide(s_currentConfig.proxy_url).c_str(),
                                         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                         185, 302, 290, 22, hwnd, (HMENU)ID_EDIT_PROXY_URL, hInst, NULL);
            SendMessage(s_edProxyUrl, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // Authenticated Proxy: Username & Password
            CreateWindowW(L"STATIC", L"Логин:", WS_CHILD | WS_VISIBLE, 35, 331, 45, 20, hwnd, NULL, hInst, NULL);
            s_edProxyUser = CreateWindowW(L"EDIT", toWide(s_currentConfig.proxy_user).c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                          85, 329, 145, 22, hwnd, (HMENU)ID_EDIT_PROXY_USER, hInst, NULL);
            SendMessage(s_edProxyUser, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"Пароль:", WS_CHILD | WS_VISIBLE, 245, 331, 55, 20, hwnd, NULL, hInst, NULL);
            s_edProxyPass = CreateWindowW(L"EDIT", toWide(s_currentConfig.proxy_pass).c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
                                          305, 329, 170, 22, hwnd, (HMENU)ID_EDIT_PROXY_PASS, hInst, NULL);
            SendMessage(s_edProxyPass, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // --- Блок 3: Промпты ---
            HWND grpPrompts = CreateWindowW(L"BUTTON", L"📝 Промпты",
                                            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                            20, 380, 470, 135, hwnd, NULL, hInst, NULL);
            SendMessage(grpPrompts, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"Системный промпт (роль/стиль):", WS_CHILD | WS_VISIBLE, 35, 400, 430, 18, hwnd, NULL, hInst, NULL);
            s_edSystemPrompt = CreateWindowW(L"EDIT", toWide(s_currentConfig.system_prompt).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
                                             35, 420, 440, 32, hwnd, (HMENU)ID_EDIT_SYSTEM_PROMPT, hInst, NULL);
            SendMessage(s_edSystemPrompt, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"Промпт пользователя (для скриншота):", WS_CHILD | WS_VISIBLE, 35, 456, 430, 18, hwnd, NULL, hInst, NULL);
            s_edUserPrompt = CreateWindowW(L"EDIT", toWide(s_currentConfig.user_prompt).c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
                                           35, 476, 440, 30, hwnd, (HMENU)ID_EDIT_USER_PROMPT, hInst, NULL);
            SendMessage(s_edUserPrompt, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // --- Блок 4: Контекст ---
            HWND grpCtx = CreateWindowW(L"BUTTON", L"💬 Контекст диалога",
                                        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                        20, 525, 470, 120, hwnd, NULL, hInst, NULL);
            SendMessage(grpCtx, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"Максимум пар диалога (0 = выкл):", WS_CHILD | WS_VISIBLE, 35, 545, 230, 20, hwnd, NULL, hInst, NULL);
            s_edMaxPairs = CreateWindowW(L"EDIT", std::to_wstring(s_currentConfig.max_context_pairs).c_str(),
                                         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP,
                                         270, 543, 60, 22, hwnd, (HMENU)ID_EDIT_MAX_PAIRS, hInst, NULL);
            SendMessage(s_edMaxPairs, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            s_chkPersistCtx = CreateWindowW(L"BUTTON", L"Сохранять контекст между запусками",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                            35, 569, 430, 20, hwnd, (HMENU)ID_CHECK_PERSIST_CTX, hInst, NULL);
            SendMessage(s_chkPersistCtx, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);
            if (s_currentConfig.persist_context) SendMessage(s_chkPersistCtx, BM_SETCHECK, BST_CHECKED, 0);

            s_chkCtxImages = CreateWindowW(L"BUTTON", L"Включать скриншоты в историю запросов",
                                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                           35, 591, 430, 20, hwnd, (HMENU)ID_CHECK_CTX_IMAGES, hInst, NULL);
            SendMessage(s_chkCtxImages, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);
            if (s_currentConfig.include_images_in_context) SendMessage(s_chkCtxImages, BM_SETCHECK, BST_CHECKED, 0);

            CreateWindowW(L"STATIC", L"🧹 Очистить контекст (хоткей):", WS_CHILD | WS_VISIBLE, 35, 615, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkResetContext = CreateWindowW(L"EDIT", toWide(s_currentConfig.reset_context_key.empty() ? "ctrl+shift+x" : s_currentConfig.reset_context_key).c_str(),
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                               260, 613, 215, 22, hwnd, (HMENU)ID_EDIT_HK_RESET_CONTEXT, hInst, NULL);
            SendMessage(s_edHkResetContext, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);


            // =========================================================================
            //  ПРАВАЯ КОЛОНКА (X = 510, W = 475): Все горячие клавиши (Бинды)
            // =========================================================================

            // --- Блок 1: Основные хоткеи ---
            HWND grpHotkeys = CreateWindowW(L"BUTTON", L"🔥 Основные горячие клавиши",
                                             WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                             510, 45, 475, 145, hwnd, NULL, hInst, NULL);
            SendMessage(grpHotkeys, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"📸 Скриншот и анализ:", WS_CHILD | WS_VISIBLE, 525, 70, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkScreenshot = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.hotkeys, "screenshot", "esc")).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                             755, 68, 215, 22, hwnd, (HMENU)ID_EDIT_HK_SCREENSHOT, hInst, NULL);
            SendMessage(s_edHkScreenshot, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🎯 Следование курсору:", WS_CHILD | WS_VISIBLE, 525, 96, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkFollow = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.hotkeys, "toggle_follow", "right ctrl")).c_str(),
                                         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                         755, 94, 215, 22, hwnd, (HMENU)ID_EDIT_HK_FOLLOW, hInst, NULL);
            SendMessage(s_edHkFollow, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🔄 Сброс UI / позиции:", WS_CHILD | WS_VISIBLE, 525, 122, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkReset = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.hotkeys, "reset", "ctrl+shift+r")).c_str(),
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                        755, 120, 215, 22, hwnd, (HMENU)ID_EDIT_HK_RESET, hInst, NULL);
            SendMessage(s_edHkReset, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"💀 Kill Switch (выход):", WS_CHILD | WS_VISIBLE, 525, 148, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkKill = CreateWindowW(L"EDIT", toWide(s_currentConfig.kill_key.empty() ? "ctrl+shift+q" : s_currentConfig.kill_key).c_str(),
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                       755, 146, 215, 22, hwnd, (HMENU)ID_EDIT_HK_KILL, hInst, NULL);
            SendMessage(s_edHkKill, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // --- Блок 2: Прозрачность и видимость ---
            HWND grpVisibility = CreateWindowW(L"BUTTON", L"👁 Прозрачность и видимость окна",
                                               WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                               510, 200, 475, 145, hwnd, NULL, hInst, NULL);
            SendMessage(grpVisibility, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"🔼 Увеличить видимость (+):", WS_CHILD | WS_VISIBLE, 525, 225, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkIncreaseAlpha = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.visibility_keys, "increase_alpha", "ctrl+up")).c_str(),
                                                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                                755, 223, 215, 22, hwnd, (HMENU)ID_EDIT_HK_INCREASE_ALPHA, hInst, NULL);
            SendMessage(s_edHkIncreaseAlpha, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🔽 Уменьшить видимость (-):", WS_CHILD | WS_VISIBLE, 525, 251, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkDecreaseAlpha = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.visibility_keys, "decrease_alpha", "ctrl+down")).c_str(),
                                                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                                755, 249, 215, 22, hwnd, (HMENU)ID_EDIT_HK_DECREASE_ALPHA, hInst, NULL);
            SendMessage(s_edHkDecreaseAlpha, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"👻 Скрыть / показать окно:", WS_CHILD | WS_VISIBLE, 525, 277, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkToggleInvisible = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.visibility_keys, "toggle_invisible", "ctrl+shift+h")).c_str(),
                                                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                                  755, 275, 215, 22, hwnd, (HMENU)ID_EDIT_HK_TOGGLE_INVISIBLE, hInst, NULL);
            SendMessage(s_edHkToggleInvisible, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🖱 Пропуск кликов (click-through):", WS_CHILD | WS_VISIBLE, 525, 303, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkToggleClickthrough = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.visibility_keys, "toggle_clickthrough", "ctrl+shift+c")).c_str(),
                                                     WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                                     755, 301, 215, 22, hwnd, (HMENU)ID_EDIT_HK_TOGGLE_CLICKTHROUGH, hInst, NULL);
            SendMessage(s_edHkToggleClickthrough, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // --- Блок 3: Размер окна и зоны захвата ---
            HWND grpCapture = CreateWindowW(L"BUTTON", L"📐 Размер окна и зоны захвата",
                                            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                            510, 355, 475, 225, hwnd, NULL, hInst, NULL);
            SendMessage(grpCapture, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"→ Расширить вправо:", WS_CHILD | WS_VISIBLE, 525, 380, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkExpandRight = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "expand_right", "right")).c_str(),
                                              WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                              755, 378, 215, 22, hwnd, (HMENU)ID_EDIT_HK_EXPAND_RIGHT, hInst, NULL);
            SendMessage(s_edHkExpandRight, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"← Сузить слева:", WS_CHILD | WS_VISIBLE, 525, 406, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkShrinkLeft = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "shrink_left", "left")).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                             755, 404, 215, 22, hwnd, (HMENU)ID_EDIT_HK_SHRINK_LEFT, hInst, NULL);
            SendMessage(s_edHkShrinkLeft, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"↓ Расширить вниз:", WS_CHILD | WS_VISIBLE, 525, 432, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkExpandDown = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "expand_down", "down")).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                             755, 430, 215, 22, hwnd, (HMENU)ID_EDIT_HK_EXPAND_DOWN, hInst, NULL);
            SendMessage(s_edHkExpandDown, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"↑ Сузить сверху:", WS_CHILD | WS_VISIBLE, 525, 458, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkShrinkUp = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "shrink_up", "up")).c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                           755, 456, 215, 22, hwnd, (HMENU)ID_EDIT_HK_SHRINK_UP, hInst, NULL);
            SendMessage(s_edHkShrinkUp, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"⤢ Расширить во все стороны:", WS_CHILD | WS_VISIBLE, 525, 484, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkExpandAll = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "expand_all", "mouse wheel up")).c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                            755, 482, 215, 22, hwnd, (HMENU)ID_EDIT_HK_EXPAND_ALL, hInst, NULL);
            SendMessage(s_edHkExpandAll, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"⤡ Сузить со всех сторон:", WS_CHILD | WS_VISIBLE, 525, 510, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkShrinkAll = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "shrink_all", "mouse wheel down")).c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                            755, 508, 215, 22, hwnd, (HMENU)ID_EDIT_HK_SHRINK_ALL, hInst, NULL);
            SendMessage(s_edHkShrinkAll, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"⏹ Сброс зоны захвата:", WS_CHILD | WS_VISIBLE, 525, 536, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkResetCapture = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "reset_capture", "ctrl+0")).c_str(),
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                               755, 534, 215, 22, hwnd, (HMENU)ID_EDIT_HK_RESET_CAPTURE, hInst, NULL);
            SendMessage(s_edHkResetCapture, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // Информационный бейдж безопасности
            HWND lblSecurityTip = CreateWindowW(L"STATIC", L"🔒 Секреты (API ключи и пароли) шифруются через Windows DPAPI",
                                                WS_CHILD | WS_VISIBLE, 525, 595, 450, 20, hwnd, NULL, hInst, NULL);
            SendMessage(lblSecurityTip, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // --- Кнопки снизу ---
            HWND btnCancel = CreateWindowW(L"BUTTON", L"Отмена",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                           715, 645, 110, 35, hwnd, (HMENU)ID_BTN_CANCEL, hInst, NULL);
            SendMessage(btnCancel, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            HWND btnSave = CreateWindowW(L"BUTTON", L"💾 Сохранить и запустить",
                                         WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                         835, 645, 150, 35, hwnd, (HMENU)ID_BTN_SAVE_START, hInst, NULL);
            SendMessage(btnSave, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            updateBackendControlsState();
            return 0;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, RGB(255, 255, 255));
            SetBkColor(hdcEdit, RGB(42, 42, 42));
            return (LRESULT)s_editBgBrush;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(230, 230, 230));
            SetBkColor(hdc, RGB(30, 30, 30));
            return (LRESULT)s_darkBgBrush;
        }

        case WM_CTLCOLORDLG: {
            return (LRESULT)s_darkBgBrush;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_RADIO_OLLAMA || id == ID_RADIO_OPENROUTER) {
                updateBackendControlsState();
            } else if (id == ID_BTN_SAVE_START) {
                // Считываем поля
                AppConfig cfg = s_currentConfig;
                bool isOllama = (SendMessage(s_rdoOllama, BM_GETCHECK, 0, 0) == BST_CHECKED);
                cfg.backend = isOllama ? "ollama" : "openrouter";
                cfg.ollama_url = getControlText(s_edOllamaUrl);
                cfg.ollama_model = getControlText(s_edOllamaModel);
                cfg.openrouter_api_key = getControlText(s_edOpenRouterKey);
                cfg.openrouter_model = getControlText(s_edOpenRouterModel);
                if (cfg.openrouter_model.empty()) {
                    cfg.openrouter_model = "meta-llama/llama-3.2-11b-vision-instruct:free";
                }

                // Resilient Network: Cascade Fallback & Proxy
                cfg.fallback_url = getControlText(s_edFallbackUrl);
                cfg.fallback_api_key = getControlText(s_edFallbackKey);
                cfg.enable_local_fallback = (SendMessage(s_chkLocalFallback, BM_GETCHECK, 0, 0) == BST_CHECKED);
                cfg.proxy_url = getControlText(s_edProxyUrl);
                cfg.proxy_user = getControlText(s_edProxyUser);
                cfg.proxy_pass = getControlText(s_edProxyPass);
                if (cfg.fallback_model.empty()) {
                    cfg.fallback_model = cfg.openrouter_model;
                }

                cfg.system_prompt = getControlText(s_edSystemPrompt);
                cfg.user_prompt = getControlText(s_edUserPrompt);

                // Основные клавиши
                cfg.hotkeys["screenshot"] = getControlText(s_edHkScreenshot);
                cfg.hotkeys["toggle_follow"] = getControlText(s_edHkFollow);
                cfg.hotkeys["reset"] = getControlText(s_edHkReset);
                cfg.kill_key = getControlText(s_edHkKill);

                // Клавиши видимости и прозрачности
                cfg.visibility_keys["increase_alpha"] = getControlText(s_edHkIncreaseAlpha);
                cfg.visibility_keys["decrease_alpha"] = getControlText(s_edHkDecreaseAlpha);
                cfg.visibility_keys["toggle_invisible"] = getControlText(s_edHkToggleInvisible);
                cfg.visibility_keys["toggle_clickthrough"] = getControlText(s_edHkToggleClickthrough);

                // Клавиши изменения размера и зоны захвата
                cfg.capture_keys["expand_right"] = getControlText(s_edHkExpandRight);
                cfg.capture_keys["shrink_left"] = getControlText(s_edHkShrinkLeft);
                cfg.capture_keys["expand_down"] = getControlText(s_edHkExpandDown);
                cfg.capture_keys["shrink_up"] = getControlText(s_edHkShrinkUp);
                cfg.capture_keys["expand_all"] = getControlText(s_edHkExpandAll);
                cfg.capture_keys["shrink_all"] = getControlText(s_edHkShrinkAll);
                cfg.capture_keys["reset_capture"] = getControlText(s_edHkResetCapture);

                // Контекст
                std::string pairsStr = getControlText(s_edMaxPairs);
                if (!pairsStr.empty()) {
                    try {
                        cfg.max_context_pairs = (std::max)(0, std::stoi(pairsStr));
                    } catch (...) {
                        cfg.max_context_pairs = 10;
                    }
                }
                cfg.persist_context = (SendMessage(s_chkPersistCtx, BM_GETCHECK, 0, 0) == BST_CHECKED);
                cfg.include_images_in_context = (SendMessage(s_chkCtxImages, BM_GETCHECK, 0, 0) == BST_CHECKED);
                cfg.reset_context_key = getControlText(s_edHkResetContext);

                cfg.save();
                s_resultConfig = cfg;
                DestroyWindow(hwnd);
            } else if (id == ID_BTN_CANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY: {
            if (s_fontBold) { DeleteObject(s_fontBold); s_fontBold = NULL; }
            if (s_fontNormal) { DeleteObject(s_fontNormal); s_fontNormal = NULL; }
            if (s_darkBgBrush) { DeleteObject(s_darkBgBrush); s_darkBgBrush = NULL; }
            if (s_editBgBrush) { DeleteObject(s_editBgBrush); s_editBgBrush = NULL; }
            PostQuitMessage(0);
            return 0;
        }
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

std::optional<AppConfig> SetupUI::showSetupDialog(const AppConfig& initialConfig) {
    s_currentConfig = initialConfig;
    s_resultConfig = std::nullopt;

    HINSTANCE hInstance = GetModuleHandle(NULL);
    const wchar_t* CLASS_NAME = L"GhostAI_SetupDialogClass";

    WNDCLASSEXW existingWc = { 0 };
    if (!GetClassInfoExW(hInstance, CLASS_NAME, &existingWc)) {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = SetupWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = CLASS_NAME;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassExW(&wc);
    }

    int winW = 1020;
    int winH = 740;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (std::max)(0, (screenW - winW) / 2);
    int posY = (std::max)(0, (screenH - winH) / 2);

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        CLASS_NAME,
        L"Ghost-AI — Setup",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return s_resultConfig;
}
