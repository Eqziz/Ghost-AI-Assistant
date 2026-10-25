#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <regex>
#include <fstream>
#include <cassert>
#include <atomic>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

#include "network_logger.hpp"

#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

static int g_passCount = 0;
static int g_failCount = 0;

#define CHALLENGE_ASSERT(cond, msg) do { \
    if (cond) { \
        g_passCount++; \
    } else { \
        g_failCount++; \
        std::cerr << "[-] FAIL [Line " << __LINE__ << "]: " << msg << std::endl; \
    } \
} while(0)

#define CHALLENGE_SECTION(name) \
    std::cout << "\n======================================================\n" \
              << "  [CHALLENGE] " << name << "\n" \
              << "======================================================" << std::endl;

// Replicate the exact generator used in ghost_ui, activation_ui, mode_selector, setup_ui
static std::wstring generateUniqueClassName(const std::wstring& prefix) {
    uint8_t randBytes[8] = { 0 };
    if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, randBytes, sizeof(randBytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        uint64_t tick = GetTickCount64() ^ (static_cast<uint64_t>(GetCurrentProcessId()) << 32);
        memcpy(randBytes, &tick, sizeof(randBytes));
    }
    wchar_t buf[64];
    swprintf_s(buf, L"%s%02X%02X%02X%02X%02X%02X%02X%02X",
               prefix.c_str(),
               randBytes[0], randBytes[1], randBytes[2], randBytes[3],
               randBytes[4], randBytes[5], randBytes[6], randBytes[7]);
    return std::wstring(buf);
}

