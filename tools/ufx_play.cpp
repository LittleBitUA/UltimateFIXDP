// UltimateFIXDP quick start: replaces the stock launcher window (PLAY / Setting / Exit) with a program that starts
// DP.exe at once.
//
//   ufx_play.exe           in the game folder: puts itself in place of DPLauncher.exe (the original is kept as
//                          DPLauncher_original.exe). Steam then starts it instead of the launcher. When UltimateFix.ini
//                          is there, it also sets [General] ReplaceLauncher=1, so the mod keeps the stub.
//   ufx_play.exe --undo    puts the original launcher back and sets ReplaceLauncher=0 in UltimateFix.ini; without that
//                          the mod (ReplaceLauncher=1 by default) would put the stub back at the next game start.
//
// Started under the name DPLauncher.exe it launches DP.exe from its own folder; hold Shift to open the original
// launcher (language, key bindings). Steam can restore the original file when it verifies the game: run
// ufx_play.exe again then. Freestanding: no C runtime, only kernel32 and user32.
//
// DP.exe is started without arguments, so the game runs in a window: real fullscreen comes only from the argument
// the stock launcher passes when its Full Screen box is ticked. That is what the mod's borderless window (Borderless=1)
// replaces anyway. With Borderless=0 in UltimateFix.ini, or with DPfix, the launcher is needed for fullscreen, so the stub is not
// installed. Without the mod (no X3DAudio1_7.dll next to DP.exe) there is no borderless window either: the stub is installed, but
// the game then stays a 1280x720 window, and the final message says so (Shift still opens the stock launcher).
//
// The mod (launcher_install.cpp) recognises any build of this program by the UTF-16 texts "UltimateFIXDP" (the
// message box title) and "DPLauncher_original.exe": keep both in this file.
#include <windows.h>

static void Say(const wchar_t* text, UINT icon) { MessageBoxW(nullptr, text, L"UltimateFIXDP", MB_OK | icon); }

static bool SameName(const wchar_t* path, const wchar_t* name) {
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/') base = p + 1;
    return lstrcmpiW(base, name) == 0;
}

static void Zero(void* p, unsigned n) {
    volatile unsigned char* b = (volatile unsigned char*)p;
    while (n--) *b++ = 0;
}

static bool Exists(const wchar_t* path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

static bool HasText(const unsigned char* b, DWORD n, const wchar_t* text) {
    DWORD len = (DWORD)lstrlenW(text) * 2;
    for (DWORD i = 0; i + len <= n; ++i) {
        DWORD k = 0;
        while (k < len && b[i + k] == (unsigned char)(k & 1 ? text[k / 2] >> 8 : text[k / 2] & 0xFF)) ++k;
        if (k == len) return true;
    }
    return false;
}

// True when the file is a build of this program (any release: the bytes differ from one build to the next).
static bool IsStub(const wchar_t* path) {
    static unsigned char buf[32768];
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr), got = 0;
    bool ok = size <= sizeof(buf) && ReadFile(h, buf, size, &got, nullptr) && got == size;
    CloseHandle(h);
    return ok && HasText(buf, size, L"UltimateFIXDP") && HasText(buf, size, L"DPLauncher_original.exe");
}

// Command line after the program name (what Steam may pass on).
static const wchar_t* Tail() {
    const wchar_t* c = GetCommandLineW();
    if (*c == L'"') {
        ++c;
        while (*c && *c != L'"') ++c;
        if (*c) ++c;
    } else {
        while (*c && *c != L' ') ++c;
    }
    while (*c == L' ') ++c;
    return c;
}

