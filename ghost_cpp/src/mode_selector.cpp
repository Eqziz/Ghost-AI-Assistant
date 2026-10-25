#include "mode_selector.hpp"
#include <windows.h>
#include <string>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace {
    std::wstring generateUniqueClassName(const std::wstring& prefix = L"SysMode_") {
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

    std::string s_selectedMode = "light";
    bool s_confirmed = false;
    HWND s_btnLight = NULL;
    HWND s_btnHard = NULL;
    HFONT s_titleFont = NULL;
    HFONT s_btnFont = NULL;
    HBRUSH s_darkBgBrush = NULL;

    LRESULT CALLBACK ModeSelectorWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_CREATE: {
            s_titleFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            s_btnFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            s_darkBgBrush = CreateSolidBrush(RGB(30, 30, 30));

            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

            s_btnLight = CreateWindowW(L"BUTTON", L"Light Mode",
                                       WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                                       30, 70, 320, 45, hwnd, (HMENU)101, hInst, NULL);
            SendMessage(s_btnLight, WM_SETFONT, (WPARAM)s_btnFont, TRUE);

            s_btnHard = CreateWindowW(L"BUTTON", L"Hard Mode",
                                      WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                      30, 130, 320, 45, hwnd, (HMENU)102, hInst, NULL);
            SendMessage(s_btnHard, WM_SETFONT, (WPARAM)s_btnFont, TRUE);

            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(255, 255, 255));
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
            SetTextColor(hdc, RGB(255, 255, 255));

            RECT titleRc = { 0, 20, rc.right, 60 };
            DrawTextW(hdc, L"Выберите профиль работы", -1, &titleRc, DT_CENTER | DT_SINGLELINE);

            SelectObject(hdc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == 101) {
                s_selectedMode = "light";
                s_confirmed = true;
                DestroyWindow(hwnd);
            } else if (id == 102) {
                s_selectedMode = "hard";
                s_confirmed = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY: {
            if (s_titleFont) { DeleteObject(s_titleFont); s_titleFont = NULL; }
            if (s_btnFont) { DeleteObject(s_btnFont); s_btnFont = NULL; }
            if (s_darkBgBrush) { DeleteObject(s_darkBgBrush); s_darkBgBrush = NULL; }
            PostQuitMessage(0);
            return 0;
        }
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

std::string ModeSelector::selectMode() {
    s_selectedMode = "light";
    s_confirmed = false;

    HINSTANCE hInstance = GetModuleHandle(NULL);
    std::wstring className = generateUniqueClassName(L"SysMode_");

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = ModeSelectorWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = className.c_str();
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    int winW = 396;
    int winH = 250;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        className.c_str(),
        L"Ghost-AI: Режим работы",
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

    if (!s_confirmed) {
        return "";
    }

    return s_selectedMode;
}
