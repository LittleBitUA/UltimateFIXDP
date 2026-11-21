// Texture replacement by content hash, compatible with the packs made for DPfix and ZachFix.
//
// The game creates every texture through D3DXCreateTextureFromFileInMemoryEx. The name of a replacement is the 8-digit
// hexadecimal hash of the original data: SuperFastHash (Paul Hsieh's public algorithm, written out here) over the data
// and its size; for the DDS images embedded in the exe, whose size is passed as 7FFFFFFFh, over Width*Height/2 bytes.
// Files are looked for in
//     UltimateFix/textures/override, dpfix/tex_override, ZachFix/textures/override (under the game folder)
// as <hash>.dds or <hash>.png. The folders are indexed once at start, and only with TextureOverride on. The same hook also
// serves GlyphTheme (the pad icon atlas) and DumpTextures, so it is installed for those even when no pack is present.
#include "../base.h"
#include "../hash.h"
#include "../inflate.h"

namespace ufx {
namespace {

struct Entry {
    u32 hash;
    u8 dir;  // index into g_dirs
    u8 png;  // 1 = .png only, 0 = .dds
};

constexpr int kMaxEntries = 120000;
constexpr int kOwnDirs = 3;
const wchar_t* const kDirNames[kOwnDirs] = {L"UltimateFix\\textures\\override\\", L"dpfix\\tex_override\\",
                                            L"ZachFix\\textures\\override\\"};
// the textures folders of the mods that are on (mods.cpp, in their order), then the three folders above
wchar_t g_dirs[kMaxMods + kOwnDirs][MAX_PATH];
int g_dirCount = 0;
Entry* g_index = nullptr;
int g_count = 0;
u32 g_applied = 0;

typedef HRESULT(WINAPI* FromMemoryExFn)(void*, const void*, UINT, UINT, UINT, UINT, DWORD, DWORD, DWORD, DWORD, DWORD,
                                        DWORD, void*, void*, void**);
typedef HRESULT(WINAPI* FromFileExWFn)(void*, const wchar_t*, UINT, UINT, UINT, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                       void*, void*, void**);
FromMemoryExFn g_real = nullptr;
typedef HRESULT(WINAPI* FromMemoryCubeFn)(void*, const void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, void*, void*,
                                          void**);
FromMemoryCubeFn g_realCube = nullptr;
void DumpTexture(u32 hash, const void* data, UINT size);
u32 DdsBytes(const void* data);
HRESULT WINAPI H_CubeFromMemoryEx(void* dev, const void* data, UINT size, UINT edge, UINT mips, DWORD usage, DWORD fmt, DWORD pool,
                                  DWORD filter, DWORD mipFilter, DWORD colorKey, void* info, void* palette, void** out) {
    if (data) {
        const UINT real = size == 0x7FFFFFFFu ? DdsBytes(data) : size;
        if (real >= 128) DumpTexture(SuperFastHash((const u8*)data, (int)real), data, real);
    }
    return g_realCube(dev, data, size, edge, mips, usage, fmt, pool, filter, mipFilter, colorKey, info, palette, out);
}
FromFileExWFn g_fromFile = nullptr;

bool ParseHash(const wchar_t* name, u32* hash, bool* png) {
    u32 h = 0;
    int i = 0;
    for (; i < 8; ++i) {
        wchar_t c = name[i];
        u32 v;
        if (c >= L'0' && c <= L'9') v = (u32)(c - L'0');
        else if (c >= L'a' && c <= L'f') v = (u32)(c - L'a' + 10);
        else if (c >= L'A' && c <= L'F') v = (u32)(c - L'A' + 10);
        else return false;
        h = (h << 4) | v;
    }
    if (name[i] != L'.') return false;
    const wchar_t* ext = name + i + 1;
    if (lstrcmpiW(ext, L"dds") == 0) *png = false;
    else if (lstrcmpiW(ext, L"png") == 0) *png = true;
    else return false;
    *hash = h;
    return true;
}

// Order: hash, then dds before png, then the folder (own folder first). A shell sort is not stable, so the folder has to be
// a key of its own, or the same <hash>.dds in two packs could come out in either order.
bool After(const Entry& a, const Entry& b) {
    if (a.hash != b.hash) return a.hash > b.hash;
    if (a.png != b.png) return a.png > b.png;
    return a.dir > b.dir;
}

void SortIndex() {  // shell sort
    for (int gap = g_count / 2; gap > 0; gap /= 2)
        for (int i = gap; i < g_count; ++i) {
            Entry t = g_index[i];
            int j = i;
            while (j >= gap && After(g_index[j - gap], t)) {
                g_index[j] = g_index[j - gap];
                j -= gap;
            }
            g_index[j] = t;
        }
}

void ScanDir(int dir) {
    wchar_t pattern[MAX_PATH + 8];
    lstrcpyW(pattern, g_dirs[dir]);
    lstrcatW(pattern, L"*.*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        u32 hash;
        bool png;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && ParseHash(fd.cFileName, &hash, &png) && g_count < kMaxEntries)
            g_index[g_count++] = {hash, (u8)dir, (u8)png};
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

const Entry* Find(u32 hash) {
    int lo = 0, hi = g_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_index[mid].hash < hash) lo = mid + 1;
        else if (g_index[mid].hash > hash) hi = mid - 1;
        else {
            while (mid > 0 && g_index[mid - 1].hash == hash) --mid;  // the first of equal hashes (dds, own folder first)
            return &g_index[mid];
        }
    }
    return nullptr;
}

// The byte size of a DDS file from its header (several levels, the six faces of a cube, the slices of a volume), or 0 when the
// format is not one this knows. The game hands some textures over with the size 0x7FFFFFFF (the menus, the add-ons, the DDS kept
// inside DP.exe): their true size is only in the header.
u32 DdsBytes(const void* data) {
    const u32* d = (const u32*)data;
    if (!d || d[0] != 0x20534444u /*"DDS "*/ || d[1] != 124) return 0;
    const u32 height = d[3], width = d[4], depth = d[6], fourCC = d[21], pfFlags = d[20], bits = d[22], caps2 = d[28];
    if (!width || !height || width > 16384 || height > 16384) return 0;
    u32 block = 0, pixel = 0;  // bytes per 4x4 block, or per pixel
    if (pfFlags & 4 /*DDPF_FOURCC*/) {
        switch (fourCC) {
            case 0x31545844: /*DXT1*/ case 0x31495441: /*ATI1*/ block = 8; break;
            case 0x32545844: case 0x33545844: case 0x34545844: case 0x35545844: /*DXT2..5*/ case 0x32495441: /*ATI2*/ block = 16; break;
            case 111: pixel = 2; break;                       // R16F
            case 112: case 114: pixel = 4; break;             // G16R16F, R32F
            case 36: case 113: case 115: pixel = 8; break;    // A16B16G16R16, A16B16G16R16F, G32R32F
            case 116: pixel = 16; break;                      // A32B32G32R32F
            default: return 0;
        }
    } else {
        pixel = bits / 8;
        if (!pixel || pixel > 16) return 0;
    }
    const u32 levels = (d[2] & 0x20000 /*MIPMAPCOUNT*/) && d[7] ? (d[7] > 16 ? 16 : d[7]) : 1;
    u32 faces = 1;
    if (caps2 & 0x200 /*CUBEMAP*/) {
        faces = 0;
        for (u32 f = 0x400; f <= 0x8000; f <<= 1) faces += (caps2 & f) ? 1 : 0;
        if (!faces) faces = 6;
    }
    const bool volume = (caps2 & 0x200000) && depth > 1;
    u64 total = 0;
    for (u32 i = 0; i < levels; ++i) {
        u32 w = width >> i, h = height >> i, z = volume ? depth >> i : 1;
        if (!w) w = 1;
        if (!h) h = 1;
        if (!z) z = 1;
        const u64 one = block ? (u64)((w + 3) / 4) * ((h + 3) / 4) * block : (u64)w * h * pixel;
        total += one * z;
    }
    total = total * faces + 128;
    return total > (256u << 20) ? 0 : (u32)total;
}

// Diagnostic: writes the original DDS data of every texture the game loads to UltimateFix\textures\dump\<hash>.dds (once each),
// so textures can be found and replaced. The name is the hash a replacement is looked up by.
u32 g_dumped = 0;
void DumpTexture(u32 hash, const void* data, UINT size) {
    if (size == 0x7FFFFFFFu) size = DdsBytes(data);  // the size is only in the header
    if (g_dumped >= 100000 || size < 128 || size > (256u << 20)) return;
    wchar_t dir[MAX_PATH], path[MAX_PATH + 24];
    lstrcpyW(dir, g_paths.dataDir);
    lstrcatW(dir, L"textures");
    CreateDirectoryW(dir, nullptr);
    lstrcatW(dir, L"\\dump");
    CreateDirectoryW(dir, nullptr);
    wsprintfW(path, L"%s\\%08x.dds", dir, hash);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;  // exists already
    DWORD w;
    WriteFile(f, data, size, &w, nullptr);
    CloseHandle(f);
    if (++g_dumped <= 3) Log("textures: dumping to %S", dir);
    if (g_dumped % 500 == 0) Log("textures: %u textures dumped so far", g_dumped);
}

// ---- Interface icon atlas ---------------------------------------------------------------------------------------
// The game builds one atlas of button icons at start: 1024x1408 for the keyboard, 1024x1024 for the pad, and changes
// only the coordinates when the input mode changes (USEJOY). Started with the keyboard, it has no pad atlas, so
// every pad icon is read from the keyboard atlas at the wrong place. We keep the pad theme in a texture of our own
// and hand it to the game in place of the keyboard atlas whenever the pad mode is on.
void* g_keyboardAtlas = nullptr;
void* g_gamePadAtlas = nullptr;  // the game's own pad atlas, when it started in pad mode
u8* g_padFile = nullptr;         // the unpacked DDS of the game's BTNWIDE.XPC (ReadGamePadFile), or nullptr
size_t g_padFileSize = 0;
bool g_padFileTried = false;
void* g_padTheme[2] = {nullptr, nullptr};  // 0 Xbox, 1 PlayStation, each loaded the first time it is wanted
bool g_themeTried[2] = {false, false};
int g_themeShown = -1;

void NoteAtlas(void* tex) {
    typedef HRESULT(WINAPI * GetDescFn)(void*, UINT, u32*);
    u32 d[8] = {};
    ((GetDescFn)(*(void***)tex)[17])(tex, 0, d);  // IDirect3DTexture9::GetLevelDesc: Format, Type, Usage, Pool, ..., Width, Height
    if (d[6] == 1024 && d[7] == 1408 && !g_keyboardAtlas) {
        g_keyboardAtlas = tex;
        Log("textures: keyboard icon atlas found (1024x1408), pad icons will be swapped in with the pad mode");
    }
}

// ---- Load timing (log only) -------------------------------------------------------------------------------------
// What a texture load costs in this hook (the hash when a pack or the dump needs it, then D3DX), so that the slow pause map
// and the cost of GenerateMips can be read from the log: with mipmaps made D3DX decodes the DXT data, filters every level
// and encodes it again on the CPU of the thread that loads.
// Three kinds (kLoadKinds): mipmaps made, as the game gave it, a file of a pack or a mod. A window starts at a load and is
// written once it is a second old (by the next load or by the Present hook), so there is at most one line a second and none
// while nothing loads. A run of loads ends after 2 s without one; a run that took more than one window gets a summary line.
// Loads come from the game's loading thread and the window thread, so the counters sit behind a small spin lock.
struct LoadWindow {
    u64 first, last;            // ticks of the first and the last load in it
    u32 loads[kLoadKinds];
    u32 micros[kLoadKinds];
    u32 worstUs, worstW, worstH;
    int worstKind;
    DWORD worstThread;
    bool otherThreads;          // some load in it ran on another thread than the worst one
    u32 windows;                // windows it took (only the run uses this)
};
const char* const kKindNames[kLoadKinds] = {"with mipmaps made", "as given", "from a pack"};
constexpr u32 kWindowUs = 1000000, kRunGapUs = 2000000;
constexpr u32 kMaxWindowLines = 600, kMaxRunLines = 300;
volatile LONG g_statLock = 0;
volatile LONG g_statBusy = 0;  // a window is open
volatile LONG g_runOpen = 0;   // a run has loads: the Present hook takes the lock only while one of the two is set
bool g_statHooked = false;     // the texture hook is in
LoadWindow g_window, g_run;
u32 g_totalLoads[kLoadKinds], g_totalUs[kLoadKinds];  // since the start, wrapping (map_fix.cpp takes differences)
u32 g_windowLines = 0, g_runLines = 0;

void StatLock() {
    while (InterlockedExchange(&g_statLock, 1)) Sleep(0);
}
void StatUnlock() { InterlockedExchange(&g_statLock, 0); }

void Add(LoadWindow& win, int kind, u64 started, u64 now, u32 us, u32 width, u32 height, DWORD thread) {
    if (!win.loads[0] && !win.loads[1] && !win.loads[2]) win.first = started;
    win.last = now;
    ++win.loads[kind];
    win.micros[kind] += us;
    if (us >= win.worstUs) {
        if (win.worstThread && win.worstThread != thread) win.otherThreads = true;
        win.worstUs = us;
        win.worstW = width;
        win.worstH = height;
        win.worstKind = kind;
        win.worstThread = thread;
    } else if (win.worstThread != thread) {
        win.otherThreads = true;
    }
}

// Writes one window or run; `what` begins the line. Called outside the lock with a copy.
void LogLoads(const char* what, const LoadWindow& win) {
    const u32 span = TickMicros(win.first, win.last);
    char parts[kLoadKinds][64];
    for (int k = 0; k < kLoadKinds; ++k)
        wsprintfA(parts[k], "%u %s %u.%u ms", win.loads[k], kKindNames[k], MsWhole(win.micros[k]), MsTenth(win.micros[k]));
    const HWND window = GameWindowHandle();
    const DWORD windowThread = window ? GetWindowThreadProcessId(window, nullptr) : 0;
    Log("textures: %s %u.%u s: %s, %s, %s; worst %u.%u ms (%ux%u %s) on thread %u (%s%s)", what, span / 1000000, span / 100000 % 10,
        parts[0], parts[1], parts[2], MsWhole(win.worstUs), MsTenth(win.worstUs), win.worstW, win.worstH, kKindNames[win.worstKind],
        (u32)win.worstThread, !windowThread ? "no game window yet" : win.worstThread == windowThread ? "the window thread" : "not the window thread",
        win.otherThreads ? ", other threads loaded too" : "");
}

// Under the lock: takes the window when it is a second old at `now`, and the run when its last load is 2 s before `now`
// (the window is older still, so it has gone first). Logs outside the lock, from copies.
void FlushLoads(u64 now) {
    LoadWindow window = {}, run = {};
    bool logWindow = false, logRun = false;
    StatLock();
    if (g_statBusy && TickMicros(g_window.first, now) >= kWindowUs) {
        window = g_window;
        g_window = LoadWindow{};
        g_statBusy = 0;
        ++g_run.windows;
        logWindow = g_windowLines < kMaxWindowLines;
        if (logWindow) ++g_windowLines;
    }
    if (g_runOpen && !g_statBusy && TickMicros(g_run.last, now) >= kRunGapUs) {
        run = g_run;
        g_run = LoadWindow{};
        g_runOpen = 0;
        logRun = run.windows > 1 && g_runLines < kMaxRunLines;  // one window: its own line said it all
        if (logRun) ++g_runLines;
    }
    StatUnlock();
    if (logWindow) LogLoads("loads in", window);
    if (logWindow && g_windowLines == kMaxWindowLines) Log("textures: no more per-second load lines this run (%u), runs of loads are still summed up", kMaxWindowLines);
    if (logRun) LogLoads("a run of loads ended, in all over", run);
}

void NoteLoad(int kind, u64 started, const void* data, UINT size) {
    const u64 now = TickNow();
    const u32 us = TickMicros(started, now);
    u32 width = 0, height = 0;
    if (data && size >= 128 && size != 0x7FFFFFFFu && ((const u32*)data)[0] == 0x20534444u /*"DDS "*/) {  // the size marker: no header
        width = ((const u32*)data)[4];
        height = ((const u32*)data)[3];
    }
    const DWORD thread = GetCurrentThreadId();
    FlushLoads(started);  // a window a second old, or a run 2 s quiet, ends before this load (the Present hook may not run in a loading screen)
    StatLock();
    Add(g_window, kind, started, now, us, width, height, thread);
    Add(g_run, kind, started, now, us, width, height, thread);
    g_statBusy = 1;
    g_runOpen = 1;
    ++g_totalLoads[kind];
    g_totalUs[kind] += us;
    StatUnlock();
}

HRESULT FromMemoryInner(void* dev, const void* data, UINT size, UINT w, UINT h, UINT mips, DWORD usage, DWORD fmt, DWORD pool,
                        DWORD filter, DWORD mipFilter, DWORD colorKey, void* info, void* palette, void** out);

// The keyboard icon atlas is recognised by the game's own file (a DDS of 1024x1408), before any replacement: a texture pack may
// replace that atlas with a picture of another size, and the replacement path returned before the check, so with such a pack the
// pad icons were never swapped in.
HRESULT WINAPI H_FromMemoryEx(void* dev, const void* data, UINT size, UINT w, UINT h, UINT mips, DWORD usage, DWORD fmt,
                              DWORD pool, DWORD filter, DWORD mipFilter, DWORD colorKey, void* info, void* palette,
                              void** out) {
    const bool atlasFile = data && size >= 128 && size != 0x7FFFFFFFu && ((const u32*)data)[0] == 0x20534444u /*"DDS "*/ &&
                           ((const u32*)data)[3] == 1408 && ((const u32*)data)[4] == 1024;
    // Started in pad mode the game loads its pad atlas (BTNWIDE.XPC from the disk, 256x256) instead, and the keyboard one never
    // comes: that load is recognised by its bytes, the same as the file next to DP.exe.
    const size_t padCmp = g_padFile && data && size != 0x7FFFFFFFu ? (size < g_padFileSize ? size : g_padFileSize) : 0;
    const bool padFile = padCmp >= 4096 && !__builtin_memcmp(data, g_padFile, padCmp);
    const HRESULT hr = FromMemoryInner(dev, data, size, w, h, mips, usage, fmt, pool, filter, mipFilter, colorKey, info, palette, out);
    if (atlasFile && hr >= 0 && out && *out && g_cfg.glyphTheme && !g_keyboardAtlas) {
        g_keyboardAtlas = *out;
        Log("textures: keyboard icon atlas found (the game's 1024x1408 file), pad icons will be swapped in with the pad mode");
    }
    if (padFile && hr >= 0 && out && *out && g_cfg.glyphTheme && !g_gamePadAtlas) {
        g_gamePadAtlas = *out;
        Log("textures: the game's pad icon atlas found (started in pad mode), PlayStation icons will be swapped in for a DualSense");
    }
    return hr;
}

HRESULT FromMemoryInner(void* dev, const void* data, UINT size, UINT w, UINT h, UINT mips, DWORD usage, DWORD fmt, DWORD pool,
                        DWORD filter, DWORD mipFilter, DWORD colorKey, void* info, void* palette, void** out) {
    // One start for every path below: a file that cannot be loaded and the original made after it are one load to the game.
    const u64 started = TickNow();
    if (data && size != 0x7FFFFFFFu) {  // a texture of an XPC by name from a mod (game_files.cpp), exact size from the file
        wchar_t named[MAX_PATH + 16];
        if (GameFileNextTexture(named)) {
            HRESULT hr = g_fromFile(dev, named, 0xFFFFFFFEu /*D3DX_DEFAULT_NONPOW2*/, 0xFFFFFFFEu, mips, usage, fmt, pool, filter,
                                    mipFilter, colorKey, info, palette, out);
            if (hr >= 0) {
                NoteLoad(2, started, data, size);  // the size of the game's own picture, as for a pack below
                Log("textures: %S used by name", named);
                return hr;
            }
            Log("textures: %S could not be loaded (%08X), the original kept", named, (u32)hr);
        }
    }
    UINT hashed = size == 0x7FFFFFFFu ? (w * h / 2) : size;
    const bool dump = g_cfg.dumpTextures != 0;
    // one pass over the data, and only when something needs the hash (a pack or the dump)
    const u32 hash = (data && hashed && (g_count || dump)) ? SuperFastHash((const u8*)data, (int)hashed) : 0;
    if (dump && data && hashed) DumpTexture(hash, data, size);
    if (g_count && data && hashed) {
        const Entry* e = Find(hash);
        if (e) {
            wchar_t path[MAX_PATH + 16];
            // e->dir indexes g_dirs (the mods' textures folders first, then the own three), which hold full paths. It used to index
            // kDirNames, so a mod's texture was looked for in UltimateFix\textures\override\ and failed (D3DXERR_INVALIDDATA).
            wsprintfW(path, L"%s%08x.%s", g_dirs[e->dir], hash, e->png ? L"png" : L"dds");
            HRESULT hr = g_fromFile(dev, path, 0xFFFFFFFFu, 0xFFFFFFFFu, mips, usage, fmt, pool, filter, mipFilter, colorKey,
                                    info, palette, out);
            if (hr >= 0) {
                NoteLoad(2, started, data, size);  // the size of the game's picture: a pack's PNG has no DDS header to read
                if (++g_applied <= 20 || g_applied % 500 == 0) Log("textures: override %08x applied (%u so far)", hash, g_applied);
                return hr;
            }
            Log("textures: override %08x could not be loaded (%08X), original kept", hash, (u32)hr);
        }
    }
    // A world texture stored without mipmaps is drawn far away from its full-size picture: it shimmers and flickers as the
    // camera moves. With GenerateMips D3DX builds the smaller levels on load (box filter). Only plain DDS textures of 128 or
    // more on each side that carry one level; render targets, cube maps and the 2D atlases of the interface are left alone.
    if (g_cfg.generateMips && mips <= 1 && data && size >= 128 && size != 0x7FFFFFFFu && usage == 0) {
        const u32* dds = (const u32*)data;
        const u32 height = dds[3], width = dds[4], levels = dds[7], caps2 = dds[28];
        const bool square2 = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;  // the atlases are not powers of two
        if (dds[0] == 0x20534444 /*"DDS "*/ && levels <= 1 && width >= 128 && height >= 128 && square2 && !(caps2 & 0x200) /*cube*/) {
            HRESULT hr = g_real(dev, data, size, w, h, 0 /*all levels*/, usage, fmt, pool, filter, 5 /*D3DX_FILTER_BOX*/, colorKey, info,
                                palette, out);
            if (hr >= 0) {
                NoteLoad(0, started, data, size);
                static u32 made = 0;
                if (++made <= 5 || made % 200 == 0) Log("textures: mipmaps made for a %ux%u texture without them (%u so far)", width, height, made);
                return hr;
            }
        }
    }
    HRESULT real = g_real(dev, data, size, w, h, mips, usage, fmt, pool, filter, mipFilter, colorKey, info, palette, out);
    NoteLoad(1, started, data, size);
    if (real >= 0 && out && *out && g_cfg.glyphTheme && !g_keyboardAtlas && size != 0x7FFFFFFFu) NoteAtlas(*out);
    return real;
}

void* GlyphAtlasForImpl(void* dev, void* tex);

}  // namespace

