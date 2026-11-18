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

// Diagnostic: writes the original DDS data of every texture the game loads to UltimateFix\textures\dump\<hash>.dds
// (small ones only, once each), so that the atlas of the interface icons and other textures can be found and replaced.
u32 g_dumped = 0;
void DumpTexture(u32 hash, const void* data, UINT size) {
    if (g_dumped >= 6000 || size < 128 || size > (1u << 20)) return;
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
    if (++g_dumped <= 5) Log("textures: dumping to %S (first of up to 6000)", dir);
}

// ---- Interface icon atlas ---------------------------------------------------------------------------------------
// The game builds one atlas of button icons at start: 1024x1408 for the keyboard, 1024x1024 for the pad, and changes
// only the coordinates when the input mode changes (USEJOY). Started with the keyboard, it has no pad atlas, so
// every pad icon is read from the keyboard atlas at the wrong place. We keep the pad theme in a texture of our own
// and hand it to the game in place of the keyboard atlas whenever the pad mode is on.
void* g_keyboardAtlas = nullptr;
void* g_padTheme = nullptr;
bool g_themeTried = false;

void NoteAtlas(void* tex) {
    typedef HRESULT(WINAPI * GetDescFn)(void*, UINT, u32*);
    u32 d[8] = {};
    ((GetDescFn)(*(void***)tex)[17])(tex, 0, d);  // IDirect3DTexture9::GetLevelDesc: Format, Type, Usage, Pool, ..., Width, Height
    if (d[6] == 1024 && d[7] == 1408 && !g_keyboardAtlas) {
        g_keyboardAtlas = tex;
        Log("textures: keyboard icon atlas found (1024x1408), pad icons will be swapped in with the pad mode");
    }
}

HRESULT WINAPI H_FromMemoryEx(void* dev, const void* data, UINT size, UINT w, UINT h, UINT mips, DWORD usage, DWORD fmt,
                              DWORD pool, DWORD filter, DWORD mipFilter, DWORD colorKey, void* info, void* palette,
                              void** out) {
    UINT hashed = size == 0x7FFFFFFFu ? (w * h / 2) : size;
    const bool dump = g_cfg.dumpTextures && size != 0x7FFFFFFFu;
    // one pass over the data, and only when something needs the hash (a pack or the dump)
    const u32 hash = (data && hashed && (g_count || dump)) ? SuperFastHash((const u8*)data, (int)hashed) : 0;
    if (dump && data && hashed) DumpTexture(hash, data, size);
    if (g_count && data && hashed) {
        const Entry* e = Find(hash);
        if (e) {
            wchar_t path[MAX_PATH + 16];
            wsprintfW(path, L"%s%s%08x.%s", g_paths.gameDir, kDirNames[e->dir], hash, e->png ? L"png" : L"dds");
            HRESULT hr = g_fromFile(dev, path, 0xFFFFFFFFu, 0xFFFFFFFFu, mips, usage, fmt, pool, filter, mipFilter, colorKey,
                                    info, palette, out);
            if (hr >= 0) {
                if (++g_applied <= 20 || g_applied % 500 == 0) Log("textures: override %08x applied (%u so far)", hash, g_applied);
                return hr;
            }
            Log("textures: override %08x could not be loaded (%08X), original kept", hash, (u32)hr);
        }
    }
    HRESULT real = g_real(dev, data, size, w, h, mips, usage, fmt, pool, filter, mipFilter, colorKey, info, palette, out);
    if (real >= 0 && out && *out && g_cfg.glyphTheme && !g_keyboardAtlas && size != 0x7FFFFFFFu) NoteAtlas(*out);
    return real;
}

void* GlyphAtlasForImpl(void* dev, void* tex);

}  // namespace

void* GlyphAtlasFor(void* dev, void* tex) { return GlyphAtlasForImpl(dev, tex); }

namespace {

void* GlyphAtlasForImpl(void* dev, void* tex) {
    if (!g_keyboardAtlas || tex != g_keyboardAtlas || !*(volatile u8*)(uintptr_t)0x014810F0) return tex;
    if (!g_themeTried) {
        g_themeTried = true;
        const wchar_t* name = g_cfg.glyphTheme == 2 ? L"xbox" : g_cfg.glyphTheme == 3 ? L"playstation" : (SonyPadActive() ? L"playstation" : L"xbox");
        wchar_t path[MAX_PATH + 40];
        wsprintfW(path, L"%sUltimateFix\\glyphs\\gamepad\\%s.png", g_paths.gameDir, name);
        if (g_fromFile && SUCCEEDED(g_fromFile(dev, path, 1024, 1024, 1, 0, 21 /*A8R8G8B8*/, 1 /*MANAGED*/, 1 /*POINT? no: default*/, 1, 0, nullptr,
                                                 nullptr, &g_padTheme)) && g_padTheme)
            Log("textures: pad icons loaded: %S", path);
        else
            Log("textures: pad icons %S could not be loaded, the keyboard atlas stays", path);
    }
    return g_padTheme ? g_padTheme : tex;
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
    if (g_count == 0 && !g_cfg.dumpTextures && !g_cfg.glyphTheme) {
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
    if (!SwapImport(slot, (void*)H_FromMemoryEx, nullptr)) return false;
    Log("textures: %d replacement files indexed, texture loading hooked", g_count);
    return true;
}

}  // namespace ufx
