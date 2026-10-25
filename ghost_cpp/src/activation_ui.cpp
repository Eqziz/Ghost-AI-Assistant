#include "activation_ui.hpp"
#include "obfuscator.hpp"
#include "anti_analysis.hpp"
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace {
    std::wstring generateUniqueClassName(const std::wstring& prefix = L"SysAct_") {
        uint8_t randBytes[8] = { 0 };
        if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, randBytes, sizeof(randBytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            uint64_t tick = GetTickCount64() ^ (static_cast<uint64_t>(GetCurrentProcessId()) << 32);
            memcpy(randBytes, &tick, sizeof(randBytes));
        }
        wchar_t buf[64];
        swprintf_s(buf, L"%s%02X%02X%02X%02X%02X%02X%02X%02X",
                   prefix.c_str(),
                   randBytes[0], randBytes[1], randBytes[2], randBytes[3],
                   randBytes[4], randBytes[5], randBytes[6], randBytes[7]);
        return std::wstring(buf);
    }

    std::optional<LicenseData> s_activatedData = std::nullopt;
    HWND s_keyEdit = NULL;
    HWND s_statusLabel = NULL;
    HWND s_btnActivate = NULL;
    HWND s_btnBuy = NULL;
    HWND s_btnCopyHwid = NULL;
    std::wstring s_cachedHwid;
    HFONT s_titleFont = NULL;
    HFONT s_subFont = NULL;
    HFONT s_editFont = NULL;
    HBRUSH s_darkBgBrush = NULL;
    HBRUSH s_editBgBrush = NULL;

    LRESULT CALLBACK ActivationWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_CREATE: {
            std::string hwidShort = LicenseManager::getHWIDHash().substr(0, 16);
            s_cachedHwid = std::wstring(hwidShort.begin(), hwidShort.end());

            s_titleFont = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, _XWC(L"Segoe UI"));
            s_subFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, _XWC(L"Segoe UI"));
            s_editFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, FIXED_PITCH, _XWC(L"Consolas"));
            s_darkBgBrush = CreateSolidBrush(RGB(30, 30, 30));
            s_editBgBrush = CreateSolidBrush(RGB(42, 42, 42));

            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

            s_btnCopyHwid = CreateWindowW(_XWC(L"BUTTON"), _XWC(L"📋 Копировать HWID"),
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          310, 94, 160, 26, hwnd, (HMENU)205, hInst, NULL);
            SendMessage(s_btnCopyHwid, WM_SETFONT, (WPARAM)s_subFont, TRUE);

            s_keyEdit = CreateWindowW(_XWC(L"EDIT"), L"",
                                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                      40, 130, 430, 32, hwnd, (HMENU)201, hInst, NULL);
            SendMessage(s_keyEdit, WM_SETFONT, (WPARAM)s_editFont, TRUE);

            s_btnActivate = CreateWindowW(_XWC(L"BUTTON"), _XWC(L"✓ Активировать"),
                                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                          60, 200, 175, 40, hwnd, (HMENU)202, hInst, NULL);
            SendMessage(s_btnActivate, WM_SETFONT, (WPARAM)s_subFont, TRUE);

            s_btnBuy = CreateWindowW(_XWC(L"BUTTON"), _XWC(L"💫 Купить в Telegram"),
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     255, 200, 195, 40, hwnd, (HMENU)203, hInst, NULL);
            SendMessage(s_btnBuy, WM_SETFONT, (WPARAM)s_subFont, TRUE);

            s_statusLabel = CreateWindowW(_XWC(L"STATIC"), L"",
                                          WS_CHILD | WS_VISIBLE | SS_CENTER,
                                          40, 255, 430, 28, hwnd, (HMENU)204, hInst, NULL);
            SendMessage(s_statusLabel, WM_SETFONT, (WPARAM)s_subFont, TRUE);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetBkMode(hdcStatic, TRANSPARENT);
            SetTextColor(hdcStatic, RGB(255, 80, 80));
            return (LRESULT)s_darkBgBrush;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, RGB(240, 240, 240));
            SetBkColor(hdcEdit, RGB(42, 42, 42));
            return (LRESULT)s_editBgBrush;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, s_darkBgBrush);

            SetBkMode(hdc, TRANSPARENT);

            HFONT oldFont = (HFONT)SelectObject(hdc, s_titleFont);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT titleRc = { 0, 16, rc.right, 50 };
            DrawTextW(hdc, _XWC(L"GHOST AI"), -1, &titleRc, DT_CENTER | DT_SINGLELINE);

            SelectObject(hdc, s_subFont);
            SetTextColor(hdc, RGB(180, 180, 180));
            RECT subRc = { 0, 55, rc.right, 90 };
            DrawTextW(hdc, _XWC(L"Введите лицензионный ключ\n(получен в @gghostaibot после оплаты)"), -1, &subRc, DT_CENTER);

            RECT hwidRc = { 40, 98, 305, 120 };
            SetTextColor(hdc, RGB(0, 212, 255));
            std::wstring hwidDisplay = _XW(L"HWID: ") + s_cachedHwid;
            DrawTextW(hdc, hwidDisplay.c_str(), -1, &hwidRc, DT_LEFT | DT_SINGLELINE);

            RECT formatRc = { 0, 168, rc.right, 190 };
            SetTextColor(hdc, RGB(120, 120, 120));
            DrawTextW(hdc, _XWC(L"Формат: <base64>.<hmac_hex>"), -1, &formatRc, DT_CENTER | DT_SINGLELINE);

            SelectObject(hdc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == 205) { // Копировать HWID
                if (OpenClipboard(hwnd)) {
                    EmptyClipboard();
                    size_t bytes = (s_cachedHwid.size() + 1) * sizeof(wchar_t);
                    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, bytes);
                    if (hGlob) {
                        void* pMem = GlobalLock(hGlob);
                        if (pMem) {
                            memcpy(pMem, s_cachedHwid.c_str(), bytes);
                            GlobalUnlock(hGlob);
                            SetClipboardData(CF_UNICODETEXT, hGlob);
                        }
                    }
                    CloseClipboard();
                    SetWindowTextW(s_statusLabel, _XWC(L"✓ HWID скопирован в буфер обмена!"));
                }
                return 0;
            } else if (id == 202) { // Активировать
                AntiAnalysis::runGuard();

                wchar_t keyBuf[512] = { 0 };
                GetWindowTextW(s_keyEdit, keyBuf, 512);

                std::string key;
                if (keyBuf[0] != L'\0') {
                    int len = WideCharToMultiByte(CP_UTF8, 0, keyBuf, -1, NULL, 0, NULL, NULL);
                    if (len > 1) {
                        key.resize(len - 1);
                        WideCharToMultiByte(CP_UTF8, 0, keyBuf, -1, &key[0], len, NULL, NULL);
                    }
                }

                while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
                while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();

                if (key.empty()) {
                    SetWindowTextW(s_statusLabel, _XWC(L"⚠️ Введите ключ"));
                    return 0;
                }

                auto optData = LicenseManager::verifyKey(key);
                if (!optData) {
                    SetWindowTextW(s_statusLabel, _XWC(L"❌ Невалидный ключ или ключ привязан к другому ПК."));
                    return 0;
                }

                if (optData->isExpired()) {
                    SetWindowTextW(s_statusLabel, _XWC(L"❌ Срок действия лицензии истёк."));
                    return 0;
                }

                if (!LicenseManager::saveLicense(key)) {
                    SetWindowTextW(s_statusLabel, _XWC(L"❌ Не удалось сохранить лицензию."));
                    return 0;
                }

                s_activatedData = optData;
                DestroyWindow(hwnd);
            } else if (id == 203) { // Купить в Telegram
                ShellExecuteA(NULL, _XC("open"), _XC("https://t.me/gghostaibot"), NULL, NULL, SW_SHOWNORMAL);
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
    std::wstring className = generateUniqueClassName(L"SysAct_");

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = ActivationWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = className.c_str();
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    int winW = 530;
    int winH = 340;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        className.c_str(),
        _XWC(L"Ghost AI — Активация"),
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

    UnregisterClassW(className.c_str(), hInstance);

    return s_activatedData;
}
