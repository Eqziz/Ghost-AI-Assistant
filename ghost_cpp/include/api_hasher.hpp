#pragma once

#include <windows.h>
#include <cstdint>
#include <intrin.h>

namespace ApiHasher {

constexpr uint32_t fnv1a(const char* s) {
    uint32_t hash = 0x811c9dc5;
    while (*s) {
        hash ^= static_cast<uint8_t>(*s++);
        hash *= 0x01000193;
    }
    return hash;
}

constexpr uint32_t fnv1a_w(const wchar_t* s) {
    uint32_t hash = 0x811c9dc5;
    while (*s) {
        wchar_t c = *s++;
        if (c >= L'A' && c <= L'Z') c += 32;
        hash ^= static_cast<uint8_t>(c & 0xFF);
        hash *= 0x01000193;
        hash ^= static_cast<uint8_t>((c >> 8) & 0xFF);
        hash *= 0x01000193;
    }
    return hash;
}

constexpr uint32_t HASH_NTDLL    = fnv1a_w(L"ntdll.dll");
constexpr uint32_t HASH_KERNEL32 = fnv1a_w(L"kernel32.dll");
constexpr uint32_t HASH_ADVAPI32 = fnv1a_w(L"advapi32.dll");

constexpr uint32_t HASH_IsDebuggerPresent          = fnv1a("IsDebuggerPresent");
constexpr uint32_t HASH_CheckRemoteDebuggerPresent = fnv1a("CheckRemoteDebuggerPresent");
constexpr uint32_t HASH_NtSetInformationThread     = fnv1a("NtSetInformationThread");
constexpr uint32_t HASH_NtQueryInformationProcess  = fnv1a("NtQueryInformationProcess");
constexpr uint32_t HASH_NtTerminateProcess         = fnv1a("NtTerminateProcess");

// Safe and portable module base resolver
inline uintptr_t getModuleBase(uint32_t moduleHash) {
#if defined(_M_X64) || defined(__x86_64__)
    uint8_t* peb = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    if (peb) {
        // PEB.Ldr offset is 0x18 on 64-bit Windows
        uint8_t* ldr = *reinterpret_cast<uint8_t**>(peb + 0x18);
        if (ldr) {
            // InLoadOrderModuleList head is at offset 0x10 in PEB_LDR_DATA
            PLIST_ENTRY head = reinterpret_cast<PLIST_ENTRY>(ldr + 0x10);
            for (PLIST_ENTRY curr = head->Flink; curr && curr != head; curr = curr->Flink) {
                uint8_t* entry = reinterpret_cast<uint8_t*>(curr);
                // DllBase is at offset 0x30 on x64
                PVOID dllBase = *reinterpret_cast<PVOID*>(entry + 0x30);
                // BaseDllName UNICODE_STRING is at offset 0x58 on x64
                USHORT length = *reinterpret_cast<USHORT*>(entry + 0x58);
                PWSTR buffer = *reinterpret_cast<PWSTR*>(entry + 0x60);
                if (buffer && length > 0) {
                    uint32_t hash = 0x811c9dc5;
                    size_t numChars = length / sizeof(wchar_t);
                    for (size_t i = 0; i < numChars; ++i) {
                        wchar_t c = buffer[i];
                        if (c >= L'A' && c <= L'Z') c += 32;
                        hash ^= static_cast<uint8_t>(c & 0xFF);
                        hash *= 0x01000193;
                        hash ^= static_cast<uint8_t>((c >> 8) & 0xFF);
                        hash *= 0x01000193;
                    }
                    if (hash == moduleHash) {
                        return reinterpret_cast<uintptr_t>(dllBase);
                    }
                }
            }
        }
    }
#endif

    // Fallback: standard resolution without string literals in user code
    if (moduleHash == HASH_NTDLL) return reinterpret_cast<uintptr_t>(GetModuleHandleW(L"ntdll.dll"));
    if (moduleHash == HASH_KERNEL32) return reinterpret_cast<uintptr_t>(GetModuleHandleW(L"kernel32.dll"));
    if (moduleHash == HASH_ADVAPI32) return reinterpret_cast<uintptr_t>(GetModuleHandleW(L"advapi32.dll"));
    return 0;
}

inline void* getExportByHash(uintptr_t modBase, uint32_t funcHash) {
    if (!modBase) return nullptr;

    auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(modBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(modBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    auto exportDirRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    auto exportDirSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    if (!exportDirRVA || !exportDirSize) return nullptr;

    auto exports = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(modBase + exportDirRVA);
    auto names = reinterpret_cast<const DWORD*>(modBase + exports->AddressOfNames);
    auto ordinals = reinterpret_cast<const WORD*>(modBase + exports->AddressOfNameOrdinals);
    auto funcs = reinterpret_cast<const DWORD*>(modBase + exports->AddressOfFunctions);

    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        const char* funcName = reinterpret_cast<const char*>(modBase + names[i]);
        if (fnv1a(funcName) == funcHash) {
            WORD ordinal = ordinals[i];
            DWORD funcRVA = funcs[ordinal];
            if (funcRVA >= exportDirRVA && funcRVA < exportDirRVA + exportDirSize) {
                return nullptr;
            }
            return reinterpret_cast<void*>(modBase + funcRVA);
        }
    }
    return nullptr;
}

template <typename T>
inline T resolve(uint32_t modHash, uint32_t funcHash) {
    uintptr_t base = getModuleBase(modHash);
    if (!base) return nullptr;
    return reinterpret_cast<T>(getExportByHash(base, funcHash));
}

} // namespace ApiHasher
