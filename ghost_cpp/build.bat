@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo          Building Ghost-AI Native C++ Edition
echo =======================================================

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 (
    echo [ERROR] Failed to initialize Visual Studio build tools!
    exit /b 1
)

set "CMAKE_BIN=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if "%1"=="--clean" goto do_clean
if "%1"=="clean" goto do_clean
goto check_build_dir

:do_clean
echo [INFO] Clean build requested. Purging build directory...
if exist build rmdir /s /q build

:check_build_dir
if not exist build mkdir build
cd build

if not exist CMakeCache.txt goto do_fresh_cmake

echo [INFO] Verifying existing CMake configuration...
"!CMAKE_BIN!" .. >nul 2>&1
if !errorlevel! equ 0 goto do_build

echo [WARN] Stale or incompatible CMakeCache.txt detected (source path or compiler mismatch).
echo [INFO] Triggering self-healing: purging build directory and regenerating configuration...
cd ..
rmdir /s /q build
mkdir build
cd build

:do_fresh_cmake
echo [INFO] Configuring fresh CMake project...
"!CMAKE_BIN!" ..
if !errorlevel! neq 0 (
    echo [ERROR] CMake configuration failed!
    exit /b 1
)

:do_build
echo [INFO] Building Release configuration...
"!CMAKE_BIN!" --build . --config Release
if !errorlevel! neq 0 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo.
echo =======================================================
echo   Build Successful! Binary at: build\Release\GhostAI.exe
echo =======================================================
cd ..
exit /b 0
