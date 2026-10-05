
#pragma once

#ifndef _GHOST_DRIVER_H_
#define _GHOST_DRIVER_H_

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

/* ===== Device Names ===== */
#define GHOST_DEVICE_NAME        L"\\Device\\GhostAI"
#define GHOST_DOS_DEVICE_NAME    L"\\DosDevices\\GhostAI"
#define GHOST_USER_DEVICE_NAME   "\\\\.\\GhostAI"

#define GHOST_DEVICE_TYPE 0x8000

/* ===== IOCTL Codes ===== */

/* Временное скрытие процесса (DKOM): скрыть на DurationMs миллисекунд, потом автовозврат */
#define IOCTL_GHOST_HIDE_PROCESS_TIMED \
    CTL_CODE(GHOST_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Защита процесса от открытия дескрипторов (ObRegisterCallbacks) — безопасно, без DKOM */
#define IOCTL_GHOST_PROTECT_PROCESS \
    CTL_CODE(GHOST_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Низкоуровневый перехват клавиш в Ring 0 */
#define IOCTL_GHOST_POLL_KEY \
    CTL_CODE(GHOST_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* ===== Request / Response Structures ===== */

typedef struct _GHOST_HIDE_REQUEST {
    ULONG ProcessId;
    ULONG DurationMs;      /* Время скрытия в миллисекундах (0 = по умолчанию 3000) */
} GHOST_HIDE_REQUEST, *PGHOST_HIDE_REQUEST;

typedef struct _GHOST_PROCESS_REQUEST {
    ULONG ProcessId;
    ULONG Flags;
} GHOST_PROCESS_REQUEST, *PGHOST_PROCESS_REQUEST;

typedef struct _GHOST_KEY_RESPONSE {
    ULONG   KeyCode;
    BOOLEAN IsPressed;
} GHOST_KEY_RESPONSE, *PGHOST_KEY_RESPONSE;

/* ===== Kernel-mode process access rights (for ObRegisterCallbacks) ===== */
#ifdef _KERNEL_MODE

#ifndef PROCESS_TERMINATE
#define PROCESS_TERMINATE           0x0001
#endif
#ifndef PROCESS_VM_OPERATION
#define PROCESS_VM_OPERATION        0x0008
#endif
#ifndef PROCESS_VM_READ
#define PROCESS_VM_READ             0x0010
#endif
#ifndef PROCESS_VM_WRITE
#define PROCESS_VM_WRITE            0x0020
#endif
#ifndef PROCESS_DUP_HANDLE
#define PROCESS_DUP_HANDLE          0x0040
#endif
#ifndef PROCESS_SUSPEND_RESUME
#define PROCESS_SUSPEND_RESUME      0x0800
#endif

#endif /* _KERNEL_MODE */

#endif /* _GHOST_DRIVER_H_ */
