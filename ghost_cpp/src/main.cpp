#include "anti_analysis.hpp"
#include "license_manager.hpp"
#include "mode_selector.hpp"
#include "activation_ui.hpp"
#include "setup_ui.hpp"
#include "ghost_assistant.hpp"
#include "screen_capture.hpp"
#include "driver_client.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* pExceptionPointers) {
    std::ofstream ofs("crash.log", std::ios::trunc);
    if (ofs.is_open()) {
        ofs << "Ghost-AI C++ Crash: ExceptionCode = 0x" 
            << std::hex << pExceptionPointers->ExceptionRecord->ExceptionCode 
            << " at Address = 0x" 
            << pExceptionPointers->ExceptionRecord->ExceptionAddress << std::endl;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. Защита от анализа и отладчиков
    AntiAnalysis::runGuard();

    // 2. Обработчик аварийных ситуаций
    SetUnhandledExceptionFilter(CrashFilter);

    // 3. Инициализация GDI+ для скриншотов
    ScreenCapture::initialize();

    // 4. Проверка лицензии
    auto lic = LicenseManager::loadLicense();
    if (!lic || lic->isExpired()) {
        auto activated = ActivationUI::showActivationDialog();
        if (!activated) {
            ScreenCapture::shutdown();
            return 0;
        }
    }

    // 5. Выбор режима (Light vs Hard)
    std::string mode = ModeSelector::selectMode();

    // 6. Окно настроек SetupUI
    AppConfig initialCfg = AppConfig::load();
    auto updatedCfg = SetupUI::showSetupDialog(initialCfg);
    if (!updatedCfg) {
        ScreenCapture::shutdown();
        return 0;
    }

    // 7. Подключение к Ring 0 драйверу (если загружен)
    try {
        auto& driver = DriverClient::instance();
        if (driver.connect()) {
            // ObRegisterCallbacks — безопасная защита (ВСЕГДА, без DKOM)
            driver.protectProcess();

            // Временный DKOM — только в Hard Mode, на 5 секунд с автовозвратом
            if (mode == "hard") {
                driver.hideProcessTimed(0, 5000);
            }
        }
    } catch (...) {
        // Драйвер недоступен — работаем без Ring 0
    }

    // 8. Запуск главного ассистента
    GhostAssistant app(*updatedCfg, mode);
    app.run();

    // Отключение драйвера и снятие защиты
    try {
        auto& driver = DriverClient::instance();
        if (driver.isConnected()) {
            driver.unprotectProcess();
            driver.disconnect();
        }
    } catch (...) {}

    ScreenCapture::shutdown();
    return 0;
}
