// Diagnostic: records what the game asks Direct3D to create, without changing anything.
//
// Direct3DCreate9 is intercepted through the import table of DP.exe. The IDirect3D9 object it returns
// gets its CreateDevice slot replaced; once the device exists, a few device slots (CreateTexture,
// CreateRenderTarget, CreateDepthStencilSurface, SetViewport, StretchRect) are replaced as well. Every
// replacement calls the original and logs the arguments and where in DP.exe the call came from.
// These vtable swaps are the foundation the later graphics features build on.
#include "../base.h"
#include "../sites.h"

namespace ufx {
bool g_usingDxvk = false;
bool g_d3dWrapper = false;  // a d3d9.dll that is neither DXVK nor the Windows one (DPfix, ReShade, dgVoodoo...)
namespace {

enum { kSetTexture = 65, kSetVertexShaderConstantF = 94, kSetPixelShaderConstantF = 109, kCreateDevice = 16, kCreateTexture = 23, kCreateRenderTarget = 28, kCreateDepthStencil = 29,
       kStretchRect = 34, kSetViewport = 47 };

typedef void*(WINAPI* CreateD3dFn)(UINT);
typedef HRESULT(WINAPI* CreateDeviceFn)(void*, UINT, DWORD, HWND, DWORD, void*, void**);
typedef HRESULT(WINAPI* CreateTextureFn)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**, void*);
typedef HRESULT(WINAPI* CreateSurfaceFn)(void*, UINT, UINT, DWORD, DWORD, DWORD, BOOL, void**, void*);
typedef HRESULT(WINAPI* CreateDepthFn)(void*, UINT, UINT, DWORD, DWORD, DWORD, BOOL, void**, void*);
typedef HRESULT(WINAPI* StretchRectFn)(void*, void*, const RECT*, void*, const RECT*, DWORD);
typedef HRESULT(WINAPI* SetViewportFn)(void*, const void*);
typedef HRESULT(WINAPI* SetPsConstFFn)(void*, UINT, const float*, UINT);

CreateD3dFn r_Direct3DCreate9 = nullptr;
CreateDeviceFn r_CreateDevice = nullptr;
CreateTextureFn r_CreateTexture = nullptr;
CreateSurfaceFn r_CreateRenderTarget = nullptr;
CreateDepthFn r_CreateDepthStencil = nullptr;
StretchRectFn r_StretchRect = nullptr;
SetViewportFn r_SetViewport = nullptr;
SetPsConstFFn r_SetPsConstF = nullptr;
SetPsConstFFn r_SetVsConstF = nullptr;
typedef HRESULT(WINAPI* SetTextureFn)(void*, DWORD, void*);
SetTextureFn r_SetTexture = nullptr;
u32 g_vpW = 0, g_vpH = 0;  // the last viewport the game set

int g_lines = 0;
constexpr int kMaxLines = 900;

struct Seen {
    u32 x, y, w, h;
};
Seen g_viewports[48];
int g_viewportCount = 0;
Seen g_stretches[48];
int g_stretchCount = 0;

u32 Caller() { return (u32)(uintptr_t)__builtin_return_address(0); }

bool PatchSlot(void** vtable, int index, void* replacement, void** originalOut) {
    if (vtable[index] == replacement) return true;
    DWORD old;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old)) return false;
    *originalOut = vtable[index];
    vtable[index] = replacement;
    DWORD ignored;
    VirtualProtect(&vtable[index], sizeof(void*), old, &ignored);
    return true;
}

HRESULT WINAPI H_CreateTexture(void* dev, UINT w, UINT h, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void** out,
                               void* shared) __attribute__((noinline));
