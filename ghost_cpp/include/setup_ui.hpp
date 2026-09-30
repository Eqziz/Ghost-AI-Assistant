#pragma once

#include "config.hpp"
#include <optional>

class SetupUI {
public:
    // Показывает окно настроек Ghost-AI и возвращает обновленный конфиг при нажатии "Сохранить и запустить"
    static std::optional<AppConfig> showSetupDialog(const AppConfig& initialConfig);
};
