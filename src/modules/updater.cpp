// Updates from GitHub (LittleBitUA/UltimateFIXDP), in place.
//
//   Check     some seconds after the start, on its own thread: GET api.github.com/repos/LittleBitUA/UltimateFIXDP/releases/latest
//             (stable releases only; GitHub leaves pre-releases out of "latest"). A newer tag than ours shows in the start card and
//             on the About page: F8 updates.
//   Update    the release zip is downloaded into memory and read with the zip reader of inflate.h (every entry CRC-checked).
//             X3DAudio1_7.dll: the new one is written next to ours, ours (loaded, so it can be renamed but not overwritten) is
//             renamed to X3DAudio1_7.dll.old, the new one takes its name. Files of the archive's UltimateFix\ folder are written
//             over (icons and the like); UltimateFix.ini, mods\, savebackups\ and the logs are not in the archive and stay.
//             Then the game is started again and this one closes. Any failure before the swap leaves everything as it was.
//   Privacy   one anonymous request to GitHub with the User-Agent "UltimateFIXDP"; nothing about the player is sent.
// [Menu] CheckUpdates=1. WinHTTP is loaded at run time (the DLL imports only kernel32, user32 and gdi32).
#include "../base.h"
#include "../inflate.h"

namespace ufx {
namespace {

typedef void* HINTERNET_;
typedef HINTERNET_(WINAPI* OpenFn)(const wchar_t*, DWORD, const wchar_t*, const wchar_t*, DWORD);
typedef HINTERNET_(WINAPI* ConnectFn)(HINTERNET_, const wchar_t*, WORD, DWORD);
typedef HINTERNET_(WINAPI* RequestFn)(HINTERNET_, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t**, DWORD);
typedef BOOL(WINAPI* SendFn)(HINTERNET_, const wchar_t*, DWORD, void*, DWORD, DWORD, DWORD_PTR);
typedef BOOL(WINAPI* ReceiveFn)(HINTERNET_, void*);
typedef BOOL(WINAPI* QueryHeadersFn)(HINTERNET_, DWORD, const wchar_t*, void*, DWORD*, DWORD*);
typedef BOOL(WINAPI* AvailableFn)(HINTERNET_, DWORD*);
typedef BOOL(WINAPI* ReadFn)(HINTERNET_, void*, DWORD, DWORD*);
typedef BOOL(WINAPI* CloseFn)(HINTERNET_);
typedef BOOL(WINAPI* SetTimeoutsFn)(HINTERNET_, int, int, int, int);

struct WinHttp {
    OpenFn open; ConnectFn connect; RequestFn request; SendFn send; ReceiveFn receive; QueryHeadersFn query;
    AvailableFn available; ReadFn read; CloseFn close; SetTimeoutsFn timeouts;
} g_http;

bool LoadWinHttp() {
    if (g_http.open) return true;
    HMODULE m = LoadLibraryW(L"winhttp.dll");
    if (!m) return false;
    g_http.open = (OpenFn)(void*)GetProcAddress(m, "WinHttpOpen");
    g_http.connect = (ConnectFn)(void*)GetProcAddress(m, "WinHttpConnect");
    g_http.request = (RequestFn)(void*)GetProcAddress(m, "WinHttpOpenRequest");
    g_http.send = (SendFn)(void*)GetProcAddress(m, "WinHttpSendRequest");
    g_http.receive = (ReceiveFn)(void*)GetProcAddress(m, "WinHttpReceiveResponse");
    g_http.query = (QueryHeadersFn)(void*)GetProcAddress(m, "WinHttpQueryHeaders");
    g_http.available = (AvailableFn)(void*)GetProcAddress(m, "WinHttpQueryDataAvailable");
    g_http.read = (ReadFn)(void*)GetProcAddress(m, "WinHttpReadData");
    g_http.close = (CloseFn)(void*)GetProcAddress(m, "WinHttpCloseHandle");
    g_http.timeouts = (SetTimeoutsFn)(void*)GetProcAddress(m, "WinHttpSetTimeouts");
    return g_http.open && g_http.connect && g_http.request && g_http.send && g_http.receive && g_http.query && g_http.available &&
           g_http.read && g_http.close;
}

// GET https://host/path into a VirtualAlloc'd buffer (redirects followed by WinHTTP). Returns the size, 0 on failure.
DWORD HttpsGet(const wchar_t* host, const wchar_t* path, u8** out, DWORD cap) {
    *out = nullptr;
    if (!LoadWinHttp()) return 0;
    HINTERNET_ s = g_http.open(L"UltimateFIXDP", 0 /*default proxy*/, nullptr, nullptr, 0);
    if (!s) return 0;
    if (g_http.timeouts) g_http.timeouts(s, 10000, 10000, 15000, 30000);
    DWORD got = 0;
    HINTERNET_ c = g_http.connect(s, host, 443, 0);
    HINTERNET_ r = c ? g_http.request(c, L"GET", path, nullptr, nullptr, nullptr, 0x00800000 /*WINHTTP_FLAG_SECURE*/) : nullptr;
    const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
    if (r && g_http.send(r, headers, (DWORD)-1, nullptr, 0, 0, 0) && g_http.receive(r, nullptr)) {
        DWORD status = 0, len = sizeof(status);
        g_http.query(r, 19 /*STATUS_CODE*/ | 0x20000000 /*FLAG_NUMBER*/, nullptr, &status, &len, nullptr);
        u8* buf = status == 200 ? (u8*)VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
        DWORD avail = 0;
        while (buf && g_http.available(r, &avail) && avail) {
            if (got + avail > cap) { got = 0; break; }
            DWORD n = 0;
            if (!g_http.read(r, buf + got, avail, &n) || !n) break;
            got += n;
        }
        if (buf && got) *out = buf;
        else if (buf) VirtualFree(buf, 0, MEM_RELEASE);
        if (status != 200) Log("update: %S%S answered %u", host, path, status);
    }
    if (r) g_http.close(r);
    if (c) g_http.close(c);
    g_http.close(s);
    return *out ? got : 0;
}

// ---- versions ----
// "v0.7.2" or "0.7.1-test10" -> three numbers and whether it is a test build
void ParseVersion(const char* v, int n[3], bool* test) {
    n[0] = n[1] = n[2] = 0;
    if (*v == 'v' || *v == 'V') ++v;
    for (int i = 0; i < 3; ++i) {
        while (*v >= '0' && *v <= '9') n[i] = n[i] * 10 + (*v++ - '0');
        if (*v == '.') ++v;
        else break;
    }
    *test = *v == '-';
}

bool IsNewer(const char* remote, const char* local) {
    int a[3], b[3];
    bool ta, tb;
    ParseVersion(remote, a, &ta);
    ParseVersion(local, b, &tb);
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return !ta && tb;  // the release of the same number is newer than its test builds
}

// The string value after "key": in a JSON text (no escapes expected in these values).
bool JsonString(const char* from, const char* key, char* out, int cap, const char** next) {
    const int kl = lstrlenA(key);
    for (const char* p = from; *p; ++p) {
        if (*p != '"' || CompareStringA(LOCALE_INVARIANT, 0, p + 1, kl, key, kl) != CSTR_EQUAL || p[1 + kl] != '"') continue;
        const char* q = p + 2 + kl;
        while (*q == ' ' || *q == ':') ++q;
        if (*q != '"') return false;
        ++q;
        int n = 0;
        while (*q && *q != '"' && n < cap - 1) out[n++] = *q++;
        out[n] = 0;
        if (next) *next = q;
        return true;
    }
    return false;
}

// ---- state ----
volatile long g_state = 0;  // 0 nothing, 1 newer version available, 2 updating, 3 updated (restart), 4 failed
char g_remote[32] = "";
char g_assetUrl[512] = "";
bool g_noticeShown = false;

DWORD WINAPI CheckWorker(void*) {
    Sleep(5000);
    u8* body = nullptr;
    const DWORD n = HttpsGet(L"api.github.com", L"/repos/LittleBitUA/UltimateFIXDP/releases/latest", &body, 1u << 20);
    if (!n) {
        Log("update: no answer from GitHub (offline?), not checked");
        return 0;
    }
    body[n < (1u << 20) ? n : n - 1] = 0;
    char tag[32] = "";
    const char* json = (const char*)body;
    if (JsonString(json, "tag_name", tag, 32, nullptr)) {
        // the release archive: an asset named UltimateFIXDP-*.zip
        const char* p = json;
        char url[512];
        while (JsonString(p, "browser_download_url", url, 512, &p)) {
            const int len = lstrlenA(url);
            bool ours = len > 4 && lstrcmpiA(url + len - 4, ".zip") == 0;
            const char* slash = url;
            for (const char* q = url; *q; ++q)
                if (*q == '/') slash = q + 1;
            ours = ours && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, slash, 14, "UltimateFIXDP-", 14) == CSTR_EQUAL;
            if (ours) {
                lstrcpynA(g_assetUrl, url, 512);
                break;
            }
        }
        lstrcpynA(g_remote, tag[0] == 'v' ? tag + 1 : tag, 32);
        if (IsNewer(tag, kVersion) && g_assetUrl[0]) {
            g_state = 1;
            Log("update: version %s is available (this is %s): F9 on the start card or the About page updates", g_remote, kVersion);
        } else {
            Log("update: %s is the newest release (this is %s)", g_remote, kVersion);
        }
    }
    VirtualFree(body, 0, MEM_RELEASE);
    return 0;
}

bool WriteFileAll(const wchar_t* path, const u8* data, DWORD size) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = (size == 0 || WriteFile(f, data, size, &put, nullptr)) && put == size && FlushFileBuffers(f);
    CloseHandle(f);
    return ok;
}

