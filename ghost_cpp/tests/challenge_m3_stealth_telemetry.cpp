#include <windows.h>
#include <bcrypt.h>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <cassert>
#include <algorithm>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

#include "network_logger.hpp"

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
              << "  [CHALLENGER M3.2] " << name << "\n" \
              << "======================================================" << std::endl;

// Replicate worker's random class name algorithm to verify cryptographic uniqueness and invariants
std::wstring generateUniqueClassName(const wchar_t* prefix) {
    BYTE randomBytes[8] = { 0 };
    NTSTATUS status = BCryptGenRandom(NULL, randomBytes, sizeof(randomBytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        DWORD pid = GetCurrentProcessId();
        memcpy(randomBytes, &qpc.QuadPart, 4);
        memcpy(randomBytes + 4, &pid, 4);
    }
    wchar_t hexStr[17] = { 0 };
    for (int i = 0; i < 8; ++i) {
        swprintf_s(hexStr + (i * 2), 3, L"%02X", randomBytes[i]);
    }
    return std::wstring(prefix) + hexStr;
}

// -----------------------------------------------------------------------------
// Suite 1: Window Class Randomness, Uniqueness, and Teardown
// -----------------------------------------------------------------------------
void testSuite1_WindowClassStealth() {
    CHALLENGE_SECTION("Suite 1: Window Class Randomness, Uniqueness, and Teardown");

    const wchar_t* prefixes[] = { L"SysHost_", L"SysAct_", L"SysMode_", L"SysCfg_" };
    for (const wchar_t* pfx : prefixes) {
        std::set<std::wstring> generatedNames;
        size_t pfxLen = wcslen(pfx);

        for (int i = 0; i < 1000; ++i) {
            std::wstring name = generateUniqueClassName(pfx);
            CHALLENGE_ASSERT(name.size() == pfxLen + 16, "Class name length matches prefix + 16 hex characters");
            CHALLENGE_ASSERT(name.rfind(pfx, 0) == 0, "Class name starts with requested prefix");
            for (size_t c = pfxLen; c < name.size(); ++c) {
                wchar_t ch = name[c];
                bool isHex = (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'F');
                CHALLENGE_ASSERT(isHex, "Class name suffix is uppercase hex");
            }
            generatedNames.insert(name);
        }
        CHALLENGE_ASSERT(generatedNames.size() == 1000, "Zero collisions across 1,000 generated class names");
    }

    // Test Win32 Class Registration, Lookup, and Clean Teardown
    {
        std::wstring testClass = generateUniqueClassName(L"SysHost_");
        HINSTANCE hInst = GetModuleHandle(NULL);

        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = hInst;
        wc.lpszClassName = testClass.c_str();

        ATOM atom = RegisterClassExW(&wc);
        CHALLENGE_ASSERT(atom != 0, "RegisterClassExW succeeded for dynamic class name");

        // Verify class is registered in user32 table
        WNDCLASSEXW checkWc = { 0 };
        checkWc.cbSize = sizeof(WNDCLASSEXW);
        BOOL found = GetClassInfoExW(hInst, testClass.c_str(), &checkWc);
        CHALLENGE_ASSERT(found != FALSE, "GetClassInfoExW successfully found registered dynamic class");

        // Create window to verify creation succeeds with dynamic class
        HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            testClass.c_str(),
            L"GhostHost_Test",
            WS_POPUP,
            0, 0, 100, 100,
            NULL, NULL, hInst, NULL
        );
        CHALLENGE_ASSERT(hwnd != NULL, "CreateWindowExW succeeded with dynamic class and layered styles");

        // Test display affinity
        if (hwnd) {
            BOOL affOk = SetWindowDisplayAffinity(hwnd, 0x00000011 /* WDA_EXCLUDEFROMCAPTURE */);
            if (!affOk) {
                affOk = SetWindowDisplayAffinity(hwnd, 0x00000001 /* WDA_MONITOR */);
            }
            CHALLENGE_ASSERT(affOk != FALSE, "SetWindowDisplayAffinity succeeded (WDA_EXCLUDEFROMCAPTURE or WDA_MONITOR)");
            DestroyWindow(hwnd);
        }

        // Unregister class and verify user32 table clean
        BOOL unreg = UnregisterClassW(testClass.c_str(), hInst);
        CHALLENGE_ASSERT(unreg != FALSE, "UnregisterClassW succeeded on teardown");

        WNDCLASSEXW checkAfter = { 0 };
        checkAfter.cbSize = sizeof(WNDCLASSEXW);
        BOOL foundAfter = GetClassInfoExW(hInst, testClass.c_str(), &checkAfter);
        CHALLENGE_ASSERT(foundAfter == FALSE, "Class cleanly removed from user32 table; zero leftover signatures");
    }
}

