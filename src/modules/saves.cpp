// Protection of savedata\dp.sav.
//
// The game opens dp.sav with CREATE_ALWAYS, which empties it at once, then writes the whole 8 MB in
// one WriteFile. A crash or power loss in between leaves an empty or truncated file, and the loader
// has no integrity check at all (a wrong size is "padded" and, if bigger, overflows the buffer).
//
// What we do, using only the import table of DP.exe (CreateFileA / WriteFile / CloseHandle):
//   * write: the game's handle really points at dp.sav.tmp; on CloseHandle the temporary file is
//     flushed, checked, the current dp.sav is backed up and the temporary file replaces it atomically
//     (a file that is incomplete, or, while RestoreDamaged is on, not of the one size the read side accepts, is kept aside instead);
//   * read: if dp.sav has the wrong size, the newest good backup is restored first;
//   * start-up: an interrupted dp.sav.tmp is moved into the backup folder.
#include "../base.h"
#include "../sites.h"

namespace ufx {
namespace {

HANDLE(WINAPI* r_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) = nullptr;
BOOL(WINAPI* r_WriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED) = nullptr;
BOOL(WINAPI* r_CloseHandle)(HANDLE) = nullptr;

SRWLOCK g_lock = SRWLOCK_INIT;
HANDLE volatile g_h = nullptr;  // the handle the game believes is dp.sav
wchar_t g_live[MAX_PATH];
wchar_t g_tmp[MAX_PATH];
DWORD g_total = 0;
bool g_bad = false;

// ---------------------------------------------------------------------------------------------

bool IsSavePath(const char* p) {
    if (!p) return false;
    char b[MAX_PATH];
    size_t n = 0;
    for (; p[n] && n < sizeof(b) - 1; ++n) {
        char c = p[n];
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c += 32;
        b[n] = c;
    }
    b[n] = 0;
    static const char tail[] = "savedata/dp.sav";
    const size_t tl = sizeof(tail) - 1;
    if (n < tl) return false;
    for (size_t i = 0; i < tl; ++i)
        if (b[n - tl + i] != tail[i]) return false;
    return n == tl || b[n - tl - 1] == '/';
}

bool ResolveLive(const char* name, wchar_t* live) {
    wchar_t w[MAX_PATH];
    if (MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH) <= 0) return false;
    DWORD r = GetFullPathNameW(w, MAX_PATH, live, nullptr);
    return r > 0 && r < MAX_PATH;
}

void Stamp(wchar_t* out) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wsprintfW(out, L"%04d%02d%02d_%02d%02d%02d_%03d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
              t.wSecond, t.wMilliseconds);
}

void BackupName(const wchar_t* prefix, wchar_t* out) {
    wchar_t stamp[32];
    Stamp(stamp);
    wsprintfW(out, L"%s\\%s_%s.sav", g_paths.saveBackupDir, prefix, stamp);
}

bool FileSizeOf(const wchar_t* path, DWORD* size, bool* tooBig) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return false;
    *tooBig = d.nFileSizeHigh != 0;
    *size = d.nFileSizeLow;
    return true;
}

