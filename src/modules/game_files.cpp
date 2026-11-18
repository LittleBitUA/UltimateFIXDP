// Game files from mods, by name: UltimateFix\mods\<mod>\files\ holds files laid out as the game's resource paths, for example
// files\UPDATA\MESSAGE\FONTWIDE.XPC. Whatever the game loads from its archives (DPSERIAL.001..003) or from the language folders
// is then read from the mod instead. A file that lies right in files\ is found by its name alone when that name is unique among
// the game's files (FONTWIDE.XPC, MES_ALL.MES); a name used in several folders needs the full path.
//
// A .dds or .png next to where an .XPC would be (FONTWIDE.DDS for FONTWIDE.XPC) replaces the texture inside that XPC, so a
// picture can be changed without building the XPC container: the first texture of the XPC is NAME.dds (or NAME_0.dds), the
// next ones NAME_1.dds, NAME_2.dds... The game creates the textures of an XPC on the loader thread right after reading it,
// and the texture hook (texture_override.cpp) asks GameFileNextTexture() for each of them.
//
// How: one jump in the loader FUN_006B33F0 (sites.h, kSiteGameFileBranch), only on LoadThread. A CAddon (DLC) file keeps its
// way. The file is read whole into memory from the game's own malloc, because the game frees slot data with its free. The mods
// are read once at start (the first mod in the list wins); a file the game already holds is used from its next load.
// Not taken: an XZP1-packed file (the archive keeps XMD so; give the unpacked XMD3) and empty files (the game would make no slot).
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

extern "C" void* __cdecl GameFileWorker(const void* addonPath, int requested, int final, ufx::u32* sizeOut);

