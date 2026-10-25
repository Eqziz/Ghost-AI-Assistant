@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 (
    echo [ERROR] vcvarsall failed
    exit /b 1
)

cd /d "c:\Users\nodir\Desktop\project 2\ghost_cpp"

cl /EHsc /std:c++17 /utf-8 /MT /O2 /I"include" ^
  /I"C:\Users\nodir\Desktop\project 2\SDK_TEST\WINDOWS KITS\10\INCLUDE\10.0.26100.0\UCRT" ^
  /I"C:\Users\nodir\Desktop\project 2\SDK_TEST\WINDOWS KITS\10\INCLUDE\10.0.26100.0\UM" ^
  /I"C:\Users\nodir\Desktop\project 2\SDK_TEST\WINDOWS KITS\10\INCLUDE\10.0.26100.0\SHARED" ^
  tests\challenge_m3_c1_empirical.cpp ^
  src\proxy_utils.cpp ^
  src\vision_client.cpp ^
  src\conversation.cpp ^
  src\config.cpp ^
  src\crypto.cpp ^
  src\network_logger.cpp ^
  /Fe:tests\challenge_m3_c1_empirical.exe /link ^
  /LIBPATH:"C:\Users\nodir\Desktop\project 2\SDK_TEST\WINDOWS KITS\10\LIB\10.0.26100.0\UM\X64" ^
  /LIBPATH:"C:\Users\nodir\Desktop\project 2\SDK_TEST\WINDOWS KITS\10\LIB\10.0.26100.0\UCRT\X64" ^
  winhttp.lib crypt32.lib bcrypt.lib advapi32.lib user32.lib shell32.lib ole32.lib ws2_32.lib

exit /b %errorlevel%