HRESULT WINAPI H_CreateTexture(void* dev, UINT w, UINT h, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void** out,
                               void* shared) {
    u32 from = Caller();
    // Shadow depth precision: the 16-bit depth buffers of the shadow passes become 32-bit float (D3DFMT_D32F_LOCKABLE),
    // which removes the banding of shadow edges. If the device cannot make one, the original format is used.
    HRESULT hr;
    if (g_cfg.shadowPrecision && (usage & 2 /*D3DUSAGE_DEPTHSTENCIL*/) && fmt == 80 /*D3DFMT_D16*/ && w == h && w >= 512 &&
        pool == 0) {
        hr = r_CreateTexture(dev, w, h, levels, usage, 82 /*D3DFMT_D32F_LOCKABLE*/, pool, out, shared);
        static bool said = false;
        if (!said) {
            said = true;
            Log("d3d: shadow depth buffers use 32-bit float (%s)", hr >= 0 ? "ok" : "not supported, kept 16-bit");
        }
        if (hr < 0) hr = r_CreateTexture(dev, w, h, levels, usage, fmt, pool, out, shared);
    } else {
        hr = r_CreateTexture(dev, w, h, levels, usage, fmt, pool, out, shared);
    }
    // The hook also exists for the shadow precision alone: without TraceResources=1 nothing is listed.
    if (!g_cfg.traceResources) return hr;
    // Ordinary textures loaded from the game files (usage 0, managed pool) are only counted: there are
    // thousands and they would push everything interesting out of the log.
    static u32 plain = 0;
    bool interesting = (usage != 0) || pool != 1;
    if (!interesting) {
        if (++plain % 500 == 0) Log("d3d: %u ordinary textures created so far (not listed)", plain);
        return hr;
    }
    if (g_lines < kMaxLines) {
        ++g_lines;
        Log("d3d: CreateTexture %ux%u levels=%u usage=%X format=%u pool=%u -> %08X  from %08X", w, h, levels, usage, fmt,
            pool, (u32)hr, from);
    }
    return hr;
}

HRESULT WINAPI H_CreateRenderTarget(void* dev, UINT w, UINT h, DWORD fmt, DWORD ms, DWORD q, BOOL lockable, void** out,
                                    void* shared) __attribute__((noinline));
HRESULT WINAPI H_CreateRenderTarget(void* dev, UINT w, UINT h, DWORD fmt, DWORD ms, DWORD q, BOOL lockable, void** out,
                                    void* shared) {
    u32 from = Caller();
    HRESULT hr = r_CreateRenderTarget(dev, w, h, fmt, ms, q, lockable, out, shared);
    if (g_lines < kMaxLines) {
        ++g_lines;
        Log("d3d: CreateRenderTarget %ux%u format=%u multisample=%u -> %08X  from %08X", w, h, fmt, ms, (u32)hr, from);
    }
    return hr;
}

HRESULT WINAPI H_CreateDepthStencil(void* dev, UINT w, UINT h, DWORD fmt, DWORD ms, DWORD q, BOOL discard, void** out,
                                    void* shared) __attribute__((noinline));
HRESULT WINAPI H_CreateDepthStencil(void* dev, UINT w, UINT h, DWORD fmt, DWORD ms, DWORD q, BOOL discard, void** out,
                                    void* shared) {
    u32 from = Caller();
    HRESULT hr = r_CreateDepthStencil(dev, w, h, fmt, ms, q, discard, out, shared);
    if (g_lines < kMaxLines) {
        ++g_lines;
        Log("d3d: CreateDepthStencilSurface %ux%u format=%u multisample=%u -> %08X  from %08X", w, h, fmt, ms,
            (u32)hr, from);
    }
    return hr;
}

HRESULT WINAPI H_SetViewport(void* dev, const void* vp) __attribute__((noinline));
HRESULT WINAPI H_SetViewport(void* dev, const void* vp) {
    u32 from = Caller();
    if (vp) {
        g_vpW = ((const u32*)vp)[2];
        g_vpH = ((const u32*)vp)[3];
    }
    if (vp && g_cfg.traceResources && g_viewportCount < 48 && g_lines < kMaxLines) {
        const u32* v = (const u32*)vp;  // X, Y, Width, Height
        bool known = false;
        for (int i = 0; i < g_viewportCount; ++i)
            if (g_viewports[i].x == v[0] && g_viewports[i].y == v[1] && g_viewports[i].w == v[2] &&
                g_viewports[i].h == v[3])
                known = true;
        if (!known) {
            g_viewports[g_viewportCount++] = {v[0], v[1], v[2], v[3]};
            ++g_lines;
            Log("d3d: SetViewport %u,%u %ux%u  from %08X", v[0], v[1], v[2], v[3], from);
        }
    }
    return r_SetViewport(dev, vp);
}

