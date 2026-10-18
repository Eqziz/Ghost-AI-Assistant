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
}
