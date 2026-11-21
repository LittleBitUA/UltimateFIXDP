// What the player sees of the mod's own diagnostics: a short notice box at the top of the screen, the one-time hint after
// a long session, and the two buttons of the F10 menu (open the folder with the logs, write a report file).
//
// The long-session hint. Another project's notes say that the game's pool of sound load requests leaks and that the
// game breaks after three hours or more. What the disassembly shows (sites.h, "Sound load requests"): the pool has 64
// entries and taking one does not check that any is left (an empty pool hands out index -1 and the request is written
// over the pool's own free list), a streamed sound holds one request while it plays, and at most 32 sounds stream at a
// time. No path that loses a request was found: on the PC every request taken by the start of a stream (FUN_00737A80)
// is given back by its stop (FUN_007380A0), and the refill callback FUN_00737580 is never called. So the leak is not
// confirmed. The hint therefore only says that long sessions can be unstable, and the number of requests in use is
// read once a second: it is logged every hour and written to the report, and 48 or more (which only a leak can reach)
// brings the hint at once.
#include "../base.h"
#include "../sites.h"
#include "../ui/ui.h"
#include "../ui/theme.h"

namespace ufx {

extern bool g_dpfixPresent;  // main.cpp: DPfix.ini lies next to the game

namespace {

// ---- The notice box: one message at a time, a title and up to three lines, gone after a few seconds ---------------
constexpr int kNoticeLines = 3, kNoticeLineChars = 128;
constexpr float kNoticeWidth = 760.f, kNoticeTop = 58.f;
struct NoticeText {
    wchar_t title[96];
    wchar_t text[320];
};
NoticeText g_notice[2];  // Ukrainian and English: the language of the menu can change while the box is up
bool g_noticeActive = false;
bool g_noticeDrawn = false;  // its time runs from the first frame it is drawn in (g_noticeStart)
DWORD g_noticeStart = 0, g_noticeMs = 0;
wchar_t g_noticeLine[kNoticeLines][kNoticeLineChars];
int g_noticeLineCount = 0;
int g_noticeLaidOut = -1;  // the language the lines above were broken for (-1 = not yet)

// Breaks the text into lines no wider than kNoticeWidth, at spaces. What does not fit into kNoticeLines lines is left out.
void LayOutNotice(const wchar_t* text) {
    g_noticeLineCount = 0;
    const wchar_t* p = text;
    while (*p && g_noticeLineCount < kNoticeLines) {
        const int rest = lstrlenW(p);
        int n = ui::TextFit(p, rest < kNoticeLineChars ? rest : kNoticeLineChars - 1, kNoticeWidth, 14);
        if (n < 1) n = 1;
        if (n < rest) {  // break at the last space that fits
            int b = n;
            while (b > 0 && p[b] != L' ') --b;
            if (b > 0) n = b;
        }
        wchar_t* line = g_noticeLine[g_noticeLineCount++];
        for (int i = 0; i < n; ++i) line[i] = p[i];
        line[n] = 0;
        p += n;
        while (*p == L' ') ++p;
    }
}

u32 WithAlpha(u32 argb, u32 alpha) { return (argb & 0x00FFFFFF) | (((argb >> 24) * alpha / 255) << 24); }

void DrawNotice(void* device) {
    if (!g_noticeActive) return;
    const DWORD now = GetTickCount();
    if (!g_noticeDrawn) {
        g_noticeDrawn = true;
        g_noticeStart = now;
    }
    const DWORD age = now - g_noticeStart;
    if (age >= g_noticeMs) {
        g_noticeActive = false;
        return;
    }
    if (!ui::OverlayBegin(device)) return;
    const int lang = g_cfg.language == 1 ? 1 : 0;
    const NoticeText& t = g_notice[lang];
    if (g_noticeLaidOut != lang) {
        g_noticeLaidOut = lang;
        LayOutNotice(t.text);
    }
    float w = ui::TextWidth(t.title, 16, ui::kSemibold);
    for (int i = 0; i < g_noticeLineCount; ++i) {
        const float lw = ui::TextWidth(g_noticeLine[i], 14);
        if (lw > w) w = lw;
    }
    w += 32.f;
    const float x = (1280.f - w) / 2, h = 38.f + 18.f * (float)g_noticeLineCount;
    const u32 alpha = g_noticeMs - age < 1000 ? (g_noticeMs - age) * 255 / 1000 : 255;  // fades out over the last second
    // The start card's look (theme.h): a raised box with a hairline border and rounded corners, and the one red mark inside
    // it. Not amber: most notices are no warning ("Report written"). Opaque: the inside is drawn over the border's shape, so a
    // see-through one would not let the game show anyway, only the border's grey.
    namespace theme = ui::theme;
    ui::FillRoundFramed(x, kNoticeTop, w, h, 10, WithAlpha(theme::kRaised, alpha), WithAlpha(theme::kLine2, alpha));
    ui::FillRound(x + 7.f, kNoticeTop + 10.f, 3.f, h - 20.f, 1.5f, WithAlpha(theme::kAccent, alpha));
    ui::DrawText(t.title, x + 16.f, kNoticeTop + 8.f, WithAlpha(theme::kWhite, alpha), 16, ui::kSemibold);
    for (int i = 0; i < g_noticeLineCount; ++i)
        ui::DrawText(g_noticeLine[i], x + 16.f, kNoticeTop + 31.f + 18.f * (float)i, WithAlpha(theme::kMuted, alpha), 14);
    ui::OverlayEnd();
}

// ---- The session: how long it runs, the sound load requests in use ------------------------------------------------
constexpr int kPoolWarnAt = 48;  // 32 streams hold at most 32 requests: more than that is not given back
bool g_sessionStarted = false;
DWORD g_sessionStart = 0;        // the tick of the first frame
int g_poolHighest = -1;

// Requests in use in the game's sound load pool: 0..64, 65 = the counter is out of range (the pool was overrun),
// -1 = cannot be read (the object does not exist yet, or the code that proves where it lives differs).
int SoundRequestsInUse() {
    static int facts = -1;
    if (facts < 0)
        facts = BytesMatch(kSiteAudioDataLoad.va, kSiteAudioDataLoad.bytes, kSiteAudioDataLoad.len) &&
                BytesMatch(kSiteAudioPoolOffset.va, kSiteAudioPoolOffset.bytes, kSiteAudioPoolOffset.len) &&
                BytesMatch(kSiteAudioPoolVtable.va, kSiteAudioPoolVtable.bytes, kSiteAudioPoolVtable.len);
    if (!facts) return -1;
    const u8* audio = *(const u8* volatile*)(uintptr_t)kAudioDataPtrVa;
    if ((uintptr_t)audio < 0x10000) return -1;
    const u32* pool = (const u32*)(audio + kAudioRequestPoolOffset);
    if (IsBadReadPtr(pool, 8) || pool[0] != kAudioRequestPoolVtable) return -1;
    const int n = (int)*(const volatile u32*)(pool + 1);
    return n >= 0 && n <= kAudioRequestPoolSize ? n : kAudioRequestPoolSize + 1;
}

void PoolText(char* out, int inUse) {
    if (inUse < 0) lstrcpyA(out, "not readable");
    else if (inUse > kAudioRequestPoolSize) lstrcpyA(out, "counter out of range (the pool was overrun)");
    else wsprintfA(out, "%d of %d", inUse, kAudioRequestPoolSize);
}

u32 SessionMinutes() { return g_sessionStarted ? (GetTickCount() - g_sessionStart) / 60000u : 0; }

// Once a second: what the loading thread reported, the hourly log line, the one-time hint.
void PollSession(DWORD now) {
    char failed[64];
    if (ResourceGuardTakeFailure(failed, sizeof(failed))) {
        wchar_t uk[320], en[320];
        wsprintfW(uk, L"Ресурс %S не розпакувався після кількох спроб, тож гра вважатиме його відсутнім. Перевірте цілісність файлів гри у Steam. Подробиці в UltimateFix\\ufx.log.", failed);
        wsprintfW(en, L"The resource %S did not unpack after several attempts, so the game will treat it as missing. Verify the game files in Steam. Details are in UltimateFix\\ufx.log.", failed);
        ShowNotice(L"Файл гри не читається", L"A game file cannot be read", uk, en, 20);
    }
    const int inUse = SoundRequestsInUse();
    if (inUse > g_poolHighest) g_poolHighest = inUse;
    const u32 hours = (now - g_sessionStart) / 3600000u;
    static u32 loggedHours = 0;
    if (hours > loggedHours) {
        loggedHours = hours;
        char now64[64], top[64];
        PoolText(now64, inUse);
        PoolText(top, g_poolHighest);
        Log("session: %u h; sound load requests in use: %s, highest seen: %s", hours, now64, top);
    }
    static bool hinted = false;
    if (hinted || g_cfg.sessionReminderHours <= 0 || g_noticeActive) return;
    const bool poolLow = inUse >= kPoolWarnAt;
    const bool memLow = MemoryIsLow();
    if (hours < (u32)g_cfg.sessionReminderHours && !poolLow && !memLow) return;
    HWND w = GameWindowHandle();
    if (!w || GetForegroundWindow() != w) return;  // shown once: wait until the player is looking at the game
    hinted = true;
    wchar_t titleUk[96], titleEn[96], uk[320], en[320];
    if (memLow) {
        lstrcpyW(titleUk, L"Грі бракує пам'яті");
        lstrcpyW(titleEn, L"The game is running out of memory");
        lstrcpyW(uk, L"Вільного адресного простору гри майже не лишилось, і наступне завантаження може закінчитися вильотом. Збережіться й перезапустіть гру. Допомагає й ufx_laa.exe (4 ГБ) та менший масштаб рендеру.");
        lstrcpyW(en, L"The game has almost no free address space left, and the next load may crash. Save and restart the game. ufx_laa.exe (4 GB) and a smaller render scale help too.");
    } else if (poolLow) {
        lstrcpyW(titleUk, L"Варто перезапустити гру");
        lstrcpyW(titleEn, L"Time to restart the game");
        lstrcpyW(uk, L"Звукова система гри зайняла майже всі свої 64 комірки завантаження й не перевіряє, чи вони скінчилися. Збережіться й перезапустіть гру.");
        lstrcpyW(en, L"The game's sound system has used up nearly all of its 64 load slots and does not check for running out. Save and restart the game.");
    } else {
        wsprintfW(titleUk, L"Ви граєте вже %u год", hours);
        wsprintfW(titleEn, L"You have been playing for %u h", hours);
        lstrcpyW(uk, L"Довгі сесії цієї гри бувають нестабільними. Збережіться, а коли буде зручно, перезапустіть гру. Нагадування вимикається в F10, розділ «Виправлення».");
        lstrcpyW(en, L"Long sessions of this game can become unstable. Save, and restart the game when convenient. The reminder is switched off in F10, section \"Fixes\".");
    }
    ShowNotice(titleUk, titleEn, uk, en, 15);
    char pool[64];
    PoolText(pool, inUse);
    Log("session: the one-time hint to save and restart is shown after %u h (%s; sound load requests in use: %s)", hours,
        memLow ? "the address space is nearly used up" : poolLow ? "the sound load pool is nearly used up" : "SessionReminderHours", pool);
}

// ---- The report file ------------------------------------------------------------------------------------------------
void Line(HANDLE file, const char* fmt, ...) {
    char buf[1100];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);  // never more than 1024 bytes
    va_end(ap);
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD written;
    WriteFile(file, buf, (DWORD)n, &written, nullptr);
}

bool InGameFolder(const wchar_t* name) {
    wchar_t path[MAX_PATH + 32];
    lstrcpyW(path, g_paths.gameDir);
    lstrcatW(path, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// The file name (no folder) of the module that `address` belongs to.
void ModuleName(const void* address, char* out, int cap) {
    HMODULE mod = nullptr;
    char path[MAX_PATH];
    lstrcpynA(out, "no module", cap);
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &mod) || !mod) return;
    const DWORD n = GetModuleFileNameA(mod, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    const char* name = path;
    for (const char* p = path; *p; ++p)
        if (*p == '\\' || *p == '/') name = p + 1;
    lstrcpynA(out, name, cap);
}

void ReportEnvironment(HANDLE f) {
    HMODULE exe = GetModuleHandleW(nullptr);
    Line(f, "[Environment]");
    Line(f, "DP.exe: Steam 1.01b; large address aware: %s (user addresses end at %08X)", HasLargeAddressAware(exe) ? "yes" : "no", UserAddressEnd());
    // KUSER_SHARED_DATA, mapped at 7FFE0000h in every process: the version Windows really is (no manifest needed). The build
    // number field exists from Windows 10 on.
    const u8* shared = (const u8*)(uintptr_t)0x7FFE0000;
    const u32 major = *(const u32*)(shared + 0x26C), minor = *(const u32*)(shared + 0x270);
    if (major >= 10) Line(f, "Windows: %u.%u build %u", major, minor, *(const u32*)(shared + 0x260));
    else Line(f, "Windows: %u.%u", major, minor);
    typedef const char*(__cdecl * WineVersionFn)();
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    WineVersionFn wine = ntdll ? (WineVersionFn)(void*)GetProcAddress(ntdll, "wine_get_version") : nullptr;
    if (wine) Line(f, "Wine: %s", wine());
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem))
        Line(f, "Processors: %u; memory: %u MB, free %u MB; address space of the game: %u MB, free %u MB", (u32)si.dwNumberOfProcessors,
             (u32)(mem.ullTotalPhys >> 20), (u32)(mem.ullAvailPhys >> 20), (u32)(mem.ullTotalVirtual >> 20), (u32)(mem.ullAvailVirtual >> 20));
    Line(f, "Direct3D 9: %s", g_usingDxvk ? "DXVK (Direct3D 9 on Vulkan)"
                              : g_d3dWrapper ? "a d3d9.dll that is neither the Windows one nor DXVK (DPfix, ReShade, dgVoodoo...)" : "the Windows library");
    static const wchar_t* const kFiles[8] = {L"d3d9.dll", L"dxvk.conf", L"DPfix.ini", L"dinput8.dll", L"dxgi.dll", L"winmm.dll", L"ReShade.ini", L"dxvk.dll"};
    char found[160];
    found[0] = 0;
    for (const wchar_t* name : kFiles)
        if (InGameFolder(name)) wsprintfA(found + lstrlenA(found), " %S", name);
    Line(f, "Next to DP.exe (of d3d9.dll dxvk.conf DPfix.ini dinput8.dll dxgi.dll winmm.dll ReShade.ini dxvk.dll):%s", found[0] ? found : " none");
    Line(f, "DPfix mode (window and resolution left to DPfix): %s; SafeMode: %s", g_dpfixPresent ? "yes" : "no", g_cfg.safeMode ? "on" : "off");
    Line(f, "Output: %dx%d, scene width %d, picture in the window %dx%d, screen %d Hz", g_outputW ? g_outputW : 1280, g_outputH ? g_outputH : 720,
         g_sceneWidth, g_fit.w, g_fit.h, DisplayRefreshHz());
    const u32 minutes = SessionMinutes();
    char pool[64], top[64];
    PoolText(pool, SoundRequestsInUse());
    PoolText(top, g_poolHighest);
    Line(f, "Session: %u h %02u min; sound load requests in use: %s, highest seen: %s", minutes / 60, minutes % 60, pool, top);
    u32 retries, givenUp, overflows;
    ResourceGuardCounts(&retries, &givenUp, &overflows);
    Line(f, "Resource read guard: %u repeated reads, %u times a resource was reported missing, %u streams stopped at the end of their buffer",
         retries, givenUp, overflows);
    wchar_t pattern[MAX_PATH + 32];
    lstrcpyW(pattern, g_paths.dataDir);
    lstrcatW(pattern, L"*.dmp");
    WIN32_FIND_DATAW fd;
    int dumps = 0;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do ++dumps; while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    Line(f, "Dump files (crash_*.dmp, hang_*.dmp) in the UltimateFix folder: %d", dumps);
}