HRESULT WINAPI H_StretchRect(void* dev, void* src, const RECT* sr, void* dst, const RECT* dr, DWORD filter)
    __attribute__((noinline));
HRESULT WINAPI H_StretchRect(void* dev, void* src, const RECT* sr, void* dst, const RECT* dr, DWORD filter) {
    u32 from = Caller();
    if (g_stretchCount < 48 && g_lines < kMaxLines) {
        u32 a = sr ? (u32)(sr->right - sr->left) : 0, b = sr ? (u32)(sr->bottom - sr->top) : 0;
        u32 c = dr ? (u32)(dr->right - dr->left) : 0, d = dr ? (u32)(dr->bottom - dr->top) : 0;
        bool known = false;
        for (int i = 0; i < g_stretchCount; ++i)
            if (g_stretches[i].x == a && g_stretches[i].y == b && g_stretches[i].w == c && g_stretches[i].h == d)
                known = true;
        if (!known) {
            g_stretches[g_stretchCount++] = {a, b, c, d};
            ++g_lines;
            Log("d3d: StretchRect source %ux%u -> destination %ux%u (0 = whole surface) filter=%u  from %08X", a, b, c,
                d, filter, from);
        }
    }
    return r_StretchRect(dev, src, sr, dst, dr, filter);
}

// The shadow pass hands its map size to the pixel shader as register c0 = {512 or 1024, 0, 0, 0}. When the
// maps are enlarged the value has to follow, or the filtering steps no longer match the texels.
HRESULT WINAPI H_SetPsConstF(void* dev, UINT start, const float* data, UINT count) {
    if (start == 0 && count >= 1 && data && g_cfg.shadowScale > 100) {
        const u32* b = (const u32*)data;
        int base = b[0] == 0x44000000u ? 512 : (b[0] == 0x44800000u ? 1024 : 0);  // 512.0f, 1024.0f
        if (base && b[1] == 0 && b[2] == 0 && b[3] == 0) {
            float v[4] = {(float)(base * g_cfg.shadowScale / 100), 0.f, 0.f, 0.f};
            return r_SetPsConstF(dev, start, v, 1);
        }
    }
    return r_SetPsConstF(dev, start, data, count);
}

// The 2D pass hands its vertex shader a half-pixel offset in c0 = {x, y, 0, 0}, made for 1280x720. With the 2D layer
// drawn at the real output resolution the offset has to be half a pixel of THAT viewport, or sprites sample their
// neighbours in the atlas (icons show a strip of the next icon). Only calls made at the output viewport are changed.
HRESULT WINAPI H_SetVsConstF(void* dev, UINT start, const float* data, UINT count) {
    if (start == 0 && count == 1 && data && g_cfg.pixelOffsetFix && g_outputW && g_vpW == (u32)g_outputW &&
        g_vpH == (u32)g_outputH) {
        const u32* b = (const u32*)data;
        if (b[2] == 0 && b[3] == 0) {
            static int said = 0;
            if (said < 6) {
                ++said;
                Log("d3d: 2D half-pixel constant %08X %08X replaced for the %ux%u viewport", b[0], b[1], g_vpW, g_vpH);
            }
            float v[4] = {0.5f / (float)g_vpW, 0.5f / (float)g_vpH, 0.f, 0.f};
            return r_SetVsConstF(dev, start, v, 1);
        }
    }
    return r_SetVsConstF(dev, start, data, count);
}

HRESULT WINAPI H_SetTexture(void* dev, DWORD stage, void* tex) {
    if (stage == 0 && tex) tex = GlyphAtlasFor(dev, tex);
    return r_SetTexture(dev, stage, tex);
}

