#pragma once

#include "license_manager.hpp"
#include <optional>

class ActivationUI {
public:
    // Показывает окно активации и возвращает LicenseData при успехе
    static std::optional<LicenseData> showActivationDialog();
};
