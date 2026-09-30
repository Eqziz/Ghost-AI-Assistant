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
        ID_EDIT_HK_SCREENSHOT,
        ID_EDIT_HK_FOLLOW,
        ID_EDIT_HK_RESET,
        ID_EDIT_HK_KILL,
        ID_EDIT_MAX_PAIRS,
        ID_CHECK_PERSIST_CTX,
        ID_CHECK_CTX_IMAGES,
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
    HWND s_edHkScreenshot = NULL;
    HWND s_edHkFollow = NULL;
    HWND s_edHkReset = NULL;
    HWND s_edHkKill = NULL;
    HWND s_edMaxPairs = NULL;
    HWND s_chkPersistCtx = NULL;
    HWND s_chkCtxImages = NULL;

    HFONT s_fontBold = NULL;
    HFONT s_fontNormal = NULL;
    HBRUSH s_darkBgBrush = NULL;
    HBRUSH s_editBgBrush = NULL;

    std::wstring toWide(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        std::wstring w(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &w[0], size);
        return w;
    }

    std::string toUtf8(const wchar_t* wstr) {
        if (!wstr) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
        if (size <= 1) return "";
        std::string s(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &s[0], size, NULL, NULL);
        return s;
    }

    std::string getControlText(HWND hwnd) {
        int len = GetWindowTextLengthW(hwnd);
        if (len <= 0) return "";
        std::vector<wchar_t> buf(len + 1);
        GetWindowTextW(hwnd, buf.data(), len + 1);
        return toUtf8(buf.data());
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

            int y = 15;
            // Заголовок
            HWND lblTitle = CreateWindowW(L"STATIC", L"👻 Ghost-AI Setup",
                                         WS_CHILD | WS_VISIBLE, 20, y, 400, 24, hwnd, NULL, hInst, NULL);
            SendMessage(lblTitle, WM_SETFONT, (WPARAM)s_fontBold, TRUE);
            y += 30;

            // --- Блок Backend ---
            HWND grpBackend = CreateWindowW(L"BUTTON", L"Режим работы (Backend)",
                                           WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                           15, y, 545, 160, hwnd, NULL, hInst, NULL);
            SendMessage(grpBackend, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            s_rdoOllama = CreateWindowW(L"BUTTON", L"🦙 Ollama (локально)",
                                       WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                                       30, y + 25, 200, 20, hwnd, (HMENU)ID_RADIO_OLLAMA, hInst, NULL);
            SendMessage(s_rdoOllama, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            s_rdoOpenRouter = CreateWindowW(L"BUTTON", L"☁️ OpenRouter (облако)",
                                           WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                           260, y + 25, 200, 20, hwnd, (HMENU)ID_RADIO_OPENROUTER, hInst, NULL);
            SendMessage(s_rdoOpenRouter, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // Ollama URL & Model
            CreateWindowW(L"STATIC", L"Ollama URL:", WS_CHILD | WS_VISIBLE, 30, y + 55, 110, 20, hwnd, NULL, hInst, NULL);
            s_edOllamaUrl = CreateWindowW(L"EDIT", toWide(s_currentConfig.ollama_url).c_str(),
                                         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                         150, y + 53, 385, 22, hwnd, (HMENU)ID_EDIT_OLLAMA_URL, hInst, NULL);
            SendMessage(s_edOllamaUrl, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"Ollama Model:", WS_CHILD | WS_VISIBLE, 30, y + 80, 110, 20, hwnd, NULL, hInst, NULL);
            s_edOllamaModel = CreateWindowW(L"EDIT", toWide(s_currentConfig.ollama_model).c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                           150, y + 78, 385, 22, hwnd, (HMENU)ID_EDIT_OLLAMA_MODEL, hInst, NULL);
            SendMessage(s_edOllamaModel, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            // OpenRouter Key & Model
            CreateWindowW(L"STATIC", L"OpenRouter Key:", WS_CHILD | WS_VISIBLE, 30, y + 105, 110, 20, hwnd, NULL, hInst, NULL);
            s_edOpenRouterKey = CreateWindowW(L"EDIT", toWide(s_currentConfig.openrouter_api_key).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                                             150, y + 103, 385, 22, hwnd, (HMENU)ID_EDIT_OPENROUTER_KEY, hInst, NULL);
            SendMessage(s_edOpenRouterKey, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"OpenRouter Model:", WS_CHILD | WS_VISIBLE, 30, y + 130, 120, 20, hwnd, NULL, hInst, NULL);
            s_edOpenRouterModel = CreateWindowW(L"EDIT", toWide(s_currentConfig.openrouter_model).c_str(),
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                                               150, y + 128, 385, 22, hwnd, (HMENU)ID_EDIT_OPENROUTER_MODEL, hInst, NULL);
            SendMessage(s_edOpenRouterModel, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            if (s_currentConfig.backend == "openrouter") {
                SendMessage(s_rdoOpenRouter, BM_SETCHECK, BST_CHECKED, 0);
            } else {
                SendMessage(s_rdoOllama, BM_SETCHECK, BST_CHECKED, 0);
            }

            y += 175;

            // --- Блок Промпты ---
            HWND grpPrompts = CreateWindowW(L"BUTTON", L"📝 Промпты",
                                           WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                           15, y, 545, 150, hwnd, NULL, hInst, NULL);
            SendMessage(grpPrompts, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"Системный промпт (роль/стиль):", WS_CHILD | WS_VISIBLE, 30, y + 25, 300, 18, hwnd, NULL, hInst, NULL);
            s_edSystemPrompt = CreateWindowW(L"EDIT", toWide(s_currentConfig.system_prompt).c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
                                            30, y + 45, 505, 38, hwnd, (HMENU)ID_EDIT_SYSTEM_PROMPT, hInst, NULL);
            SendMessage(s_edSystemPrompt, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"Промпт пользователя (для каждого скриншота):", WS_CHILD | WS_VISIBLE, 30, y + 88, 350, 18, hwnd, NULL, hInst, NULL);
            s_edUserPrompt = CreateWindowW(L"EDIT", toWide(s_currentConfig.user_prompt).c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
                                          30, y + 108, 505, 30, hwnd, (HMENU)ID_EDIT_USER_PROMPT, hInst, NULL);
            SendMessage(s_edUserPrompt, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            y += 165;

            // --- Блок Хоткеи ---
            HWND grpHotkeys = CreateWindowW(L"BUTTON", L"🔥 Основные горячие клавиши",
                                            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                            15, y, 545, 125, hwnd, NULL, hInst, NULL);
            SendMessage(grpHotkeys, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"📸 Скриншот и анализ:", WS_CHILD | WS_VISIBLE, 30, y + 25, 180, 20, hwnd, NULL, hInst, NULL);
            s_edHkScreenshot = CreateWindowW(L"EDIT", toWide(s_currentConfig.hotkeys["screenshot"]).c_str(),
                                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                            220, y + 23, 140, 22, hwnd, (HMENU)ID_EDIT_HK_SCREENSHOT, hInst, NULL);
            SendMessage(s_edHkScreenshot, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🎯 Следование курсору:", WS_CHILD | WS_VISIBLE, 30, y + 50, 180, 20, hwnd, NULL, hInst, NULL);
            s_edHkFollow = CreateWindowW(L"EDIT", toWide(s_currentConfig.hotkeys["toggle_follow"]).c_str(),
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                        220, y + 48, 140, 22, hwnd, (HMENU)ID_EDIT_HK_FOLLOW, hInst, NULL);
            SendMessage(s_edHkFollow, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"🔄 Сброс UI:", WS_CHILD | WS_VISIBLE, 30, y + 75, 180, 20, hwnd, NULL, hInst, NULL);
            s_edHkReset = CreateWindowW(L"EDIT", toWide(s_currentConfig.hotkeys["reset"]).c_str(),
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                       220, y + 73, 140, 22, hwnd, (HMENU)ID_EDIT_HK_RESET, hInst, NULL);
            SendMessage(s_edHkReset, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            CreateWindowW(L"STATIC", L"💀 Kill Switch (выход):", WS_CHILD | WS_VISIBLE, 30, y + 100, 180, 20, hwnd, NULL, hInst, NULL);
            s_edHkKill = CreateWindowW(L"EDIT", toWide(s_currentConfig.kill_key).c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                      220, y + 98, 140, 22, hwnd, (HMENU)ID_EDIT_HK_KILL, hInst, NULL);
            SendMessage(s_edHkKill, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            y += 140;

            // --- Блок Контекст ---
            HWND grpCtx = CreateWindowW(L"BUTTON", L"💬 Контекст диалога",
                                       WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                       15, y, 545, 95, hwnd, NULL, hInst, NULL);
            SendMessage(grpCtx, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            CreateWindowW(L"STATIC", L"Максимум пар диалога (0 = выкл):", WS_CHILD | WS_VISIBLE, 30, y + 25, 230, 20, hwnd, NULL, hInst, NULL);
            s_edMaxPairs = CreateWindowW(L"EDIT", std::to_wstring(s_currentConfig.max_context_pairs).c_str(),
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                        270, y + 23, 60, 22, hwnd, (HMENU)ID_EDIT_MAX_PAIRS, hInst, NULL);
            SendMessage(s_edMaxPairs, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);

            s_chkPersistCtx = CreateWindowW(L"BUTTON", L"Сохранять контекст между запусками",
                                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                           30, y + 48, 300, 20, hwnd, (HMENU)ID_CHECK_PERSIST_CTX, hInst, NULL);
            SendMessage(s_chkPersistCtx, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);
            if (s_currentConfig.persist_context) SendMessage(s_chkPersistCtx, BM_SETCHECK, BST_CHECKED, 0);

            s_chkCtxImages = CreateWindowW(L"BUTTON", L"Включать скриншоты в историю запросов",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          30, y + 70, 320, 20, hwnd, (HMENU)ID_CHECK_CTX_IMAGES, hInst, NULL);
            SendMessage(s_chkCtxImages, WM_SETFONT, (WPARAM)s_fontNormal, TRUE);
            if (s_currentConfig.include_images_in_context) SendMessage(s_chkCtxImages, BM_SETCHECK, BST_CHECKED, 0);

            y += 110;

            // --- Кнопки снизу ---
            HWND btnCancel = CreateWindowW(L"BUTTON", L"Отмена",
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          330, y, 100, 35, hwnd, (HMENU)ID_BTN_CANCEL, hInst, NULL);
            SendMessage(btnCancel, WM_SETFONT, (WPARAM)s_fontBold, TRUE);

            HWND btnSave = CreateWindowW(L"BUTTON", L"💾 Сохранить и запустить",
                                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                        440, y, 120, 35, hwnd, (HMENU)ID_BTN_SAVE_START, hInst, NULL);
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

                cfg.hotkeys["screenshot"] = getControlText(s_edHkScreenshot);
                cfg.hotkeys["toggle_follow"] = getControlText(s_edHkFollow);
                cfg.hotkeys["reset"] = getControlText(s_edHkReset);
                cfg.kill_key = getControlText(s_edHkKill);

                std::string pairsStr = getControlText(s_edMaxPairs);
                if (!pairsStr.empty()) {
                    cfg.max_context_pairs = std::stoi(pairsStr);
                }
                cfg.persist_context = (SendMessage(s_chkPersistCtx, BM_GETCHECK, 0, 0) == BST_CHECKED);
                cfg.include_images_in_context = (SendMessage(s_chkCtxImages, BM_GETCHECK, 0, 0) == BST_CHECKED);

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

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = SetupWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 30));
    RegisterClassExW(&wc);

    int winW = 590;
    int winH = 710;
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
