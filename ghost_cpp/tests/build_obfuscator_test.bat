@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 (
    echo [ERROR] vcvarsall failed
    exit /b 1
)

cd /d "%~dp0.."

cl /EHsc /std:c++17 /MT /O2 /I"include" ^
  /I"%~dp0..\..\SDK_TEST\WINDOWS KITS\10\INCLUDE\10.0.26100.0\UCRT" ^
  /I"%~dp0..\..\SDK_TEST\WINDOWS KITS\10\INCLUDE\10.0.26100.0\UM" ^
  /I"%~dp0..\..\SDK_TEST\WINDOWS KITS\10\INCLUDE\10.0.26100.0\SHARED" ^
  tests\test_obfuscator_concurrency.cpp ^
  /Fo:tests\ /Fe:tests\test_obfuscator_concurrency.exe /link ^
  /LIBPATH:"%~dp0..\..\SDK_TEST\WINDOWS KITS\10\LIB\10.0.26100.0\UM\X64" ^
  /LIBPATH:"%~dp0..\..\SDK_TEST\WINDOWS KITS\10\LIB\10.0.26100.0\UCRT\X64"

exit /b %errorlevel%
