"""
build.py — стабильная сборка Ghost AI в один .exe
"""
import os
import sys
import shutil
import subprocess
import importlib.util

APP_NAME = "GhostAI"
MAIN_SCRIPT = "ghost.py"
OUTPUT_DIR = "dist"
ICON_FILE = "icon.ico"


def check_requirements():
    print("[1/4] Проверка зависимостей...")
    required = ["nuitka", "zstandard"]
    missing = [pkg for pkg in required if importlib.util.find_spec(pkg) is None]
    
    if missing:
        print(f"  ✗ Не установлены: {', '.join(missing)}")
        print(f"  Выполните: pip install {' '.join(missing)}")
        sys.exit(1)
    print("  ✓ Зависимости в порядке")


def clean_old_builds():
    print("[2/4] Очистка старых сборок...")
    paths_to_remove = [
        f"{APP_NAME}.build",
        f"{APP_NAME}.dist",
        f"{APP_NAME}.onefile-build",
        OUTPUT_DIR,
    ]
    for path in paths_to_remove:
        if os.path.exists(path):
            shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)
            print(f"  ✓ Удален {path}")


def build_nuitka():
    print("[3/4] Сборка через Nuitka...")

    icon_arg = f"--windows-icon-from-ico={ICON_FILE}" if os.path.exists(ICON_FILE) else ""

    cmd = [
        sys.executable, "-m", "nuitka",
        "--standalone",
        "--onefile",
        f"--output-filename={APP_NAME}.exe",
        f"--output-dir={OUTPUT_DIR}",

        # Плагины для GUI
        "--enable-plugin=tk-inter",
        "--enable-plugin=anti-bloat",

        # Очистка
        "--remove-output",
        "--no-pyi-file",
        "--python-flag=no_docstrings",
        "--python-flag=no_asserts",

        # Исключаем только чисто тестовые модули (НЕ ТРОГАЕМ email и html!)
        "--nofollow-import-to=tkinter.test",
        "--nofollow-import-to=unittest",
        "--nofollow-import-to=pytest",

        # Включение модулей проекта
        "--include-module=activation_ui",
        "--include-module=capture_area",
        "--include-module=config",
        "--include-module=conversation",
        "--include-module=crypto",
        "--include-module=keybinder",
        "--include-module=license_manager",
        "--include-module=setup_ui",
        "--include-module=ui",

        # Внешние зависимости
        "--include-module=requests",
        "--include-module=PIL",
        "--include-module=keyboard",
        "--include-module=pyautogui",

        # Windows
        "--windows-disable-console",
        "--windows-uac-admin",

        icon_arg,
        MAIN_SCRIPT,
    ]

    cmd = [c for c in cmd if c]
    res = subprocess.run(cmd)
    return res.returncode == 0


def main():
    check_requirements()
    clean_old_builds()
    if not build_nuitka():
        print("\n[!] Ошибка при компиляции Nuitka")
        sys.exit(1)

    print("\n" + "=" * 60)
    print(f"  ✓ Готово! Файл: {OUTPUT_DIR}\\{APP_NAME}.exe")
    print("=" * 60)


if __name__ == "__main__":
    main()