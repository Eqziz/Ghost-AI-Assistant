#pragma once

#include <string>
#include <map>
#include <functional>
#include <windows.h>

struct KeyCombo {
    int vkCode = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool win = false;
};

class KeyBinder {
public:
    KeyBinder();
    ~KeyBinder();

    void registerHotkey(const std::string& action, const std::string& keyStr, std::function<void()> callback);
    void unregisterHotkey(const std::string& action);
    void unregisterAll();

    static KeyCombo parseKeyCombo(const std::string& keyStr);
    static void installHook();
    static void uninstallHook();

    // Обработка хука
    static bool handleHookEvent(int vkCode, bool isDown);

private:
    static std::map<std::string, std::pair<KeyCombo, std::function<void()>>> s_bindings;
    static HHOOK s_hook;
};