void PatchDevice(void* dev) {
    void** vt = *(void***)dev;
    void* ignored;
    (void)ignored;
    if (g_cfg.shadowScale > 100) {
        bool f = PatchSlot(vt, kSetPixelShaderConstantF, (void*)H_SetPsConstF, (void**)&r_SetPsConstF);
        Log("d3d: shadow size constant hooked (%d)", f);
    }
    // Hooked whenever the output resolution is active, not only when the fix is on at start: H_SetVsConstF reads
    // the setting on every call, so switching it in the menu works at once in both directions.
    if (g_outputW) {
        bool v = PatchSlot(vt, kSetVertexShaderConstantF, (void*)H_SetVsConstF, (void**)&r_SetVsConstF);
        bool w = PatchSlot(vt, kSetViewport, (void*)H_SetViewport, (void**)&r_SetViewport);
        Log("d3d: 2D half-pixel fix hooked (%d%d), %s", v, w, g_cfg.pixelOffsetFix ? "on" : "off for now");
    }
    if (g_cfg.glyphTheme) {
        bool t = PatchSlot(vt, kSetTexture, (void*)H_SetTexture, (void**)&r_SetTexture);
        Log("d3d: icon atlas swap hooked (%d)", t);
    }
    if (g_cfg.shadowPrecision && !g_cfg.traceResources)
        PatchSlot(vt, kCreateTexture, (void*)H_CreateTexture, (void**)&r_CreateTexture);
    if (!g_cfg.traceResources) return;
    bool a = PatchSlot(vt, kCreateTexture, (void*)H_CreateTexture, (void**)&r_CreateTexture);
    bool b = PatchSlot(vt, kCreateRenderTarget, (void*)H_CreateRenderTarget, (void**)&r_CreateRenderTarget);
    bool c = PatchSlot(vt, kCreateDepthStencil, (void*)H_CreateDepthStencil, (void**)&r_CreateDepthStencil);
    bool d = PatchSlot(vt, kSetViewport, (void*)H_SetViewport, (void**)&r_SetViewport);  // already ours when the half-pixel fix is on
    bool e = PatchSlot(vt, kStretchRect, (void*)H_StretchRect, (void**)&r_StretchRect);
    Log("d3d: device methods hooked for tracing (%d%d%d%d%d)", a, b, c, d, e);
}

HRESULT WINAPI H_CreateDevice(void* d3d, UINT adapter, DWORD type, HWND focus, DWORD flags, void* pp, void** dev) {
    if (pp && g_cfg.traceResources) {
        const u32* p = (const u32*)pp;
        Log("d3d: CreateDevice adapter=%u type=%u flags=%X; back buffer %ux%u format=%u count=%u swap=%u windowed=%u "
            "refresh=%u interval=%u",
            adapter, type, flags, p[0], p[1], p[2], p[3], p[6], p[8], p[12], p[13]);
    }
    HRESULT hr = r_CreateDevice(d3d, adapter, type, focus, flags, pp, dev);
    if (g_cfg.traceResources) Log("d3d: CreateDevice -> %08X", (u32)hr);
    if (SUCCEEDED(hr) && dev && *dev) PatchDevice(*dev);
    return hr;
}

bool HasText(const wchar_t* hay, const wchar_t* needle) {
    for (; *hay; ++hay) {
        const wchar_t *a = hay, *b = needle;
        while (*a && *b && *a == *b) ++a, ++b;
        if (!*b) return true;
    }
    return false;
}

// True if [p, p + size) holds the ASCII text. Only the parts that VirtualQuery reports as committed and readable are
// read: a packed or protected wrapper may leave pages of a data section without access, as guard pages or decommitted.
bool ReadableHasText(const u8* p, DWORD size, const char* text, int len) {
    const u8* end = p + size;
    while (p < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(p, &mbi, sizeof mbi)) return false;
        const u8* regionEnd = (const u8*)mbi.BaseAddress + mbi.RegionSize;
        if (regionEnd <= p) return false;
        if (regionEnd > end) regionEnd = end;
        const DWORD prot = mbi.Protect;
        const bool readable = mbi.State == MEM_COMMIT && !(prot & (PAGE_GUARD | PAGE_NOACCESS)) &&
                              (prot & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        if (readable) {
            const DWORD n = (DWORD)(regionEnd - p);
            for (DWORD k = 0; k + (DWORD)len <= n; ++k) {
                if (p[k] != (u8)text[0]) continue;
                int j = 1;
                while (j < len && p[k + j] == (u8)text[j]) ++j;
                if (j == len) return true;
            }
        }
        p = regionEnd;
    }
    return false;
}