void* GlyphAtlasFor(void* dev, void* tex) { return GlyphAtlasForImpl(dev, tex); }

bool TextureLoadTotals(u32* loads, u32* micros) {
    if (!g_statHooked) return false;
    StatLock();
    for (int k = 0; k < kLoadKinds; ++k) {
        loads[k] = g_totalLoads[k];
        micros[k] = g_totalUs[k];
    }
    StatUnlock();
    return true;
}

void TextureLoadFrame() {
    if (!g_statHooked || (!g_statBusy && !g_runOpen)) return;  // nothing to write: no lock taken in the common frame
    FlushLoads(TickNow());
}

namespace {

// The game's own pad icons (the Xbox ones): UPDATA\SOURCE\MESSAGE\BTNWIDE.XPC next to DP.exe, which the game itself loads (through
// CAddon, in place of the 1024x1408 keyboard atlas) only when it starts in pad mode. An XPC2 with a zlib DDS of 256x256 DXT3 (cells of
// 32 px), unpacked here once: it is made a texture as the game would, and it tells the game's own load of that file apart.

void ReadGamePadFile() {
    if (g_padFileTried) return;
    g_padFileTried = true;
    wchar_t path[MAX_PATH + 48];
    wsprintfW(path, L"%sUPDATA\\SOURCE\\MESSAGE\\BTNWIDE.XPC", g_paths.gameDir);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        Log("textures: the game's pad icons %S are not there", path);
        return;
    }
    DWORD size = GetFileSize(f, nullptr), got = 0;
    u8* xpc = (size > 0x60 && size < (8u << 20)) ? (u8*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    const bool read = xpc && ReadFile(f, xpc, size, &got, nullptr) && got == size;
    CloseHandle(f);
    size_t need = 0;
    if (read) XpcFirstPicture(xpc, size, nullptr, 0, &need);
    u8* dds = (need >= 128 && need < (16u << 20)) ? (u8*)VirtualAlloc(nullptr, need, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    const size_t n = dds ? XpcFirstPicture(xpc, size, dds, need, nullptr) : 0;
    if (xpc) VirtualFree(xpc, 0, MEM_RELEASE);
    if (n >= 128 && ((const u32*)dds)[0] == 0x20534444u /*"DDS "*/) {
        g_padFile = dds;
        g_padFileSize = n;
        Log("textures: the game's pad icons read (%S, DDS %ux%u, %u bytes)", path, ((const u32*)dds)[4], ((const u32*)dds)[3], (u32)n);
    } else {
        if (dds) VirtualFree(dds, 0, MEM_RELEASE);
        Log("textures: the game's pad icons %S could not be read (%u bytes unpacked)", path, (u32)n);
    }
}

void* LoadGamePadAtlas(void* dev) {
    void* tex = nullptr;
    if (g_padFile && g_real &&
        SUCCEEDED(g_real(dev, g_padFile, (UINT)g_padFileSize, 0xFFFFFFFEu, 0xFFFFFFFEu, 1, 0, 0 /*the file's format*/, 1 /*MANAGED*/, 1, 1, 0,
                         nullptr, nullptr, &tex)) && tex)
        Log("textures: the game's own pad icons made a texture");
    return tex;
}

void* GlyphAtlasForImpl(void* dev, void* tex) {
    static bool s_noAtlas = false;
    if (!g_keyboardAtlas && !g_gamePadAtlas && !s_noAtlas && *(volatile u8*)(uintptr_t)0x014810F0) {
        s_noAtlas = true;
        Log("textures: pad mode, but neither icon atlas of the game was recognised at its load (keyboard 1024x1408, pad BTNWIDE.XPC): "
            "the icons stay as the game draws them");
    }
    if (!tex || (tex != g_keyboardAtlas && tex != g_gamePadAtlas) || !*(volatile u8*)(uintptr_t)0x014810F0) return tex;
    // Asked again on every use: a DualSense over Bluetooth is found up to a few seconds after the start (the reader looks every
    // 2 s), so a choice made once at the first draw kept the Xbox icons for good. Both sets are loaded once and kept.
    const int ps = g_cfg.glyphTheme == 2 ? 0 : g_cfg.glyphTheme == 3 ? 1 : (SonyPadActive() ? 1 : 0);
    if (!g_themeTried[ps]) {
        g_themeTried[ps] = true;
        const wchar_t* name = ps ? L"playstation" : L"xbox";
        // our drawing of both sets (the same look for Xbox and PlayStation); the game's own Xbox atlas only when xbox.png is not there
        wchar_t path[MAX_PATH + 40];
        wsprintfW(path, L"%sUltimateFix\\glyphs\\gamepad\\%s.png", g_paths.gameDir, name);
        if (g_fromFile && SUCCEEDED(g_fromFile(dev, path, 1024, 1024, 1, 0, 21 /*A8R8G8B8*/, 1 /*MANAGED*/, 1, 1, 0, nullptr, nullptr,
                                                 &g_padTheme[ps])) && g_padTheme[ps])
            Log("textures: pad icons loaded: %S", path);
        else if (!ps && (g_padTheme[0] = LoadGamePadAtlas(dev)) != nullptr)
            Log("textures: %S could not be loaded, the game's own Xbox icons are used", path);
        else
            Log("textures: pad icons %S could not be loaded, the game's atlas stays", path);
    }
    if (g_padTheme[ps] && g_themeShown != ps) {
        g_themeShown = ps;
        Log("textures: pad icons now %s%s", ps ? "PlayStation" : "Xbox", g_cfg.glyphTheme == 1 ? " (automatic)" : "");
    }
    return g_padTheme[ps] ? g_padTheme[ps] : tex;
}

}  // namespace

bool InstallTextureOverride() {
    g_dirCount = ModTextureDirs(g_dirs, kMaxMods);
    for (int i = 0; i < kOwnDirs; ++i) {
        lstrcpyW(g_dirs[g_dirCount], g_paths.gameDir);
        lstrcatW(g_dirs[g_dirCount++], kDirNames[i]);
    }
    // TextureOverride=0 must switch the packs off even when GlyphTheme or DumpTextures keep this hook installed: the folders are
    // then not indexed at all, g_count stays 0 and the hook never replaces anything
    if (g_cfg.textureOverride) {
        g_index = (Entry*)VirtualAlloc(nullptr, sizeof(Entry) * kMaxEntries, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (g_index) {
            for (int i = 0; i < g_dirCount; ++i) ScanDir(i);
            if (g_count == 0) {  // no pack: give the index memory back
                VirtualFree(g_index, 0, MEM_RELEASE);
                g_index = nullptr;
            }
        }
    } else {
        Log("textures: TextureOverride=0, replacement packs are not used");
    }
    if (g_count == 0 && !g_cfg.dumpTextures && !g_cfg.glyphTheme && !GameFileTextures() && !g_cfg.generateMips) {
        if (g_cfg.textureOverride)
            Log("textures: no replacement pack found (UltimateFix\\textures\\override, dpfix\\tex_override), not hooked");
        return false;
    }
    SortIndex();
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    if (!d3dx) d3dx = LoadLibraryW(L"d3dx9_43.dll");
    g_fromFile = d3dx ? (FromFileExWFn)(void*)GetProcAddress(d3dx, "D3DXCreateTextureFromFileExW") : nullptr;
    void** slot = FindImportSlot(GetModuleHandleW(nullptr), "d3dx9_43.dll", "D3DXCreateTextureFromFileInMemoryEx");
    if (!g_fromFile || !slot) {
        Log("textures: D3DX entry points not found, not hooked");
        return false;
    }
    g_real = (FromMemoryExFn)*slot;
    if (g_cfg.glyphTheme) ReadGamePadFile();
    if (g_cfg.dumpTextures) {  // cube maps (the environment reflections) only for the dump; they are not replaced
        void** cube = FindImportSlot(GetModuleHandleW(nullptr), "d3dx9_43.dll", "D3DXCreateCubeTextureFromFileInMemoryEx");
        if (cube) {
            g_realCube = (FromMemoryCubeFn)*cube;
            Log("textures: cube textures dumped too (%d)", (int)SwapImport(cube, (void*)H_CubeFromMemoryEx, nullptr));
        }
    }
    if (!SwapImport(slot, (void*)H_FromMemoryEx, nullptr)) return false;
    g_statHooked = true;
    Log("textures: %d replacement files indexed, texture loading hooked (what the loads cost is logged once a second while they run)",
        g_count);
    return true;
}

}  // namespace ufx