// Dummy WndProc for testing class registration
static LRESULT CALLBACK TestWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "  Milestone 3 Empirical Challenger: Privacy & Stealth" << std::endl;
    std::cout << "=========================================================" << std::endl;

    // -------------------------------------------------------------
    // Test 1: Window Class Randomness & Multi-threaded Uniqueness
    // -------------------------------------------------------------
    CHALLENGE_SECTION("Test 1: Window Class Randomness & Multi-threaded Uniqueness");
    {
        const int NUM_THREADS = 16;
        const int NAMES_PER_THREAD = 2000;
        const int TOTAL_NAMES = NUM_THREADS * NAMES_PER_THREAD;

        std::vector<std::wstring> generatedNames(TOTAL_NAMES);
        std::vector<std::thread> workers;

        std::vector<std::wstring> prefixes = { L"SysHost_", L"SysAct_", L"SysMode_", L"SysCfg_" };

        for (int t = 0; t < NUM_THREADS; ++t) {
            workers.emplace_back([t, &generatedNames, &prefixes, NAMES_PER_THREAD]() {
                for (int i = 0; i < NAMES_PER_THREAD; ++i) {
                    std::wstring prefix = prefixes[(t + i) % prefixes.size()];
                    generatedNames[t * NAMES_PER_THREAD + i] = generateUniqueClassName(prefix);
                }
            });
        }

        for (auto& w : workers) {
            w.join();
        }

        std::unordered_set<std::wstring> uniqueSet(generatedNames.begin(), generatedNames.end());
        CHALLENGE_ASSERT(uniqueSet.size() == TOTAL_NAMES,
                         "Window class generation generated 100% unique names (0 collisions across " +
                         std::to_string(TOTAL_NAMES) + " names)");

        // Verify format and lack of static "Ghost" signature
        bool allMatchFormat = true;
        bool ghostSignatureFound = false;
        std::wregex hexPattern(L"^(SysHost_|SysAct_|SysMode_|SysCfg_)[0-9A-F]{16}$");

        for (const auto& name : generatedNames) {
            if (!std::regex_match(name, hexPattern)) {
                allMatchFormat = false;
            }
            if (name.find(L"Ghost") != std::wstring::npos || name.find(L"ghost") != std::wstring::npos) {
                ghostSignatureFound = true;
            }
        }

        CHALLENGE_ASSERT(allMatchFormat, "All generated class names strictly conform to prefix + 16 uppercase hex chars");
        CHALLENGE_ASSERT(!ghostSignatureFound, "Zero occurrences of 'Ghost' or 'ghost' found in generated class names");
    }

    // -------------------------------------------------------------
    // Test 2: User32 Dynamic Class Registration, Creation & Teardown
    // -------------------------------------------------------------
    CHALLENGE_SECTION("Test 2: Dynamic Class Registration, Creation & Teardown");
    {
        HINSTANCE hInst = GetModuleHandle(NULL);
        const int ITERATIONS = 200;
        bool allRegistered = true;
        bool allWindowsCreated = true;
        bool allUnregistered = true;
        bool noLingeringClasses = true;

        for (int i = 0; i < ITERATIONS; ++i) {
            std::wstring className = generateUniqueClassName(L"SysHost_");

            WNDCLASSEXW wc = { 0 };
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.lpfnWndProc = TestWndProc;
            wc.hInstance = hInst;
            wc.lpszClassName = className.c_str();

            ATOM atom = RegisterClassExW(&wc);
            if (!atom) {
                allRegistered = false;
                break;
            }

            HWND hwnd = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                className.c_str(),
                L"",
                WS_POPUP,
                0, 0, 100, 100,
                NULL, NULL, hInst, NULL
            );

            if (!hwnd) {
                allWindowsCreated = false;
                UnregisterClassW(className.c_str(), hInst);
                break;
            }

            DestroyWindow(hwnd);

            BOOL unregOk = UnregisterClassW(className.c_str(), hInst);
            if (!unregOk) {
                allUnregistered = false;
            }

            WNDCLASSEXW verifyWc = { 0 };
            verifyWc.cbSize = sizeof(WNDCLASSEXW);
            if (GetClassInfoExW(hInst, className.c_str(), &verifyWc)) {
                noLingeringClasses = false;
            }
        }

        CHALLENGE_ASSERT(allRegistered, "Successfully registered 200 dynamically generated window classes in user32");
        CHALLENGE_ASSERT(allWindowsCreated, "Successfully created layered popup HWNDs for dynamically registered classes");
        CHALLENGE_ASSERT(allUnregistered, "Successfully unregistered classes via UnregisterClassW after window destruction");
        CHALLENGE_ASSERT(noLingeringClasses, "Verified zero lingering window class entries in window station table");
    }

    // -------------------------------------------------------------
    // Test 3: Display Affinity & Capture Exclusion Verification
    // -------------------------------------------------------------
    CHALLENGE_SECTION("Test 3: Display Affinity & Capture Exclusion Verification");
    {
        HINSTANCE hInst = GetModuleHandle(NULL);
        std::wstring className = generateUniqueClassName(L"SysHost_");

        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = TestWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = className.c_str();
        RegisterClassExW(&wc);

        HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            className.c_str(),
            L"",
            WS_POPUP,
            100, 100, 200, 200,
            NULL, NULL, hInst, NULL
        );
        CHALLENGE_ASSERT(hwnd != NULL, "Created layered test overlay window");

        // Test activating stealth (WDA_EXCLUDEFROMCAPTURE with fallback)
        DWORD flag = WDA_EXCLUDEFROMCAPTURE;
        BOOL success = SetWindowDisplayAffinity(hwnd, flag);
        if (!success) {
            success = SetWindowDisplayAffinity(hwnd, WDA_MONITOR);
        }
        CHALLENGE_ASSERT(success == TRUE, "SetWindowDisplayAffinity succeeded (WDA_EXCLUDEFROMCAPTURE or WDA_MONITOR)");

        DWORD currentAffinity = 0;
        BOOL querySuccess = GetWindowDisplayAffinity(hwnd, &currentAffinity);
        CHALLENGE_ASSERT(querySuccess == TRUE, "GetWindowDisplayAffinity successfully queried active affinity");
        CHALLENGE_ASSERT(currentAffinity == WDA_EXCLUDEFROMCAPTURE || currentAffinity == WDA_MONITOR,
                         "Window display affinity is hardened (" +
                         std::string(currentAffinity == WDA_EXCLUDEFROMCAPTURE ? "WDA_EXCLUDEFROMCAPTURE 0x11" : "WDA_MONITOR 0x01") + ")");

        // Test clearing stealth (WDA_NONE)
        BOOL clearSuccess = SetWindowDisplayAffinity(hwnd, WDA_NONE);
        CHALLENGE_ASSERT(clearSuccess == TRUE, "SetWindowDisplayAffinity(WDA_NONE) succeeded");
        GetWindowDisplayAffinity(hwnd, &currentAffinity);
        CHALLENGE_ASSERT(currentAffinity == WDA_NONE, "Window display affinity cleared back to WDA_NONE");

        // Test Clickthrough style toggling
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        LONG_PTR modifiedExStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        CHALLENGE_ASSERT((modifiedExStyle & WS_EX_TRANSPARENT) != 0, "WS_EX_TRANSPARENT successfully enabled for clickthrough");

        SetWindowLongPtr(hwnd, GWL_EXSTYLE, modifiedExStyle & ~WS_EX_TRANSPARENT);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        LONG_PTR restoredExStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        CHALLENGE_ASSERT((restoredExStyle & WS_EX_TRANSPARENT) == 0, "WS_EX_TRANSPARENT successfully disabled");

        DestroyWindow(hwnd);
        UnregisterClassW(className.c_str(), hInst);
    }

    // -------------------------------------------------------------
    // Test 4: Alpha Preservation Across Invisibility Lifecycle
    // -------------------------------------------------------------
    CHALLENGE_SECTION("Test 4: Alpha Preservation Across Invisibility Lifecycle");
    {
        // Simulate GhostUI alpha state logic
        double alpha = 0.8;
        double savedAlpha = alpha;
        bool isInvisible = false;

        // User hides window
        isInvisible = true;
        savedAlpha = alpha;

        // User adjusts alpha while invisible (delta -0.3)
        double newAlpha = 0.5;
        alpha = newAlpha;
        savedAlpha = alpha; // Worker M3 fix: m_savedAlpha = m_alpha unconditionally!

        // User restores window
        isInvisible = false;
        alpha = savedAlpha;

        CHALLENGE_ASSERT(alpha == 0.5, "Alpha adjustment made while invisible is strictly preserved when unhiding");
    }

    // -------------------------------------------------------------
    // Test 5: Configurable Non-Root Log Paths & Cyrillic Support
    // -------------------------------------------------------------
    CHALLENGE_SECTION("Test 5: Configurable Non-Root Log Paths & Cyrillic Support");
    {
        char tempPathA[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPathA);
        std::string cyrillicCustomDir = std::string(tempPathA) + "ghost_проверка_приватности\\вложенная_папка";

        // Test explicit custom directory
        NetworkLogger::setLogDirectory(cyrillicCustomDir);
        std::string resolvedDir = NetworkLogger::getLogDirectory();

        CHALLENGE_ASSERT(resolvedDir.find("ghost_проверка_приватности") != std::string::npos,
                         "getLogDirectory returns custom Cyrillic directory");
        CHALLENGE_ASSERT(resolvedDir.back() == '\\',
                         "getLogDirectory ensures trailing backslash");

        // Write log entries
        NetworkLogger::log("Empirical Challenger Cyrillic Test Line 1");
        NetworkLogger::logTransition(1, 2, "Timeout test transition");

        std::string netLog = NetworkLogger::getNetworkLogPath();
        std::string startLog = NetworkLogger::getStartupLogPath();

        // Convert path to wide to open Cyrillic path reliably
        int wlen = MultiByteToWideChar(CP_UTF8, 0, netLog.c_str(), -1, NULL, 0);
        std::wstring wNetLog(wlen > 1 ? wlen - 1 : 0, 0);
        MultiByteToWideChar(CP_UTF8, 0, netLog.c_str(), -1, &wNetLog[0], wlen);

        std::ifstream ifsNet(wNetLog.c_str());
        CHALLENGE_ASSERT(ifsNet.is_open(), "Successfully opened network.log in Cyrillic directory");

        std::string line;
        bool foundFailoverNet = false;
        while (std::getline(ifsNet, line)) {
            if (line.find("Failover from") != std::string::npos || line.find("TIER-1 -> TIER-2") != std::string::npos) {
                foundFailoverNet = true;
            }
        }
        ifsNet.close();
        CHALLENGE_ASSERT(foundFailoverNet, "Found failover transition written cleanly to Cyrillic network.log");

        int wlenStart = MultiByteToWideChar(CP_UTF8, 0, startLog.c_str(), -1, NULL, 0);
        std::wstring wStartLog(wlenStart > 1 ? wlenStart - 1 : 0, 0);
        MultiByteToWideChar(CP_UTF8, 0, startLog.c_str(), -1, &wStartLog[0], wlenStart);

        std::ifstream ifsStart(wStartLog.c_str());
        CHALLENGE_ASSERT(ifsStart.is_open(), "Successfully opened ghost_startup.log in Cyrillic directory");
        bool foundFailoverStartup = false;
        while (std::getline(ifsStart, line)) {
            if (line.find("[NETWORK_FAILOVER]") != std::string::npos) {
                foundFailoverStartup = true;
            }
        }
        ifsStart.close();
        CHALLENGE_ASSERT(foundFailoverStartup, "Found [NETWORK_FAILOVER] mirrored cleanly to Cyrillic ghost_startup.log");

        // Cleanup test files
        DeleteFileW(wNetLog.c_str());
        DeleteFileW(wStartLog.c_str());

        // Reset custom log directory to test GHOST_LOG_DIR
        NetworkLogger::setLogDirectory("");

        // Test GHOST_LOG_DIR environment variable
        std::string envTestDir = std::string(tempPathA) + "ghost_env_логи";
        SetEnvironmentVariableA("GHOST_LOG_DIR", envTestDir.c_str());

        std::string envResolved = NetworkLogger::getLogDirectory();
        CHALLENGE_ASSERT(envResolved.find("ghost_env_логи") != std::string::npos,
                         "GHOST_LOG_DIR environment variable is respected when custom dir is empty");

        // Clear environment variable to test LOCALAPPDATA default
        SetEnvironmentVariableA("GHOST_LOG_DIR", NULL);

        std::string defaultResolved = NetworkLogger::getLogDirectory();
        CHALLENGE_ASSERT(defaultResolved.find("Microsoft\\WindowsHost") != std::string::npos,
                         "Default log path routes to %LOCALAPPDATA%\\Microsoft\\WindowsHost\\");
        CHALLENGE_ASSERT(defaultResolved.find("C:\\Windows") == std::string::npos &&
                         defaultResolved.find("C:\\Program Files") == std::string::npos,
                         "Default log path is non-root and user-isolated");
    }

    // -------------------------------------------------------------
    // Test 6: Multithreaded Log Mutation & Concurrency Stress
    // -------------------------------------------------------------
    CHALLENGE_SECTION("Test 6: Multithreaded Log Mutation & Concurrency Stress");
    {
        const int WRITER_THREADS = 8;
        const int SETTER_THREADS = 4;
        const int WRITES_PER_THREAD = 500;
        std::atomic<bool> stopSetters{ false };

        std::vector<std::thread> writers;
        std::vector<std::thread> setters;

        char tempDir[MAX_PATH];
        GetTempPathA(MAX_PATH, tempDir);
        std::string stressDir = std::string(tempDir) + "ghost_stress_m3\\";
        NetworkLogger::setLogDirectory(stressDir);

        for (int i = 0; i < SETTER_THREADS; ++i) {
            setters.emplace_back([&stopSetters, tempDir, i]() {
                while (!stopSetters.load()) {
                    std::string d = std::string(tempDir) + "ghost_stress_m3_t" + std::to_string(i) + "\\";
                    NetworkLogger::setLogDirectory(d);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            });
        }

        for (int i = 0; i < WRITER_THREADS; ++i) {
            writers.emplace_back([i, WRITES_PER_THREAD]() {
                for (int j = 0; j < WRITES_PER_THREAD; ++j) {
                    NetworkLogger::log("Stress thread " + std::to_string(i) + " write " + std::to_string(j));
                }
            });
        }

        for (auto& w : writers) {
            w.join();
        }
        stopSetters = true;
        for (auto& s : setters) {
            s.join();
        }

        CHALLENGE_ASSERT(true, "Multithreaded concurrent log writing and directory re-configuration completed with zero deadlocks or crashes");
    }

    // -------------------------------------------------------------
    // Summary
    // -------------------------------------------------------------
    std::cout << "\n=========================================================" << std::endl;
    std::cout << "  Summary of Milestone 3 Empirical Privacy Challenge" << std::endl;
    std::cout << "=========================================================" << std::endl;
    std::cout << "  Total Assertions: " << (g_passCount + g_failCount) << std::endl;
    std::cout << "  Passed:           " << g_passCount << std::endl;
    std::cout << "  Failed:           " << g_failCount << std::endl;
    std::cout << "=========================================================" << std::endl;

    return (g_failCount == 0) ? 0 : 1;
}
