#include "anti_analysis.hpp"
#include <windows.h>
#include <chrono>
#include <intrin.h>

namespace AntiAnalysis {

void runGuard() {
    // 1. Проверка PEB (IsDebuggerPresent)
    if (IsDebuggerPresent()) {
        ExitProcess(0);
    }

    // 2. Remote Debugger
    BOOL isDebugger = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &isDebugger) && isDebugger) {
        ExitProcess(0);
    }

    // 3. RDTSC / High resolution latency check
    auto t1 = std::chrono::high_resolution_clock::now();
    volatile long long sum = 0;
    for (int i = 0; i < 100; ++i) {
        sum += i;
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
    if (elapsed_ns > 50'000'000) { // 50 мс
        ExitProcess(0);
    }

    // Дополнительно: ThreadHideFromDebugger через ntdll
    typedef NTSTATUS(NTAPI* pfnNtSetInformationThread)(
        HANDLE ThreadHandle,
        ULONG ThreadInformationClass,
        PVOID ThreadInformation,
        ULONG ThreadInformationLength
    );
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        pfnNtSetInformationThread NtSetInfo = (pfnNtSetInformationThread)GetProcAddress(hNtdll, "NtSetInformationThread");
        if (NtSetInfo) {
            // ThreadHideFromDebugger = 0x11
            NtSetInfo(GetCurrentThread(), 0x11, NULL, 0);
        }
    }
}

} // namespace AntiAnalysis
