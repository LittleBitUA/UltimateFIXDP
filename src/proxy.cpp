// Forwarding of the two functions DP.exe imports from X3DAudio1_7.dll to the real system DLL.
// The stubs keep the stack exactly as the caller left it, so any calling convention works.
#include "base.h"

extern "C" void* __cdecl UfxResolve(int index) {
    static void* cache[2];
    static HMODULE real;
    if (cache[index]) return cache[index];
    if (!real) {
        // Several mods can sit in a chain under the same DLL name: a copy named X3DAudio1_7.next.dll in the game
        // folder is loaded instead of the system DLL, and is expected to forward the calls in its turn.
        wchar_t path[MAX_PATH];
        DWORD m = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (m > 0 && m < MAX_PATH) {
            wchar_t* slash = nullptr;
            for (wchar_t* p = path; *p; ++p)
                if (*p == L'\\') slash = p;
            if (slash && (slash + 1 - path) + 24 < MAX_PATH) {
                lstrcpyW(slash + 1, L"X3DAudio1_7.next.dll");
                if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) real = LoadLibraryW(path);
            }
        }
    }
    if (!real) {
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for a 32-bit process
        if (n > 0 && n < MAX_PATH - 24) {
            lstrcatW(path, L"\\X3DAudio1_7.dll");
            real = LoadLibraryW(path);
        }
    }
    static const char* const names[2] = {"X3DAudioCalculate", "X3DAudioInitialize"};
    void* fn = real ? (void*)GetProcAddress(real, names[index]) : nullptr;
    if (!fn) {
        MessageBoxW(nullptr,
                    L"Не знайдено системний X3DAudio1_7.dll. Встановіть DirectX (червень 2010).\n"
                    L"The system X3DAudio1_7.dll was not found. Install the DirectX June 2010 runtime.",
                    L"UltimateFIXDP", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    cache[index] = fn;
    return fn;
}

extern "C" __attribute__((naked)) void ProxyCalculate(void) {
    __asm__ volatile(
        "push $0\n\t"
        "call _UfxResolve\n\t"
        "add $4, %esp\n\t"
        "jmp *%eax\n\t");
}

extern "C" __attribute__((naked)) void ProxyInitialize(void) {
    __asm__ volatile(
        "push $1\n\t"
        "call _UfxResolve\n\t"
        "add $4, %esp\n\t"
        "jmp *%eax\n\t");
}
