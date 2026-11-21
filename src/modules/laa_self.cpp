// Four gigabytes of address space without a separate program. The "large address aware" bit of the PE header is read by
// Windows when it starts DP.exe, so the running game cannot change its own limit; the mod prepares the next start instead:
//   1. a copy of DP.exe gets the bit (only the 2 bytes of the header change, as ufx_laa.exe does),
//   2. the running DP.exe is renamed (Windows allows renaming a running program) to DP.exe.bak, or to DP.exe.old when a
//      backup is already there (deleted at the next start),
//   3. the copy takes the name DP.exe.
// Any failure puts the original name back. After Steam restores the file (verify or update) it is done again at the next
// start. Done on its own thread some seconds after the start, never from DllMain. [Fixes] AutoLargeAddress=1.
#include "../base.h"

namespace ufx {
namespace {

volatile long g_laaResult = 0;  // 1 = done (next start has 4 GB), -1 = failed; read by LargeAddressFrame on the main thread

bool ReadWhole(const wchar_t* path, u8** data, DWORD* size) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD high = 0, n = GetFileSize(f, &high), got = 0;
    u8* buf = (!high && n >= 0x400 && n < (64u << 20)) ? (u8*)VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    const bool ok = buf && ReadFile(f, buf, n, &got, nullptr) && got == n;
    CloseHandle(f);
    if (!ok && buf) VirtualFree(buf, 0, MEM_RELEASE);
    *data = ok ? buf : nullptr;
    *size = n;
    return ok;
}

bool WriteWhole(const wchar_t* path, const u8* data, DWORD size) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(f, data, size, &put, nullptr) && put == size && FlushFileBuffers(f);
    CloseHandle(f);
    return ok;
}

DWORD WINAPI LaaWorker(void*) {
    Sleep(15000);  // the game is up and running; the file work never competes with the start
    wchar_t exe[MAX_PATH + 16], tmp[MAX_PATH + 16], bak[MAX_PATH + 16], old[MAX_PATH + 16];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return 0;
    wsprintfW(tmp, L"%s.ufx_laa", exe);
    wsprintfW(bak, L"%s.bak", exe);
    wsprintfW(old, L"%s.old", exe);
    u8* data = nullptr;
    DWORD size = 0;
    if (!ReadWhole(exe, &data, &size)) {
        Log("laa: DP.exe cannot be read, 4 GB not prepared");
        return 0;
    }
    const u32 nt = *(const u32*)(data + 0x3C);
    bool ok = nt <= size - 0x18 && *(const u32*)(data + nt) == 0x00004550 && *(const u16*)(data + nt + 4) == 0x014C;
    if (ok && (*(u16*)(data + nt + 0x16) & 0x20)) {  // the file has it already (a restart is all that is missing)
        VirtualFree(data, 0, MEM_RELEASE);
        Log("laa: DP.exe on disk is already large address aware; 4 GB from the next start");
        return 0;
    }
    if (ok) {
        *(u16*)(data + nt + 0x16) |= 0x20;
        ok = WriteWhole(tmp, data, size);
    }
    VirtualFree(data, 0, MEM_RELEASE);
    if (!ok) {
        DeleteFileW(tmp);
        Log("laa: the patched copy could not be written next to DP.exe (error %u), 4 GB not prepared", GetLastError());
        g_laaResult = -1;
        return 0;
    }
    const bool haveBak = GetFileAttributesW(bak) != INVALID_FILE_ATTRIBUTES;
    const wchar_t* aside = haveBak ? old : bak;
    if (!MoveFileExW(exe, aside, MOVEFILE_REPLACE_EXISTING)) {
        Log("laa: the running DP.exe could not be renamed (error %u), 4 GB not prepared", GetLastError());
        DeleteFileW(tmp);
        g_laaResult = -1;
        return 0;
    }
    if (!MoveFileExW(tmp, exe, 0)) {
        const DWORD e = GetLastError();
        MoveFileExW(aside, exe, 0);  // put the original back
        DeleteFileW(tmp);
        Log("laa: the patched copy could not take the name DP.exe (error %u), the original is back", e);
        g_laaResult = -1;
        return 0;
    }
    Log("laa: DP.exe now has the 4 GB flag (the original is %S); it takes effect at the next start", aside);
    g_laaResult = 1;
    return 0;
}

}  // namespace

void InstallAutoLargeAddress() {
    wchar_t exe[MAX_PATH + 16], old[MAX_PATH + 16];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {  // the renamed original of an earlier run, no longer running
        wsprintfW(old, L"%s.old", exe);
        DeleteFileW(old);
    }
    if (!g_cfg.autoLargeAddress || HasLargeAddressAware(GetModuleHandleW(nullptr))) return;
    HANDLE t = CreateThread(nullptr, 0, LaaWorker, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}

// Main thread, once per frame: the one notice about the result.
void LargeAddressFrame() {
    const long r = g_laaResult;
    if (!r) return;
    g_laaResult = 0;
    if (r > 0)
        ShowNotice(L"4 ГБ пам'яті увімкнено", L"4 GB of memory switched on",
                   L"Гра отримає 4 ГБ адресного простору замість 2 з наступного запуску. Оригінал DP.exe збережено як DP.exe.bak.",
                   L"The game gets 4 GB of address space instead of 2 from the next start. The original DP.exe is kept as DP.exe.bak.", 12);
    else
        ShowNotice(L"4 ГБ не ввімкнено", L"4 GB not switched on",
                   L"Мод не зміг підготувати DP.exe (тека гри без права запису?). Можна запустити ufx_laa.exe з архіву. Подробиці в ufx.log.",
                   L"The mod could not prepare DP.exe (the game folder is not writable?). You can run ufx_laa.exe from the archive. Details in ufx.log.", 12, kNoticeError);
}

}  // namespace ufx