// dir is at most MAX_PATH - 24 characters (checked in Begin), so every exeName of this file fits.
static bool Start(const wchar_t* dir, const wchar_t* exeName, const wchar_t* extra) {
    wchar_t exe[MAX_PATH], cmd[2 * MAX_PATH + 8];
    lstrcpyW(exe, dir);
    lstrcatW(exe, exeName);
    if (lstrlenW(exe) + 3 + lstrlenW(extra) >= (int)(sizeof(cmd) / sizeof(cmd[0]))) return false;  // too long a tail
    lstrcpyW(cmd, L"\"");
    lstrcatW(cmd, exe);
    lstrcatW(cmd, L"\" ");
    lstrcatW(cmd, extra);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    Zero(&si, sizeof(si));
    si.cb = sizeof(si);
    Zero(&pi, sizeof(pi));
    if (!CreateProcessW(exe, cmd, nullptr, nullptr, FALSE, 0, nullptr, dir, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

extern "C" void __cdecl Begin() {
    wchar_t self[MAX_PATH], dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) ExitProcess(1);
    lstrcpyW(dir, self);
    wchar_t* slash = nullptr;
    for (wchar_t* p = dir; *p; ++p)
        if (*p == L'\\') slash = p;
    if (!slash) ExitProcess(1);
    slash[1] = 0;
    // the longest name added to dir is "DPLauncher_original.exe" (23 characters) plus the final zero
    if (lstrlenW(dir) + 24 > MAX_PATH) {
        Say(L"Шлях до теки гри задовгий. Перенесіть гру в коротший шлях.", MB_ICONERROR);
        ExitProcess(1);
    }

    wchar_t launcher[MAX_PATH], original[MAX_PATH], ini[MAX_PATH], dpfix[MAX_PATH];
    lstrcpyW(launcher, dir);
    lstrcatW(launcher, L"DPLauncher.exe");
    lstrcpyW(original, dir);
    lstrcatW(original, L"DPLauncher_original.exe");
    lstrcpyW(ini, dir);
    lstrcatW(ini, L"UltimateFix.ini");
    lstrcpyW(dpfix, dir);
    lstrcatW(dpfix, L"DPfix.ini");

    if (SameName(self, L"DPLauncher.exe")) {
        // run mode: Shift opens the original launcher, anything else starts the game
        if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) && Exists(original)) {
            if (!Start(dir, L"DPLauncher_original.exe", Tail())) Say(L"Не вдалося запустити DPLauncher_original.exe.", MB_ICONERROR);
            ExitProcess(0);
        }
        if (!Start(dir, L"DP.exe", L"")) Say(L"Не вдалося запустити DP.exe з теки лаунчера.", MB_ICONERROR);
        ExitProcess(0);
    }

    bool undo = false;
    for (const wchar_t* t = Tail(); *t; ++t)
        if (t[0] == L'-' && t[1] == L'-' && (t[2] | 0x20) == L'u') undo = true;

    // An older build of the mod could leave a stub under the original's name; it is not the stock launcher.
    bool originalIsStub = Exists(original) && IsStub(original);

    if (undo) {
        bool haveIni = Exists(ini);
        if (haveIni) WritePrivateProfileStringW(L"General", L"ReplaceLauncher", L"0", ini);
        const wchar_t* iniNote = haveIni ? L"\n\nУ UltimateFix.ini записано ReplaceLauncher=0, щоб мод не ставив швидкий запуск знову."
                                         : L"\n\nЯкщо користуєтеся модом UltimateFIXDP, поставте ReplaceLauncher=0 в UltimateFix.ini, інакше мод знову поставить швидкий запуск.";
        wchar_t msg[512];
        UINT icon = MB_ICONINFORMATION;
        if (originalIsStub) {
            lstrcpyW(msg, L"DPLauncher_original.exe теж є швидким запуском, а не оригіналом. Перевірте цілісність файлів гри у Steam: він поверне справжній DPLauncher.exe.");
            icon = MB_ICONERROR;
        } else if (!Exists(original)) {
            lstrcpyW(msg, L"DPLauncher_original.exe не знайдено: повертати нічого.");
        } else if (MoveFileExW(original, launcher, MOVEFILE_REPLACE_EXISTING)) {
            lstrcpyW(msg, L"Готово: оригінальний лаунчер повернуто.");
        } else {
            lstrcpyW(msg, L"Не вдалося повернути лаунчер (закрийте гру й лаунчер і спробуйте ще раз). Якщо DPLauncher.exe зник, перевірте цілісність файлів гри у Steam.");
            icon = MB_ICONERROR;
        }
        lstrcatW(msg, iniNote);
        Say(msg, icon);
        ExitProcess(0);
    }

    wchar_t dpExe[MAX_PATH];
    lstrcpyW(dpExe, dir);
    lstrcatW(dpExe, L"DP.exe");
    if (!Exists(dpExe)) {
        Say(L"DP.exe поруч не знайдено. Покладіть ufx_play.exe в теку гри (де DP.exe) і запустіть знову.", MB_ICONERROR);
        ExitProcess(1);
    }
    if (!Exists(launcher)) {
        Say(L"DPLauncher.exe поруч не знайдено, нічого заміняти. Гра, ймовірно, запускається без лаунчера.", MB_ICONINFORMATION);
        ExitProcess(0);
    }
    // The stub starts DP.exe without the launcher's Full Screen argument. With the mod's borderless window off, or with
    // DPfix (the mod then leaves the window to it), that would leave the game windowed for good; the mod itself then
    // puts the original launcher back (launcher_install.cpp), so do not install.
    bool haveIni = Exists(ini);
    if ((haveIni && GetPrivateProfileIntW(L"Display", L"Borderless", 1, ini) == 0) || Exists(dpfix)) {
        Say(L"Лаунчер не замінено: у UltimateFix.ini вимкнено Borderless або поруч є DPfix. Тоді справжній повний екран гра отримує лише від галочки Full Screen у звичайному лаунчері, а швидкий запуск її не передає.\n\nЩоб користуватися швидким запуском, поставте Borderless=1 (без DPfix).",
            MB_ICONINFORMATION);
        ExitProcess(0);
    }
    if (originalIsStub && !DeleteFileW(original)) {
        // a leftover stub is no original; if it stays, it would pass for the original below and the stock launcher could be lost
        Say(L"DPLauncher_original.exe є швидким запуском, а не оригіналом, і його не вдалося видалити. Видаліть його самі й перевірте цілісність файлів гри у Steam, потім запустіть ufx_play.exe ще раз.", MB_ICONERROR);
        ExitProcess(1);
    }

    bool launcherIsStub = IsStub(launcher);
    bool kept = Exists(original);
    if (kept || launcherIsStub) {
        // a second run (or Steam restored the stock launcher): refresh our copy, keep the first original. A stub in place
        // is never renamed to DPLauncher_original.exe.
        if (!CopyFileW(self, launcher, FALSE)) {
            Say(L"Не вдалося оновити DPLauncher.exe, файл не змінено. Закрийте лаунчер і спробуйте ще раз (інколи потрібні права адміністратора).", MB_ICONERROR);
            ExitProcess(1);
        }
    } else {
        if (!MoveFileW(launcher, original)) {
            Say(L"Не вдалося перейменувати DPLauncher.exe. Закрийте лаунчер і спробуйте ще раз (інколи потрібні права адміністратора).", MB_ICONERROR);
            ExitProcess(1);
        }
        kept = true;
        if (!CopyFileW(self, launcher, FALSE)) {
            if (MoveFileW(original, launcher)) Say(L"Не вдалося створити новий DPLauncher.exe. Оригінал повернуто.", MB_ICONERROR);
            else Say(L"Не вдалося створити новий DPLauncher.exe і повернути оригінал. Перейменуйте DPLauncher_original.exe назад у DPLauncher.exe або перевірте цілісність файлів гри у Steam.", MB_ICONERROR);
            ExitProcess(1);
        }
    }
    if (haveIni) WritePrivateProfileStringW(L"General", L"ReplaceLauncher", L"1", ini);  // so the mod keeps it (an earlier --undo wrote 0)
    if (!kept) {
        Say(L"Готово, але DPLauncher_original.exe немає, тож Shift не відкриє старий лаунчер. Перевірте цілісність файлів гри у Steam (він поверне DPLauncher.exe) і запустіть ufx_play.exe ще раз.", MB_ICONINFORMATION);
        ExitProcess(0);
    }
    wchar_t mod[MAX_PATH + 16];
    lstrcpyW(mod, dir);
    lstrcatW(mod, L"X3DAudio1_7.dll");
    if (!Exists(mod)) {
        Say(L"Готово. Тепер Steam одразу запускає DP.exe без вікна лаунчера.\n\nАле поруч із DP.exe немає моду UltimateFIXDP (X3DAudio1_7.dll), тож гра запускатиметься у вікні 1280x720: справжній повний екран дає лише галочка Full Screen у звичайному лаунчері (тримайте Shift під час запуску).\nЩоб повернути як було: ufx_play.exe --undo", MB_ICONINFORMATION);
        ExitProcess(0);
    }
    Say(L"Готово. Тепер Steam одразу запускає DP.exe без вікна лаунчера.\n\nЩоб відкрити старий лаунчер (мова, клавіші), тримайте Shift під час запуску.\nЩоб повернути як було: ufx_play.exe --undo", MB_ICONINFORMATION);
    ExitProcess(0);
}
