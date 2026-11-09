# <img src="icon.ico" width="32" height="32" alt="GhostAI Icon" valign="middle"> GhostAI — Native Windows Assistive AI Visual Overlay

![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011%20x64-blue)
![Language](https://img.shields.io/badge/Language-C%2B%2B17%20%2F%20Python%203.11-brightgreen)
![Runtime](https://img.shields.io/badge/Runtime-Standalone%20%2FMT%20(Zero%20DLL)-orange)
![License](https://img.shields.io/badge/License-MIT-green)

> [!NOTE]
> ### Назначение проекта / Research & Assistive Focus
> Программный комплекс **GhostAI** разработан в качестве вспомогательной технологии визуальной доступности (Assistive / Accessibility Technology), а также для исследований архитектуры подсистем Win32/GDI+ и низкоуровневых интерфейсов ОС Windows.
> Подробнее см. в [DISCLAIMER.md](DISCLAIMER.md), [LICENSE](LICENSE) и [TERMS_OF_SERVICE.md](TERMS_OF_SERVICE.md).

---

## 🏛 Архитектура проекта

Комплекс состоит из трёх независимых уровней:

1. **`ghost_cpp/` — Высокопроизводительный нативный C++17 клиент**:
   * **Графический оверлей**: Низколатентный прозрачный клик-сквозной интерфейс Win32 с защитой от захвата экрана (`WDA_EXCLUDEFROMCAPTURE`).
   * **Сетевой стек отказоустойчивости**: Трёхуровневый каскад (Cloud API ➔ Cloudflare CDN Relay ➔ Local Offline Ollama). Мгновенный перехват сетевых блокировок (`WinHTTP 12029`, сбросов соединения `12030` и кодов `5xx`).
   * **Безопасность и анти-анализ**: Спуфинг PEB (Process Parameters & Modules), установка ограниченных DACL на потоки и процесс, шифрование секретов в конфигурации через Windows DPAPI.
   * **Автономность**: Статическая сборка (`/MT`) без внешних DLL-зависимостей.

2. **`relay/` — Защищённый CDN-шлюз Cloudflare Worker**:
   * Проксирование запросов к нейросетям через порт **443 (HTTPS)** в глобальной сети Cloudflare.
   * Маскировка заголовков под Google Chrome 131.
   * Изоляция API-ключей в облачных переменных воркера (клиент может работать в режиме Zero-Config).

3. **`ghost_cpp/driver_ring0/` — Ядерный драйвер (Ring 0, опционально)**:
   * Защита дескрипторов процесса через ядерные обратные вызовы `ObRegisterCallbacks`.

---

## 🚀 Быстрый старт (Сборка из исходников)

### Требования
* Windows 10 или 11 (x64)
* Visual Studio 2022 / 2026 с компонентом «Разработка классических приложений на C++»
* CMake 3.20+
* Python 3.10+ (для запуска E2E тестов)

### Сборка клиента C++
```cmd
cd ghost_cpp
build.bat
```
Исполняемый файл будет скомпилирован в `ghost_cpp\build\Release\GhostAI.exe`.

### Запуск тестов
```cmd
python ghost_cpp\tests\e2e\run_tests.py
```

---

## 📜 Документация и политики

* 📄 [Отказ от ответственности (DISCLAIMER.md)](DISCLAIMER.md) — разграничение ответственности и правила использования.
* 📄 [Лицензионное соглашение (LICENSE)](LICENSE) — открытая лицензия MIT.
* 📄 [Условия обслуживания (TERMS_OF_SERVICE.md)](TERMS_OF_SERVICE.md) — регламент использования ПО.
* 📄 [Политика конфиденциальности (PRIVACY_POLICY.md)](PRIVACY_POLICY.md) — архитектура Zero-Data Retention и защита пользовательских данных.
* 📄 [Политика безопасности (SECURITY.md)](SECURITY.md) — порядок ответственного раскрытия уязвимостей.

---

## 💬 Сообщество и обратная связь
* Вопросы, баг-репорты и предложения принимаются через GitHub Issues и Pull Requests.