// True if one of the readable data sections of a loaded module holds the ASCII text (up to their VirtualSize, and only
// the pages that are committed and readable). Code sections are skipped.
bool ImageHasText(HMODULE m, const char* text) {
    const u8* base = (const u8*)m;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    int len = 0;
    while (text[len]) ++len;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        const DWORD c = sec->Characteristics;
        if (!(c & IMAGE_SCN_MEM_READ) || (c & IMAGE_SCN_MEM_EXECUTE) || !(c & IMAGE_SCN_CNT_INITIALIZED_DATA)) continue;
        if (sec->VirtualAddress >= nt->OptionalHeader.SizeOfImage) continue;
        DWORD size = sec->Misc.VirtualSize;
        if (size > nt->OptionalHeader.SizeOfImage - sec->VirtualAddress) size = nt->OptionalHeader.SizeOfImage - sec->VirtualAddress;
        if (ReadableHasText(base + sec->VirtualAddress, size, text, len)) return true;
    }
    return false;
}

// Which Direct3D 9 library the game uses. The folder alone does not tell: DPfix, ReShade or dgVoodoo also sit next to
// DP.exe as d3d9.dll, and under Wine/Proton DXVK sits in the prefix's system32. DXVK is recognised by its own texts in
// the d3d9.dll image (its DXVK_LOG_PATH variable name; DXVK 3.1.1 has no export of its own that GetProcAddress could find)
// or by its DXVK_RegisterAnnotation export in the builds that have one. A loaded Vulkan loader (vulkan-1.dll,
// winevulkan.dll) is no proof: wined3d's Vulkan renderer and overlays load it too. Called after Direct3DCreate9 and at the
// first Present; logs only when the answer changes.
void NameD3dLibrary() {
    HMODULE h = GetModuleHandleW(L"d3d9.dll");
    wchar_t path[MAX_PATH] = L"";
    if (!h || !GetModuleFileNameW(h, path, MAX_PATH)) return;
    wchar_t lower[MAX_PATH];
    int i = 0;
    for (; path[i] && i < MAX_PATH - 1; ++i) lower[i] = (wchar_t)((path[i] >= L'A' && path[i] <= L'Z') ? path[i] + 32 : path[i]);
    lower[i] = 0;
    bool system = HasText(lower, L"\\windows\\system32\\") || HasText(lower, L"\\windows\\syswow64\\");
    static HMODULE scanned = nullptr;  // the image is searched once per module
    static bool scannedDxvk = false;
    if (scanned != h) {
        scanned = h;
        scannedDxvk = GetProcAddress(h, "DXVK_RegisterAnnotation") != nullptr || ImageHasText(h, "DXVK_LOG_PATH");
    }
    const bool dxvk = scannedDxvk;
    g_usingDxvk = dxvk;
    g_d3dWrapper = !dxvk && !system;
    const char* label = dxvk ? "DXVK, Direct3D 9 on Vulkan"
                             : system ? "the system library" : "a wrapper in the game folder, not recognised as DXVK";
    static const char* said = nullptr;
    if (label != said) {
        said = label;
        Log("d3d: Direct3D 9 comes from %S (%s)", path, label);
    }
}

void* WINAPI H_Direct3DCreate9(UINT sdk) {
    void* d3d = r_Direct3DCreate9(sdk);
    NameD3dLibrary();
    if (g_cfg.traceResources) Log("d3d: Direct3DCreate9(%u) -> %08X", sdk, (u32)(uintptr_t)d3d);
    if (d3d) {
        void** vt = *(void***)d3d;
        PatchSlot(vt, kCreateDevice, (void*)H_CreateDevice, (void**)&r_CreateDevice);
    }
    return d3d;
}

}  // namespace

void DetectD3dLibrary() { NameD3dLibrary(); }  // display.cpp, at the first Present

bool InstallD3dTrace() {
    void** slot = FindImportSlot(GetModuleHandleW(nullptr), "d3d9.dll", "Direct3DCreate9");
    if (!slot) {
        Log("d3d: Direct3DCreate9 import not found, device layer skipped");
        return false;
    }
    r_Direct3DCreate9 = (CreateD3dFn)*slot;
    if (!SwapImport(slot, (void*)H_Direct3DCreate9, nullptr)) {
        Log("d3d: could not patch the import table, device layer skipped");
        return false;
    }
    Log("d3d: device layer installed (tracing %s, shadow constant %s)", g_cfg.traceResources ? "on" : "off",
        g_cfg.shadowScale > 100 ? "on" : "off");
    return true;
}

}  // namespace ufx
