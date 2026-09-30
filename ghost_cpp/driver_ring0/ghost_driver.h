#pragma once

#ifndef _GHOST_DRIVER_H_
#define _GHOST_DRIVER_H_

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#define GHOST_DEVICE_NAME        L"\\Device\\GhostAI"
#define GHOST_DOS_DEVICE_NAME    L"\\DosDevices\\GhostAI"
#define GHOST_USER_DEVICE_NAME   "\\\\.\\GhostAI"

#define GHOST_DEVICE_TYPE 0x8000

// IOCTL для скрытия процесса из списка ActiveProcessLinks (DKOM)
#define IOCTL_GHOST_HIDE_PROCESS \
    CTL_CODE(GHOST_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

// IOCTL для защиты процесса от открытия дескрипторов (Strip Handles / ObRegisterCallbacks)
#define IOCTL_GHOST_PROTECT_PROCESS \
    CTL_CODE(GHOST_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// IOCTL для низкоуровневого перехвата клавиш напрямую в Ring 0
#define IOCTL_GHOST_POLL_KEY \
    CTL_CODE(GHOST_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Структура запроса к драйверу
typedef struct _GHOST_PROCESS_REQUEST {
    ULONG ProcessId;
    ULONG Flags;
} GHOST_PROCESS_REQUEST, *PGHOST_PROCESS_REQUEST;

typedef struct _GHOST_KEY_RESPONSE {
    ULONG KeyCode;
    BOOLEAN IsPressed;
} GHOST_KEY_RESPONSE, *PGHOST_KEY_RESPONSE;

#endif // _GHOST_DRIVER_H_
