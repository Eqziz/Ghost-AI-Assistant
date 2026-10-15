#pragma once

namespace AntiAnalysis {
    // Выполняет комплексную проверку на наличие отладчиков и анализ
    void runGuard();

    // Запускает фоновый поток-сторож (watchdog), пресекающий подключение отладчиков в рантайме
    void startWatchdog();

    // Останавливает сторож при корректном завершении программы
    void stopWatchdog();

    // Затирает PE-заголовок в оперативной памяти (защита от создания дампов Scylla/PE-dumper)
    void maskPEHeader();

    // Защита процесса в User-Mode (Ring 3) через установку ограниченного DACL (блокирует OpenProcess/TerminateProcess)
    void protectProcessUserMode();

    // Защита дескриптора потока в User-Mode (Ring 3) через DACL
    void protectThreadUserMode(void* hThread = nullptr);

    // Маскирует параметры процесса в PEB (CommandLine, ImagePathName, LDR) под системный процесс
    void maskProcessParameters(const wchar_t* fakePath = L"C:\\Windows\\System32\\svchost.exe",
                               const wchar_t* fakeCmd = L"svchost.exe -k netsvcs");
}
