#include "keybinder.hpp"
#include "anti_analysis.hpp"
#include <algorithm>
#include <sstream>
#include <vector>
#include <mutex>

std::map<std::string, std::pair<KeyCombo, std::function<void()>>> KeyBinder::s_bindings;
HHOOK KeyBinder::s_hook = NULL;
static std::mutex s_keyMutex;
static std::map<std::string, uint64_t> s_lastTriggerTimes;

namespace {
    // Маппинг русских букв (UTF-8) на виртуальные коды клавиш QWERTY
    int mapCyrillicToVk(const std::string& token) {
        if (token.size() == 2 && static_cast<unsigned char>(token[0]) >= 0xD0) {
            unsigned char b1 = static_cast<unsigned char>(token[0]);
            unsigned char b2 = static_cast<unsigned char>(token[1]);
            uint32_t uc = ((b1 & 0x1F) << 6) | (b2 & 0x3F);

            // Приведение строчных русских к прописным (0x0430..0x044F -> 0x0410..0x042F)
            if (uc >= 0x0430 && uc <= 0x044F) uc -= 0x20;
            else if (uc == 0x0451) uc = 0x0401; // ё -> Ё

            switch (uc) {
                case 0x0419: return 'Q'; // Й
                case 0x0426: return 'W'; // Ц
                case 0x0423: return 'E'; // У
                case 0x041A: return 'R'; // К
                case 0x0415: return 'T'; // Е
                case 0x041D: return 'Y'; // Н
                case 0x0413: return 'U'; // Г
                case 0x0428: return 'I'; // Ш
                case 0x0429: return 'O'; // Щ
                case 0x0417: return 'P'; // З
                case 0x0425: return VK_OEM_4; // Х -> [
                case 0x042A: return VK_OEM_6; // Ъ -> ]
                case 0x0424: return 'A'; // Ф
                case 0x042B: return 'S'; // Ы
                case 0x0412: return 'D'; // В
                case 0x0410: return 'F'; // А
                case 0x041F: return 'G'; // П
                case 0x0420: return 'H'; // Р
                case 0x041E: return 'J'; // О
                case 0x041B: return 'K'; // Л
                case 0x0414: return 'L'; // Д
                case 0x0416: return VK_OEM_1; // Ж -> ;
                case 0x042D: return VK_OEM_7; // Э -> '
                case 0x042F: return 'Z'; // Я
                case 0x0427: return 'X'; // Ч
                case 0x0421: return 'C'; // С
                case 0x041C: return 'V'; // М
                case 0x0418: return 'B'; // И
                case 0x0422: return 'N'; // Т
                case 0x042C: return 'M'; // Ь
                case 0x0411: return VK_OEM_COMMA; // Б -> ,
                case 0x042E: return VK_OEM_PERIOD; // Ю -> .
                case 0x0401: return VK_OEM_3; // Ё -> `
            }
        }
        return 0;
    }

