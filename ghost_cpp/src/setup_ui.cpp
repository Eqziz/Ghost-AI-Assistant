#include "setup_ui.hpp"
#include <windows.h>
#include <commctrl.h>
#include <string>

#pragma comment(lib, "comctl32.lib")

namespace {
    AppConfig s_currentConfig;
    std::optional<AppConfig> s_resultConfig = std::nullopt;

    // Controls IDs
    enum {
        ID_RADIO_OLLAMA = 1001,
        ID_RADIO_OPENROUTER,
        ID_EDIT_OLLAMA_URL,
        ID_EDIT_OLLAMA_MODEL,
        ID_EDIT_OPENROUTER_KEY,
        ID_EDIT_OPENROUTER_MODEL,
        ID_EDIT_SYSTEM_PROMPT,
        ID_EDIT_USER_PROMPT,
        // Basic hotkeys
        ID_EDIT_HK_SCREENSHOT,
        ID_EDIT_HK_FOLLOW,
        ID_EDIT_HK_RESET,
        ID_EDIT_HK_KILL,
        // Visibility hotkeys
        ID_EDIT_HK_INCREASE_ALPHA,
        ID_EDIT_HK_DECREASE_ALPHA,
        ID_EDIT_HK_TOGGLE_INVISIBLE,
        ID_EDIT_HK_TOGGLE_CLICKTHROUGH,
        // Capture / size hotkeys
        ID_EDIT_HK_EXPAND_RIGHT,
        ID_EDIT_HK_SHRINK_LEFT,
        ID_EDIT_HK_EXPAND_DOWN,
        ID_EDIT_HK_SHRINK_UP,
        ID_EDIT_HK_EXPAND_ALL,
        ID_EDIT_HK_SHRINK_ALL,
        ID_EDIT_HK_RESET_CAPTURE,
        // Context
        ID_EDIT_MAX_PAIRS,
        ID_CHECK_PERSIST_CTX,
        ID_CHECK_CTX_IMAGES,
        ID_EDIT_HK_RESET_CONTEXT,
        ID_BTN_SAVE_START,
        ID_BTN_CANCEL
    };

    HWND s_rdoOllama = NULL;
    HWND s_rdoOpenRouter = NULL;
    HWND s_edOllamaUrl = NULL;
    HWND s_edOllamaModel = NULL;
    HWND s_edOpenRouterKey = NULL;
    HWND s_edOpenRouterModel = NULL;
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
        if (len <= 0) return "";
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
            //  ЛЕВАЯ КОЛОНКА (X = 20, W = 460): Backend, Промпты, Контекст
            // =========================================================================

