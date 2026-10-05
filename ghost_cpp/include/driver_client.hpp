#pragma once

#include <windows.h>
#include <iostream>
#include "../driver_ring0/ghost_driver.h"

class DriverClient {
public:
    static DriverClient& instance() {
        static DriverClient inst;
        return inst;
    }

    bool connect() {
        if (m_hDevice != INVALID_HANDLE_VALUE) {
            return true;
        }
        m_hDevice = CreateFileA(
            GHOST_USER_DEVICE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL
        );
        if (m_hDevice == INVALID_HANDLE_VALUE) {
            m_lastError = GetLastError();
            return false;
        }
        m_lastError = 0;
        return true;
    }

    bool isConnected() const {
        return m_hDevice != INVALID_HANDLE_VALUE;
    }

    DWORD getLastError() const {
        return m_lastError;
    }

    /**
     * Защита процесса через ObRegisterCallbacks (SAFE — без DKOM, без PatchGuard).
     * Блокирует чтение памяти, завершение, подключение дебаггера.
     */
    bool protectProcess(DWORD pid = 0) {
        if (!ensureConnected()) return false;
        if (pid == 0) pid = GetCurrentProcessId();

        GHOST_PROCESS_REQUEST req = {};
        req.ProcessId = pid;
        req.Flags     = 1;

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            m_hDevice,
            IOCTL_GHOST_PROTECT_PROCESS,
            &req, sizeof(req),
            NULL, 0,
            &bytesReturned, NULL
        );
        if (!ok) m_lastError = GetLastError();
        return (ok == TRUE);
    }

    /**
     * Снятие защиты процесса.
     */
    bool unprotectProcess() {
        if (!ensureConnected()) return false;

        GHOST_PROCESS_REQUEST req = {};
        req.ProcessId = 0;
        req.Flags     = 0;

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            m_hDevice,
            IOCTL_GHOST_PROTECT_PROCESS,
            &req, sizeof(req),
            NULL, 0,
            &bytesReturned, NULL
        );
        if (!ok) m_lastError = GetLastError();
        return (ok == TRUE);
    }

    /**
     * Временное скрытие процесса из Task Manager через DKOM.
     * Процесс скрывается на durationMs миллисекунд, затем автоматически
     * возвращается в список (минимизация окна PatchGuard).
     *
     * durationMs: 500..10000 мс (по умолчанию 3000)
     */
    bool hideProcessTimed(DWORD pid = 0, DWORD durationMs = 3000) {
        if (!ensureConnected()) return false;
        if (pid == 0) pid = GetCurrentProcessId();

        GHOST_HIDE_REQUEST req = {};
        req.ProcessId  = pid;
        req.DurationMs = durationMs;

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            m_hDevice,
            IOCTL_GHOST_HIDE_PROCESS_TIMED,
            &req, sizeof(req),
            NULL, 0,
            &bytesReturned, NULL
        );
        if (!ok) m_lastError = GetLastError();
        return (ok == TRUE);
    }

    void disconnect() {
        if (m_hDevice != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hDevice);
            m_hDevice = INVALID_HANDLE_VALUE;
        }
    }

private:
    DriverClient() = default;
    ~DriverClient() { disconnect(); }

    DriverClient(const DriverClient&)            = delete;
    DriverClient& operator=(const DriverClient&) = delete;

    bool ensureConnected() {
        if (m_hDevice != INVALID_HANDLE_VALUE) return true;
        return connect();
    }

    HANDLE m_hDevice   = INVALID_HANDLE_VALUE;
    DWORD  m_lastError = 0;
};
