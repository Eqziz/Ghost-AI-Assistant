#include "anti_analysis.hpp"
#include "license_manager.hpp"
#include "mode_selector.hpp"
#include "activation_ui.hpp"
#include "setup_ui.hpp"
#include "ghost_assistant.hpp"
#include "screen_capture.hpp"
#include "driver_client.hpp"
#include "obfuscator.hpp"
#include "network_logger.hpp"
#include <windows.h>
#include <commctrl.h>
#include <dbghelp.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dbghelp.lib")

namespace {
    void logStartupStep(const std::string& step) {
        NetworkLogger::logStartupStep(step);
    }

    std::string getModuleNameFromAddress(void* addr, uintptr_t& offset) {
        HMODULE hMod = NULL;
        offset = 0;
        if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(addr),
            &hMod) && hMod != NULL)
        {
            char modPath[MAX_PATH] = { 0 };
            GetModuleFileNameA(hMod, modPath, MAX_PATH);
            offset = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(hMod);
            std::string fullPath(modPath);
            size_t slash = fullPath.find_last_of("\\/");
            return (slash != std::string::npos) ? fullPath.substr(slash + 1) : fullPath;
        }
        return "Unknown_Module";
    }
}
#define LOG_STEP(s) logStartupStep(s)

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* pExceptionPointers) {
    std::ofstream ofs("crash.log", std::ios::trunc);
    if (ofs.is_open()) {
        DWORD code = pExceptionPointers->ExceptionRecord->ExceptionCode;
        void* addr = pExceptionPointers->ExceptionRecord->ExceptionAddress;
        uintptr_t offset = 0;
        std::string modName = getModuleNameFromAddress(addr, offset);

        ofs << "=== Ghost-AI Crash Report ===" << std::endl;
        ofs << "ExceptionCode: 0x" << std::hex << code;
        if (code == 0xc0000005) {
            ofs << " (EXCEPTION_ACCESS_VIOLATION)";
        }
        ofs << std::endl;
        ofs << "Crashing Address: 0x" << addr << " (" << modName << " + 0x" << offset << ")" << std::endl;

        if (code == 0xc0000005 && pExceptionPointers->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR accessType = pExceptionPointers->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR targetAddr = pExceptionPointers->ExceptionRecord->ExceptionInformation[1];
            ofs << "Access Violation: ";
            if (accessType == 0) ofs << "Reading from ";
            else if (accessType == 1) ofs << "Writing to ";
            else if (accessType == 8) ofs << "Executing DEP at ";
            else ofs << "Access at ";
            ofs << "0x" << reinterpret_cast<void*>(targetAddr) << std::endl;
        }

        ofs << "\n--- Call Stack ---" << std::endl;
        void* stack[16] = { 0 };
        USHORT frames = CaptureStackBackTrace(0, 16, stack, NULL);
        for (USHORT i = 0; i < frames; ++i) {
            uintptr_t frameOffset = 0;
            std::string frameMod = getModuleNameFromAddress(stack[i], frameOffset);
            ofs << "  [" << std::dec << i << "] 0x" << std::hex << stack[i] 
                << " in " << frameMod << " + 0x" << frameOffset << std::endl;
        }

        HANDLE hDumpFile = CreateFileA(
            "crash.dmp",
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hDumpFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
            dumpInfo.ThreadId = GetCurrentThreadId();
            dumpInfo.ExceptionPointers = pExceptionPointers;
            dumpInfo.ClientPointers = FALSE;

            MiniDumpWriteDump(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                hDumpFile,
                MiniDumpNormal,
                &dumpInfo,
                NULL,
                NULL
            );
            CloseHandle(hDumpFile);
            ofs << "\nMiniDump saved to: crash.dmp" << std::endl;
        }
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. Немедленная защита от анализа и запуск сторожа
    AntiAnalysis::runGuard();
    AntiAnalysis::startWatchdog();

    LOG_STEP("WinMain: Started");

    // 2. Обработчик аварийных ситуаций
    SetUnhandledExceptionFilter(CrashFilter);
    LOG_STEP("CrashFilter: Registered");

    // 3. Инициализация Common Controls
    INITCOMMONCONTROLSEX icex = { 0 };
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);
    LOG_STEP("CommonControls: Initialized");

    // 4. Инициализация GDI+ для скриншотов
    ScreenCapture::initialize();
    LOG_STEP("ScreenCapture: Initialized");

    // 5. Проверка лицензии
    LOG_STEP("LicenseManager: Loading license...");
    auto lic = LicenseManager::loadLicense();
    if (!lic || lic->isExpired()) {
        LOG_STEP("LicenseManager: No valid license found, showing ActivationUI...");
        auto activated = ActivationUI::showActivationDialog();
        if (!activated) {
            LOG_STEP("ActivationUI: Canceled or not activated, shutting down.");
            AntiAnalysis::stopWatchdog();
            ScreenCapture::shutdown();
            return 0;
        }
        LOG_STEP("ActivationUI: Successfully activated!");
    } else {
        LOG_STEP("LicenseManager: Valid license loaded");
    }

    // 6. Выбор режима (Light vs Hard)
    LOG_STEP("ModeSelector: Showing mode selection dialog...");
    std::string mode = ModeSelector::selectMode();
    LOG_STEP("ModeSelector: Selected mode = " + mode);

    // 7. Окно настроек SetupUI
    LOG_STEP("SetupUI: Loading initial config...");
    AppConfig initialCfg = AppConfig::load();
    LOG_STEP("SetupUI: Showing setup dialog...");
    auto updatedCfg = SetupUI::showSetupDialog(initialCfg);
    if (!updatedCfg) {
        LOG_STEP("SetupUI: Canceled, shutting down.");
        AntiAnalysis::stopWatchdog();
        ScreenCapture::shutdown();
        return 0;
    }
    LOG_STEP("SetupUI: Config confirmed.");

    // 8. Подключение к Ring 0 драйверу (если загружен)
    try {
        LOG_STEP("DriverClient: Connecting to Ring 0 driver...");
        auto& driver = DriverClient::instance();
        if (driver.connect()) {
            driver.protectProcess();
            LOG_STEP("DriverClient: Connected and process protected via ObCallbacks.");
        } else {
            // Без драйвера: включаем Ring 3 защиту процесса через ограниченный DACL и маскировку PEB
            AntiAnalysis::protectProcessUserMode();
            AntiAnalysis::maskProcessParameters();
            LOG_STEP("DriverClient: Driver not found. Activated Ring 3 DACL & PEB mask.");
        }
    } catch (...) {
        AntiAnalysis::protectProcessUserMode();
        AntiAnalysis::maskProcessParameters();
        LOG_STEP("DriverClient: Exception while connecting to driver. Fallback to Ring 3 DACL & PEB mask.");
    }

    // Очистка очереди сообщений от остаточных WM_QUIT предыдущих модальных окон
    MSG cleanMsg;
    while (PeekMessage(&cleanMsg, NULL, 0, 0, PM_REMOVE)) {
        if (cleanMsg.message != WM_QUIT) {
            TranslateMessage(&cleanMsg);
            DispatchMessage(&cleanMsg);
        }
    }

    // 9. Запуск главного ассистента
    LOG_STEP("GhostAssistant: Launching assistant...");
    GhostAssistant app(*updatedCfg, mode);
    LOG_STEP("GhostAssistant: Running message loop...");
    app.run();
    LOG_STEP("GhostAssistant: Message loop finished.");

    // Отключение драйвера и снятие защиты
    try {
        auto& driver = DriverClient::instance();
        if (driver.isConnected()) {
            driver.unprotectProcess();
            driver.disconnect();
        }
    } catch (...) {}

    AntiAnalysis::stopWatchdog();
    ScreenCapture::shutdown();
    LOG_STEP("GhostAI: Terminated cleanly.");
    return 0;
}
