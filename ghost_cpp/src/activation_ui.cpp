#include "activation_ui.hpp"
#include <windows.h>
#include <shellapi.h>

namespace {
    std::optional<LicenseData> s_activatedData = std::nullopt;
    HWND s_keyEdit = NULL;
    HWND s_statusLabel = NULL;
    HWND s_btnActivate = NULL;
    HWND s_btnBuy = NULL;
    HFONT s_titleFont = NULL;
    HFONT s_subFont = NULL;
    HFONT s_editFont = NULL;
    HBRUSH s_darkBgBrush = NULL;
    HBRUSH s_editBgBrush = NULL;

    LRESULT CALLBACK ActivationWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_CREATE: {
            s_titleFont = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            s_subFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            s_editFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
            s_darkBgBrush = CreateSolidBrush(RGB(30, 30, 30));
            s_editBgBrush = CreateSolidBrush(RGB(42, 42, 42));

            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

            s_keyEdit = CreateWindowW(L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                      40, 130, 430, 32, hwnd, (HMENU)201, hInst, NULL);
            SendMessage(s_keyEdit, WM_SETFONT, (WPARAM)s_editFont, TRUE);

            s_btnActivate = CreateWindowW(L"BUTTON", L"✓ Активировать",
                                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                          70, 200, 170, 40, hwnd, (HMENU)202, hInst, NULL);
            SendMessage(s_btnActivate, WM_SETFONT, (WPARAM)s_subFont, TRUE);

            s_btnBuy = CreateWindowW(L"BUTTON", L"💫 Купить в Telegram",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     260, 200, 180, 40, hwnd, (HMENU)203, hInst, NULL);
            SendMessage(s_btnBuy, WM_SETFONT, (WPARAM)s_subFont, TRUE);

            s_statusLabel = CreateWindowW(L"STATIC", L"",
                                          WS_CHILD | WS_VISIBLE | SS_CENTER,
                                          30, 255, 450, 40, hwnd, (HMENU)204, hInst, NULL);
            SendMessage(s_statusLabel, WM_SETFONT, (WPARAM)s_subFont, TRUE);

            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, RGB(255, 255, 255));
            SetBkColor(hdcEdit, RGB(42, 42, 42));
            return (LRESULT)s_editBgBrush;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hStatic = (HWND)lParam;
            if (hStatic == s_statusLabel) {
                SetTextColor(hdcStatic, RGB(255, 85, 85));
            } else {
                SetTextColor(hdcStatic, RGB(200, 200, 200));
            }
            SetBkColor(hdcStatic, RGB(30, 30, 30));
            return (LRESULT)s_darkBgBrush;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, s_darkBgBrush);

            HFONT oldFont = (HFONT)SelectObject(hdc, s_titleFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 212, 255));
            RECT titleRc = { 0, 20, rc.right, 55 };
            DrawTextW(hdc, L"👻 Ghost AI", -1, &titleRc, DT_CENTER | DT_SINGLELINE);

            SelectObject(hdc, s_subFont);
            SetTextColor(hdc, RGB(180, 180, 180));
            RECT subRc = { 0, 60, rc.right, 110 };
            DrawTextW(hdc, L"Введите лицензионный ключ\n(получен в @gghostaibot после оплаты)", -1, &subRc, DT_CENTER);

            RECT formatRc = { 0, 168, rc.right, 190 };
            SetTextColor(hdc, RGB(120, 120, 120));
            DrawTextW(hdc, L"Формат: <base64>.<hmac_hex>", -1, &formatRc, DT_CENTER | DT_SINGLELINE);

            SelectObject(hdc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == 202) { // Активировать
                wchar_t keyBuf[512] = { 0 };
                GetWindowTextW(s_keyEdit, keyBuf, 512);

                int len = WideCharToMultiByte(CP_UTF8, 0, keyBuf, -1, NULL, 0, NULL, NULL);
                std::string key(len - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, keyBuf, -1, &key[0], len, NULL, NULL);

                while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
                while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();

                if (key.empty()) {
                    SetWindowTextW(s_statusLabel, L"⚠️ Введите ключ");
                    return 0;
                }

                auto optData = LicenseManager::verifyKey(key);
                if (!optData) {
                    SetWindowTextW(s_statusLabel, L"❌ Невалидный ключ. Проверьте правильность ввода.");
                    return 0;
                }

                if (optData->isExpired()) {
                    SetWindowTextW(s_statusLabel, L"❌ Срок действия лицензии истёк.");
                    return 0;
                }

                if (!LicenseManager::saveLicense(key)) {
                    SetWindowTextW(s_statusLabel, L"❌ Не удалось сохранить лицензию.");
                    return 0;
                }

                s_activatedData = optData;
                DestroyWindow(hwnd);
            } else if (id == 203) { // Купить в Telegram
                ShellExecuteA(NULL, "open", "https://t.me/gghostaibot", NULL, NULL, SW_SHOWNORMAL);
            }
            return 0;
        }

        case WM_DESTROY: {
            if (s_titleFont) { DeleteObject(s_titleFont); s_titleFont = NULL; }
            if (s_subFont) { DeleteObject(s_subFont); s_subFont = NULL; }
            if (s_editFont) { DeleteObject(s_editFont); s_editFont = NULL; }
            if (s_darkBgBrush) { DeleteObject(s_darkBgBrush); s_darkBgBrush = NULL; }
            if (s_editBgBrush) { DeleteObject(s_editBgBrush); s_editBgBrush = NULL; }
            PostQuitMessage(0);
            return 0;
        }
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

std::optional<LicenseData> ActivationUI::showActivationDialog() {
    s_activatedData = std::nullopt;

    HINSTANCE hInstance = GetModuleHandle(NULL);
    const wchar_t* CLASS_NAME = L"GhostAI_ActivationClass";

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = ActivationWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 30));
    RegisterClassExW(&wc);

    int winW = 530;
    int winH = 340;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        CLASS_NAME,
        L"Ghost AI — Активация",
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

    return s_activatedData;
}
