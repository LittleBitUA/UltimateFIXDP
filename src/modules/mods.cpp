// Mod manager: UltimateFix\mods\<folder>\ holds one mod each.
//
//   mod.ini      optional, UTF-8: Name=, Author=, Version=, Description= (any [section], first value wins)
//   text\        a translation or text fix: mes_all.json and glyphmap.json of the SWERY Localization Tool, *.txt corrections
//                (the text overlay reads them, text_overlay.cpp)
//   textures\    replacement textures, <hash>.dds or <hash>.png (texture_override.cpp)
//   anything else game files by name, right in the mod folder (or in files\ or UPDATA\...): FONTWIDE.XPC, MES_ALL.MES,
//                a texture of an XPC as FONTWIDE.DDS, or a full game path (game_files.cpp)
//
// UltimateFix\mods\mods.txt (UTF-8, written by the F10 page) keeps the order and which mods are on: one line per mod, "+folder"
// on, "-folder" off, the first line wins over the ones below it. A folder not listed there yet is added at the end, switched on.
// Everything is read once at start: the text and the textures are loaded then, so a change in the menu needs a restart (the page
// says so). Without a mods folder nothing changes: the old places (UltimateFix\text\, the texture pack folders) work as before
// and come after the mods.
#include "../base.h"

namespace ufx {
namespace {

ModInfo g_mods[kMaxMods];
int g_count = 0;
int g_textMod = -1;            // the mod whose text folder the overlay reads, -1 = UltimateFix\text\ as before
wchar_t g_loadedOrder[1200];   // what this run started with ("+a|-b|..."), to tell whether a restart is needed
bool g_scanned = false;

void ModsDir(wchar_t* out) {  // with a trailing backslash
    lstrcpyW(out, g_paths.dataDir);
    lstrcatW(out, L"mods\\");
}

// A whole small file in memory, zero-terminated; null when it is missing or above `cap` bytes.
char* ReadSmall(const wchar_t* path, DWORD cap, DWORD* size) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    DWORD high = 0, low = GetFileSize(f, &high), got = 0;
    char* data = nullptr;
    if (!high && low <= cap) data = (char*)VirtualAlloc(nullptr, low + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (data && (!ReadFile(f, data, low, &got, nullptr) || got != low)) {
        VirtualFree(data, 0, MEM_RELEASE);
        data = nullptr;
    }
    CloseHandle(f);
    if (data) data[low] = 0;
    *size = low;
    return data;
}

void Utf8ToWide(const char* from, int length, wchar_t* to, int capacity) {
    int n = length > 0 ? MultiByteToWideChar(CP_UTF8, 0, from, length, to, capacity - 1) : 0;
    if (n < 0) n = 0;
    to[n] = 0;
}

bool SameText(const wchar_t* a, const wchar_t* b) { return lstrcmpiW(a, b) == 0; }

// The value of `key` in a UTF-8 ini text ("Key = value"), trimmed; false when the key is not there.
bool IniValue(const char* text, const char* key, wchar_t* out, int capacity) {
    const int keyLen = lstrlenA(key);
    const char* p = text;
    if ((u8)p[0] == 0xEF && (u8)p[1] == 0xBB && (u8)p[2] == 0xBF) p += 3;  // the UTF-8 mark some editors write
    while (*p) {
        const char* line = p;
        while (*p && *p != '\n') ++p;
        const char* end = p;
        if (*p) ++p;
        while (line < end && (*line == ' ' || *line == '\t')) ++line;
        if (end - line <= keyLen || CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, line, keyLen, key, keyLen) != CSTR_EQUAL) continue;
        const char* v = line + keyLen;
        while (v < end && (*v == ' ' || *v == '\t')) ++v;
        if (v >= end || *v != '=') continue;
        ++v;
        while (v < end && (*v == ' ' || *v == '\t')) ++v;
        while (end > v && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) --end;
        Utf8ToWide(v, (int)(end - v), out, capacity);
        return true;
    }
    return false;
}

bool Exists(const wchar_t* path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

int CountFiles(const wchar_t* dir, const wchar_t* mask) {
    wchar_t pattern[MAX_PATH + 16];
    lstrcpyW(pattern, dir);
    lstrcatW(pattern, mask);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++n;
    while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

// The parts of a mod folder that are not game files: its description and the folders of the overlays.
bool NotAGameFile(const wchar_t* name, bool folder) {
    return folder ? (SameText(name, L"text") || SameText(name, L"textures")) : SameText(name, L"mod.ini");
}

int CountTree(const wchar_t* dir, int depth) {
    wchar_t pattern[MAX_PATH + 16];
    if (lstrlenW(dir) + 2 >= MAX_PATH) return 0;
    lstrcpyW(pattern, dir);
    lstrcatW(pattern, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (fd.cFileName[0] == L'.') continue;
        if (depth == 0 && NotAGameFile(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            ++n;
        } else if (depth < 8 && lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 < MAX_PATH) {
            wchar_t sub[MAX_PATH + 16];
            lstrcpyW(sub, dir);
            lstrcatW(sub, fd.cFileName);
            lstrcatW(sub, L"\\");
            n += CountTree(sub, depth + 1);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

void Describe(ModInfo& m) {
    wchar_t base[MAX_PATH], path[MAX_PATH + 16];
    ModsDir(base);
    lstrcatW(base, m.folder);
    lstrcatW(base, L"\\");
    lstrcpyW(m.name, m.folder);
    m.author[0] = m.version[0] = m.description[0] = 0;
    lstrcpyW(path, base);
    lstrcatW(path, L"mod.ini");
    DWORD size = 0;
    if (char* ini = ReadSmall(path, 64 * 1024, &size)) {
        wchar_t v[200];
        if (IniValue(ini, "Name", v, 64) && v[0]) lstrcpynW(m.name, v, 64);
        if (IniValue(ini, "Author", v, 48)) lstrcpynW(m.author, v, 48);
        if (IniValue(ini, "Version", v, 24)) lstrcpynW(m.version, v, 24);
        if (IniValue(ini, "Description", v, 200)) lstrcpynW(m.description, v, 200);
        VirtualFree(ini, 0, MEM_RELEASE);
    }
    lstrcpyW(path, base);
    lstrcatW(path, L"text\\");
    m.textJson = false;
    m.textFiles = 0;
    if (Exists(path)) {
        wchar_t json[MAX_PATH + 32];
        lstrcpyW(json, path);
        lstrcatW(json, L"mes_all.json");
        m.textJson = Exists(json);
        m.textFiles = CountFiles(path, L"*.txt");
    }
    lstrcpyW(path, base);
    lstrcatW(path, L"textures\\");
    m.textures = Exists(path) ? CountFiles(path, L"*.dds") + CountFiles(path, L"*.png") : 0;
    m.files = CountTree(base, 0);  // game files lie right in the mod folder (or in files\\, the same)
}

int FindMod(const wchar_t* folder) {
    for (int i = 0; i < g_count; ++i)
        if (SameText(g_mods[i].folder, folder)) return i;
    return -1;
}

void OrderString(wchar_t* out, int capacity) {
    out[0] = 0;
    for (int i = 0; i < g_count; ++i) {
        if (lstrlenW(out) + lstrlenW(g_mods[i].folder) + 3 >= capacity) break;
        lstrcatW(out, g_mods[i].enabled ? L"+" : L"-");
        lstrcatW(out, g_mods[i].folder);
        lstrcatW(out, L"|");
    }
}

void Save() {
    wchar_t path[MAX_PATH + 16];
    ModsDir(path);
    CreateDirectoryW(path, nullptr);
    lstrcatW(path, L"mods.txt");
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        Log("mods: UltimateFix\\mods\\mods.txt could not be written: the order and the switches are not kept");
        return;
    }
    static const char head[] = "# UltimateFIXDP mods: one per line, + on, - off; a mod higher up wins over the ones below it.\r\n";
    DWORD w;
    WriteFile(f, head, sizeof(head) - 1, &w, nullptr);
    for (int i = 0; i < g_count; ++i) {
        char line[300];
        line[0] = g_mods[i].enabled ? '+' : '-';
        int n = WideCharToMultiByte(CP_UTF8, 0, g_mods[i].folder, -1, line + 1, (int)sizeof(line) - 4, nullptr, nullptr);
        if (n <= 0) continue;
        line[n] = '\r';  // n counts the terminating zero, and the text starts one byte in: the zero's place is line[n]
        line[n + 1] = '\n';
        WriteFile(f, line, (DWORD)(n + 2), &w, nullptr);
    }
    CloseHandle(f);
}

// The order and the switches from mods.txt: listed mods go first in that order, the rest keep the order they were found in.
void ApplySaved() {
    wchar_t path[MAX_PATH + 16];
    ModsDir(path);
    lstrcatW(path, L"mods.txt");
    DWORD size = 0;
    char* text = ReadSmall(path, 256 * 1024, &size);
    if (!text) return;
    int next = 0;  // where the next listed mod goes
    const char* p = text;
    if ((u8)p[0] == 0xEF && (u8)p[1] == 0xBB && (u8)p[2] == 0xBF) p += 3;
    while (*p) {
        const char* line = p;
        while (*p && *p != '\n') ++p;
        const char* end = p;
        if (*p) ++p;
        while (end > line && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) --end;
        if (end - line < 2 || (*line != '+' && *line != '-')) continue;
        wchar_t folder[64];
        Utf8ToWide(line + 1, (int)(end - line - 1), folder, 64);
        const int i = FindMod(folder);
        if (i < next) continue;  // not found (gone), or listed twice
        ModInfo m = g_mods[i];
        m.enabled = *line == '+';
        for (int k = i; k > next; --k) g_mods[k] = g_mods[k - 1];
        g_mods[next++] = m;
    }
    VirtualFree(text, 0, MEM_RELEASE);
}

}  // namespace

void ScanMods() {
    if (g_scanned) return;
    g_scanned = true;
    wchar_t dir[MAX_PATH + 16];
    ModsDir(dir);
    CreateDirectoryW(dir, nullptr);  // so the player finds where mods go
    lstrcatW(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(dir, &fd);
    bool tooMany = false, tooLong = false;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            if (lstrlenW(fd.cFileName) >= 64) { tooLong = true; continue; }
            if (g_count >= kMaxMods) { tooMany = true; continue; }
            ModInfo& m = g_mods[g_count++];
            lstrcpyW(m.folder, fd.cFileName);
            m.enabled = true;
            Describe(m);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // a stable start before mods.txt reorders: alphabetical, like Explorer shows them
    for (int i = 1; i < g_count; ++i)
        for (int k = i; k > 0 && lstrcmpiW(g_mods[k - 1].folder, g_mods[k].folder) > 0; --k) {
            ModInfo t = g_mods[k];
            g_mods[k] = g_mods[k - 1];
            g_mods[k - 1] = t;
        }
    ApplySaved();
    if (g_count) Save();  // new folders get their line, gone ones lose it
    OrderString(g_loadedOrder, (int)(sizeof(g_loadedOrder) / sizeof(wchar_t)));
    g_textMod = -1;
    int texts = 0;
    for (int i = 0; i < g_count; ++i) {
        const ModInfo& m = g_mods[i];
        if (!m.enabled || !(m.textJson || m.textFiles)) continue;
        if (g_textMod < 0) g_textMod = i;
        ++texts;
    }
    int on = 0;
    for (int i = 0; i < g_count; ++i) on += g_mods[i].enabled ? 1 : 0;
    Log("mods: %d found in UltimateFix\\mods, %d on%s", g_count, on, tooMany ? " (only the first 32 are used)" : "");
    for (int i = 0; i < g_count; ++i) {
        const ModInfo& m = g_mods[i];
        Log("mods:   %d. %s %S%s%S: text %s, %d texture files, %d game files", i + 1, m.enabled ? "on " : "off", m.name, m.version[0] ? " " : "",
            m.version, m.textJson ? "mes_all.json" : (m.textFiles ? "corrections" : "none"), m.textures, m.files);
    }
    if (tooLong) Log("mods: a folder with a name of 64 or more characters was skipped");
    if (texts > 1) Log("mods: %d mods carry text: only the first of them (%S) is used, text is not mixed from several mods", texts, g_mods[g_textMod].name);
}

int ModCount() { return g_count; }
ModInfo* ModAt(int i) { return i >= 0 && i < g_count ? &g_mods[i] : nullptr; }
int ModTextSource() { return g_textMod; }

void ModToggle(int i) {
    if (i < 0 || i >= g_count) return;
    g_mods[i].enabled = !g_mods[i].enabled;
    Save();
    Log("mods: %S switched %s (from the next start)", g_mods[i].name, g_mods[i].enabled ? "on" : "off");
}

void ModMove(int i, int dir) {
    const int j = i + dir;
    if (i < 0 || i >= g_count || j < 0 || j >= g_count) return;
    ModInfo t = g_mods[i];
    g_mods[i] = g_mods[j];
    g_mods[j] = t;
    Save();
}

bool ModsChanged() {
    wchar_t now[1200];
    OrderString(now, (int)(sizeof(now) / sizeof(wchar_t)));
    return lstrcmpW(now, g_loadedOrder) != 0;
}

void ModTextDir(wchar_t* out) {
    if (g_textMod >= 0) {
        ModsDir(out);
        lstrcatW(out, g_mods[g_textMod].folder);
        lstrcatW(out, L"\\text\\");
    } else {
        lstrcpyW(out, g_paths.dataDir);
        lstrcatW(out, L"text\\");
    }
}

int ModTextureDirs(wchar_t (*out)[MAX_PATH], int max) {
    int n = 0;
    for (int i = 0; i < g_count && n < max; ++i) {
        if (!g_mods[i].enabled || !g_mods[i].textures) continue;
        ModsDir(out[n]);
        lstrcatW(out[n], g_mods[i].folder);
        lstrcatW(out[n], L"\\textures\\");
        ++n;
    }
    return n;
}

int ModFileDirs(wchar_t (*out)[MAX_PATH], int max) {
    int n = 0;
    for (int i = 0; i < g_count && n < max; ++i) {
        if (!g_mods[i].enabled || !g_mods[i].files) continue;
        ModsDir(out[n]);
        lstrcatW(out[n], g_mods[i].folder);
        lstrcatW(out[n], L"\\");
        ++n;
    }
    return n;
}

void OpenModsFolder() {
    wchar_t explorer[MAX_PATH + 16], command[3 * MAX_PATH], folder[MAX_PATH + 16];
    const UINT n = GetWindowsDirectoryW(explorer, MAX_PATH);
    bool ok = n > 0 && n < MAX_PATH;
    if (ok) {
        lstrcatW(explorer, L"\\explorer.exe");
        ModsDir(folder);
        CreateDirectoryW(folder, nullptr);
        folder[lstrlenW(folder) - 1] = 0;  // a backslash in front of the closing quote would swallow it
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
    Log("mods: the mods folder %s", ok ? "was opened in Explorer" : "could not be opened (Explorer did not start)");
}

}  // namespace ufx
