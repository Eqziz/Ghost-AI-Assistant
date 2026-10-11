#pragma once

#include <string>

class ModeSelector {
public:
    // Показывает окно выбора режима и возвращает "light" или "hard"
    static std::string selectMode();
};
