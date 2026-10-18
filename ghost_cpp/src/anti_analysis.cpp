#include "anti_analysis.hpp"
#include "api_hasher.hpp"
#include <windows.h>
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

} // namespace AntiAnalysis
