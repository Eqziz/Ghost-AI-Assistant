#include "keybinder.hpp"
#include <algorithm>
#include <sstream>
#include <vector>
#include <mutex>

std::map<std::string, std::pair<KeyCombo, std::function<void()>>> KeyBinder::s_bindings;
HHOOK KeyBinder::s_hook = NULL;
static std::mutex s_keyMutex;

namespace {
    std::string toLower(const std::string& str) {
        std::string s = str;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }

    LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
            KBDLLHOOKSTRUCT* pKbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            if (KeyBinder::handleHookEvent(pKbd->vkCode, true)) {
                // Если обработано хоткеем
            }
        }
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }
}

KeyCombo KeyBinder::parseKeyCombo(const std::string& keyStr) {
    KeyCombo combo;
    std::string s = toLower(keyStr);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();

    std::stringstream ss(s);
    std::string token;
    std::vector<std::string> parts;
    while (std::getline(ss, token, '+')) {
        while (!token.empty() && token.front() == ' ') token.erase(token.begin());
        while (!token.empty() && token.back() == ' ') token.pop_back();
        if (!token.empty()) {
            parts.push_back(token);
        }
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string& p = parts[i];
        if (p == "ctrl" || p == "control") combo.ctrl = true;
        else if (p == "shift") combo.shift = true;
        else if (p == "alt") combo.alt = true;
        else if (p == "win") combo.win = true;
        else if (p == "right ctrl") { combo.ctrl = true; combo.vkCode = VK_RCONTROL; }
        else if (p == "left ctrl") { combo.ctrl = true; combo.vkCode = VK_LCONTROL; }
        else if (p == "esc" || p == "escape") combo.vkCode = VK_ESCAPE;
        else if (p == "tab") combo.vkCode = VK_TAB;
        else if (p == "enter" || p == "return") combo.vkCode = VK_RETURN;
        else if (p == "space") combo.vkCode = VK_SPACE;
        else if (p == "up") combo.vkCode = VK_UP;
        else if (p == "down") combo.vkCode = VK_DOWN;
        else if (p == "left") combo.vkCode = VK_LEFT;
        else if (p == "right") combo.vkCode = VK_RIGHT;
        else if (p == "end") combo.vkCode = VK_END;
        else if (p == "home") combo.vkCode = VK_HOME;
        else if (p == "`" || p == "~") combo.vkCode = VK_OEM_3;
        else if (p.length() >= 2 && p[0] == 'f' && isdigit(p[1])) {
            int fnum = std::stoi(p.substr(1));
            if (fnum >= 1 && fnum <= 24) {
                combo.vkCode = VK_F1 + (fnum - 1);
            }
        } else if (p.length() == 1) {
            char c = p[0];
            if (c >= 'a' && c <= 'z') combo.vkCode = 'A' + (c - 'a');
            else if (c >= '0' && c <= '9') combo.vkCode = '0' + (c - '0');
        }
    }

    // Если был указан только ctrl/shift/alt
    if (combo.vkCode == 0) {
        if (combo.ctrl) combo.vkCode = VK_CONTROL;
        else if (combo.shift) combo.vkCode = VK_SHIFT;
        else if (combo.alt) combo.vkCode = VK_MENU;
    }

    return combo;
}

KeyBinder::KeyBinder() {
    installHook();
}

KeyBinder::~KeyBinder() {
    unregisterAll();
}

void KeyBinder::installHook() {
    std::lock_guard<std::mutex> lock(s_keyMutex);
    if (!s_hook) {
        s_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    }
}

void KeyBinder::uninstallHook() {
    std::lock_guard<std::mutex> lock(s_keyMutex);
    if (s_hook) {
        UnhookWindowsHookEx(s_hook);
        s_hook = NULL;
    }
}

void KeyBinder::registerHotkey(const std::string& action, const std::string& keyStr, std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(s_keyMutex);
    KeyCombo combo = parseKeyCombo(keyStr);
    s_bindings[action] = { combo, callback };
}

void KeyBinder::unregisterHotkey(const std::string& action) {
    std::lock_guard<std::mutex> lock(s_keyMutex);
    s_bindings.erase(action);
}

void KeyBinder::unregisterAll() {
    std::lock_guard<std::mutex> lock(s_keyMutex);
    s_bindings.clear();
}

bool KeyBinder::handleHookEvent(int vkCode, bool isDown) {
    if (!isDown) return false;

    bool isCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool isShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool isAlt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    std::vector<std::function<void()>> callbacksToRun;
    {
        std::lock_guard<std::mutex> lock(s_keyMutex);
        for (const auto& [action, pair] : s_bindings) {
            const KeyCombo& kc = pair.first;
            if (kc.vkCode == vkCode) {
                // Проверяем модификаторы
                if (kc.ctrl && !isCtrl && vkCode != VK_CONTROL && vkCode != VK_LCONTROL && vkCode != VK_RCONTROL) continue;
                if (kc.shift && !isShift && vkCode != VK_SHIFT) continue;
                if (kc.alt && !isAlt && vkCode != VK_MENU) continue;

                if (pair.second) {
                    callbacksToRun.push_back(pair.second);
                }
            }
        }
    }

    for (auto& cb : callbacksToRun) {
        cb();
    }
    return !callbacksToRun.empty();
}
