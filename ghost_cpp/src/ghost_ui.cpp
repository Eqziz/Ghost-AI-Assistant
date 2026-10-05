#include "ghost_ui.hpp"
#include <algorithm>
#include <thread>
#include <chrono>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

namespace {
    const UINT_PTR TIMER_HARD_AUTO_HIDE = 1001;
    const wchar_t* GHOST_WINDOW_CLASS = L"GhostAI_Overlay_Class";
    bool g_classRegistered = false;
}

GhostUI::GhostUI(double alpha, double alphaMin, double alphaMax, const std::string& mode)
    : m_mode(mode), m_alphaMin(alphaMin), m_alphaMax(alphaMax)
{
    m_alpha = (std::max)(alphaMin, (std::min)(alpha, alphaMax));
    m_savedAlpha = m_alpha;

    HINSTANCE hInstance = GetModuleHandle(NULL);
    if (!g_classRegistered) {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = GhostUI::WndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = GHOST_WINDOW_CLASS;
        RegisterClassExW(&wc);
        g_classRegistered = true;
    }

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW;
    m_hwnd = CreateWindowExW(
        exStyle,
        GHOST_WINDOW_CLASS,
        L"Ghost-AI",
        WS_POPUP,
        100, 100, m_width, m_height,
        NULL, NULL, hInstance, this
    );

    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

    // Установка прозрачности
    SetLayeredWindowAttributes(m_hwnd, 0, (BYTE)(m_alpha * 255.0), LWA_ALPHA);

    // Шрифт Consolas (Unicode)
    m_font = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
    );

    if (m_mode == "hard") {
        setInvisible(true);
        setStealth(false);
    } else {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        activateStealth();
    }
}

GhostUI::~GhostUI() {
    destroy();
}

void GhostUI::destroy() {
    if (m_hwnd) {
        DWORD winThreadId = GetWindowThreadProcessId(m_hwnd, NULL);
        if (winThreadId != GetCurrentThreadId()) {
            PostMessage(m_hwnd, WM_CLOSE, 0, 0);
        } else {
            DestroyWindow(m_hwnd);
            m_hwnd = NULL;
        }
    }
    if (m_font) {
        DeleteObject(m_font);
        m_font = NULL;
    }
}

void GhostUI::activateStealth() {
    setStealth(true);
}

void GhostUI::setStealth(bool enable) {
    if (!m_hwnd) return;
    DWORD flag = enable ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    SetWindowDisplayAffinity(m_hwnd, flag);
    m_isStealth = enable;
}

void GhostUI::log(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(m_uiMutex);
        m_text = message;
    }
    if (m_hwnd) {
        InvalidateRect(m_hwnd, NULL, TRUE);
    }
}

void GhostUI::showHardResponse(const std::string& text, double durationSec) {
    if (!m_hwnd) return;
    setStealth(true);
    setInvisible(false);
    log(text);
    setState(WindowState::IDLE);

    // Запускаем таймер автоскрытия
    SetTimer(m_hwnd, TIMER_HARD_AUTO_HIDE, (UINT)(durationSec * 1000), NULL);
}

void GhostUI::setState(WindowState state) {
    m_state = state;
}

bool GhostUI::toggleFollow() {
    m_isFollowing = !m_isFollowing;
    return m_isFollowing;
}

