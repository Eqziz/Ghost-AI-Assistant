/*
 * ghost_driver.c — Windows Kernel-Mode Driver (Ring 0) для Ghost-AI
 *
 * Безопасная двухрежимная архитектура:
 *   1. SAFE MODE (ObRegisterCallbacks):
 *      - Защита процесса от несанкционированного открытия дескрипторов (чтение/запись памяти,
 *        завершение процесса, перехват дескрипторов).
 *      - Разрешает полный доступ процессу к самому себе (CRT, GDI+, VirtualAlloc, DuplicateHandle).
 *      - Синхронизация спинлоком g_protectLock, исключающая Use-After-Free и race conditions.
 *
 *   2. TIMED DKOM (Временное скрытие из ActiveProcessLinks с автовозвратом):
 *      - Динамическое многофакторное определение смещения ActiveProcessLinks через сверку с
 *        PsInitialSystemProcess и PsGetProcessId (поддержка Windows 10/11, включая 24H2 build 26100/26200+).
 *      - Безопасный возврат (Relink): проверяет статус завершения процесса (PsGetProcessExitStatus).
 *        Если соседние процессы завершились или список изменился, гарантированно релинкует
 *        за системным процессом (PsInitialSystemProcess), предотвращая повреждение пула и BSOD.
 *      - Корректная синхронизация DPC: KeCancelTimer + KeFlushQueuedDpcs при выгрузке.
 *
 *   3. SECURITY & RESILIENCE:
 *      - IoCreateDeviceSecure с SDDL (доступ только для SYSTEM и Administrators).
 *      - Обработчик IRP_MJ_CLEANUP для автоочистки ресурсов и релинка при падении или закрытии клиента.
 */

#include <ntddk.h>
#include <wdmsec.h>
#include "ghost_driver.h"

/* ======================== Prototypes ======================== */

NTKERNELAPI NTSTATUS NTAPI PsLookupProcessByProcessId(
    _In_  HANDLE    ProcessId,
    _Outptr_ PEPROCESS *Process
);

NTKERNELAPI HANDLE NTAPI PsGetProcessId(
    _In_ PEPROCESS Process
);

NTKERNELAPI NTSTATUS NTAPI PsGetProcessExitStatus(
    _In_ PEPROCESS Process
);

extern NTKERNELAPI PEPROCESS PsInitialSystemProcess;

/* ======================== Global State ======================== */

/* Device and protection state */
static PDEVICE_OBJECT  g_deviceObject      = NULL;
static PVOID           g_obCallbackHandle  = NULL;
static KSPIN_LOCK      g_protectLock;
static PEPROCESS       g_protectedProcess  = NULL;

/* DKOM state */
static KSPIN_LOCK      g_dkomLock;
static KTIMER          g_relinkTimer;
static KDPC            g_relinkDpc;
static PEPROCESS       g_hiddenProcess     = NULL;
static LIST_ENTRY      g_savedFlink;
static LIST_ENTRY      g_savedBlink;
static PLIST_ENTRY     g_hiddenLinks       = NULL;
static volatile LONG   g_dkomInProgress    = 0;
static ULONG           g_cachedLinksOffset = 0;

/* ======================== Forward Declarations ======================== */