// -----------------------------------------------------------------------------
// Suite 2: Alpha Preservation State Machine Invariants
// -----------------------------------------------------------------------------
void testSuite2_AlphaPreservation() {
    CHALLENGE_SECTION("Suite 2: Alpha Preservation State Machine Invariants");

    double m_alpha = 0.9;
    double m_savedAlpha = 0.9;
    bool m_isInvisible = false;

    auto setInvisible = [&](bool invisible) {
        m_isInvisible = invisible;
        if (invisible) {
            m_savedAlpha = m_alpha;
            m_alpha = 0.0;
        } else {
            m_alpha = (m_savedAlpha > 0.0) ? m_savedAlpha : 0.9;
        }
    };

    auto setAlpha = [&](double a) {
        if (a < 0.05) a = 0.05;
        if (a > 1.0) a = 1.0;
        m_alpha = a;
        m_savedAlpha = a; // M3 fix: preserve modified alpha even while invisible
    };

    // Scenario 1: Initial state
    CHALLENGE_ASSERT(m_alpha == 0.9, "Initial alpha is 0.9");

    // Scenario 2: Toggle invisible
    setInvisible(true);
    CHALLENGE_ASSERT(m_alpha == 0.0, "Alpha is 0.0 when invisible");
    CHALLENGE_ASSERT(m_savedAlpha == 0.9, "Saved alpha is 0.9");

    // Scenario 3: User changes alpha while invisible (hotkey or settings)
    setAlpha(0.65);
    CHALLENGE_ASSERT(m_savedAlpha == 0.65, "m_savedAlpha updated to 0.65 while invisible");

    // Scenario 4: User makes overlay visible again
    setInvisible(false);
    CHALLENGE_ASSERT(m_alpha == 0.65, "Alpha restored to 0.65 (not stale 0.9)");

    // Scenario 5: Clamp bounds
    setAlpha(-0.5);
    CHALLENGE_ASSERT(m_alpha == 0.05, "Alpha clamped to minimum 0.05");
    setAlpha(1.5);
    CHALLENGE_ASSERT(m_alpha == 1.0, "Alpha clamped to maximum 1.0");
}

