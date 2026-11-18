// Replaces the stock launcher (DPLauncher.exe: PLAY / Setting / Exit) with the quick-start stub by itself, the same thing ufx_play.exe
// does by hand. The stub (a few KB, built from tools/ufx_play.cpp and embedded here) starts DP.exe at once; hold Shift to open the
// original launcher. It happens when the game starts: the first start after installing still shows the stock launcher, every later one
// does not. The original is kept as DPLauncher_original.exe. If Steam restores the original file (verify), the next game start puts
// the stub back. ReplaceLauncher=0 in the ini puts the original back.
//
// The stub starts DP.exe without arguments, and real fullscreen comes only from the argument the stock launcher passes when its
// Full Screen box is ticked. Our borderless window (Borderless=1) replaces it; with Borderless=0, or with DPfix next to the game
// (main.cpp then turns our borderless mode off), the stub would keep the game windowed for good, so the original launcher stays.
#include "../base.h"
#include "../launcher_stub.h"

namespace ufx {
namespace {

bool Exists(const wchar_t* p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

bool HasText(const unsigned char* b, DWORD n, const wchar_t* text) {
    DWORD len = (DWORD)lstrlenW(text) * 2;
    for (DWORD i = 0; i + len <= n; ++i) {
        DWORD k = 0;
        while (k < len && b[i + k] == (unsigned char)(k & 1 ? text[k / 2] >> 8 : text[k / 2] & 0xFF)) ++k;
        if (k == len) return true;
    }
    return false;
}

enum StubKind { kNotStub, kOtherStub, kThisStub };

// Every build of ufx_play.exe has other bytes (a newer release, another pack), so ours are recognised by the UTF-16 texts each of
// them carries, "UltimateFIXDP" (its message box title) and "DPLauncher_original.exe"; the stock launcher has neither.
// kThisStub: the very stub embedded in this DLL.
StubKind KindOf(const wchar_t* path) {
    static unsigned char buf[32768];
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return kNotStub;
    DWORD size = GetFileSize(h, nullptr), got = 0;
    bool ok = size <= sizeof(buf) && ReadFile(h, buf, size, &got, nullptr) && got == size;
    CloseHandle(h);
    if (!ok || !HasText(buf, size, L"UltimateFIXDP") || !HasText(buf, size, L"DPLauncher_original.exe")) return kNotStub;
    if (size != sizeof(kLauncherStub)) return kOtherStub;
    for (DWORD i = 0; i < size; ++i)
        if (buf[i] != kLauncherStub[i]) return kOtherStub;
    return kThisStub;
}

bool WriteStub(const wchar_t* path) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(h, kLauncherStub, sizeof(kLauncherStub), &wrote, nullptr) && wrote == sizeof(kLauncherStub);
    CloseHandle(h);
    if (!ok) DeleteFileW(path);
    return ok;
}

}  // namespace

void EnsureLauncherStub(const wchar_t* gameDir) {
    // the longest name added is "DPLauncher_original.exe" (23 characters) plus the final zero
    if (lstrlenW(gameDir) + 24 > MAX_PATH) {
        Log("launcher: the game folder path is too long, the launcher is left alone");
        return;
    }
    wchar_t launcher[MAX_PATH], original[MAX_PATH], old[MAX_PATH], dpfix[MAX_PATH];
    lstrcpyW(launcher, gameDir);
    lstrcatW(launcher, L"DPLauncher.exe");
    lstrcpyW(original, gameDir);
    lstrcatW(original, L"DPLauncher_original.exe");
    lstrcpyW(old, gameDir);
    lstrcatW(old, L"DPLauncher_old.exe");
    lstrcpyW(dpfix, gameDir);
    lstrcatW(dpfix, L"DPfix.ini");
    // A DPLauncher_old.exe is only a copy moved aside below while it was still running (the original stays as
    // DPLauncher_original.exe): try to remove it on every start. This fails harmlessly while that copy still runs.
    DeleteFileW(old);

    // An older build could rename a stub of another release to DPLauncher_original.exe: that is no original, and Shift would start it
    // in its install mode. Drop it; a Steam verify brings the real launcher back and the next start keeps it as the copy.
    if (Exists(original) && KindOf(original) != kNotStub) {
        if (!DeleteFileW(original)) {  // left in place it would pass for the original below, and the stock launcher could be lost
            Log("launcher: DPLauncher_original.exe is a quick-start launcher, not the original, and could not be removed (error %u); "
                "the launcher is left alone", (unsigned)GetLastError());
            return;
        }
        Log("launcher: DPLauncher_original.exe was a quick-start launcher, not the original: removed (verify the game files in Steam)");
    }
    if (!Exists(launcher)) {
        // Steam starts DPLauncher.exe: a missing one (an antivirus took the stub, a move failed) is put back from the copy
        if (Exists(original) && MoveFileW(original, launcher)) Log("launcher: DPLauncher.exe was missing, the original launcher is back");
        return;
    }
    StubKind kind = KindOf(launcher);

    const bool needFullscreen = !g_cfg.borderless || Exists(dpfix);
    if (!g_cfg.replaceLauncher || needFullscreen) {  // switched off, or the launcher's Full Screen box is needed: the original comes back
        const char* why = !g_cfg.replaceLauncher ? "ReplaceLauncher=0" : "Borderless=0 or DPfix: real fullscreen needs the launcher's Full Screen box";
        if (kind == kNotStub) {
            if (g_cfg.replaceLauncher) Log("launcher: the stock launcher stays (%s)", why);
        } else if (!Exists(original)) {
            Log("launcher: DPLauncher_original.exe is missing, the quick-start launcher stays (%s; verify the game files in Steam)", why);
        } else {
            // The stub may still be running (it has just started the game): a running program can be renamed but not replaced,
            // so it is moved aside first. A DPLauncher_old.exe left behind is deleted on a later start (above).
            DeleteFileW(old);
            if (!MoveFileW(launcher, old)) {
                Log("launcher: could not move the quick-start launcher aside (error %u), it stays", (unsigned)GetLastError());
            } else if (MoveFileW(original, launcher)) {
                DeleteFileW(old);
                Log("launcher: the original launcher is back (%s)", why);
            } else {
                Log("launcher: could not put the original launcher back (error %u), the quick-start launcher stays", (unsigned)GetLastError());
                MoveFileW(old, launcher);
            }
        }
        return;
    }
    if (kind == kThisStub) return;  // already in place (by this mod or by ufx_play.exe of this release)

    if (kind == kOtherStub) {
        // A stub of another release (an older pack, or ufx_play.exe): replace it with this one. It is never kept as the original.
        DeleteFileW(old);
        if (!MoveFileW(launcher, old)) {
            Log("launcher: could not move the older quick-start launcher aside (error %u), it stays", (unsigned)GetLastError());
            return;
        }
        if (!WriteStub(launcher)) {
            Log("launcher: could not write the quick-start launcher (error %u), the older one stays", (unsigned)GetLastError());
            MoveFileW(old, launcher);
            return;
        }
        DeleteFileW(old);
        Log("launcher: the quick-start launcher in place of DPLauncher.exe was updated to this version");
        return;
    }

    // The file in place is the stock launcher: keep it as DPLauncher_original.exe (a running program can be renamed), or when the copy
    // already exists (Steam restored the file) move it aside and try to delete it.
    if (!Exists(original)) {
        if (!MoveFileW(launcher, original)) {
            Log("launcher: could not rename DPLauncher.exe (error %u), the stock launcher stays", (unsigned)GetLastError());
            return;
        }
    } else {
        DeleteFileW(old);
        if (!MoveFileW(launcher, old)) {
            Log("launcher: could not move DPLauncher.exe aside (error %u), the stock launcher stays", (unsigned)GetLastError());
            return;
        }
        DeleteFileW(old);
    }
    if (!WriteStub(launcher)) {
        Log("launcher: could not write the quick-start launcher (error %u), putting the original back", (unsigned)GetLastError());
        MoveFileW(Exists(original) ? original : old, launcher);
        return;
    }
    Log("launcher: the quick-start launcher is in place of DPLauncher.exe (the original is DPLauncher_original.exe); the game starts at once next time");
}

}  // namespace ufx