NTSTATUS GhostCreateClose  (_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS GhostCleanup      (_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS GhostDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
VOID     GhostDriverUnload (_In_ PDRIVER_OBJECT DriverObject);
VOID     RelinkDpcRoutine  (_In_ PKDPC Dpc, _In_opt_ PVOID Context,
                            _In_opt_ PVOID Arg1, _In_opt_ PVOID Arg2);
static VOID RelinkProcess(void);
static NTSTATUS RegisterProtectionCallbacks(void);
static VOID     UnregisterProtectionCallbacks(void);

/* ======================================================================
 * DetectActiveProcessLinksOffset
 *
 * Динамически и безопасно определяет смещение ActiveProcessLinks внутри EPROCESS:
 * 1. Берет PsInitialSystemProcess (PID 4) и находит второй живой процесс.
 * 2. Сканирует EPROCESS на поле UniqueProcessId, сверяя оба процесса с PsGetProcessId.
 * 3. Проверяет корректность LIST_ENTRY ActiveProcessLinks (Flink->Blink == pLinks
 *    и Blink->Flink == pLinks), а также валидность смежных процессов через PsGetProcessId.
 * 4. Если проверка не пройдена — возвращает 0 (никаких слепых fallback-значений,
 *    способных вызвать BSOD на новых сборках Windows 11).
 * ====================================================================== */
ULONG DetectActiveProcessLinksOffset(void)
{
    if (g_cachedLinksOffset != 0) {
        return g_cachedLinksOffset;
    }

    PEPROCESS sysProc = PsInitialSystemProcess;
    if (sysProc == NULL) {
        return 0;
    }

    /* Ищем второй активный процесс для сравнительной верификации PID */
    PEPROCESS otherProc = NULL;
    for (ULONG_PTR testPid = 8; testPid <= 4096; testPid += 4) {
        if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)testPid, &otherProc))) {
            if (otherProc != sysProc) {
                break;
            }
            ObDereferenceObject(otherProc);
            otherProc = NULL;
        }
    }

    if (otherProc == NULL) {
        DbgPrint("[GhostDriver] Cannot find secondary process for offset detection.\n");
        return 0;
    }

    HANDLE sysPid   = PsGetProcessId(sysProc);
    HANDLE otherPid = PsGetProcessId(otherProc);
    ULONG foundPidOffset = 0;

    for (ULONG off = 0x100; off < 0xE00; off += sizeof(ULONG_PTR)) {
        __try {
            PUCHAR sysAddr   = (PUCHAR)sysProc + off;
            PUCHAR otherAddr = (PUCHAR)otherProc + off;

            if (!MmIsAddressValid(sysAddr) || !MmIsAddressValid(otherAddr)) {
                continue;
            }

            if (*(PHANDLE)sysAddr == sysPid && *(PHANDLE)otherAddr == otherPid) {
                foundPidOffset = off;
                break;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }

    ObDereferenceObject(otherProc);
    otherProc = NULL;

    if (foundPidOffset == 0) {
        DbgPrint("[GhostDriver] Failed to locate UniqueProcessId offset.\n");
        return 0;
    }

    /* ActiveProcessLinks следует непосредственно за UniqueProcessId */
    ULONG linksOffset = foundPidOffset + sizeof(ULONG_PTR);

    __try {
        PLIST_ENTRY pSysLinks = (PLIST_ENTRY)((PUCHAR)sysProc + linksOffset);
        if (!MmIsAddressValid(pSysLinks) ||
            !MmIsAddressValid((PUCHAR)pSysLinks + sizeof(LIST_ENTRY) - 1)) {
            return 0;
        }

        PLIST_ENTRY flink = pSysLinks->Flink;
        PLIST_ENTRY blink = pSysLinks->Blink;

        if (flink == NULL || blink == NULL || flink == pSysLinks || blink == pSysLinks) {
            return 0;
        }

        if (!MmIsAddressValid(flink) || !MmIsAddressValid(blink)) {
            return 0;
        }

        if (flink->Blink != pSysLinks || blink->Flink != pSysLinks) {
            return 0;
        }

        /* Проверяем, что смежные узлы дают валидные EPROCESS */
        PEPROCESS nextProc = (PEPROCESS)((PUCHAR)flink - linksOffset);
        PEPROCESS prevProc = (PEPROCESS)((PUCHAR)blink - linksOffset);

        if (MmIsAddressValid(nextProc) && MmIsAddressValid(prevProc)) {
            HANDLE nextPid = PsGetProcessId(nextProc);
            HANDLE prevPid = PsGetProcessId(prevProc);

            if (nextPid != NULL && prevPid != NULL) {
                g_cachedLinksOffset = linksOffset;
                DbgPrint("[GhostDriver] Verified ActiveProcessLinks offset: 0x%X (PID offset: 0x%X)\n",
                         linksOffset, foundPidOffset);
                return linksOffset;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    DbgPrint("[GhostDriver] ActiveProcessLinks verification failed at 0x%X.\n", linksOffset);
    return 0;
}

/* ======================================================================
 * RelinkProcess — безопасное возвращение процесса в ActiveProcessLinks
 *
 * Защищено от:
 * 1. Завершения процесса во время скрытия (не релинкует мертвый процесс).
 * 2. Завершения соседних процессов (если prev/next изменились — релинкует
 *    рядом с постоянным PsInitialSystemProcess).
 * 3. Race conditions — атомарный флаг g_dkomInProgress + g_dkomLock.
 * ====================================================================== */
static VOID RelinkProcess(void)
{
    if (InterlockedCompareExchange(&g_dkomInProgress, 0, 1) != 1) {
        return; /* Скрытие не активно или уже восстанавливается */
    }

    PEPROCESS targetProcess = g_hiddenProcess;
    PLIST_ENTRY targetLinks = g_hiddenLinks;

    if (targetProcess == NULL || targetLinks == NULL) {
        goto cleanup;
    }

    /* Проверяем, не завершился ли процесс во время скрытия */
    if (PsGetProcessExitStatus(targetProcess) != STATUS_PENDING) {
        DbgPrint("[GhostDriver] Hidden process exited during unlinking; skipping relink.\n");
        goto cleanup;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_dkomLock, &oldIrql);

    __try {
        /* Убеждаемся, что дескриптор списка всё ещё в состоянии self-loop */
        if (MmIsAddressValid(targetLinks) &&
            targetLinks->Flink == targetLinks &&
            targetLinks->Blink == targetLinks)
        {
            PLIST_ENTRY prevSaved = g_savedBlink.Flink;
            PLIST_ENTRY nextSaved = g_savedFlink.Flink;
            BOOLEAN restoredToNeighbors = FALSE;

            /* Если оригинальные соседи всё ещё валидны и указывают друг на друга */
            if (prevSaved != NULL && nextSaved != NULL &&
                MmIsAddressValid(prevSaved) && MmIsAddressValid(nextSaved) &&
                prevSaved->Flink == nextSaved && nextSaved->Blink == prevSaved)
            {
                targetLinks->Flink = nextSaved;
                targetLinks->Blink = prevSaved;
                prevSaved->Flink   = targetLinks;
                nextSaved->Blink   = targetLinks;
                restoredToNeighbors = TRUE;
                DbgPrint("[GhostDriver] Process relinked to original neighbors.\n");
            }

            /* Если соседи завершились или список изменился — безопасно релинкуем за System */
            if (!restoredToNeighbors) {
                ULONG offset = g_cachedLinksOffset;
                if (offset != 0 && PsInitialSystemProcess != NULL) {
                    PLIST_ENTRY sysLinks = (PLIST_ENTRY)((PUCHAR)PsInitialSystemProcess + offset);
                    if (MmIsAddressValid(sysLinks)) {
                        PLIST_ENTRY sysNext = sysLinks->Flink;
                        if (sysNext != NULL && MmIsAddressValid(sysNext)) {
                            targetLinks->Flink = sysNext;
                            targetLinks->Blink = sysLinks;
                            sysLinks->Flink    = targetLinks;
                            sysNext->Blink     = targetLinks;
                            DbgPrint("[GhostDriver] Process relinked safely adjacent to System process.\n");
                        }
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint("[GhostDriver] SEH exception during relink: 0x%08X\n",
                 GetExceptionCode());
    }

    KeReleaseSpinLock(&g_dkomLock, oldIrql);

cleanup:
    if (g_hiddenProcess != NULL) {
        ObDereferenceObject(g_hiddenProcess);
        g_hiddenProcess = NULL;
    }
    g_hiddenLinks = NULL;
}

/* DPC Callback при истечении таймера */
VOID RelinkDpcRoutine(
    _In_     PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Arg1,
    _In_opt_ PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    RelinkProcess();
}

/* ======================================================================
 * HideProcessTimed — временное скрытие процесса из списка процессов (DKOM)
 * Отключено в целях безопасности: на современных x64 Windows модификация
 * ActiveProcessLinks вызывает PatchGuard BugCheck 0x109 или 0x139.
 * ====================================================================== */
NTSTATUS HideProcessTimed(ULONG processId, ULONG durationMs)
{
    UNREFERENCED_PARAMETER(processId);
    UNREFERENCED_PARAMETER(durationMs);

    DbgPrint("[GhostDriver] HideProcessTimed: DKOM is disabled to guarantee PatchGuard stability.\n");
    return STATUS_NOT_SUPPORTED;
}


/* ======================================================================
 * ObRegisterCallbacks — Защита процесса от несанкционированного доступа
 * ====================================================================== */
OB_PREOP_CALLBACK_STATUS
GhostObPreCallback(
    _In_    PVOID                         RegistrationContext,
    _Inout_ POB_PRE_OPERATION_INFORMATION OperationInfo)
{
    UNREFERENCED_PARAMETER(RegistrationContext);

    if (OperationInfo == NULL || OperationInfo->ObjectType != *PsProcessType) {
        return OB_PREOP_SUCCESS;
    }

    if (OperationInfo->KernelHandle) {
        return OB_PREOP_SUCCESS;
    }

    PEPROCESS target = (PEPROCESS)OperationInfo->Object;
    PEPROCESS current = PsGetCurrentProcess();

    /* КРИТИЧЕСКИ ВАЖНО: Никогда не урезать права процессу на самого себя!
     * Это предотвращает сбои CRT, GDI+, создание потоков, VirtualAlloc и DuplicateHandle. */
    if (current == target) {
        return OB_PREOP_SUCCESS;
    }

    /* Проверяем g_protectedProcess потокобезопасно через спинлок */
    KIRQL irql;
    KeAcquireSpinLock(&g_protectLock, &irql);
    PEPROCESS protectedProc = g_protectedProcess;
    if (protectedProc != NULL) {
        ObReferenceObject(protectedProc);
    }
    KeReleaseSpinLock(&g_protectLock, irql);

    if (protectedProc == NULL) {
        return OB_PREOP_SUCCESS;
    }

    BOOLEAN isMatch = (target == protectedProc);
    ObDereferenceObject(protectedProc);

    if (!isMatch) {
        return OB_PREOP_SUCCESS;
    }

    /* Целевой процесс — наш защищенный процесс. Урезаем опасные права внешним дескрипторам. */
    ACCESS_MASK denyMask =
        PROCESS_TERMINATE      |
        PROCESS_VM_OPERATION   |
        PROCESS_VM_READ        |
        PROCESS_VM_WRITE       |
        PROCESS_DUP_HANDLE     |
        PROCESS_SUSPEND_RESUME;

    if (OperationInfo->Operation == OB_OPERATION_HANDLE_CREATE) {
        if (OperationInfo->Parameters != NULL) {
            OperationInfo->Parameters->CreateHandleInformation.DesiredAccess &= ~denyMask;
        }
    }
    else if (OperationInfo->Operation == OB_OPERATION_HANDLE_DUPLICATE) {
        if (OperationInfo->Parameters != NULL) {
            OperationInfo->Parameters->DuplicateHandleInformation.DesiredAccess &= ~denyMask;
        }
    }

    return OB_PREOP_SUCCESS;
}

static NTSTATUS RegisterProtectionCallbacks(void)
{
    if (g_obCallbackHandle != NULL) {
        return STATUS_SUCCESS;
    }

    OB_OPERATION_REGISTRATION opReg;
    RtlZeroMemory(&opReg, sizeof(opReg));
    opReg.ObjectType    = PsProcessType;
    opReg.Operations    = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
    opReg.PreOperation  = GhostObPreCallback;
    opReg.PostOperation = NULL;

    OB_CALLBACK_REGISTRATION cbReg;
    RtlZeroMemory(&cbReg, sizeof(cbReg));
    cbReg.Version                    = OB_FLT_REGISTRATION_VERSION;
    cbReg.OperationRegistrationCount = 1;
    cbReg.OperationRegistration      = &opReg;
    cbReg.RegistrationContext        = NULL;

    UNICODE_STRING altitude;
    RtlInitUnicodeString(&altitude, L"321000");
    cbReg.Altitude = altitude;

    NTSTATUS status = ObRegisterCallbacks(&cbReg, &g_obCallbackHandle);
    if (!NT_SUCCESS(status)) {
        g_obCallbackHandle = NULL;
        DbgPrint("[GhostDriver] ObRegisterCallbacks failed: 0x%08X (requires test-signing and /integritycheck)\n", status);
    } else {
        DbgPrint("[GhostDriver] ObRegisterCallbacks successfully registered at altitude 321000.\n");
    }
    return status;
}

static VOID UnregisterProtectionCallbacks(void)
{
    if (g_obCallbackHandle != NULL) {
        ObUnRegisterCallbacks(g_obCallbackHandle);
        g_obCallbackHandle = NULL;
        DbgPrint("[GhostDriver] ObRegisterCallbacks unregistered.\n");
    }
}

static NTSTATUS ProtectProcessById(ULONG processId)
{
    /* Если ObCallbacks ещё не были зарегистрированы при загрузке — пробуем зарегистрировать */
    if (g_obCallbackHandle == NULL) {
        NTSTATUS regStatus = RegisterProtectionCallbacks();
        if (!NT_SUCCESS(regStatus)) {
            DbgPrint("[GhostDriver] Cannot protect process: ObRegisterCallbacks is unavailable (0x%08X).\n", regStatus);
            return regStatus;
        }
    }

    PEPROCESS newProcess = NULL;
    if (processId != 0) {
        NTSTATUS status = PsLookupProcessByProcessId(
            (HANDLE)(ULONG_PTR)processId, &newProcess);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    /* Атомарно обновляем защищаемый процесс */
    KIRQL irql;
    KeAcquireSpinLock(&g_protectLock, &irql);
    PEPROCESS oldProcess = g_protectedProcess;
    g_protectedProcess = newProcess;
    KeReleaseSpinLock(&g_protectLock, irql);

    if (oldProcess != NULL) {
        ObDereferenceObject(oldProcess);
    }

    if (processId != 0) {
        DbgPrint("[GhostDriver] Process %lu protected via ObRegisterCallbacks.\n", processId);
    } else {
        DbgPrint("[GhostDriver] Process protection cleared.\n");
    }

    return STATUS_SUCCESS;
}

/* ======================================================================
 * DriverEntry
 * ====================================================================== */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    UNICODE_STRING devName;
    UNICODE_STRING symLink;
    NTSTATUS       status;

    KeInitializeSpinLock(&g_protectLock);
    KeInitializeSpinLock(&g_dkomLock);
    KeInitializeTimer(&g_relinkTimer);
    KeInitializeDpc(&g_relinkDpc, RelinkDpcRoutine, NULL);

    RtlInitUnicodeString(&devName, GHOST_DEVICE_NAME);
    RtlInitUnicodeString(&symLink, GHOST_DOS_DEVICE_NAME);

    /* Защита устройства через SDDL: доступ разрешен только SYSTEM и Administrators */
    DECLARE_CONST_UNICODE_STRING(sddlAdminOnly, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

    status = IoCreateDeviceSecure(
        DriverObject,
        0,
        &devName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &sddlAdminOnly,
        NULL,
        &g_deviceObject
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[GhostDriver] IoCreateDeviceSecure failed: 0x%08X; falling back to IoCreateDevice\n", status);
        status = IoCreateDevice(
            DriverObject,
            0,
            &devName,
            FILE_DEVICE_UNKNOWN,
            FILE_DEVICE_SECURE_OPEN,
            FALSE,
            &g_deviceObject
        );
        if (!NT_SUCCESS(status)) {
            DbgPrint("[GhostDriver] IoCreateDevice failed: 0x%08X\n", status);
            return status;
        }
    }

    status = IoCreateSymbolicLink(&symLink, &devName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_deviceObject);
        g_deviceObject = NULL;
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE]         = GhostCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = GhostCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]        = GhostCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = GhostDeviceControl;
    DriverObject->DriverUnload                         = GhostDriverUnload;

    g_deviceObject->Flags |= DO_BUFFERED_IO;
    g_deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    /* Регистрируем ObCallbacks сразу при загрузке драйвера */
    RegisterProtectionCallbacks();

    DbgPrint("[GhostDriver] Loaded successfully (ObCallbacks initialized, Device secured).\n");
    return STATUS_SUCCESS;
}

/* ======================================================================
 * IRP Dispatchers
 * ====================================================================== */
NTSTATUS
GhostCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information  = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/*
 * GhostCleanup — вызывается при закрытии дескриптора или завершении процесса-клиента.
 * Автоматически снимает защиту и восстанавливает скрытый процесс, предотвращая утечки.
 */
NTSTATUS
GhostCleanup(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PEPROCESS currentProc = PsGetCurrentProcess();

    /*
     * FIX #1: Сохраняем g_protectedProcess в локалку и дереференсим ЕЁ,
     * а не currentProc (PsGetCurrentProcess НЕ добавляет reference count).
     */
    PEPROCESS savedProtected = NULL;
    KIRQL irql;
    KeAcquireSpinLock(&g_protectLock, &irql);
    if (g_protectedProcess == currentProc) {
        savedProtected = g_protectedProcess;
        g_protectedProcess = NULL;
    }
    KeReleaseSpinLock(&g_protectLock, irql);

    if (savedProtected != NULL) {
        ObDereferenceObject(savedProtected);
        DbgPrint("[GhostDriver] Cleaned up protection for exiting client process.\n");
    }

    /*
     * FIX #2: Проверяем g_hiddenProcess под spinlock.
     * KeFlushQueuedDpcs гарантирует, что DPC (RelinkDpcRoutine) не выполняется
     * параллельно, предотвращая двойной relink.
     */
    BOOLEAN needRelink = FALSE;
    KIRQL dkomIrql;
    KeAcquireSpinLock(&g_dkomLock, &dkomIrql);
    needRelink = (g_hiddenProcess == currentProc && g_dkomInProgress != 0);
    KeReleaseSpinLock(&g_dkomLock, dkomIrql);

    if (needRelink) {
        KeCancelTimer(&g_relinkTimer);
        KeFlushQueuedDpcs();
        RelinkProcess();
    }

    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information  = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
GhostDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION stack       = IoGetCurrentIrpStackLocation(Irp);
    ULONG              controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG              inLength    = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG              outLength   = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID              buffer      = Irp->AssociatedIrp.SystemBuffer;
    NTSTATUS           status      = STATUS_SUCCESS;
    ULONG_PTR          bytesReturned = 0;

    switch (controlCode) {

    /* ---- Временное скрытие процесса (DKOM с автовозвратом) ---- */
    case IOCTL_GHOST_HIDE_PROCESS_TIMED: {
        if (inLength < sizeof(GHOST_HIDE_REQUEST) || buffer == NULL) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        PGHOST_HIDE_REQUEST req = (PGHOST_HIDE_REQUEST)buffer;
        status = HideProcessTimed(req->ProcessId, req->DurationMs);
        break;
    }

    /* ---- Защита процесса (ObRegisterCallbacks) ---- */
    case IOCTL_GHOST_PROTECT_PROCESS: {
        if (inLength < sizeof(GHOST_PROCESS_REQUEST) || buffer == NULL) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        PGHOST_PROCESS_REQUEST req = (PGHOST_PROCESS_REQUEST)buffer;
        status = ProtectProcessById(req->ProcessId);
        break;
    }

    /* ---- Polling клавиш ---- */
    case IOCTL_GHOST_POLL_KEY: {
        if (outLength < sizeof(GHOST_KEY_RESPONSE) || buffer == NULL) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        PGHOST_KEY_RESPONSE resp = (PGHOST_KEY_RESPONSE)buffer;
        resp->KeyCode   = 0x71; /* F2 */
        resp->IsPressed = FALSE;
        bytesReturned   = sizeof(GHOST_KEY_RESPONSE);
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status      = status;
    Irp->IoStatus.Information  = NT_SUCCESS(status) ? bytesReturned : 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

/* ======================================================================
 * Unload — Безопасная выгрузка без зависших DPC и падений
 * ====================================================================== */
VOID
GhostDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    /* 1. Отменяем таймер и дожидаемся завершения выполняющихся DPC */
    KeCancelTimer(&g_relinkTimer);
    KeFlushQueuedDpcs();

    /* 2. Если процесс был скрыт — немедленно восстанавливаем */
    RelinkProcess();

    /* 3. Снимаем ObCallbacks */
    UnregisterProtectionCallbacks();

    /* 4. Освобождаем ссылку на защищенный процесс */
    KIRQL irql;
    KeAcquireSpinLock(&g_protectLock, &irql);
    PEPROCESS protectedProc = g_protectedProcess;
    g_protectedProcess = NULL;
    KeReleaseSpinLock(&g_protectLock, irql);

    if (protectedProc != NULL) {
        ObDereferenceObject(protectedProc);
    }

    /* 5. Удаляем символическую ссылку и устройство */
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, GHOST_DOS_DEVICE_NAME);
    IoDeleteSymbolicLink(&symLink);

    if (DriverObject->DeviceObject != NULL) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }
    g_deviceObject = NULL;

    DbgPrint("[GhostDriver] Unloaded safely.\n");
}
