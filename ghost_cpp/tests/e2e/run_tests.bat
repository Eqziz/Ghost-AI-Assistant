@echo off
setlocal

echo =======================================================
echo     Running GhostAI E2E Resilient Network Tests
echo =======================================================

python "%~dp0run_tests.py" %*
set RUNNER_EXIT=%ERRORLEVEL%

if %RUNNER_EXIT% neq 0 (
    echo.
    echo [ERROR] Test suite finished with failures! Exit code: %RUNNER_EXIT%
    exit /b %RUNNER_EXIT%
)

echo.
echo [SUCCESS] All E2E tests completed successfully!
exit /b 0
