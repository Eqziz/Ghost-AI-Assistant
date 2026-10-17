"""
packager.py — Автоматическая сборка релизного ZIP-архива для отправки покупателям в Telegram.
"""
import os
import zipfile
import logging
from pathlib import Path

logger = logging.getLogger(__name__)

ROOT_DIR = Path(__file__).resolve().parent.parent
DIST_DIR = Path(__file__).resolve().parent / "dist"
ZIP_PATH = DIST_DIR / "GhostAI_Release.zip"

README_TEXT = """===============================================================
                       GHOST AI — RELEASE
===============================================================

Благодарим за покупку лицензии Ghost AI!

1. БЫСТРЫЙ СТАРТ:
   - Распакуйте все файлы из архива в отдельную папку.
   - Запустите GhostAI.exe.
   - В появившемся окне введите ваш лицензионный ключ (полученный в боте).
   - Нажмите "Активировать".

2. УСТАНОВКА RING 0 ДРАЙВЕРА (ДЛЯ РЕЖИМА HARD / STEALTH):
   - Если вы используете режим Hard (максимальная скрытность в играх):
     1. Запустите install_driver.bat от имени Администратора (ПКМ -> Запуск от имени администратора).
     2. Скрипт автоматически импортирует цифровой сертификат и включит тестовый режим ядра.
     3. Перезагрузите компьютер.
     4. После перезагрузки драйвер ghost_driver.sys работает в фоне.
   - Если вам достаточно режима Light (стандартный ассистент), устанавливать драйвер не требуется!

3. УПРАВЛЕНИЕ:
   - Нажмите комбинацию клавиш из настроек (по умолчанию Ctrl+Shift+Z или настроенная вами),
     чтобы выделить область экрана и получить мгновенный анализ от ИИ.
   - Скрыть / показать ответы: настроенная горячая клавиша (например, Esc / Tab).

4. ПОДДЕРЖКА И ЛИЧНЫЙ КАБИНЕТ:
   - Продлить подписку или сменить привязку ПК (HWID): @gghostaibot
   - Вопросы и техподдержка: в меню бота (/help).
===============================================================
"""

SAMPLE_CONFIG = """{
  "api_provider": "openrouter",
  "openrouter_api_key": "YOUR_OPENROUTER_KEY",
  "openrouter_model": "anthropic/claude-3.5-sonnet",
  "ollama_url": "http://127.0.0.1:11434",
  "ollama_model": "llava:latest",
  "system_prompt": "Ты — скрытный ассистент Ghost AI. Отвечай кратко, чётко и по делу.",
  "hotkey_capture": "ctrl+shift+z",
  "hotkey_toggle": "tab",
  "hotkey_clear": "esc",
  "hotkey_kill": "ctrl+shift+k",
  "capture_mode": "selected_area",
  "font_size": 16,
  "theme": "dark_stealth",
  "opacity": 0.85
}
"""


def create_release_package(force: bool = False) -> Path:
    """
    Создает GhostAI_Release.zip в bot/dist/, если он отсутствует или обновлены бинарники.
    Возвращает путь к ZIP-файлу.
    """
    DIST_DIR.mkdir(parents=True, exist_ok=True)

    exe_path = ROOT_DIR / "ghost_cpp" / "build" / "Release" / "GhostAI.exe"
    driver_dir = ROOT_DIR / "ghost_cpp" / "driver_ring0"
    driver_path = driver_dir / "ghost_driver.sys"
    cert_path = driver_dir / "ghost_driver_test.cer"
    install_bat = driver_dir / "install_driver.bat"

    # Проверяем, нужно ли пересобирать архив
    if ZIP_PATH.exists() and not force:
        zip_mtime = ZIP_PATH.stat().st_mtime
        key_files = [exe_path, driver_path, install_bat, cert_path]
        sources_mtime = max((p.stat().st_mtime for p in key_files if p.exists()), default=0)
        if zip_mtime > sources_mtime:
            return ZIP_PATH

    logger.info(f"Создание релизного архива {ZIP_PATH}...")
    with zipfile.ZipFile(ZIP_PATH, "w", zipfile.ZIP_DEFLATED) as zf:
        if exe_path.exists():
            zf.write(exe_path, arcname="GhostAI.exe")
        if driver_path.exists():
            zf.write(driver_path, arcname="ghost_driver.sys")
        if cert_path.exists():
            zf.write(cert_path, arcname="ghost_driver_test.cer")
        if install_bat.exists():
            zf.write(install_bat, arcname="install_driver.bat")

        # Добавляем README и шаблон конфига
        zf.writestr("README_INSTALL.txt", README_TEXT)
        zf.writestr("config.json", SAMPLE_CONFIG)

    logger.info(f"Архив успешно создан: {ZIP_PATH} ({ZIP_PATH.stat().st_size} байт)")
    return ZIP_PATH


if __name__ == "__main__":
    p = create_release_package(force=True)
    print("Готово:", p, f"({p.stat().st_size} bytes)")