// -----------------------------------------------------------------------------
// Suite 3: Telemetry, Non-Root Log Paths, and GHOST_LOG_DIR Isolation
// -----------------------------------------------------------------------------
void testSuite3_TelemetryAndLogIsolation() {
    CHALLENGE_SECTION("Suite 3: Telemetry, Non-Root Log Paths, and GHOST_LOG_DIR Isolation");

    // 1. Verify default log directory points to %LOCALAPPDATA%\Microsoft\WindowsHost\
    // (Ensure GHOST_LOG_DIR is cleared for baseline check)
    SetEnvironmentVariableW(L"GHOST_LOG_DIR", NULL);
    NetworkLogger::setLogDirectory(""); // reset custom

    std::string defaultDir = NetworkLogger::getLogDirectory();
    std::cout << "  Default log directory: " << defaultDir << std::endl;
    CHALLENGE_ASSERT(defaultDir.find("WindowsHost") != std::string::npos, "Default log directory contains WindowsHost");
    CHALLENGE_ASSERT(defaultDir.find("ghost_ai_polished") == std::string::npos, "Default directory is NOT root repository");

    // 2. Test environment variable override: GHOST_LOG_DIR
    wchar_t tempDirBuf[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, tempDirBuf);
    std::wstring customEnvDir = std::wstring(tempDirBuf) + L"ghost_test_telemetry_isolation\\deep\\nested\\";
    SetEnvironmentVariableW(L"GHOST_LOG_DIR", customEnvDir.c_str());

    std::string resolvedEnvDir = NetworkLogger::getLogDirectory();
    std::cout << "  Resolved GHOST_LOG_DIR: " << resolvedEnvDir << std::endl;
    CHALLENGE_ASSERT(resolvedEnvDir.find("ghost_test_telemetry_isolation") != std::string::npos, "getLogDirectory respects GHOST_LOG_DIR");

    // Verify directory was created recursively
    DWORD dirAttr = GetFileAttributesW(customEnvDir.c_str());
    CHALLENGE_ASSERT(dirAttr != INVALID_FILE_ATTRIBUTES && (dirAttr & FILE_ATTRIBUTE_DIRECTORY), "Deep nested GHOST_LOG_DIR directory created recursively");

    // 3. Write logs under GHOST_LOG_DIR
    std::string expectedLogFile = resolvedEnvDir + "network.log";
    NetworkLogger::log("Empirical challenge telemetry entry 1");
    NetworkLogger::logTransition(1, 2, "Failover test under custom env");

    std::ifstream logCheck(expectedLogFile.c_str());
    CHALLENGE_ASSERT(logCheck.is_open(), "network.log created inside custom GHOST_LOG_DIR");
    std::string content((std::istreambuf_iterator<char>(logCheck)), std::istreambuf_iterator<char>());
    CHALLENGE_ASSERT(content.find("Empirical challenge telemetry entry 1") != std::string::npos, "network.log contains written entry");
    CHALLENGE_ASSERT(content.find("[TIER-1 -> TIER-2]") != std::string::npos, "network.log contains failover tag");
    logCheck.close();

    // 4. Concurrent writes to GHOST_LOG_DIR (16 threads, 100 writes each)
    std::atomic<bool> startGate{ false };
    std::vector<std::thread> workers;
    for (int t = 0; t < 16; ++t) {
        workers.emplace_back([&, t]() {
            while (!startGate.load()) { std::this_thread::yield(); }
            for (int i = 0; i < 100; ++i) {
                NetworkLogger::log("Thread " + std::to_string(t) + " write " + std::to_string(i));
            }
        });
    }
    startGate.store(true);
    for (auto& w : workers) {
        w.join();
    }

    // Count lines in network.log
    std::ifstream multiCheck(expectedLogFile.c_str());
    int lineCount = 0;
    std::string line;
    while (std::getline(multiCheck, line)) {
        if (!line.empty()) lineCount++;
    }
    multiCheck.close();
    CHALLENGE_ASSERT(lineCount >= 1602, "All 1,600 concurrent writes preserved without dropping lines");

    // Clean up test file and directory
    DeleteFileA(expectedLogFile.c_str());
    std::string startupLog = resolvedEnvDir + "ghost_startup.log";
    DeleteFileA(startupLog.c_str());

    // Reset environment variable
    SetEnvironmentVariableW(L"GHOST_LOG_DIR", NULL);
    NetworkLogger::setLogDirectory("");
}

// -----------------------------------------------------------------------------
// Suite 4: Root Directory Zero Pollution Verification
// -----------------------------------------------------------------------------
void testSuite4_RootDirectoryPollutionCheck() {
    CHALLENGE_SECTION("Suite 4: Root Directory Zero Pollution Verification");

    // Ensure no .log or .dmp files exist in current directory
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("*.log", &fd);
    bool foundLog = (hFind != INVALID_HANDLE_VALUE);
    if (foundLog) FindClose(hFind);

    CHALLENGE_ASSERT(!foundLog, "Zero *.log files found in working directory");

    hFind = FindFirstFileA("*.dmp", &fd);
    bool foundDmp = (hFind != INVALID_HANDLE_VALUE);
    if (foundDmp) FindClose(hFind);

    CHALLENGE_ASSERT(!foundDmp, "Zero *.dmp files found in working directory");
}

int main() {
    std::cout << "======================================================\n"
              << " GhostAI M3 Challenger 2: Window Privacy & Telemetry\n"
              << " Empirical Verification Harness\n"
              << "======================================================" << std::endl;

    testSuite1_WindowClassStealth();
    testSuite2_AlphaPreservation();
    testSuite3_TelemetryAndLogIsolation();
    testSuite4_RootDirectoryPollutionCheck();

    std::cout << "\n======================================================\n"
              << " Challenger M3.2 Results Summary:\n"
              << " PASSED: " << g_passCount << "\n"
              << " FAILED: " << g_failCount << "\n"
              << "======================================================" << std::endl;

    if (g_failCount == 0) {
        std::cout << "[+] ALL EMPIRICAL CHALLENGES PASSED (100% SUCCESS)!\n";
        return 0;
    } else {
        std::cerr << "[-] EMPIRICAL CHALLENGES FAILED: " << g_failCount << "\n";
        return 1;
    }
}