// Every place of DP.exe the mod knows: are its bytes still those of the stock exe? Returns how many are changed.
int ReportPatches(HANDLE f) {
    Line(f, "");
    Line(f, "[Patches]");
    Line(f, "patched  = the bytes in memory differ from the stock DP.exe: changed by this mod (or by another one)");
    Line(f, "original = untouched: the feature is off, was skipped (ufx.log says why), or only reads this place");
    int patched = 0, total = 0;
    for (const SiteDef* s : kAllSites) {
        const bool same = BytesMatch(s->va, s->bytes, s->len);
        ++total;
        if (!same) ++patched;
        Line(f, "%-8s %08X  %s", same ? "original" : "patched", s->va, s->name);
    }
    char self[64];
    ModuleName((const void*)(uintptr_t)&ModuleName, self, sizeof(self));
    HMODULE exe = GetModuleHandleW(nullptr);
    for (const ImportDef& imp : kImports) {
        void** slot = FindImportSlot(exe, imp.dll, imp.func);
        char owner[64];
        if (slot) ModuleName(*slot, owner, sizeof(owner));
        else lstrcpyA(owner, "not imported");
        const bool ours = slot && lstrcmpiA(owner, self) == 0;
        Line(f, "%-8s import    %s (%s)", ours ? "hooked" : "original", imp.func, ours ? "this mod" : owner);
    }
    Line(f, "%d of %d places patched", patched, total);
    return patched;
}

