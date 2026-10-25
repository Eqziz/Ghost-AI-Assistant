#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>

enum class WindowState {
    IDLE,
    PROCESSING,
    ERROR_STATE
};

class GhostUI {
public:
    static constexpr int DEFAULT_WIDTH = 350;
    static constexpr int DEFAULT_HEIGHT = 200;
    static constexpr int MIN_WIDTH = 100;
    static constexpr int MIN_HEIGHT = 50;
    static constexpr int GRIP_SIZE = 15;

    GhostUI(double alpha = 0.5, double alphaMin = 0.05, double alphaMax = 1.0, const std::string& mode = "light");
    ~GhostUI();

    void log(const std::string& message);
    void showHardResponse(const std::string& text, double durationSec = 6.0);
    void setState(WindowState state);

    bool toggleFollow();
    void setPosition(int x, int y);
    void applySize(int width, int height);
    void getGeometry(int& x, int& y, int& width, int& height);

    void setAlpha(double alpha);
    double getAlpha() const;
    void adjustAlpha(double delta);

    bool setClickthrough(bool enabled);
    bool toggleClickthrough();
    bool isClickthrough() const;

    void setInvisible(bool invisible);
    bool toggleInvisible();
    bool isInvisible() const;

    void setStealth(bool enable);
    void activateStealth();

    void destroy();
    void runMessageLoop();

    HWND getHwnd() const { return m_hwnd; }
    const std::wstring& getClassName() const { return m_className; }
    bool isFollowing() const { return m_isFollowing.load(); }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void onPaint(HDC hdc);

    std::wstring m_className;
    HWND m_hwnd = NULL;
    HFONT m_font = NULL;
    std::string m_mode = "light";
    std::atomic<WindowState> m_state{ WindowState::IDLE };

    double m_alpha = 0.5;
    double m_alphaMin = 0.05;
    double m_alphaMax = 1.0;
    double m_savedAlpha = 0.5;

    std::atomic<bool> m_isFollowing{ true };
    std::atomic<bool> m_isClickthrough{ false };
    std::atomic<bool> m_isInvisible{ false };
    std::atomic<bool> m_isStealth{ false };

    int m_width = DEFAULT_WIDTH;
    int m_height = DEFAULT_HEIGHT;

    // Перетаскивание и ресайз
    bool m_isDragging = false;
    bool m_isResizing = false;
    POINT m_dragStart = { 0, 0 };
    POINT m_windowStartPos = { 0, 0 };
    SIZE m_windowStartSize = { 0, 0 };

    std::string m_text;
    std::mutex m_uiMutex;
};
