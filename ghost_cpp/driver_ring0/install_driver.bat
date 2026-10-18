@echo off
REM ======================================================================
REM  install_driver.bat — Установка ghost_driver.sys (запускать от Admin!)
REM ======================================================================
title Ghost Driver Installer

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Этот скрипт нужно запускать от имени Администратора!
    echo     Правая кнопка → "Запуск от имени администратора"
    pause
    exit /b 1
)

set DRIVER_NAME=GhostAI
set DRIVER_PATH=%~dp0ghost_driver.sys

if not exist "%DRIVER_PATH%" (
    echo [ERROR] ghost_driver.sys не найден в папке скрипта!
    echo         Сначала скомпилируй драйвер: build_driver.bat
    pause
    exit /b 1
)

echo ======================================================
echo   Ghost Driver — Установка
echo ======================================================
echo.

REM --- Шаг 1: Проверяем test-signing mode ---
echo [1/4] Проверяю Test-Signing Mode...
bcdedit /enum {current} | findstr /i "testsigning" | findstr /i "Yes" >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo [!] Test-Signing Mode НЕ включен.
    echo     ObRegisterCallbacks требует подписанный драйвер или test-signing.
    echo.
    set /p ENABLE_TS="Включить Test-Signing и перезагрузить? (Y/N): "
    if /i "%ENABLE_TS%"=="Y" (
        bcdedit /set testsigning on
        if %errorlevel% neq 0 (
            echo [ERROR] Не удалось включить test-signing.
            echo         Возможно, нужно отключить Secure Boot в BIOS.
            pause
            exit /b 1
        )
        echo [OK] Test-Signing включен. Нужна перезагрузка!
        echo     После перезагрузки запусти этот скрипт повторно.
        echo.
        set /p REBOOT="Перезагрузить сейчас? (Y/N): "
        if /i "%REBOOT%"=="Y" (
            shutdown /r /t 3 /c "Ghost Driver: Включение Test-Signing Mode"
        )
        exit /b 0
    ) else (
        echo [!] Продолжаю без test-signing. ObRegisterCallbacks может не заработать.
    )
) else (
    echo [OK] Test-Signing Mode уже включен.
)

REM --- Шаг 2: Остановить и удалить старый сервис (если есть) ---
echo.
echo [2/4] Проверяю существующий сервис...
sc query %DRIVER_NAME% >nul 2>&1
if %errorlevel% equ 0 (
    echo     Останавливаю старый драйвер...
    sc stop %DRIVER_NAME% >nul 2>&1
    timeout /t 2 /nobreak >nul
    sc delete %DRIVER_NAME% >nul 2>&1
    timeout /t 1 /nobreak >nul
    echo [OK] Старый сервис удален.
) else (
    echo [OK] Старый сервис не найден.
)

REM --- Шаг 3: Копируем драйвер в System32\drivers ---
echo.
echo [3/4] Копирую драйвер...
copy /Y "%DRIVER_PATH%" "%SystemRoot%\System32\drivers\ghost_driver.sys" >nul
if %errorlevel% neq 0 (
    echo [ERROR] Не удалось скопировать драйвер!
    pause
    exit /b 1
)
echo [OK] ghost_driver.sys -> %SystemRoot%\System32\drivers\

REM --- Шаг 3.5: Устанавливаем сертификат тестовой подписи в доверенные ---
set "CER_PATH=%~dp0ghost_driver_test.cer"
if exist "%CER_PATH%" (
    echo     Импортирую тестовый сертификат в хранилища Root и TrustedPublisher...
    certutil -addstore -f "Root" "%CER_PATH%" >nul 2>&1
    certutil -addstore -f "TrustedPublisher" "%CER_PATH%" >nul 2>&1
    echo [OK] Сертификат доверен для Test Mode.
)

REM --- Шаг 4: Создаем и запускаем сервис ---
echo.
echo [4/4] Создаю и запускаю сервис...
sc create %DRIVER_NAME% type= kernel binPath= "%SystemRoot%\System32\drivers\ghost_driver.sys" start= demand
if %errorlevel% neq 0 (
    echo [ERROR] Не удалось создать сервис!
    pause
    exit /b 1
)

sc start %DRIVER_NAME%
if %errorlevel% neq 0 (
    echo [ERROR] Не удалось запустить драйвер!
    echo         Проверь: включен ли Test-Signing Mode?
    echo         Для диагностики: eventvwr.msc → Windows Logs → System
    pause
    exit /b 1
)

echo.
echo ======================================================
echo   [OK] Драйвер ghost_driver.sys загружен и работает!
echo ======================================================
echo.
echo   Устройство: \\.\GhostAI
echo   Сервис:     %DRIVER_NAME% (demand start)
echo   Режим:      ObRegisterCallbacks only (SAFE)
echo.
echo   Теперь запускай GhostAI.exe — он автоматически
echo   подключится к драйверу и включит защиту процесса.
echo.
echo   Для остановки:  sc stop %DRIVER_NAME%
echo   Для удаления:   sc delete %DRIVER_NAME%
echo.
pause