// UltimateFix.ini without its comments. The file holds nothing but settings.
void ReportSettings(HANDLE f) {
    Line(f, "");
    Line(f, "[Settings] (UltimateFix.ini without comments; SafeMode and DPfix can switch some of them off for the run)");
    HANDLE ini = CreateFileW(g_paths.iniFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (ini == INVALID_HANDLE_VALUE) {
        Line(f, "(the file cannot be opened)");
        return;
    }
    const DWORD size = GetFileSize(ini, nullptr);
    char* text = size && size < 0x40000 ? (char*)VirtualAlloc(nullptr, size + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    DWORD got = 0;
    if (!text || !ReadFile(ini, text, size, &got, nullptr) || got < 2 || ((u8)text[0] == 0xFF && (u8)text[1] == 0xFE)) {
        Line(f, "(the file is empty, too big or not plain text: not copied)");
    } else {
        text[got] = 0;
        for (const char* p = text; *p;) {
            const char* end = p;
            while (*end && *end != '\r' && *end != '\n') ++end;
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            if (p < end && *p != ';' && *p != '#') {
                char line[200];
                lstrcpynA(line, p, (int)(end - p) + 1 < (int)sizeof(line) ? (int)(end - p) + 1 : (int)sizeof(line));
                Line(f, "%s", line);
            }
            p = end;
            while (*p == '\r' || *p == '\n') ++p;
        }
    }
    if (text) VirtualFree(text, 0, MEM_RELEASE);
    CloseHandle(ini);
}

}  // namespace

void ShowNotice(const wchar_t* titleUk, const wchar_t* titleEn, const wchar_t* textUk, const wchar_t* textEn, int seconds) {
    lstrcpynW(g_notice[0].title, titleUk, 96);
    lstrcpynW(g_notice[1].title, titleEn, 96);
    lstrcpynW(g_notice[0].text, textUk, 320);
    lstrcpynW(g_notice[1].text, textEn, 320);
    g_noticeMs = (DWORD)seconds * 1000;
    g_noticeDrawn = false;
    g_noticeLaidOut = -1;
    g_noticeActive = true;
}

void SessionFrame(void* device) {
    const DWORD now = GetTickCount();
    if (!g_sessionStarted) {
        g_sessionStarted = true;
        g_sessionStart = now;
    }
    static DWORD lastPoll = 0;
    if (now - lastPoll >= 1000) {
        lastPoll = now;
        PollSession(now);
    }
    DrawNotice(device);
}

// Explorer on the UltimateFix folder (logs, crash dumps, save backups, the report). Started by its full path in the
// Windows folder, so nothing named explorer.exe elsewhere can run instead.
void OpenLogFolder() {
    wchar_t explorer[MAX_PATH + 16], command[3 * MAX_PATH];
    const UINT n = GetWindowsDirectoryW(explorer, MAX_PATH);
    bool ok = n > 0 && n < MAX_PATH;
    if (ok) {
        lstrcatW(explorer, L"\\explorer.exe");
        wchar_t folder[MAX_PATH];
        lstrcpyW(folder, g_paths.dataDir);
        const int len = lstrlenW(folder);
        if (len > 0 && folder[len - 1] == L'\\') folder[len - 1] = 0;  // a backslash in front of the closing quote would swallow it
        wsprintfW(command, L"\"%s\" \"%s\"", explorer, folder);
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        for (unsigned i = 0; i < sizeof(si); ++i) ((u8*)&si)[i] = 0;
        si.cb = sizeof(si);
        ok = CreateProcessW(explorer, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) != 0;
        if (ok) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    Log("diagnostics: the UltimateFix folder %s", ok ? "was opened in Explorer" : "could not be opened (Explorer did not start)");
    if (!ok)
        ShowNotice(L"Теку не вдалося відкрити", L"The folder could not be opened", L"Журнали лежать у теці UltimateFix поруч із DP.exe.",
                   L"The logs are in the UltimateFix folder next to DP.exe.", 8);
}

// One text file for a bug report: the version, the environment, which places of the exe are patched, the settings. It
// names no user, no computer and no folder (the logs do name the game folder; this file does not).
void WriteDiagnosticReport() {
    wchar_t path[MAX_PATH + 32];
    lstrcpyW(path, g_paths.dataDir);
    lstrcatW(path, L"ufx_report.txt");
    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        Log("diagnostics: the report file could not be created (error %u)", (u32)GetLastError());
        ShowNotice(L"Звіт не записано", L"The report was not written", L"Не вдалося створити файл у теці UltimateFix.",
                   L"The file could not be created in the UltimateFix folder.", 8);
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    Line(f, "UltimateFIXDP diagnostic report");
    Line(f, "version %s, written %04d-%02d-%02d %02d:%02d", kVersion, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    Line(f, "No user name, computer name or folder name is in this file. ufx.log in the same folder has the details of this run.");
    Line(f, "");
    ReportEnvironment(f);
    const int patched = ReportPatches(f);
    ReportSettings(f);
    CloseHandle(f);
    Log("diagnostics: report written to UltimateFix\\ufx_report.txt (%d places patched)", patched);
    ShowNotice(L"Звіт записано", L"Report written", L"Файл ufx_report.txt лежить у теці UltimateFix поруч із DP.exe. Кнопка «Тека журналів» відкриває її.",
               L"The file ufx_report.txt is in the UltimateFix folder next to DP.exe. The \"Log folder\" button opens it.", 6);
}

}  // namespace ufx