void MakeDirsFor(wchar_t* path) {  // creates every folder of a file path
    for (wchar_t* p = path; *p; ++p)
        if (*p == L'\\' && p > path && p[-1] != L':') {
            *p = 0;
            CreateDirectoryW(path, nullptr);
            *p = L'\\';
        }
}

DWORD WINAPI UpdateWorker(void*) {
    // https://github.com/<...>/releases/download/<tag>/<name>.zip
    const char* u = g_assetUrl;
    if (CompareStringA(LOCALE_INVARIANT, 0, u, 8, "https://", 8) != CSTR_EQUAL) { g_state = 4; return 0; }
    u += 8;
    wchar_t host[128], path[512];
    int i = 0;
    for (; *u && *u != '/' && i < 127; ++u) host[i++] = (wchar_t)*u;
    host[i] = 0;
    i = 0;
    for (; *u && i < 511; ++u) path[i++] = (wchar_t)*u;
    path[i] = 0;
    u8* zip = nullptr;
    const DWORD n = HttpsGet(host, path, &zip, 64u << 20);
    if (!n) {
        Log("update: the archive could not be downloaded");
        g_state = 4;
        return 0;
    }
    Log("update: archive of %s downloaded (%u bytes)", g_remote, n);
    // pass 1: everything unpacks and checks out; pass 2 writes
    bool okAll = true;
    int dlls = 0;
    ZipForEach(zip, n, [&](const ZipEntry& e) {
        if (!e.nameLen || e.name[e.nameLen - 1] == '/') return true;
        u8* tmp = (u8*)VirtualAlloc(nullptr, e.size + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!tmp || (e.size && !ZipExtract(e, tmp))) okAll = false;
        if (tmp) VirtualFree(tmp, 0, MEM_RELEASE);
        const char* base = e.name;
        for (int k = 0; k < e.nameLen; ++k)
            if (e.name[k] == '/') base = e.name + k + 1;
        const int baseLen = (int)(e.name + e.nameLen - base);
        if (baseLen == 15 && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, base, 15, "X3DAudio1_7.dll", 15) == CSTR_EQUAL &&
            base - e.name == 14)  // UltimateFIXDP/X3DAudio1_7.dll at the top, not one in a subfolder
            ++dlls;
        return okAll;
    });
    if (!okAll || dlls != 1) {
        Log("update: the archive does not check out (%s), nothing changed", dlls != 1 ? "no X3DAudio1_7.dll at its top" : "a file does not unpack");
        VirtualFree(zip, 0, MEM_RELEASE);
        g_state = 4;
        return 0;
    }
    wchar_t dll[MAX_PATH + 32], fresh[MAX_PATH + 32], old[MAX_PATH + 32];
    wsprintfW(dll, L"%sX3DAudio1_7.dll", g_paths.gameDir);
    wsprintfW(fresh, L"%s.new", dll);
    wsprintfW(old, L"%s.old", dll);
    bool dllOk = false;
    int files = 0;
    ZipForEach(zip, n, [&](const ZipEntry& e) {
        if (!e.nameLen || e.name[e.nameLen - 1] == '/') return true;
        // only the archive's top folder: X3DAudio1_7.dll and UltimateFix\...
        if (e.nameLen < 15 || CompareStringA(LOCALE_INVARIANT, 0, e.name, 14, "UltimateFIXDP/", 14) != CSTR_EQUAL) return true;
        const char* rel = e.name + 14;
        const int relLen = e.nameLen - 14;
        const bool isDll = relLen == 15 && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, rel, 15, "X3DAudio1_7.dll", 15) == CSTR_EQUAL;
        const bool isData = relLen > 12 && CompareStringA(LOCALE_INVARIANT, 0, rel, 12, "UltimateFix/", 12) == CSTR_EQUAL;
        if (!isDll && !isData) return true;
        u8* data = (u8*)VirtualAlloc(nullptr, e.size + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!data) return true;
        if (!e.size || ZipExtract(e, data)) {
            if (isDll) {
                dllOk = WriteFileAll(fresh, data, e.size);
            } else {
                wchar_t target[MAX_PATH + 64];
                lstrcpyW(target, g_paths.gameDir);
                int t = lstrlenW(target);
                const int room = MAX_PATH - t - 1;
                const int w = MultiByteToWideChar(CP_UTF8, 0, rel, relLen, target + t, room);
                if (w > 0) {
                    target[t + w] = 0;
                    for (wchar_t* p = target + t; *p; ++p)
                        if (*p == L'/') *p = L'\\';
                    MakeDirsFor(target);
                    if (WriteFileAll(target, data, e.size)) ++files;
                }
            }
        }
        VirtualFree(data, 0, MEM_RELEASE);
        return true;
    });
    VirtualFree(zip, 0, MEM_RELEASE);
    if (!dllOk) {
        DeleteFileW(fresh);
        Log("update: the new X3DAudio1_7.dll could not be written next to the game, nothing changed");
        g_state = 4;
        return 0;
    }
    DeleteFileW(old);
    if (!MoveFileExW(dll, old, MOVEFILE_REPLACE_EXISTING) || !MoveFileExW(fresh, dll, 0)) {
        const DWORD e = GetLastError();
        MoveFileExW(old, dll, 0);  // ours back if it was moved
        DeleteFileW(fresh);
        Log("update: X3DAudio1_7.dll could not be swapped (error %u), the old one stays", e);
        g_state = 4;
        return 0;
    }
    Log("update: updated to %s (%d files of UltimateFix written); the game restarts", g_remote, files);
    g_state = 3;
    return 0;
}

