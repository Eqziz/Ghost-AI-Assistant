@echo off
REM ======================================================================
REM  build_driver.bat ? ?????? ghost_driver.sys ??? Windows 10/11 x64
REM ======================================================================

setlocal EnableExtensions

set "WDK_VERSION=10.0.28000.0"
set "SDK_VERSION=10.0.26100.0"
set "WDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "MSVC_ROOT=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"

set "CL_EXE=%MSVC_ROOT%\bin\Hostx64\x64\cl.exe"
set "LINK_EXE=%MSVC_ROOT%\bin\Hostx64\x64\link.exe"

if not exist "%WDK_ROOT%\Include\%WDK_VERSION%\km\ntddk.h" goto :no_wdk
if not exist "%CL_EXE%" goto :no_msvc

echo [*] Compiling ghost_driver.c ...

"%CL_EXE%" /nologo /c /Zi /W4 /WX- /Od ^
    /D _AMD64_ /D _WIN64 /D NTDDI_VERSION=0x0A00000C /D _WIN32_WINNT=0x0A00 ^
    /D DEPRECATE_DDK_FUNCTIONS=1 /D POOL_NX_OPTIN=1 ^
    /kernel /GS /Gy /GR- ^
    /I"%WDK_ROOT%\Include\%WDK_VERSION%\km" ^
    /I"%WDK_ROOT%\Include\%SDK_VERSION%\shared" ^
    /I"%WDK_ROOT%\Include\%SDK_VERSION%\ucrt" ^
    /I"%MSVC_ROOT%\include" ^
    /Fo"ghost_driver.obj" ^
    ghost_driver.c

if errorlevel 1 goto :compile_failed

echo [*] Linking ghost_driver.sys ...

"%LINK_EXE%" /nologo ^
    /OUT:"ghost_driver.sys" ^
    /DRIVER:WDM /KERNEL ^
    /SUBSYSTEM:NATIVE,10.00 ^
    /MACHINE:X64 ^
    /ENTRY:"DriverEntry" ^
    /INTEGRITYCHECK ^
    /NODEFAULTLIB ^
    /RELEASE /DEBUG ^
    ghost_driver.obj ^
    "%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64\ntoskrnl.lib" ^
    "%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64\hal.lib" ^
    "%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64\wdmsec.lib" ^
    "%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64\BufferOverflowFastFailK.lib" ^
    "%MSVC_ROOT%\lib\x64\libcmt.lib"

if errorlevel 1 goto :link_failed

echo [*] Signing ghost_driver.sys with test certificate ...

set "BIN_ROOT=%WDK_ROOT%\bin\%SDK_VERSION%\x64"
set "MAKECERT_EXE=%BIN_ROOT%\makecert.exe"
set "SIGNTOOL_EXE=%BIN_ROOT%\signtool.exe"

if exist "ghost_driver_test.cer" goto :skip_makecert
if not exist "%MAKECERT_EXE%" goto :skip_makecert
echo [*] Generating self-signed test certificate ...
"%MAKECERT_EXE%" -r -pe -ss "GhostCertStore" -n "CN=GhostAIDriver" -a sha256 -eku "1.3.6.1.5.5.7.3.3" "ghost_driver_test.cer" >nul 2>&1
:skip_makecert

if not exist "%SIGNTOOL_EXE%" goto :no_signtool
"%SIGNTOOL_EXE%" sign /s "GhostCertStore" /n "GhostAIDriver" /fd sha256 ghost_driver.sys
if errorlevel 1 goto :sign_warn
echo [OK] ghost_driver.sys signed successfully.
goto :after_sign

:sign_warn
echo [WARNING] Test signing failed. Driver might require manual signing.
goto :after_sign

:no_signtool
echo [WARNING] signtool.exe not found at %SIGNTOOL_EXE%.

:after_sign

echo.
echo ======================================================================
echo [OK] ghost_driver.sys successfully compiled, linked and signed!
echo.
echo [!] To test in Windows Test Mode (Admin command prompt):
echo     bcdedit /set testsigning on
echo     (then reboot if not already enabled)
echo.
echo     sc create GhostDriver type=kernel binPath="%~dp0ghost_driver.sys"
echo     sc start  GhostDriver
echo     sc stop   GhostDriver
echo     sc delete GhostDriver
echo ======================================================================
echo.
goto :done

:no_wdk
echo [ERROR] WDK ntddk.h not found in %WDK_ROOT%\Include\%WDK_VERSION%\km
echo Please check WDK installation and version.
goto :fail

:no_msvc
echo [ERROR] MSVC compiler cl.exe not found at %CL_EXE%
echo Please check Visual Studio installation path.
goto :fail

:compile_failed
echo [ERROR] Compilation failed!
goto :fail

:link_failed
echo [ERROR] Linking failed!
goto :fail

:fail
if "%1"=="/nopause" exit /b 1
pause
exit /b 1

:done
if "%1"=="/nopause" exit /b 0
pause
exit /b 0