void Notify(const wchar_t* text) {
    if (!g_cfg.saveWarnings) return;
    MessageBoxW(nullptr, text, L"UltimateFIXDP", MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
}

// Moves a file we do not want to overwrite or delete into the backup folder under a tagged name.
void Preserve(const wchar_t* path, const wchar_t* tag) {
    wchar_t dest[MAX_PATH];
    BackupName(tag, dest);
    if (!MoveFileExW(path, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        Log("save: could not preserve %S as %S (error %d)", path, dest, (int)GetLastError());
    else
        Log("save: kept %S", dest);
}

struct BackupEntry {
    FILETIME time;
    wchar_t name[112];
};
BackupEntry g_entries[260];

int ListBackups(bool onlyGoodSize) {
    wchar_t pattern[MAX_PATH];
    wsprintfW(pattern, L"%s\\dp_*.sav", g_paths.saveBackupDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (onlyGoodSize && (fd.nFileSizeHigh != 0 || fd.nFileSizeLow != kSaveFileSize)) continue;
        if (n >= 260) break;
        int len = lstrlenW(fd.cFileName);
        if (len >= 112) continue;
        lstrcpyW(g_entries[n].name, fd.cFileName);
        g_entries[n].time = fd.ftLastWriteTime;
        ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

void Rotate(int keep) {
    int n = ListBackups(false);
    while (n > keep) {
        int oldest = 0;
        for (int i = 1; i < n; ++i)
            if (CompareFileTime(&g_entries[i].time, &g_entries[oldest].time) < 0) oldest = i;
        wchar_t path[MAX_PATH];
        wsprintfW(path, L"%s\\%s", g_paths.saveBackupDir, g_entries[oldest].name);
        DeleteFileW(path);
        g_entries[oldest] = g_entries[n - 1];
        --n;
    }
}

bool NewestGoodBackup(wchar_t* out) {
    int n = ListBackups(true);
    if (n == 0) return false;
    int newest = 0;
    for (int i = 1; i < n; ++i)
        if (CompareFileTime(&g_entries[i].time, &g_entries[newest].time) > 0) newest = i;
    wsprintfW(out, L"%s\\%s", g_paths.saveBackupDir, g_entries[newest].name);
    return true;
}

// Where the story stood in a save file: record 0 (the state the game was in when it wrote the file) gives the episode (+A0h),
// the chapter (+A1h) and the play time in seconds (+42800h), as in docs/engine/06. They go into the backup's name:
// "dp_<date>_епізод 8, розділ 25, 19 год 30 хв.sav". A file that cannot be read keeps the plain name.
void LabelBackup(const wchar_t* live, wchar_t* dest) {
    HANDLE f = CreateFileW(live, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    u8 story[2] = {};
    u32 seconds = 0;
    DWORD got1 = 0, got2 = 0;
    const bool ok = SetFilePointer(f, 0x120 + 0xA0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER && ReadFile(f, story, 2, &got1, nullptr) &&
                    SetFilePointer(f, 0x120 + 0x42800, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
                    ReadFile(f, &seconds, 4, &got2, nullptr) && got1 == 2 && got2 == 4;
    CloseHandle(f);
    if (!ok || story[0] > 20 || seconds > 3600u * 9999u) return;
    const int len = lstrlenW(dest);
    if (len < 4 || len > MAX_PATH - 64) return;
    dest[len - 4] = 0;  // drop ".sav"
    wchar_t tail[64];
    if (g_cfg.language == 1)
        wsprintfW(tail, L"_episode %d, chapter %d, %u h %02u min.sav", story[0], story[1], seconds / 3600, seconds / 60 % 60);
    else
        wsprintfW(tail, L"_епізод %d, розділ %d, %u год %02u хв.sav", story[0], story[1], seconds / 3600, seconds / 60 % 60);
    lstrcatW(dest, tail);
}

void BackupLive(const wchar_t* live) {
    DWORD size;
    bool big;
    if (!FileSizeOf(live, &size, &big)) return;  // first save ever
    if (big || size != kSaveFileSize) {
        Preserve(live, L"odd");  // keep what is there, the new file replaces it anyway
        return;
    }
    wchar_t dest[MAX_PATH];
    BackupName(L"dp", dest);
    LabelBackup(live, dest);
    if (CopyFileW(live, dest, FALSE))
        Log("save: backup %S", dest);
    else
        Log("save: backup failed (error %d)", (int)GetLastError());
    Rotate(g_cfg.saveBackups);
}

// ---------------------------------------------------------------------------------------------

HANDLE BeginWrite(const char* name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD flags,
                  HANDLE tmpl) {
    AcquireSRWLockExclusive(&g_lock);
    if (g_h) {
        Log("save: a second write open arrived while one is active, not redirected");
        ReleaseSRWLockExclusive(&g_lock);
        return INVALID_HANDLE_VALUE;
    }
    if (!ResolveLive(name, g_live)) {
        ReleaseSRWLockExclusive(&g_lock);
        return INVALID_HANDLE_VALUE;
    }
    lstrcpyW(g_tmp, g_live);
    lstrcatW(g_tmp, L".tmp");
    HANDLE h = CreateFileW(g_tmp, access, share | FILE_SHARE_READ, sa, CREATE_ALWAYS, flags, tmpl);
    if (h == INVALID_HANDLE_VALUE) {
        Log("save: cannot create %S (error %d), writing directly", g_tmp, (int)GetLastError());
        ReleaseSRWLockExclusive(&g_lock);
        return INVALID_HANDLE_VALUE;
    }
    g_total = 0;
    g_bad = false;
    g_h = h;
    ReleaseSRWLockExclusive(&g_lock);
    Log("save: write redirected to %S", g_tmp);
    return h;
}

BOOL EndWrite(HANDLE h) {
    AcquireSRWLockExclusive(&g_lock);
    g_h = nullptr;
    wchar_t live[MAX_PATH], tmp[MAX_PATH];
    lstrcpyW(live, g_live);
    lstrcpyW(tmp, g_tmp);
    DWORD total = g_total;
    bool bad = g_bad;
    ReleaseSRWLockExclusive(&g_lock);

    FlushFileBuffers(h);
    DWORD size = GetFileSize(h, nullptr);
    BOOL closed = CloseHandle(h);

    bool ok = !bad && size != INVALID_FILE_SIZE && size == total && size != 0;
    if (!ok) {
        Log("save: write was incomplete (size %d, written %d, failed %d), dp.sav left untouched",
            (int)size, (int)total, (int)bad);
        Preserve(tmp, L"rejected");
        return closed;
    }
    // One rule for both sides: with RestoreDamaged on, PrepareRead takes a dp.sav of another size for damaged and puts the newest
    // backup over it, so a file of another size committed here would be lost at the next load anyway. It is kept aside, and the
    // player is told. With RestoreDamaged off the read side keeps such a file, so it is committed as before.
    if (size != kSaveFileSize && !g_cfg.saveRestoreDamaged)
        Log("save: written size %d differs from the usual %d, committing anyway (RestoreDamaged=0)", (int)size, (int)kSaveFileSize);
    if (size != kSaveFileSize && g_cfg.saveRestoreDamaged) {
        Log("save: written size %d differs from the expected %d, dp.sav left untouched", (int)size, (int)kSaveFileSize);
        Preserve(tmp, L"rejected");
        Notify(L"Гра записала збереження незвичного розміру, тож файл збереження лишився без змін. Новий запис лежить у UltimateFix\\savebackups.\n"
               L"The game wrote a save of an unusual size, so the save file was left unchanged. The new write is in UltimateFix\\savebackups.");
        return closed;
    }
    BackupLive(live);
    if (MoveFileExW(tmp, live, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log("save: committed %S (%d bytes)", live, (int)size);
    } else {
        Log("save: replace failed (error %d), new data kept in the backup folder", (int)GetLastError());
        Preserve(tmp, L"uncommitted");
        Notify(L"Не вдалося замінити файл збереження. Нові дані лежать у UltimateFix\\savebackups.\n"
               L"Could not replace the save file. The new data is in UltimateFix\\savebackups.");
    }
    return closed;
}

void PrepareRead(const char* name) {
    wchar_t live[MAX_PATH];
    if (!ResolveLive(name, live)) return;
    DWORD size;
    bool big;
    if (!FileSizeOf(live, &size, &big)) return;  // no save yet
    if (!big && size == kSaveFileSize) return;
    Log("save: %S has size %d (expected %d)", live, (int)size, (int)kSaveFileSize);
    if (!g_cfg.saveRestoreDamaged) return;
    wchar_t backup[MAX_PATH];
    if (!NewestGoodBackup(backup)) {
        Log("save: no good backup to restore from");
        return;
    }
    Preserve(live, L"damaged");
    if (CopyFileW(backup, live, FALSE)) {
        Log("save: restored from %S", backup);
        Notify(L"Файл збереження був пошкоджений. Відновлено останню резервну копію.\n"
               L"The save file was damaged. The newest backup has been restored.");
    } else {
        Log("save: restore failed (error %d)", (int)GetLastError());
    }
}

// ---------------------------------------------------------------------------------------------

HANDLE WINAPI H_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                            DWORD flags, HANDLE tmpl) {
    if (IsSavePath(name)) {
        if ((access & GENERIC_WRITE) && disp == CREATE_ALWAYS) {
            HANDLE h = BeginWrite(name, access, share, sa, flags, tmpl);
            if (h != INVALID_HANDLE_VALUE) return h;
        } else if ((access & GENERIC_READ) && !(access & GENERIC_WRITE)) {
            PrepareRead(name);
        }
    }
    return r_CreateFileA(name, access, share, sa, disp, flags, tmpl);
}

BOOL WINAPI H_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED ov) {
    if (h == g_h && h) {
        BOOL ok = r_WriteFile(h, buf, n, written, ov);
        DWORD w = written ? *written : 0;
        if (!ok || ov || w != n)
            g_bad = true;
        else
            g_total += w;
        return ok;
    }
    return r_WriteFile(h, buf, n, written, ov);
}

BOOL WINAPI H_CloseHandle(HANDLE h) {
    if (h == g_h && h) return EndWrite(h);
    return r_CloseHandle(h);
}

}  // namespace

bool InstallSaveGuard() {
    // From the game's folder, not the current one: WinMain changes to the exe folder only later, and a shortcut or a launcher may
    // start the game in another folder.
    wchar_t tmp[MAX_PATH + 32];
    wsprintfW(tmp, L"%ssavedata\\dp.sav.tmp", g_paths.gameDir);
    if (GetFileAttributesW(tmp) != INVALID_FILE_ATTRIBUTES) {
        Log("save: found an interrupted write from a previous session");
        Preserve(tmp, L"interrupted");
    }
    HMODULE exe = GetModuleHandleW(nullptr);
    void** sCreate = FindImportSlot(exe, "KERNEL32.dll", "CreateFileA");
    void** sWrite = FindImportSlot(exe, "KERNEL32.dll", "WriteFile");
    void** sClose = FindImportSlot(exe, "KERNEL32.dll", "CloseHandle");
    if (!sCreate || !sWrite || !sClose) {
        Log("save: import slots not found, skipped");
        return false;
    }
    r_CreateFileA = (decltype(r_CreateFileA))*sCreate;
    r_WriteFile = (decltype(r_WriteFile))*sWrite;
    r_CloseHandle = (decltype(r_CloseHandle))*sClose;
    if (!SwapImport(sCreate, (void*)H_CreateFileA, nullptr) || !SwapImport(sWrite, (void*)H_WriteFile, nullptr) ||
        !SwapImport(sClose, (void*)H_CloseHandle, nullptr)) {
        // Put back whatever was replaced; the originals are still in r_*.
        SwapImport(sCreate, (void*)r_CreateFileA, nullptr);
        SwapImport(sWrite, (void*)r_WriteFile, nullptr);
        SwapImport(sClose, (void*)r_CloseHandle, nullptr);
        Log("save: could not patch the import table, skipped");
        return false;
    }
    Log("save: guard installed (keeping %d backups)", g_cfg.saveBackups);
    return true;
}

}  // namespace ufx