// A new DP.exe while this one still runs could meet the game's own single-instance checks: a hidden cmd waits two seconds
// (this process is gone by then) and starts the game with the same command line.
void RestartGame() {
    wchar_t exe[MAX_PATH + 8];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    // the arguments of this start: the command line after the program name
    const wchar_t* args = GetCommandLineW();
    if (*args == L'"') {
        ++args;
        while (*args && *args != L'"') ++args;
        if (*args) ++args;
    } else {
        while (*args && *args != L' ') ++args;
    }
    while (*args == L' ') ++args;
    wchar_t sys[MAX_PATH], cmd[MAX_PATH * 2 + 1200];
    GetSystemDirectoryW(sys, MAX_PATH);
    wchar_t argsCut[1024];
    lstrcpynW(argsCut, args, 1024);
    wsprintfW(cmd, L"\"%s\\cmd.exe\" /c ping -n 3 127.0.0.1 >nul & start \"\" \"%s\" %s", sys, exe, argsCut);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    for (DWORD i = 0; i < sizeof(si); ++i) ((u8*)&si)[i] = 0;
    si.cb = sizeof(si);
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, g_paths.gameDir, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Log("update: the game starts again in two seconds");
    } else {
        Log("update: the game could not be started again (error %u): start it by hand", GetLastError());
    }
    TerminateProcess(GetCurrentProcess(), 0);  // nothing is being saved now; the game's own shutdown can hang on its sound threads
}

}  // namespace