            // --- Блок Backend ---
            HWND grpBackend = CreateWindowW(L"BUTTON", L"Режим работы (Backend)",
                                           WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                           20, 45, 460, 165, hwnd, NULL, hInst, NULL);
            SendMessage(grpBackend, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            s_rdoOllama = CreateWindowW(L"BUTTON", L"🦙 Ollama (локально)",
                                       WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                                       35, 68, 190, 20, hwnd, (HMENU)ID_RADIO_OLLAMA, hInst, NULL);
            SendMessage(s_rdoOllama, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            s_rdoOpenRouter = CreateWindowW(L"BUTTON", L"☁️ OpenRouter (облако)",
                                           WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
                                           235, 68, 200, 20, hwnd, (HMENU)ID_RADIO_OPENROUTER, hInst, NULL);
            SendMessage(s_rdoOpenRouter, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // Ollama URL & Model
            CreateWindowW(L"STATIC", L"Ollama URL:", WS_CHILD | WS_VISIBLE, 35, 94, 110, 20, hwnd, NULL, hInst, NULL);
            s_edOllamaUrl = CreateWindowW(L"EDIT", toWide(s_currentConfig.ollama_url).c_str(),
                                         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                         150, 92, 315, 22, hwnd, (HMENU)ID_EDIT_OLLAMA_URL, hInst, NULL);
            SendMessage(s_edOllamaUrl, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"Ollama Model:", WS_CHILD | WS_VISIBLE, 35, 119, 110, 20, hwnd, NULL, hInst, NULL);
            s_edOllamaModel = CreateWindowW(L"EDIT", toWide(s_currentConfig.ollama_model).c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                           150, 117, 315, 22, hwnd, (HMENU)ID_EDIT_OLLAMA_MODEL, hInst, NULL);
            SendMessage(s_edOllamaModel, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // OpenRouter Key & Model
            CreateWindowW(L"STATIC", L"OpenRouter Key:", WS_CHILD | WS_VISIBLE, 35, 144, 110, 20, hwnd, NULL, hInst, NULL);
            s_edOpenRouterKey = CreateWindowW(L"EDIT", toWide(s_currentConfig.openrouter_api_key).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
                                             150, 142, 315, 22, hwnd, (HMENU)ID_EDIT_OPENROUTER_KEY, hInst, NULL);
            SendMessage(s_edOpenRouterKey, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"OpenRouter Model:", WS_CHILD | WS_VISIBLE, 35, 169, 120, 20, hwnd, NULL, hInst, NULL);
            s_edOpenRouterModel = CreateWindowW(L"EDIT", toWide(s_currentConfig.openrouter_model).c_str(),
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP,
                                               150, 167, 315, 22, hwnd, (HMENU)ID_EDIT_OPENROUTER_MODEL, hInst, NULL);
            SendMessage(s_edOpenRouterModel, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            if (s_currentConfig.backend == "openrouter") {
                SendMessage(s_rdoOpenRouter, BM_SETCHECK, BST_CHECKED, 0);
            } else {
                SendMessage(s_rdoOllama, BM_SETCHECK, BST_CHECKED, 0);
            }

            // --- Блок Промпты ---
            HWND grpPrompts = CreateWindowW(L"BUTTON", L"📝 Промпты",
                                           WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                           20, 220, 460, 155, hwnd, NULL, hInst, NULL);
            SendMessage(grpPrompts, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"Системный промпт (роль/стиль):", WS_CHILD | WS_VISIBLE, 35, 242, 400, 18, hwnd, NULL, hInst, NULL);
            s_edSystemPrompt = CreateWindowW(L"EDIT", toWide(s_currentConfig.system_prompt).c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
                                            35, 262, 430, 38, hwnd, (HMENU)ID_EDIT_SYSTEM_PROMPT, hInst, NULL);
            SendMessage(s_edSystemPrompt, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"Промпт пользователя (для скриншота):", WS_CHILD | WS_VISIBLE, 35, 305, 400, 18, hwnd, NULL, hInst, NULL);
            s_edUserPrompt = CreateWindowW(L"EDIT", toWide(s_currentConfig.user_prompt).c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
                                          35, 325, 430, 34, hwnd, (HMENU)ID_EDIT_USER_PROMPT, hInst, NULL);
            SendMessage(s_edUserPrompt, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // --- Блок Контекст ---
            HWND grpCtx = CreateWindowW(L"BUTTON", L"💬 Контекст диалога",
                                       WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                       20, 385, 460, 155, hwnd, NULL, hInst, NULL);
            SendMessage(grpCtx, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"Максимум пар диалога (0 = выкл):", WS_CHILD | WS_VISIBLE, 35, 408, 230, 20, hwnd, NULL, hInst, NULL);
            s_edMaxPairs = CreateWindowW(L"EDIT", std::to_wstring(s_currentConfig.max_context_pairs).c_str(),
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP,
                                        270, 406, 60, 22, hwnd, (HMENU)ID_EDIT_MAX_PAIRS, hInst, NULL);
            SendMessage(s_edMaxPairs, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            s_chkPersistCtx = CreateWindowW(L"BUTTON", L"Сохранять контекст между запусками",
                                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                           35, 432, 410, 20, hwnd, (HMENU)ID_CHECK_PERSIST_CTX, hInst, NULL);
            SendMessage(s_chkPersistCtx, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);
            if (s_currentConfig.persist_context) SendMessage(s_chkPersistCtx, BM_SETCHECK, BST_CHECKED, 0);

            s_chkCtxImages = CreateWindowW(L"BUTTON", L"Включать скриншоты в историю запросов",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                          35, 455, 410, 20, hwnd, (HMENU)ID_CHECK_CTX_IMAGES, hInst, NULL);
            SendMessage(s_chkCtxImages, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);
            if (s_currentConfig.include_images_in_context) SendMessage(s_chkCtxImages, BM_SETCHECK, BST_CHECKED, 0);

            CreateWindowW(L"STATIC", L"🧹 Очистить контекст (хоткей):", WS_CHILD | WS_VISIBLE, 35, 482, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkResetContext = CreateWindowW(L"EDIT", toWide(s_currentConfig.reset_context_key.empty() ? "ctrl+shift+x" : s_currentConfig.reset_context_key).c_str(),
                                              WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                              260, 480, 205, 22, hwnd, (HMENU)ID_EDIT_HK_RESET_CONTEXT, hInst, NULL);
            SendMessage(s_edHkResetContext, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);


            // =========================================================================
            //  ПРАВАЯ КОЛОНКА (X = 500, W = 460): Все горячие клавиши (Бинды)
            // =========================================================================

            // --- Блок 1: Основные хоткеи ---
            HWND grpHotkeys = CreateWindowW(L"BUTTON", L"🔥 Основные горячие клавиши",
                                            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                            500, 45, 460, 135, hwnd, NULL, hInst, NULL);
            SendMessage(grpHotkeys, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"📸 Скриншот и анализ:", WS_CHILD | WS_VISIBLE, 515, 70, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkScreenshot = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.hotkeys, "screenshot", "esc")).c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                            740, 68, 205, 22, hwnd, (HMENU)ID_EDIT_HK_SCREENSHOT, hInst, NULL);
            SendMessage(s_edHkScreenshot, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🎯 Следование курсору:", WS_CHILD | WS_VISIBLE, 515, 95, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkFollow = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.hotkeys, "toggle_follow", "right ctrl")).c_str(),
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                        740, 93, 205, 22, hwnd, (HMENU)ID_EDIT_HK_FOLLOW, hInst, NULL);
            SendMessage(s_edHkFollow, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🔄 Сброс UI / позиции:", WS_CHILD | WS_VISIBLE, 515, 120, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkReset = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.hotkeys, "reset", "ctrl+shift+r")).c_str(),
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                       740, 118, 205, 22, hwnd, (HMENU)ID_EDIT_HK_RESET, hInst, NULL);
            SendMessage(s_edHkReset, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"💀 Kill Switch (выход):", WS_CHILD | WS_VISIBLE, 515, 145, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkKill = CreateWindowW(L"EDIT", toWide(s_currentConfig.kill_key.empty() ? "ctrl+shift+q" : s_currentConfig.kill_key).c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                      740, 143, 205, 22, hwnd, (HMENU)ID_EDIT_HK_KILL, hInst, NULL);
            SendMessage(s_edHkKill, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // --- Блок 2: Прозрачность и видимость ---
            HWND grpVisibility = CreateWindowW(L"BUTTON", L"👁 Прозрачность и видимость окна",
                                              WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                              500, 190, 460, 135, hwnd, NULL, hInst, NULL);
            SendMessage(grpVisibility, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"🔼 Увеличить видимость (+):", WS_CHILD | WS_VISIBLE, 515, 215, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkIncreaseAlpha = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.visibility_keys, "increase_alpha", "ctrl+up")).c_str(),
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                               740, 213, 205, 22, hwnd, (HMENU)ID_EDIT_HK_INCREASE_ALPHA, hInst, NULL);
            SendMessage(s_edHkIncreaseAlpha, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🔽 Уменьшить видимость (-):", WS_CHILD | WS_VISIBLE, 515, 240, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkDecreaseAlpha = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.visibility_keys, "decrease_alpha", "ctrl+down")).c_str(),
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                               740, 238, 205, 22, hwnd, (HMENU)ID_EDIT_HK_DECREASE_ALPHA, hInst, NULL);
            SendMessage(s_edHkDecreaseAlpha, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"👻 Скрыть / показать окно:", WS_CHILD | WS_VISIBLE, 515, 265, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkToggleInvisible = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.visibility_keys, "toggle_invisible", "ctrl+shift+h")).c_str(),
                                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                                 740, 263, 205, 22, hwnd, (HMENU)ID_EDIT_HK_TOGGLE_INVISIBLE, hInst, NULL);
            SendMessage(s_edHkToggleInvisible, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🖱 Пропуск кликов (click-through):", WS_CHILD | WS_VISIBLE, 515, 290, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkToggleClickthrough = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.visibility_keys, "toggle_clickthrough", "ctrl+shift+c")).c_str(),
                                                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                                    740, 288, 205, 22, hwnd, (HMENU)ID_EDIT_HK_TOGGLE_CLICKTHROUGH, hInst, NULL);
            SendMessage(s_edHkToggleClickthrough, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // --- Блок 3: Размер окна и зоны захвата ---
            HWND grpCapture = CreateWindowW(L"BUTTON", L"📐 Размер окна и зоны захвата",
                                           WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                           500, 335, 460, 205, hwnd, NULL, hInst, NULL);
            SendMessage(grpCapture, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"→ Расширить вправо:", WS_CHILD | WS_VISIBLE, 515, 357, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkExpandRight = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "expand_right", "right")).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                             740, 355, 205, 22, hwnd, (HMENU)ID_EDIT_HK_EXPAND_RIGHT, hInst, NULL);
            SendMessage(s_edHkExpandRight, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"← Сузить слева:", WS_CHILD | WS_VISIBLE, 515, 380, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkShrinkLeft = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "shrink_left", "left")).c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                            740, 378, 205, 22, hwnd, (HMENU)ID_EDIT_HK_SHRINK_LEFT, hInst, NULL);
            SendMessage(s_edHkShrinkLeft, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"↓ Расширить вниз:", WS_CHILD | WS_VISIBLE, 515, 403, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkExpandDown = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "expand_down", "down")).c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                            740, 401, 205, 22, hwnd, (HMENU)ID_EDIT_HK_EXPAND_DOWN, hInst, NULL);
            SendMessage(s_edHkExpandDown, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"↑ Сузить сверху:", WS_CHILD | WS_VISIBLE, 515, 426, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkShrinkUp = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "shrink_up", "up")).c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                          740, 424, 205, 22, hwnd, (HMENU)ID_EDIT_HK_SHRINK_UP, hInst, NULL);
            SendMessage(s_edHkShrinkUp, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"⤢ Расширить во все стороны:", WS_CHILD | WS_VISIBLE, 515, 449, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkExpandAll = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "expand_all", "mouse wheel up")).c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                           740, 447, 205, 22, hwnd, (HMENU)ID_EDIT_HK_EXPAND_ALL, hInst, NULL);
            SendMessage(s_edHkExpandAll, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"⤡ Сузить со всех сторон:", WS_CHILD | WS_VISIBLE, 515, 472, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkShrinkAll = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "shrink_all", "mouse wheel down")).c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                           740, 470, 205, 22, hwnd, (HMENU)ID_EDIT_HK_SHRINK_ALL, hInst, NULL);
            SendMessage(s_edHkShrinkAll, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"⏹ Сброс зоны захвата:", WS_CHILD | WS_VISIBLE, 515, 495, 220, 20, hwnd, NULL, hInst, NULL);
            s_edHkResetCapture = CreateWindowW(L"EDIT", toWide(getMapKey(s_currentConfig.capture_keys, "reset_capture", "ctrl+0")).c_str(),
                                              WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                              740, 493, 205, 22, hwnd, (HMENU)ID_EDIT_HK_RESET_CAPTURE, hInst, NULL);
            SendMessage(s_edHkResetCapture, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // --- Кнопки снизу ---
            HWND btnCancel = CreateWindowW(L"BUTTON", L"Отмена",
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                          710, 555, 110, 35, hwnd, (HMENU)ID_BTN_CANCEL, hInst, NULL);
            SendMessage(btnCancel, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            HWND btnSave = CreateWindowW(L"BUTTON", L"💾 Сохранить и запустить",
                                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                        830, 555, 130, 35, hwnd, (HMENU)ID_BTN_SAVE_START, hInst, NULL);
            SendMessage(btnSave, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            updateBackendControlsState();
            return 0;
        }

        case WM_CTLCOLOREDIT: {
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

    int winW = 995;
    int winH = 650;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

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