void GhostUI::setPosition(int x, int y) {
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void GhostUI::applySize(int width, int height) {
    if (!m_hwnd) return;
    m_width = (std::max)(MIN_WIDTH, width);
    m_height = (std::max)(MIN_HEIGHT, height);
    SetWindowPos(m_hwnd, NULL, 0, 0, m_width, m_height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(m_hwnd, NULL, TRUE);
}

void GhostUI::getGeometry(int& x, int& y, int& width, int& height) {
    if (!m_hwnd) {
        x = 0; y = 0; width = m_width; height = m_height;
        return;
    }
    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    x = rc.left;
    y = rc.top;
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
}

void GhostUI::setAlpha(double alpha) {
    m_alpha = (std::max)(m_alphaMin, (std::min)(alpha, m_alphaMax));
    if (m_hwnd && !m_isInvisible) {
        SetLayeredWindowAttributes(m_hwnd, 0, (BYTE)(m_alpha * 255.0), LWA_ALPHA);
    }
}

double GhostUI::getAlpha() const {
    return m_alpha;
}

void GhostUI::adjustAlpha(double delta) {
    setAlpha(m_alpha + delta);
}

bool GhostUI::setClickthrough(bool enabled) {
    if (!m_hwnd) return false;
    LONG_PTR exStyle = GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
    if (enabled) {
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(m_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    m_isClickthrough = enabled;
    return true;
}

bool GhostUI::toggleClickthrough() {
    return setClickthrough(!m_isClickthrough);
}

bool GhostUI::isClickthrough() const {
    return m_isClickthrough;
}

void GhostUI::setInvisible(bool invisible) {
    m_isInvisible = invisible;
    if (!m_hwnd) return;

    if (invisible) {
        m_savedAlpha = m_alpha;
        ShowWindow(m_hwnd, SW_HIDE);
    } else {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        setAlpha(m_savedAlpha);
    }
}

bool GhostUI::toggleInvisible() {
    setInvisible(!m_isInvisible);
    return m_isInvisible;
}

bool GhostUI::isInvisible() const {
    return m_isInvisible;
}

void GhostUI::runMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void GhostUI::onPaint(HDC hdc) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    // Double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // Фон (темно-серый / черный)
    HBRUSH bgBrush = CreateSolidBrush(RGB(15, 15, 15));
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    // Рамка (тонкая полупрозрачная)
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 45));
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    SelectObject(memDC, nullBrush);
    Rectangle(memDC, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);

    // Текст (Unicode / UTF-8 поддержка)
    HFONT oldFont = (HFONT)SelectObject(memDC, m_font);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(235, 235, 235));

    RECT textRc = rc;
    textRc.left += 8;
    textRc.top += 8;
    textRc.right -= 8;
    textRc.bottom -= (GRIP_SIZE + 4);

    std::wstring textToDrawW;
    {
        std::lock_guard<std::mutex> lock(m_uiMutex);
        if (!m_text.empty()) {
            int len = MultiByteToWideChar(CP_UTF8, 0, m_text.c_str(), -1, NULL, 0);
            if (len > 0) {
                textToDrawW.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, m_text.c_str(), -1, &textToDrawW[0], len);
            }
        }
    }

    DrawTextW(memDC, textToDrawW.c_str(), -1, &textRc, DT_LEFT | DT_TOP | DT_WORDBREAK);

    // Grip в правом нижнем углу
    HBRUSH gripBrush = CreateSolidBrush(RGB(60, 60, 60));
    RECT gripRc;
    gripRc.right = rc.right;
    gripRc.bottom = rc.bottom;
    gripRc.left = rc.right - GRIP_SIZE;
    gripRc.top = rc.bottom - GRIP_SIZE;
    FillRect(memDC, &gripRc, gripBrush);
    DeleteObject(gripBrush);

    SelectObject(memDC, oldFont);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

LRESULT CALLBACK GhostUI::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    GhostUI* self = reinterpret_cast<GhostUI*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self) self->onPaint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER: {
        if (wParam == TIMER_HARD_AUTO_HIDE && self) {
            KillTimer(hwnd, TIMER_HARD_AUTO_HIDE);
            self->setInvisible(true);
            self->setStealth(false);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (self && !self->m_isFollowing && !self->m_isClickthrough) {
            SetCapture(hwnd);
            GetCursorPos(&self->m_dragStart);

            RECT rc;
            GetWindowRect(hwnd, &rc);
            self->m_windowStartPos = { rc.left, rc.top };
            self->m_windowStartSize = { rc.right - rc.left, rc.bottom - rc.top };

            POINT pt;
            pt.x = (short)LOWORD(lParam);
            pt.y = (short)HIWORD(lParam);

            // Проверяем клик по grip
            if (pt.x >= self->m_windowStartSize.cx - GRIP_SIZE && pt.y >= self->m_windowStartSize.cy - GRIP_SIZE) {
                self->m_isResizing = true;
            } else {
                self->m_isDragging = true;
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (self) {
            POINT cur;
            GetCursorPos(&cur);
            if (self->m_isDragging) {
                int dx = cur.x - self->m_dragStart.x;
                int dy = cur.y - self->m_dragStart.y;
                SetWindowPos(hwnd, NULL, self->m_windowStartPos.x + dx, self->m_windowStartPos.y + dy,
                             0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            } else if (self->m_isResizing) {
                int dx = cur.x - self->m_dragStart.x;
                int dy = cur.y - self->m_dragStart.y;
                self->applySize(self->m_windowStartSize.cx + dx, self->m_windowStartSize.cy + dy);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (self) {
            ReleaseCapture();
            self->m_isDragging = false;
            self->m_isResizing = false;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (self) {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int step = 25;
            if (delta > 0) {
                self->applySize(self->m_width + step, self->m_height + step);
            } else if (delta < 0) {
                self->applySize(self->m_width - step, self->m_height - step);
            }
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