void InstallUpdater() {
    wchar_t old[MAX_PATH + 32];
    wsprintfW(old, L"%sX3DAudio1_7.dll.old", g_paths.gameDir);
    DeleteFileW(old);  // the DLL an update replaced last time (no longer loaded)
    if (!g_cfg.checkUpdates) return;
    HANDLE t = CreateThread(nullptr, 0, CheckWorker, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}

int UpdateState() { return g_state; }
const char* UpdateVersion() { return g_remote; }

void UpdateStart() {
    if (g_state != 1 && g_state != 4) return;
    g_noticeShown = false;
    if (!g_assetUrl[0]) return;
    g_state = 2;
    Log("update: updating to %s", g_remote);
    HANDLE t = CreateThread(nullptr, 0, UpdateWorker, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    else g_state = 4;
}

// Main thread, once per frame: the notices and the restart (F9 is read by the menu: the start card and the About page).
void UpdateFrame() {
    static DWORD restartAt = 0;
    const long s = g_state;
    if (s == 3 && !restartAt) {
        wchar_t uk[200], en[200];
        wsprintfW(uk, L"Встановлено версію %S. Гра перезапуститься за кілька секунд.", g_remote);
        wsprintfW(en, L"Version %S is installed. The game restarts in a few seconds.", g_remote);
        ShowNotice(L"Оновлено", L"Updated", uk, en, 5);
        restartAt = GetTickCount() + 3500;
        if (!restartAt) restartAt = 1;
    }
    if (restartAt && (int)(GetTickCount() - restartAt) >= 0) RestartGame();
    if (s == 4 && !g_noticeShown) {
        g_noticeShown = true;
        ShowNotice(L"Оновлення не вдалося", L"The update failed",
                   L"Гра й мод лишились як були. Подробиці в UltimateFix\\ufx.log; нову версію можна взяти з GitHub вручну.",
                   L"The game and the mod are as they were. Details in UltimateFix\\ufx.log; the new version can be taken from GitHub by hand.", 12, kNoticeError);
    }
}

}  // namespace ufx