namespace ufx {
namespace {

constexpr int kMaxOverrides = 2048;
constexpr int kMaxDepth = 8;
constexpr u32 kMaxBytes = 256u << 20;

struct Override {
    wchar_t path[MAX_PATH];  // the file, or for a texture the path without the extension (…\FONTWIDE)
};

Override* g_over = nullptr;    // VirtualAlloc'd
int g_overCount = 0;
u16* g_dataOf = nullptr;       // per resource id: 1 + the override with the file's data, 0 none
u16* g_texOf = nullptr;        // per resource id: 1 + the override with the textures by name, 0 none
u16* g_sorted = nullptr;       // resource ids sorted by path
int g_files = 0, g_textures = 0, g_unknown = 0;
volatile long g_read = 0;

// texture by name: the XPC being created on the loader thread
volatile DWORD g_texThread = 0;
int g_texOver = -1, g_texNext = 0;

const char* PathOf(int id) {
    const u32 va = *(const u32*)(uintptr_t)(kResourceIndexVa + (u32)id * 0x20 + 0x1C);
    return va ? (const char*)(uintptr_t)va : "";
}

int Compare(const char* a, const char* b) {
    while (*a && *a == *b) ++a, ++b;
    return (int)(u8)*a - (int)(u8)*b;
}

bool StartsWith(const char* s, const char* head) {
    while (*head)
        if (*s++ != *head++) return false;
    return true;
}

const char* BaseName(const char* path) {
    const char* b = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/') b = p + 1;
    return b;
}

void SortIds() {  // shell sort by path
    for (int i = 0; i < kResourceCount; ++i) g_sorted[i] = (u16)i;
    for (int gap = kResourceCount / 2; gap > 0; gap /= 2)
        for (int i = gap; i < kResourceCount; ++i) {
            const u16 t = g_sorted[i];
            int j = i;
            while (j >= gap && Compare(PathOf(g_sorted[j - gap]), PathOf(t)) > 0) {
                g_sorted[j] = g_sorted[j - gap];
                j -= gap;
            }
            g_sorted[j] = t;
        }
}

int FindPath(const char* key) {
    int lo = 0, hi = kResourceCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2, c = Compare(PathOf(g_sorted[mid]), key);
        if (c == 0) return g_sorted[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

// A name alone: the one resource outside the language folders (UPDATA_EU/...) with that file name; -1 none, -2 several.
int FindName(const char* name) {
    int found = -1;
    for (int i = 0; i < kResourceCount; ++i) {
        const char* p = PathOf(i);
        if (StartsWith(p, "UPDATA_EU/")) continue;
        if (Compare(BaseName(p), name) != 0) continue;
        if (found >= 0) return -2;
        found = i;
    }
    return found;
}

int Find(const char* key, bool byName) { return byName ? FindName(key) : FindPath(key); }

bool EndsWith(const char* s, int len, const char* tail) {
    const int n = lstrlenA(tail);
    return len >= n && Compare(s + len - n, tail) == 0;
}

int AddOverride(const wchar_t* path) {
    if (g_overCount >= kMaxOverrides) return -1;
    lstrcpynW(g_over[g_overCount].path, path, MAX_PATH);
    return g_overCount++;
}

void Unknown(const wchar_t* file, const char* why) {
    if (++g_unknown <= 30) Log("game files: %S is not used: %s", file, why);
}

// One file of a mod. `key` is its path below files\ in upper case with '/', `file` the full path on disk.
void Take(const char* key, const wchar_t* file) {
    const int len = lstrlenA(key);
    bool byName = true;
    for (int i = 0; i < len; ++i)
        if (key[i] == '/') byName = false;
    // a texture by name: NAME.DDS / NAME_n.DDS (or .PNG) stands for NAME.XPC
    if (EndsWith(key, len, ".DDS") || EndsWith(key, len, ".PNG")) {
        char xpc[300];
        int stem = len - 4;
        bool numbered = false;
        int u = stem;
        while (u > 0 && key[u - 1] >= '0' && key[u - 1] <= '9') --u;
        if (u < stem && u > 0 && key[u - 1] == '_') {
            numbered = true;
            stem = u - 1;
        }
        if (stem <= 0 || stem > 280) return Unknown(file, "the name is too long");
        for (int i = 0; i < stem; ++i) xpc[i] = key[i];
        lstrcpyA(xpc + stem, ".XPC");
        int id = Find(xpc, byName);
        if (id == -2) return Unknown(file, "several game folders have an XPC of that name: put it under its full path (files\\UPDATA\\...)");
        if (id < 0) return Unknown(file, "the game has no XPC of that name");
        if (g_texOf[id]) return;  // a mod higher in the list has it already (or another number of the same XPC)
        wchar_t base[MAX_PATH];
        lstrcpynW(base, file, MAX_PATH);
        int cut = lstrlenW(base) - 4 - (len - 4 - stem);  // the stem on disk keeps its own case
        if (numbered && cut > 0) base[cut] = 0;
        else base[lstrlenW(base) - 4] = 0;
        const int o = AddOverride(base);
        if (o < 0) return Unknown(file, "too many files (2048 at most)");
        g_texOf[id] = (u16)(o + 1);
        ++g_textures;
        return;
    }
    const int id = Find(key, byName);
    if (id == -2) return Unknown(file, "several game folders have a file of that name: put it under its full path (files\\UPDATA\\...)");
    if (id < 0) return Unknown(file, "the game has no file of that path (paths start with UPDATA\\ or UPDATA_EU\\)");
    if (g_dataOf[id]) return;  // a mod higher in the list wins
    const int o = AddOverride(file);
    if (o < 0) return Unknown(file, "too many files (2048 at most)");
    g_dataOf[id] = (u16)(o + 1);
    ++g_files;
}

void Walk(const wchar_t* dir, char* key, int keyLen, int depth) {
    wchar_t pattern[MAX_PATH + 8];
    lstrcpynW(pattern, dir, MAX_PATH);
    lstrcatW(pattern, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        wchar_t full[MAX_PATH + 8];
        if (lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 >= MAX_PATH) continue;
        lstrcpyW(full, dir);
        lstrcatW(full, fd.cFileName);
        // the key: ASCII in upper case; game paths are ASCII only
        int n = keyLen, i = 0;
        bool ascii = true;
        for (; fd.cFileName[i] && n < 290; ++i) {
            wchar_t c = fd.cFileName[i];
            if (c > 0x7E) ascii = false;
            if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
            key[n++] = (char)c;
        }
        key[n] = 0;
        if (!ascii || fd.cFileName[i]) {
            Unknown(full, "game file names are Latin letters only");
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth >= kMaxDepth) continue;
            lstrcatW(full, L"\\");
            key[n] = '/';
            key[n + 1] = 0;
            Walk(full, key, n + 1, depth + 1);
        } else {
            Take(key, full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool IndexLooksRight() {
    return Compare(PathOf(0x2400), "UPDATA/MESSAGE/OUTPUT/MES_ALL.MES") == 0 && Compare(PathOf(0x23FA), "UPDATA/MESSAGE/FONTWIDE.XPC") == 0;
}

}  // namespace

bool GameFileTextures() { return g_textures > 0; }

// From the texture hook, on any thread: the file for the next texture of the XPC the loader is creating, if a mod has one.
bool GameFileNextTexture(wchar_t* path) {
    if (g_texOver < 0 || g_texThread != GetCurrentThreadId()) return false;
    const int n = g_texNext++;
    const wchar_t* base = g_over[g_texOver].path;
    static const wchar_t* const kExt[2] = {L"dds", L"png"};
    for (int e = 0; e < 2; ++e) {
        if (n == 0) {
            wsprintfW(path, L"%s.%s", base, kExt[e]);
            if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return true;
        }
        wsprintfW(path, L"%s_%d.%s", base, n, kExt[e]);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}

bool InstallGameFiles() {
    wchar_t dirs[kMaxMods][MAX_PATH];
    const int n = ModFileDirs(dirs, kMaxMods);
    if (n == 0) return false;
    if (!IndexLooksRight()) {
        Log("game files: the resource index of DP.exe is not the one of 1.01b, files from mods are not used");
        return false;
    }
    const SiteDef& s = kSiteGameFileBranch;
    if (!BytesMatch(s.va, s.bytes, s.len) || !BytesMatch(kSiteGameFileHave.va, kSiteGameFileHave.bytes, kSiteGameFileHave.len) ||
        !BytesMatch(kSiteGameFileStore.va, kSiteGameFileStore.bytes, kSiteGameFileStore.len)) {
        Log("game files: the loader at %08X differs from DP.exe 1.01b (another mod?), files from mods are not used", s.va);
        return false;
    }
    const SIZE_T bytes = sizeof(Override) * kMaxOverrides + 3 * sizeof(u16) * kResourceCount;
    u8* mem = (u8*)VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) return false;
    g_over = (Override*)mem;
    g_dataOf = (u16*)(mem + sizeof(Override) * kMaxOverrides);
    g_texOf = g_dataOf + kResourceCount;
    g_sorted = g_texOf + kResourceCount;
    SortIds();
    char key[300];
    for (int i = 0; i < n; ++i) Walk(dirs[i], key, 0, 0);
    if (g_unknown > 30) Log("game files: %d more files not used", g_unknown - 30);
    if (g_files + g_textures == 0) {
        Log("game files: the mods' files\\ folders hold nothing the game loads");
        VirtualFree(mem, 0, MEM_RELEASE);
        g_over = nullptr;
        return false;
    }
    u8* stub = AllocStub(64);
    if (!stub) return false;
    BuildGameFileHook(stub, (u32)(uintptr_t)stub, (u32)(uintptr_t)GameFileWorker, kSiteGameFileAddon.va, kSiteGameFileArchive.va,
                      kSiteGameFileHave.va);
    if (!PatchJump(s.va, s.bytes, s.len, stub)) return false;
    Log("game files: %d files and %d textures by name from mods replace the game's, loader hooked", g_files, g_textures);
    return true;
}

}  // namespace ufx

// LoadThread, from the loader hook: the data of a mod's file for this resource, or 0 to let the game read its own.
// Called for every load, also for a CAddon (DLC) file, which keeps its own way: so the textures by name of the load before never
// spill over to this one.
extern "C" void* __cdecl GameFileWorker(const void* addonPath, int requested, int final, ufx::u32* sizeOut) {
    using namespace ufx;
    g_texOver = -1;
    if (addonPath || requested < 0 || requested >= kResourceCount) return nullptr;
    const bool finalOk = final >= 0 && final < kResourceCount;
    const int tex = g_texOf[requested] ? g_texOf[requested] : (finalOk ? g_texOf[final] : 0);
    if (tex) {  // the textures of this XPC by name; the data itself may still come from a file below or from the game
        g_texThread = GetCurrentThreadId();
        g_texNext = 0;
        g_texOver = tex - 1;
    }
    const int o = g_dataOf[requested] ? g_dataOf[requested] : (finalOk ? g_dataOf[final] : 0);
    if (!o) return nullptr;
    const wchar_t* path = g_over[o - 1].path;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        Log("game files: %S could not be opened (%u), the game's own file is used", path, GetLastError());
        return nullptr;
    }
    DWORD high = 0, size = GetFileSize(f, &high), got = 0;
    void* data = nullptr;
    char head[4] = {};
    if (high || size == 0 || size > kMaxBytes) {
        Log("game files: %S is empty or too big, the game's own file is used", path);
    } else if (ReadFile(f, head, 4, &got, nullptr) && got == 4 && head[0] == 'X' && head[1] == 'Z' && head[2] == 'P' && head[3] == '1') {
        Log("game files: %S is packed (XZP1): give the unpacked file, the game's own is used", path);
    } else {
        typedef void*(__cdecl * MallocFn)(u32);
        data = ((MallocFn)(uintptr_t)kGameMalloc)(size);
        SetFilePointer(f, 0, nullptr, FILE_BEGIN);
        if (data && (!ReadFile(f, data, size, &got, nullptr) || got != size)) {
            Log("game files: %S could not be read whole, the game's own file is used", path);
            typedef void(__cdecl * FreeFn)(void*);
            ((FreeFn)(uintptr_t)0x74F25D)(data);  // _free
            data = nullptr;
        }
    }
    CloseHandle(f);
    if (!data) return nullptr;
    *sizeOut = size;
    const long r = InterlockedIncrement(&g_read);
    if (r <= 40 || r % 200 == 0) Log("game files: %s from %S (%u bytes)", PathOf(requested), path, size);
    return data;
}
