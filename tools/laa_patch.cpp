// UltimateFIXDP LAA patcher: lets 32-bit DP.exe use 4 GB of address space on 64-bit Windows (the "large address
// aware" bit of the PE header). Needed with big render scales, which can run the 2 GB limit out.
//
//   ufx_laa.exe [path\to\DP.exe]        sets the bit (default: DP.exe next to this program), keeps DP.exe.bak
//   ufx_laa.exe --undo [path\to\DP.exe] restores the original from DP.exe.bak
//
// Steam may restore the file when it verifies or updates the game; run the patcher again then.
// Freestanding: no C runtime, only kernel32 and user32.
#include <windows.h>
#include <stdint.h>

static void Say(const wchar_t* text, UINT icon) { MessageBoxW(nullptr, text, L"UltimateFIXDP LAA", MB_OK | icon); }

static bool ReadAll(const wchar_t* path, uint8_t** data, DWORD* size) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD n = GetFileSize(f, nullptr);
    uint8_t* buf = (uint8_t*)VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD got = 0;
    bool ok = buf && ReadFile(f, buf, n, &got, nullptr) && got == n;
    CloseHandle(f);
    *data = buf;
    *size = n;
    return ok;
}

// Writes only the bytes that change, in place: a failed write cannot leave a truncated DP.exe behind.
static bool WriteAt(const wchar_t* path, DWORD offset, const void* data, DWORD size) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = SetFilePointer(f, (LONG)offset, nullptr, FILE_BEGIN) == offset && WriteFile(f, data, size, &put, nullptr) && put == size;
    CloseHandle(f);
    return ok;
}

// Splits the command line into at most 3 arguments (quotes supported).
static int Args(wchar_t* cmd, wchar_t** out, int max) {
    int n = 0;
    wchar_t* p = cmd;
    while (*p && n < max) {
        while (*p == L' ') ++p;
        if (!*p) break;
        if (*p == L'"') {
            out[n++] = ++p;
            while (*p && *p != L'"') ++p;
        } else {
            out[n++] = p;
            while (*p && *p != L' ') ++p;
        }
        if (*p) *p++ = 0;
    }
    return n;
}

extern "C" void __cdecl Start() {
    wchar_t* argv[4];
    int argc = Args(GetCommandLineW(), argv, 4);  // argv[0] is this program
    bool undo = false;
    wchar_t exe[MAX_PATH + 8] = L"";
    for (int i = 1; i < argc; ++i) {
        if (lstrcmpW(argv[i], L"--undo") == 0)
            undo = true;
        else
            lstrcpynW(exe, argv[i], MAX_PATH);
    }
    if (!exe[0]) {
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wchar_t* slash = exe;
        for (wchar_t* p = exe; *p; ++p)
            if (*p == L'\\') slash = p + 1;
        lstrcpyW(slash, L"DP.exe");
    }
    wchar_t bak[MAX_PATH + 8];
    lstrcpyW(bak, exe);
    lstrcatW(bak, L".bak");

    if (undo) {
        if (!CopyFileW(bak, exe, FALSE)) {
            Say(L"Не вдалося відновити DP.exe з DP.exe.bak (немає копії?).", MB_ICONERROR);
            ExitProcess(1);
        }
        Say(L"DP.exe відновлено з DP.exe.bak.", MB_ICONINFORMATION);
        ExitProcess(0);
    }

    uint8_t* data;
    DWORD size;
    if (!ReadAll(exe, &data, &size)) {
        Say(L"Не вдалося прочитати DP.exe. Покладіть цю програму в теку гри або вкажіть шлях.", MB_ICONERROR);
        ExitProcess(1);
    }
    if (size < 0x200 || data[0] != 'M' || data[1] != 'Z') {
        Say(L"Це не виконуваний файл Windows.", MB_ICONERROR);
        ExitProcess(1);
    }
    uint32_t nt = *(uint32_t*)(data + 0x3C);
    // size is at least 0x200 here, so size - 0x18 cannot wrap (nt + 0x18 could, for a bogus e_lfanew)
    if (nt > size - 0x18 ||*(uint32_t*)(data + nt) != 0x00004550 || *(uint16_t*)(data + nt + 4) != 0x014C) {
        Say(L"Це не 32-бітний виконуваний файл Windows.", MB_ICONERROR);
        ExitProcess(1);
    }
    uint16_t* ch = (uint16_t*)(data + nt + 0x16);
    if (*ch & 0x20) {
        Say(L"DP.exe вже має прапор 4 ГБ. Нічого робити не треба.", MB_ICONINFORMATION);
        ExitProcess(0);
    }
    if (GetFileAttributesW(bak) == INVALID_FILE_ATTRIBUTES && !CopyFileW(exe, bak, TRUE)) {
        Say(L"Не вдалося створити резервну копію DP.exe.bak, нічого не змінено.", MB_ICONERROR);
        ExitProcess(1);
    }
    *ch |= 0x20;
    if (!WriteAt(exe, nt + 0x16, ch, 2)) {
        Say(L"Не вдалося записати DP.exe (закрийте гру й запустіть від імені адміністратора, якщо гра в Program Files). "
            L"Якщо після цього гра не запускається, поверніть копію: ufx_laa.exe --undo (DP.exe.bak).",
            MB_ICONERROR);
        ExitProcess(1);
    }
    Say(L"Готово: DP.exe тепер може використовувати 4 ГБ пам'яті. Резервна копія: DP.exe.bak. "
        L"Якщо Steam перевірить файли гри, запустіть цю програму ще раз.",
        MB_ICONINFORMATION);
    ExitProcess(0);
}