    std::string toLower(const std::string& str) {
        std::string s;
        s.reserve(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(str[i]);
            // Базовый ASCII
            if (c >= 'A' && c <= 'Z') {
                s.push_back(static_cast<char>(c + ('a' - 'A')));
            } else if (c == 0xD0 && i + 1 < str.size()) {
                // Кириллица А..Я (0xD0 0x90 .. 0xD0 0xAF) и Ё (0xD0 0x81)
                unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
                if (c2 >= 0x90 && c2 <= 0x9F) {
                    s.push_back(static_cast<char>(0xD0));
                    s.push_back(static_cast<char>(c2 + 0x20));
                    i++;
                } else if (c2 >= 0xA0 && c2 <= 0xAF) {
                    s.push_back(static_cast<char>(0xD1));
                    s.push_back(static_cast<char>(c2 - 0x20));
                    i++;
                } else if (c2 == 0x81) { // Ё -> ё
                    s.push_back(static_cast<char>(0xD1));
                    s.push_back(static_cast<char>(0x91));
                    i++;
                } else {
                    s.push_back(static_cast<char>(c));
                }
            } else {
                s.push_back(static_cast<char>(c));
            }
        }
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
        if (p == "ctrl" || p == "control" || p == "rctrl" || p == "lctrl" || p == "ктрл" || p == "контрол") combo.ctrl = true;
        else if (p == "shift" || p == "rshift" || p == "lshift" || p == "шифт") combo.shift = true;
        else if (p == "alt" || p == "ralt" || p == "lalt" || p == "альт") combo.alt = true;
        else if (p == "win" || p == "вин") combo.win = true;
        else if (p == "right ctrl") { combo.ctrl = true; combo.vkCode = VK_RCONTROL; }
        else if (p == "left ctrl") { combo.ctrl = true; combo.vkCode = VK_LCONTROL; }
        else if (p == "esc" || p == "escape" || p == "эск" || p == "эскейп") combo.vkCode = VK_ESCAPE;
        else if (p == "tab" || p == "таб") combo.vkCode = VK_TAB;
        else if (p == "enter" || p == "return" || p == "ввод") combo.vkCode = VK_RETURN;
        else if (p == "space" || p == "пробел") combo.vkCode = VK_SPACE;
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
        } else if (int cyrVk = mapCyrillicToVk(p)) {
            combo.vkCode = cyrVk;
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
    startPollingLoop();
}

KeyBinder::~KeyBinder() {
    stopPollingLoop();
    unregisterAll();
    uninstallHook();
}

void KeyBinder::startPollingLoop() {
    m_pollRunning = true;
    m_pollThread = std::thread([this]() {
        AntiAnalysis::protectThreadUserMode(GetCurrentThread());
        std::map<int, bool> lastState;
        while (m_pollRunning) {
            std::vector<std::function<void()>> callbacksToRun;
            uint64_t now = GetTickCount64();

            {
                std::lock_guard<std::mutex> lock(s_keyMutex);
                for (const auto& [action, pair] : s_bindings) {
                    const KeyCombo& kc = pair.first;
                    if (kc.vkCode == 0) continue;

                    bool isKeyDown = (GetAsyncKeyState(kc.vkCode) & 0x8000) != 0;
                    bool wasDown = lastState[kc.vkCode];
                    lastState[kc.vkCode] = isKeyDown;

                    if (isKeyDown && !wasDown) {
                        bool isCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                        bool isShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                        bool isAlt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                        bool isWin = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);

                        bool needCtrl = kc.ctrl && (kc.vkCode != VK_CONTROL && kc.vkCode != VK_LCONTROL && kc.vkCode != VK_RCONTROL);
                        bool hasCtrl = isCtrl && (kc.vkCode != VK_CONTROL && kc.vkCode != VK_LCONTROL && kc.vkCode != VK_RCONTROL);
                        if (needCtrl != hasCtrl) continue;

                        bool needShift = kc.shift && (kc.vkCode != VK_SHIFT && kc.vkCode != VK_LSHIFT && kc.vkCode != VK_RSHIFT);
                        bool hasShift = isShift && (kc.vkCode != VK_SHIFT && kc.vkCode != VK_LSHIFT && kc.vkCode != VK_RSHIFT);
                        if (needShift != hasShift) continue;

                        bool needAlt = kc.alt && (kc.vkCode != VK_MENU && kc.vkCode != VK_LMENU && kc.vkCode != VK_RMENU);
                        bool hasAlt = isAlt && (kc.vkCode != VK_MENU && kc.vkCode != VK_LMENU && kc.vkCode != VK_RMENU);
                        if (needAlt != hasAlt) continue;

                        bool needWin = kc.win && (kc.vkCode != VK_LWIN && kc.vkCode != VK_RWIN);
                        bool hasWin = isWin && (kc.vkCode != VK_LWIN && kc.vkCode != VK_RWIN);
                        if (needWin != hasWin) continue;

                        // Дебаунс: защита от дублирования вызова с хуком
                        if (now - s_lastTriggerTimes[action] > 180) {
                            s_lastTriggerTimes[action] = now;
                            if (pair.second) {
                                callbacksToRun.push_back(pair.second);
                            }
                        }
                    }
                }
            }

            for (auto& cb : callbacksToRun) {
                try {
                    cb();
                } catch (...) {}
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });
}

void KeyBinder::stopPollingLoop() {
    m_pollRunning = false;
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
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

            uint64_t now = GetTickCount64();
            if (now - s_lastTriggerTimes[action] > 180) {
                s_lastTriggerTimes[action] = now;
                if (pair.second) {
                    callbacksToRun.push_back(pair.second);
                }
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
