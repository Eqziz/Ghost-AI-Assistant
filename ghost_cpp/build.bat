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

if not exist build (
    mkdir build
)

cd build
"%CMAKE_BIN%" -G "Visual Studio 17 2022" -A x64 ..
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed!
    exit /b 1
)

"%CMAKE_BIN%" --build . --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo.
echo =======================================================
echo   Build Successful! Binary at: build\Release\GhostAI.exe
echo =======================================================
cd ..
