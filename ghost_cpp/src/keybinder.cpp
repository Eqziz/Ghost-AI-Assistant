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
            int vk = pKbd->vkCode;
            if (vk == VK_CONTROL) {
                vk = (pKbd->flags & LLKHF_EXTENDED) ? VK_RCONTROL : VK_LCONTROL;
            } else if (vk == VK_MENU) {
                vk = (pKbd->flags & LLKHF_EXTENDED) ? VK_RMENU : VK_LMENU;
            }
            KeyBinder::handleHookEvent(vk, true);
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
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.erase(token.begin());
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) token.pop_back();
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
        else if (p == "pageup" || p == "page up" || p == "prior") combo.vkCode = VK_PRIOR;
        else if (p == "pagedown" || p == "page down" || p == "next") combo.vkCode = VK_NEXT;
        else if (p == "insert" || p == "ins") combo.vkCode = VK_INSERT;
        else if (p == "delete" || p == "del") combo.vkCode = VK_DELETE;
        else if (p == "backspace" || p == "back") combo.vkCode = VK_BACK;
        else if (p == "+" || p == "plus" || p == "add") combo.vkCode = VK_OEM_PLUS;
        else if (p == "-" || p == "minus" || p == "sub" || p == "subtract") combo.vkCode = VK_OEM_MINUS;
        else if (p == "[" || p == "{") combo.vkCode = VK_OEM_4;
        else if (p == "]" || p == "}") combo.vkCode = VK_OEM_6;
        else if (p == ";" || p == ":") combo.vkCode = VK_OEM_1;
        else if (p == "'" || p == "\"") combo.vkCode = VK_OEM_7;
        else if (p == "," || p == "<") combo.vkCode = VK_OEM_COMMA;
        else if (p == "." || p == ">") combo.vkCode = VK_OEM_PERIOD;
        else if (p == "/" || p == "?") combo.vkCode = VK_OEM_2;
        else if (p == "\\" || p == "|") combo.vkCode = VK_OEM_5;
        else if (p == "`" || p == "~") combo.vkCode = VK_OEM_3;
        else if (p.length() >= 2 && p[0] == 'f' && isdigit(p[1])) {
            try {
                int fnum = std::stoi(p.substr(1));
                if (fnum >= 1 && fnum <= 24) {
                    combo.vkCode = VK_F1 + (fnum - 1);
                }
            } catch (...) {}
        } else if (p.length() == 1) {
            char c = p[0];
            if (c >= 'a' && c <= 'z') combo.vkCode = 'A' + (c - 'a');
            else if (c >= '0' && c <= '9') combo.vkCode = '0' + (c - '0');
        }
    }

    // Обработка синтаксиса "ctrl++" или "+" где '+' был разделителем
    if (combo.vkCode == 0 && (s == "+" || s.find("++") != std::string::npos || (s.length() > 1 && s.back() == '+' && s[s.length() - 2] == '+'))) {
        combo.vkCode = VK_OEM_PLUS;
    }

    // Если был указан только ctrl/shift/alt/win
    if (combo.vkCode == 0) {
        if (combo.ctrl) combo.vkCode = VK_CONTROL;
        else if (combo.shift) combo.vkCode = VK_SHIFT;
        else if (combo.alt) combo.vkCode = VK_MENU;
        else if (combo.win) combo.vkCode = VK_LWIN;
    }

    return combo;
}

KeyBinder::KeyBinder() {
    installHook();
}

KeyBinder::~KeyBinder() {
    unregisterAll();
    uninstallHook();
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
    bool isWin = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);

    std::vector<std::function<void()>> callbacksToRun;
    {
        std::lock_guard<std::mutex> lock(s_keyMutex);
        for (const auto& [action, pair] : s_bindings) {
            const KeyCombo& kc = pair.first;
            if (kc.vkCode == 0) continue;

            // Проверяем совпадение базовой клавиши
            bool keyMatches = (kc.vkCode == vkCode) ||
                (kc.vkCode == VK_CONTROL && (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL)) ||
                (kc.vkCode == VK_MENU && (vkCode == VK_LMENU || vkCode == VK_RMENU)) ||
                (kc.vkCode == VK_SHIFT && (vkCode == VK_LSHIFT || vkCode == VK_RSHIFT));

            if (!keyMatches) continue;

            // Проверяем модификаторы, только если сама клавиша не является этим модификатором
            bool needCtrl = kc.ctrl && (kc.vkCode != VK_CONTROL && kc.vkCode != VK_LCONTROL && kc.vkCode != VK_RCONTROL);
            bool hasCtrl = isCtrl && (vkCode != VK_CONTROL && vkCode != VK_LCONTROL && vkCode != VK_RCONTROL);
            if (needCtrl != hasCtrl) continue;

            bool needShift = kc.shift && (kc.vkCode != VK_SHIFT && kc.vkCode != VK_LSHIFT && kc.vkCode != VK_RSHIFT);
            bool hasShift = isShift && (vkCode != VK_SHIFT && vkCode != VK_LSHIFT && vkCode != VK_RSHIFT);
            if (needShift != hasShift) continue;

            bool needAlt = kc.alt && (kc.vkCode != VK_MENU && kc.vkCode != VK_LMENU && kc.vkCode != VK_RMENU);
            bool hasAlt = isAlt && (vkCode != VK_MENU && vkCode != VK_LMENU && vkCode != VK_RMENU);
            if (needAlt != hasAlt) continue;

            bool needWin = kc.win && (kc.vkCode != VK_LWIN && kc.vkCode != VK_RWIN);
            bool hasWin = isWin && (vkCode != VK_LWIN && vkCode != VK_RWIN);
            if (needWin != hasWin) continue;

            if (pair.second) {
                callbacksToRun.push_back(pair.second);
            }
        }
    }

    for (auto& cb : callbacksToRun) {
        try {
            cb();
        } catch (...) {}
    }
    return !callbacksToRun.empty();
}
