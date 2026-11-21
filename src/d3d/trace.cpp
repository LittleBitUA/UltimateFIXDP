// Diagnostic: records what the game asks Direct3D to create, without changing anything.
//
// Direct3DCreate9 is intercepted through the import table of DP.exe. The IDirect3D9 object it returns
// gets its CreateDevice slot replaced; once the device exists, a few device slots (CreateTexture,
// CreateRenderTarget, CreateDepthStencilSurface, SetViewport, StretchRect) are replaced as well. Every
// replacement calls the original and logs the arguments and where in DP.exe the call came from.
// These vtable swaps are the foundation the later graphics features build on.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {
bool g_usingDxvk = false;
bool g_d3dWrapper = false;
void NoteSmokeMaterial();  // below: the material of the smoke sheets, seen by the slot probe  // a d3d9.dll that is neither DXVK nor the Windows one (DPfix, ReShade, dgVoodoo...)
namespace {

enum { kSetTexture = 65, kSetVertexShaderConstantF = 94, kSetPixelShaderConstantF = 109, kCreateDevice = 16, kCreateTexture = 23, kCreateRenderTarget = 28, kCreateDepthStencil = 29,
       kStretchRect = 34, kSetViewport = 47, kDrawPrimitive = 81, kDrawIndexedPrimitive = 82, kCreatePixelShader = 106, kSetPixelShader = 107, kSetRenderTarget = 37 };

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
bool g_fullLayer = false;  // the device slots are hooked too (tracing, shadows, icons, output); otherwise only a failed CreateDevice is caught
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


// ---- water diagnostic ([Debug] TraceWater=1) ----
// The water pixel shaders (272 of the 544 in the pack at ADCE20h) are the only ones whose constant table names g_vMirror:
// s0 diffuse, s1 normal map, s5 reflection, s6 refraction; c10 = (distortion, reflection mix, alpha bias, alpha scale),
// c13 = an added colour. The river shows as one flat colour on PC; this lists what each distinct water draw really has bound.
typedef HRESULT(WINAPI* CreatePsFn)(void*, const DWORD*, void**);
typedef HRESULT(WINAPI* SetPsFn)(void*, void*);
typedef HRESULT(WINAPI* DrawIpFn)(void*, DWORD, INT, UINT, UINT, UINT, UINT);
CreatePsFn r_CreatePs = nullptr;
SetPsFn r_SetPs = nullptr;
DrawIpFn r_DrawIp = nullptr;
typedef HRESULT(WINAPI* DrawPFn)(void*, DWORD, UINT, UINT);
DrawPFn r_DrawP = nullptr;
void* g_waterShaders[512];  // the game has 272 of them (index rule (i%272)%16 in 8..15); 64 missed the river
int g_waterShaderCount = 0;
void* g_curPs = nullptr;
void* g_stageTex[8];
float g_psC[16][4];  // c0..c15 as last set
float g_vsC243[4];
u32 g_waterKeys[40];
int g_waterKeyCount = 0;
u32 g_reflKeys[24];   // draws by other shaders that read a render target in s5 or s6 (a reflection)
int g_reflKeyCount = 0;
u32 g_smallKeys[32];  // draws that read a small square render target (64/128/256) in s0..s3: the Xbox fills such targets with an
int g_smallKeyCount = 0;  // animated water/fog picture every frame, a pass the PC port does not have (they stay empty or black)

// Saves a texture as UltimateFix\target_<side>_<shader>_<caller>.png (D3DXSaveTextureToFileW of the game's d3dx9_43), so
// what such a target holds at the moment of the draw can be seen: an empty one is black.
void SaveTargetPng(void* tex, UINT side, u32 shader, u32 from) {
    typedef HRESULT(WINAPI * SaveFn)(const wchar_t*, DWORD, void*, const void*);
    static SaveFn save = nullptr;
    static bool tried = false;
    static int saved = 0;
    if (!tried) {
        tried = true;
        HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
        save = d3dx ? (SaveFn)(void*)GetProcAddress(d3dx, "D3DXSaveTextureToFileW") : nullptr;
    }
    if (!save || saved >= 16) return;
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%sUltimateFix\\target_%u_%08X_%08X.png", g_paths.gameDir, side, shader, from);
    const HRESULT hr = save(path, 3 /*D3DXIFF_PNG*/, tex, nullptr);
    ++saved;
    Log("water:   saved as %S (%s)", path, SUCCEEDED(hr) ? "ok" : "failed");
}

// The side of a square render target of 64, 128 or 256, else 0.
UINT SmallSquareTarget(void* tex) {
    if (!tex) return 0;
    void** vt = *(void***)tex;
    typedef DWORD(WINAPI * TypeFn)(void*);
    if (((TypeFn)vt[10])(tex) != 3) return 0;
    u32 desc[8] = {0};
    typedef HRESULT(WINAPI * DescFn)(void*, UINT, void*);
    if (FAILED(((DescFn)vt[17])(tex, 0, desc)) || !(desc[2] & 1) || desc[6] != desc[7]) return 0;
    return (desc[6] == 64 || desc[6] == 128 || desc[6] == 256) ? desc[6] : 0;
}

bool IsTarget(void* tex) {
    if (!tex) return false;
    void** vt = *(void***)tex;
    typedef DWORD(WINAPI * TypeFn)(void*);
    if (((TypeFn)vt[10])(tex) != 3) return false;
    u32 desc[8] = {0};
    typedef HRESULT(WINAPI * DescFn)(void*, UINT, void*);
    return SUCCEEDED(((DescFn)vt[17])(tex, 0, desc)) && (desc[2] & 1);
}

bool NamesMirror(const DWORD* code) {
    // the CTAB comment block comes first: 0xFFFE token with its length in DWORDs
    if (!code || (code[0] & 0xFFFF0000u) != 0xFFFF0000u) return false;
    const DWORD tok = code[1];
    if ((tok & 0xFFFF) != 0xFFFE) return false;
    const DWORD len = (tok >> 16) & 0x7FFF;
    const char* b = (const char*)(code + 2);
    for (DWORD i = 0; i + 9 <= len * 4; ++i)
        if (b[i] == 'g' && b[i + 1] == '_' && b[i + 2] == 'v' && b[i + 3] == 'M' && b[i + 4] == 'i' && b[i + 5] == 'r' &&
            b[i + 6] == 'r' && b[i + 7] == 'o' && b[i + 8] == 'r')
            return true;
    return false;
}

// True when the shader's constant table names `name` (the CTAB comment block that comes first).
bool NamesConstant(const DWORD* code, const char* name) {
    if (!code || (code[0] & 0xFFFF0000u) != 0xFFFF0000u) return false;
    const DWORD tok = code[1];
    if ((tok & 0xFFFF) != 0xFFFE) return false;
    const DWORD len = (tok >> 16) & 0x7FFF;
    const char* b = (const char*)(code + 2);
    int n = 0;
    while (name[n]) ++n;
    for (DWORD i = 0; i + n <= len * 4; ++i) {
        int k = 0;
        while (k < n && b[i + k] == name[k]) ++k;
        if (k == n) return true;
    }
    return false;
}

// Every pixel shader with its bytecode, so one draw can be explained (DumpDraw), and whether it writes a second colour target
// (oC1: the normal of the G-buffer pass).
void* g_psObj[2048];
const DWORD* g_psCode[2048];
bool g_psOC1[2048];
bool g_psSmoke[2048];     // the lit shader family (g_vDensity + g_tShadow): the prologue's black smoke is drawn with it
int g_psCount = 0;

// True when the shader bytecode writes oC1. Instructions are walked by their length field (ps_2_0 and later), and the def
// instructions (their float values could look like register tokens) are skipped.
bool WritesOC1(const DWORD* code) {
    if (!code || (code[0] & 0xFFFF0000u) != 0xFFFF0000u || ((code[0] >> 8) & 0xFF) < 2) return false;
    for (int i = 1; i < 8192;) {
        const DWORD tok = code[i];
        if (tok == 0x0000FFFFu) break;                       // end
        if ((tok & 0xFFFF) == 0xFFFE) { i += 1 + (int)((tok >> 16) & 0x7FFF); continue; }  // comment
        const DWORD op = tok & 0xFFFF, len = (tok >> 24) & 0xF;
        if (op != 0x51 && op != 0x52 && op != 0x53 && len) {
            const DWORD dst = code[i + 1];
            const DWORD type = ((dst >> 28) & 7) | ((dst >> 8) & 0x18);
            if ((dst & 0x80000000u) && type == 8 /*COLOROUT*/ && (dst & 0x7FF) == 1) return true;
        }
        i += 1 + (int)len;
    }
    return false;
}
bool CurrentPsWritesOC1() {
    for (int i = 0; i < g_psCount; ++i) if (g_psObj[i] == g_curPs) return g_psOC1[i];
    return false;
}

HRESULT WINAPI H_CreatePs(void* dev, const DWORD* code, void** out) {
    HRESULT hr = r_CreatePs(dev, code, out);
    if (SUCCEEDED(hr) && out && *out && g_psCount < 2048) {
        g_psObj[g_psCount] = *out;
        g_psCode[g_psCount] = code;
        g_psSmoke[g_psCount] = NamesConstant(code, "g_vDensity") && NamesConstant(code, "g_tShadow");
        g_psOC1[g_psCount++] = WritesOC1(code);
    }
    if (SUCCEEDED(hr) && out && *out && NamesMirror(code) && g_waterShaderCount < 512) {
        g_waterShaders[g_waterShaderCount++] = *out;
        if (g_waterShaderCount == 1) Log("water: water pixel shaders recognised (g_vMirror); the first is from %08X", (u32)(uintptr_t)code);
    }
    return hr;
}

HRESULT WINAPI H_SetPs(void* dev, void* ps) {
    g_curPs = ps;
    return r_SetPs(dev, ps);
}

void DescribeTexture(void* tex, char* out) {
    if (!tex) { lstrcpyA(out, "none"); return; }
    void** vt = *(void***)tex;
    typedef DWORD(WINAPI * TypeFn)(void*);
    const DWORD type = ((TypeFn)vt[10])(tex);
    if (type != 3) { wsprintfA(out, "type %u", type); return; }  // not a 2D texture
    u32 desc[8] = {0};
    typedef HRESULT(WINAPI * DescFn)(void*, UINT, void*);
    if (FAILED(((DescFn)vt[17])(tex, 0, desc))) { lstrcpyA(out, "?"); return; }
    wsprintfA(out, "%ux%u fmt %u%s", desc[6], desc[7], desc[0], (desc[2] & 1) ? " target" : "");
}

int Milli(float f) { return (int)(f * 1000.f + (f < 0 ? -0.5f : 0.5f)); }

bool NoteWaterDraw(UINT count, u32 from) {
    if (!g_curPs) return false;
    bool water = false;
    for (int i = 0; i < g_waterShaderCount && !water; ++i) water = g_waterShaders[i] == g_curPs;
    if (!g_cfg.traceWater) return water;  // the hooks are also in for FogFix: no log lines then
    if (water && g_waterKeyCount < 40) {
        {
            u32 key = (u32)(uintptr_t)g_curPs * 31u + (u32)(uintptr_t)g_stageTex[0] * 7u + (u32)(uintptr_t)g_stageTex[1] * 13u +
                      (u32)(uintptr_t)g_stageTex[5] * 17u + (u32)(uintptr_t)g_stageTex[6] * 19u + (u32)Milli(g_psC[10][2]) * 23u;
            bool seen = false;
            for (int i = 0; i < g_waterKeyCount && !seen; ++i) seen = g_waterKeys[i] == key;
            if (!seen) {
                g_waterKeys[g_waterKeyCount++] = key;
                char t0[48], t1[48], t5[48], t6[48];
                DescribeTexture(g_stageTex[0], t0);
                DescribeTexture(g_stageTex[1], t1);
                DescribeTexture(g_stageTex[5], t5);
                DescribeTexture(g_stageTex[6], t6);
                Log("water: draw of %u triangles from %08X: s0 %s | s1 %s | s5 %s | s6 %s", count, from, t0, t1, t5, t6);
                Log("water:   c10 %d %d %d %d  c13 %d %d %d %d  c12 %d %d %d %d  vs c243 %d %d %d %d (thousandths)", Milli(g_psC[10][0]),
                    Milli(g_psC[10][1]), Milli(g_psC[10][2]), Milli(g_psC[10][3]), Milli(g_psC[13][0]), Milli(g_psC[13][1]),
                    Milli(g_psC[13][2]), Milli(g_psC[13][3]), Milli(g_psC[12][0]), Milli(g_psC[12][1]), Milli(g_psC[12][2]),
                    Milli(g_psC[12][3]), Milli(g_vsC243[0]), Milli(g_vsC243[1]), Milli(g_vsC243[2]), Milli(g_vsC243[3]));
            }
        }
    }
    if (g_smallKeyCount < 32) {
        int stage = -1;
        UINT side = 0;
        for (int st = 0; st < 4 && stage < 0; ++st)
            if ((side = SmallSquareTarget(g_stageTex[st])) != 0) stage = st;
        if (stage >= 0) {
            const u32 key = (u32)(uintptr_t)g_curPs * 31u + (u32)(uintptr_t)g_stageTex[stage] * 7u + (u32)stage;
            bool seen = false;
            for (int i = 0; i < g_smallKeyCount && !seen; ++i) seen = g_smallKeys[i] == key;
            if (!seen) {
                g_smallKeys[g_smallKeyCount++] = key;
                char t0[48], t1[48], t2[48], t3[48];
                DescribeTexture(g_stageTex[0], t0);
                DescribeTexture(g_stageTex[1], t1);
                DescribeTexture(g_stageTex[2], t2);
                DescribeTexture(g_stageTex[3], t3);
                Log("water: a %s draw (shader %08X, %u triangles from %08X) reads a %ux%u target in s%d: s0 %s | s1 %s | s2 %s | s3 %s",
                    water ? "water" : "non-water", (u32)(uintptr_t)g_curPs, count, from, side, side, stage, t0, t1, t2, t3);
                SaveTargetPng(g_stageTex[stage], side, (u32)(uintptr_t)g_curPs, from);
            }
        }
    }
    if (!water && g_reflKeyCount < 24 && (IsTarget(g_stageTex[5]) || IsTarget(g_stageTex[6]))) {
        const u32 key = (u32)(uintptr_t)g_curPs * 31u + (u32)(uintptr_t)g_stageTex[0] * 7u + (u32)(uintptr_t)g_stageTex[5] * 17u;
        bool seen = false;
        for (int i = 0; i < g_reflKeyCount && !seen; ++i) seen = g_reflKeys[i] == key;
        if (!seen) {
            g_reflKeys[g_reflKeyCount++] = key;
            char t0[48], t1[48], t5[48], t6[48];
            DescribeTexture(g_stageTex[0], t0);
            DescribeTexture(g_stageTex[1], t1);
            DescribeTexture(g_stageTex[5], t5);
            DescribeTexture(g_stageTex[6], t6);
            Log("water: a draw by another shader (%08X) reads a target in s5/s6: %u triangles from %08X: s0 %s | s1 %s | s5 %s | s6 %s",
                (u32)(uintptr_t)g_curPs, count, from, t0, t1, t5, t6);
        }
    }
    return water;
}

// [Debug] WaterTest, for the water draw only: c10.z is the bias of the river's own texture over the reflection
// (out = lerp(reflection, s0, saturate(s0.a + c10.z))). The river came with 1.0 (texture only, the flat blue), the rippled
// water with -0.75 at times. 1 = -0.75 (reflection mostly), 2 = 0 (the texture by its own alpha).
template <typename Draw> HRESULT WaterDraw(void* dev, bool water, Draw draw) {
    if (!water || (g_cfg.waterTest != 1 && g_cfg.waterTest != 2) || !r_SetPsConstF) return draw();
    float v[4] = {g_psC[10][0], g_psC[10][1], g_cfg.waterTest == 1 ? -0.75f : 0.0f, g_psC[10][3]};
    r_SetPsConstF(dev, 10, v, 1);
    const HRESULT hr = draw();
    r_SetPsConstF(dev, 10, g_psC[10], 1);
    return hr;
}
// One draw explained, once per caller address of interest (the prologue fog at 006D6E1B): the pixel shader disassembled into
// UltimateFix\draw_<caller>.txt with its constants c0..c31 and the blend, alpha and depth states, to see why it comes out black.
float g_psCx[32][4];
void WriteDrawFile(void* dev, u32 from, const wchar_t* path);
UINT g_drawFirstVertex = 0;  // the draw being explained: its first vertex in stream 0 (base + min index), 0 for DrawPrimitive's start
const DWORD* CurrentPsCode() {
    for (int i = 0; i < g_psCount; ++i) if (g_psObj[i] == g_curPs) return g_psCode[i];
    return nullptr;
}
bool CurrentPsIsSmoke() {
    for (int i = 0; i < g_psCount; ++i) if (g_psObj[i] == g_curPs) return g_psSmoke[i];
    return false;
}
// With TraceWater: each lit particle draw (the prologue's black smoke) explained once per particle texture, at most 8, in
// UltimateFix\smoke_<caller>_<texture>.txt with its vertex shader, the vertices and its texture as PNG.
// SmokeFix. The prologue's smoke is an animated model of sheets (skinned) with a black DXT3 texture: the smoke is only in its
// alpha. On the Xbox 360 its material used the game's special material slot 0 (the table at A000A0, filled by FUN_006D1610): the
// animated cloud's normal map in s1 (g_tNormal, A000C8), a mask in s2 (g_tMask, A000CC), specular (1.0, 128.0, 2.0) and a UV scale
// of (1.5, 1.0): the light glints over the black sheets along the moving cloud, the grey wisps. In the PC data no material asks for
// that slot, so the smoke is drawn with the plain particle shader and stays one dark patch. The game's own pair for it is still in
// DP.exe: the skinned vertex shader with tangents and the UV scale (bytecode at A36BE8) and the particle pixel shader with normal,
// mask and specular for the same lights (ABE804, the plain one being ABD8D0). For the smoke sheets the mod draws with that pair and
// the table's textures and values; nothing of its own.
constexpr u32 kSmokePlainPs = 0x00ABD8D0, kSmokeNormalPs = 0x00ABE804, kSmokeNormalVs = 0x00A36BE8;
void* g_smokePs = nullptr;
void* g_smokeVs = nullptr;
bool g_smokeShadersTried = false;

void BuildSmokeShaders(void* dev) {
    if (g_smokeShadersTried) return;
    g_smokeShadersTried = true;
    const DWORD* ps = (const DWORD*)(uintptr_t)kSmokeNormalPs;
    const DWORD* vs = (const DWORD*)(uintptr_t)kSmokeNormalVs;
    if (ps[0] != 0xFFFF0300u || vs[0] != 0xFFFE0300u) {
        Log("smoke: the game's shaders at %08X / %08X differ from DP.exe 1.01b, no smoke fix", kSmokeNormalPs, kSmokeNormalVs);
        return;
    }
    typedef HRESULT(WINAPI * MakeFn)(void*, const DWORD*, void**);
    CreatePsFn makePs = r_CreatePs ? r_CreatePs : (CreatePsFn)(*(void***)dev)[kCreatePixelShader];
    MakeFn makeVs = (MakeFn)(*(void***)dev)[91];  // IDirect3DDevice9::CreateVertexShader
    const HRESULT a = makePs(dev, ps, &g_smokePs);
    const HRESULT b = makeVs(dev, vs, &g_smokeVs);
    Log("smoke: the game's shaders for the cloud material made (pixel %08X, vertex %08X)", (u32)a, (u32)b);
}

// A DXT3 texture whose colour is black wherever its pixels look (decoded per block), cached per texture
struct BlackTex {
    void* tex;
    bool black;
};
BlackTex g_black[64];
int g_blackCount = 0;
bool IsBlackDxt3(void* tex) {
    for (int i = 0; i < g_blackCount; ++i) if (g_black[i].tex == tex) return g_black[i].black;
    bool black = false;
    void** vt = *(void***)tex;
    typedef HRESULT(WINAPI * DescFn)(void*, UINT, u32*);
    typedef HRESULT(WINAPI * LockFn)(void*, UINT, void*, const RECT*, DWORD);
    typedef HRESULT(WINAPI * UnlockFn)(void*, UINT);
    struct Locked {
        INT pitch;
        void* bits;
    } lr = {0, nullptr};
    u32 d[8] = {};
    if (SUCCEEDED(((DescFn)vt[17])(tex, 0, d)) && d[0] == 0x33545844u /*DXT3*/ &&
        SUCCEEDED(((LockFn)vt[19])(tex, 0, &lr, nullptr, 0x10 /*READONLY*/)) && lr.bits) {
        black = true;
        const UINT bw = (d[6] + 3) / 4, bh = (d[7] + 3) / 4;
        auto dark = [](u32 c) { return ((c >> 11) & 31) <= 1 && ((c >> 5) & 63) <= 2 && (c & 31) <= 1; };
        for (UINT y = 0; y < bh && black; ++y)
            for (UINT x = 0; x < bw && black; ++x) {
                const u8* b = (const u8*)lr.bits + y * lr.pitch + x * 16;
                const u32 c0 = b[8] | b[9] << 8, c1 = b[10] | b[11] << 8, idx = b[12] | b[13] << 8 | b[14] << 16 | (u32)b[15] << 24;
                bool uses[4] = {false, false, false, false};
                for (int i = 0; i < 16; ++i) uses[(idx >> (i * 2)) & 3] = true;
                black = (!uses[0] || dark(c0)) && (!uses[1] || dark(c1)) && (!(uses[2] || uses[3]) || (dark(c0) && dark(c1)));
            }
        ((UnlockFn)vt[20])(tex, 0);
    }
    if (g_blackCount < 64) g_black[g_blackCount++] = {tex, black};
    if (black) Log("smoke: a black %ux%u DXT3 smoke texture found; the cloud goes over it", d[6], d[7]);
    return black;
}

// The game's grey cloud: the 256x256 picture that FUN_006DBC10 turns into a normal map (its draw returns to 006DBCBD, the cloud in
// s0). The special material table (A000C8) holds that normal map, the blue one, not the grey cloud.
void* g_greyCloud = nullptr;
void NoteGreyCloud(u32 from) {
    if (from == 0x6DBCBDu && g_stageTex[0] && SmallSquareTarget(g_stageTex[0]) == 256) {
        if (g_greyCloud != g_stageTex[0]) Log("smoke: the grey cloud is texture %08X", (u32)(uintptr_t)g_stageTex[0]);
        g_greyCloud = g_stageTex[0];
    }
}

// The texture of a holder in the special material table (the way FUN_006B6020 picks it)
void* HolderTexture(u32 slotVa) {
    const u8* holder = *(const u8* const*)(uintptr_t)slotVa;
    if (!holder) return nullptr;
    void* t = *(void* const*)(holder + 0x14);
    if (!t) t = *(void* const*)(holder + 0x18);
    if (!t) t = *(void* const*)(holder + 0x1C);
    return t;
}

const DWORD* CurrentPsCode();

// Draws a smoke sheet with the game's cloud material when this is one; false when the game draws it as it is
template <typename Draw> bool SmokeDraw(void* dev, Draw draw, HRESULT* hr) {
    if (!g_cfg.smokeFix || !g_stageTex[0] || CurrentPsCode() != (const DWORD*)(uintptr_t)kSmokePlainPs) return false;
    typedef HRESULT(WINAPI * GetRsFn)(void*, DWORD, DWORD*);
    void** dvt = *(void***)dev;
    GetRsFn getRs = (GetRsFn)dvt[58];
    DWORD blend = 0, zwrite = 1;
    getRs(dev, 27 /*ALPHABLENDENABLE*/, &blend);
    getRs(dev, 14 /*ZWRITEENABLE*/, &zwrite);
    if (!blend || zwrite) return false;
    typedef DWORD(WINAPI * TypeFn)(void*);
    if (((TypeFn)(*(void***)g_stageTex[0])[10])(g_stageTex[0]) != 3 || !IsBlackDxt3(g_stageTex[0])) return false;
    BuildSmokeShaders(dev);
    void* normal = HolderTexture(0x00A000C8);
    void* mask = HolderTexture(0x00A000CC);
    if (!g_smokePs || !g_smokeVs || !normal || !r_SetPs || !r_SetTexture || !r_SetPsConstF || !r_SetVsConstF) {
        static bool told = false;
        if (!told) {
            told = true;
            Log("smoke: the cloud material cannot be drawn (shaders %08X %08X, normal map %08X, mask %08X)", (u32)(uintptr_t)g_smokePs,
                (u32)(uintptr_t)g_smokeVs, (u32)(uintptr_t)normal, (u32)(uintptr_t)mask);
        }
        return false;
    }
    typedef HRESULT(WINAPI * GetVsFn)(void*, void**);
    typedef HRESULT(WINAPI * SetVsFn)(void*, void*);
    typedef HRESULT(WINAPI * GetSsFn)(void*, DWORD, DWORD, DWORD*);
    typedef HRESULT(WINAPI * SetSsFn)(void*, DWORD, DWORD, DWORD);
    typedef HRESULT(WINAPI * GetPsCFn)(void*, UINT, float*, UINT);
    typedef ULONG(WINAPI * RelFn)(void*);
    GetSsFn getSs = (GetSsFn)dvt[68];
    SetSsFn setSs = (SetSsFn)dvt[69];
    // the table's values: UV scale/offset at A000AC (VS c243 g_vScrlParam), specular at A000BC.. (PS c9 g_vSpecular)
    const float* table = (const float*)(uintptr_t)0x00A000A0;
    const float scroll[4] = {table[3], table[4], table[5], table[6]};
    const float specular[4] = {table[7], table[8], table[9], 0.f};
    const float* cam = (const float*)(uintptr_t)(0x00BD9648 + 4);  // CRdCamera: the eye
    const float eye[4] = {cam[0], cam[1], cam[2], 1.f};
    float ownVs243[4], ownPs9[4], ownPs62[4];
    ((GetPsCFn)dvt[95])(dev, 243, ownVs243, 1);  // GetVertexShaderConstantF
    ((GetPsCFn)dvt[110])(dev, 9, ownPs9, 1);     // GetPixelShaderConstantF
    ((GetPsCFn)dvt[110])(dev, 62, ownPs62, 1);
    void* ownVs = nullptr;
    ((GetVsFn)dvt[93])(dev, &ownVs);
    void* ownPs = g_curPs;
    void* ownTex1 = g_stageTex[1];
    void* ownTex2 = g_stageTex[2];
    static const DWORD kStates[5] = {1 /*ADDRESSU*/, 2 /*ADDRESSV*/, 5 /*MAGFILTER*/, 6 /*MINFILTER*/, 7 /*MIPFILTER*/};
    static const DWORD kValues[5] = {1 /*WRAP*/, 1, 2 /*LINEAR*/, 2, 2 /*LINEAR*/};
    DWORD saved[2][5];
    for (int st = 1; st <= 2; ++st)
        for (int i = 0; i < 5; ++i) {
            saved[st - 1][i] = 0;
            getSs(dev, st, kStates[i], &saved[st - 1][i]);
            setSs(dev, st, kStates[i], kValues[i]);
        }
    ((SetVsFn)dvt[92])(dev, g_smokeVs);
    r_SetPs(dev, g_smokePs);
    r_SetTexture(dev, 1, normal);
    r_SetTexture(dev, 2, mask);
    r_SetVsConstF(dev, 243, scroll, 1);
    r_SetPsConstF(dev, 9, specular, 1);
    r_SetPsConstF(dev, 62, eye, 1);
    *hr = draw();
    r_SetPsConstF(dev, 62, ownPs62, 1);
    r_SetPsConstF(dev, 9, ownPs9, 1);
    r_SetVsConstF(dev, 243, ownVs243, 1);
    r_SetTexture(dev, 2, ownTex2);
    r_SetTexture(dev, 1, ownTex1);
    r_SetPs(dev, ownPs);
    ((SetVsFn)dvt[92])(dev, ownVs);
    if (ownVs) ((RelFn)(*(void***)ownVs)[2])(ownVs);
    for (int st = 1; st <= 2; ++st)
        for (int i = 0; i < 5; ++i) setSs(dev, st, kStates[i], saved[st - 1][i]);
    static bool told = false;
    if (!told) {
        told = true;
        Log("smoke: drawn with the game's cloud material (UV %d %d, specular %d %d %d, thousandths)", Milli(scroll[0]), Milli(scroll[1]),
            Milli(specular[0]), Milli(specular[1]), Milli(specular[2]));
    }
    return true;
}

// With TraceWater: every draw that reads the 256x256 cloud target (the smoke's noise picture) explained once per caller in
// UltimateFix\draw_<caller>.txt, and the other render targets it reads saved as cloud_<caller>_s<stage>.png
void ExplainCloudReader(void* dev, u32 from) {
    int cloud = -1;
    for (int st = 0; st < 4 && cloud < 0; ++st)
        if (SmallSquareTarget(g_stageTex[st]) == 256) cloud = st;
    if (cloud < 0) return;
    static u32 done[12];
    static int count = 0;
    for (int i = 0; i < count; ++i) if (done[i] == from) return;
    if (count >= 12) return;
    done[count++] = from;
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%sUltimateFix\\draw_%08X.txt", g_paths.gameDir, from);
    WriteDrawFile(dev, from, path);
    char t[8][48];
    for (int st = 0; st < 8; ++st) DescribeTexture(g_stageTex[st], t[st]);
    Log("cloud: a draw from %08X reads the 256x256 cloud in s%d: s0 %s | s1 %s | s2 %s | s3 %s | s4 %s | s5 %s | s6 %s | s7 %s", from, cloud, t[0], t[1],
        t[2], t[3], t[4], t[5], t[6], t[7]);
    typedef HRESULT(WINAPI * SaveFn)(const wchar_t*, DWORD, void*, const void*);
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    SaveFn save = d3dx ? (SaveFn)(void*)GetProcAddress(d3dx, "D3DXSaveTextureToFileW") : nullptr;
    for (int st = 0; st < 8 && save; ++st) {
        if (!g_stageTex[st]) continue;
        // a full-size target (3840x2160, maybe 16-bit float) takes D3DX seconds to write as PNG: the picture stood still for 8 s
        void** tvt = *(void***)g_stageTex[st];
        typedef DWORD(WINAPI * TypeFn)(void*);
        typedef HRESULT(WINAPI * DescFn)(void*, UINT, void*);
        u32 dd[8] = {0};
        if (((TypeFn)tvt[10])(g_stageTex[st]) != 3 || FAILED(((DescFn)tvt[17])(g_stageTex[st], 0, dd)) || dd[6] > 1024 || dd[7] > 1024) continue;
        wsprintfW(path, L"%sUltimateFix\\cloud_%08X_s%d.png", g_paths.gameDir, from, st);
        const HRESULT hr = save(path, 3 /*PNG*/, g_stageTex[st], nullptr);
        Log("cloud:   s%d saved as %S (%s)", st, path, SUCCEEDED(hr) ? "ok" : "failed");
    }
}

// With TraceWater: the draws that write into a 256x256 A8R8G8B8 render target (where the cloud is made), once per caller and
// pixel shader, in UltimateFix\cloudw_<caller>_<shader>.txt with the textures they read saved as PNG
typedef HRESULT(WINAPI* SetRtFn)(void*, DWORD, void*);
SetRtFn r_SetRt = nullptr;
void* g_rt0 = nullptr;
HRESULT WINAPI H_SetRt(void* dev, DWORD index, void* surface) {
    if (index == 0) g_rt0 = surface;
    return r_SetRt(dev, index, surface);
}
void ExplainCloudWriter(void* dev, u32 from) {
    if (!g_rt0) return;
    typedef HRESULT(WINAPI * SurfDescFn)(void*, u32*);
    u32 d[8] = {};
    if (FAILED(((SurfDescFn)(*(void***)g_rt0)[12])(g_rt0, d)) || d[6] != 256 || d[7] != 256 || d[0] != 21 /*A8R8G8B8*/) return;
    static u32 done[16];
    static int count = 0;
    const u32 key = from * 31u + (u32)(uintptr_t)g_curPs;
    for (int i = 0; i < count; ++i) if (done[i] == key) return;
    if (count >= 16) return;
    done[count++] = key;
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%sUltimateFix\\cloudw_%08X_%08X.txt", g_paths.gameDir, from, (u32)(uintptr_t)g_curPs);
    WriteDrawFile(dev, from, path);
    char t[4][48];
    for (int st = 0; st < 4; ++st) DescribeTexture(g_stageTex[st], t[st]);
    Log("cloud: a draw from %08X (shader %08X) writes a 256x256 target (%08X): s0 %s | s1 %s | s2 %s | s3 %s", from, (u32)(uintptr_t)g_curPs,
        (u32)(uintptr_t)g_rt0, t[0], t[1], t[2], t[3]);
    typedef HRESULT(WINAPI * SaveFn)(const wchar_t*, DWORD, void*, const void*);
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    SaveFn save = d3dx ? (SaveFn)(void*)GetProcAddress(d3dx, "D3DXSaveTextureToFileW") : nullptr;
    for (int st = 0; st < 4 && save; ++st) {
        if (!g_stageTex[st]) continue;
        wsprintfW(path, L"%sUltimateFix\\cloudw_%08X_%08X_s%d.png", g_paths.gameDir, from, (u32)(uintptr_t)g_curPs, st);
        save(path, 3 /*PNG*/, g_stageTex[st], nullptr);
    }
}

void ExplainSmoke(void* dev, u32 from) {
    // the smoke particles only: blended (ALPHABLENDENABLE 1), no depth writes, a DXT3 texture; the same shader family also draws
    // opaque characters, which filled the first list
    typedef HRESULT(WINAPI * GetRsFn)(void*, DWORD, DWORD*);
    GetRsFn getRs = (GetRsFn)(*(void***)dev)[58];
    DWORD blend = 0, zwrite = 1;
    getRs(dev, 27 /*ALPHABLENDENABLE*/, &blend);
    getRs(dev, 14 /*ZWRITEENABLE*/, &zwrite);
    void* t0 = g_stageTex[0];
    if (!blend || zwrite || !t0) return;
    void** vt = *(void***)t0;
    typedef DWORD(WINAPI * TypeFn)(void*);
    typedef HRESULT(WINAPI * DescFn)(void*, UINT, void*);
    u32 desc[8] = {0};
    if (((TypeFn)vt[10])(t0) != 3 || FAILED(((DescFn)vt[17])(t0, 0, desc)) || desc[0] != 0x33545844u /*DXT3*/) return;
    NoteSmokeMaterial();
    static u32 seen[16];
    static int count = 0;
    const u32 tex = (u32)(uintptr_t)t0;
    for (int i = 0; i < count; ++i) if (seen[i] == tex) return;
    if (count >= 16) return;
    seen[count++] = tex;
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%sUltimateFix\\smoke_%08X_%08X.txt", g_paths.gameDir, from, tex);
    WriteDrawFile(dev, from, path);
    Log("smoke: a particle draw explained (%ux%u DXT3 texture %08X)", desc[6], desc[7], tex);
    typedef HRESULT(WINAPI * SaveFn)(const wchar_t*, DWORD, void*, const void*);
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    SaveFn save = d3dx ? (SaveFn)(void*)GetProcAddress(d3dx, "D3DXSaveTextureToFileW") : nullptr;
    if (save) {
        wsprintfW(path, L"%sUltimateFix\\smoke_%08X_%08X.png", g_paths.gameDir, from, tex);
        save(path, 3 /*PNG*/, t0, nullptr);
    }
}
void DumpDraw(void* dev, u32 from) {
    static u32 done[8];
    static int doneCount = 0;
    for (int i = 0; i < doneCount; ++i) if (done[i] == from) return;
    if (doneCount >= 8) return;
    done[doneCount++] = from;
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%sUltimateFix\\draw_%08X.txt", g_paths.gameDir, from);
    WriteDrawFile(dev, from, path);
}
void WriteDrawFile(void* dev, u32 from, const wchar_t* path) {
    const DWORD* code = nullptr;
    for (int i = 0; i < g_psCount && !code; ++i) if (g_psObj[i] == g_curPs) code = g_psCode[i];
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD put;
    char line[256];
    int n = wsprintfA(line, "draw from %08X, pixel shader %08X, bytecode at %08X\r\n\r\n", from, (u32)(uintptr_t)g_curPs, (u32)(uintptr_t)code);
    WriteFile(f, line, n, &put, nullptr);
    typedef HRESULT(WINAPI * DisFn)(const DWORD*, BOOL, const char*, void**);
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    DisFn dis = d3dx ? (DisFn)(void*)GetProcAddress(d3dx, "D3DXDisassembleShader") : nullptr;
    void* buf = nullptr;
    if (code && dis && SUCCEEDED(dis(code, FALSE, nullptr, &buf)) && buf) {
        void** vt = *(void***)buf;
        typedef void*(WINAPI * PtrFn)(void*);
        typedef DWORD(WINAPI * SizeFn)(void*);
        typedef ULONG(WINAPI * RelFn)(void*);
        const char* text = (const char*)((PtrFn)vt[3])(buf);
        DWORD size = ((SizeFn)vt[4])(buf);
        while (size && text[size - 1] == 0) --size;
        WriteFile(f, text, size, &put, nullptr);
        ((RelFn)vt[2])(buf);
    } else {
        n = wsprintfA(line, "(no disassembly)\r\n");
        WriteFile(f, line, n, &put, nullptr);
    }
    n = wsprintfA(line, "\r\nPS constants (thousandths):\r\n");
    WriteFile(f, line, n, &put, nullptr);
    for (int r = 0; r < 32; ++r) {
        n = wsprintfA(line, "c%d %d %d %d %d\r\n", r, Milli(g_psCx[r][0]), Milli(g_psCx[r][1]), Milli(g_psCx[r][2]), Milli(g_psCx[r][3]));
        WriteFile(f, line, n, &put, nullptr);
    }
    typedef HRESULT(WINAPI * GetRsFn)(void*, DWORD, DWORD*);
    GetRsFn getRs = (GetRsFn)(*(void***)dev)[58];  // IDirect3DDevice9::GetRenderState
    static const struct { DWORD id; const char* name; } states[] = {
        {7, "ZENABLE"}, {14, "ZWRITEENABLE"}, {15, "ALPHATESTENABLE"}, {24, "ALPHAREF"}, {25, "ALPHAFUNC"}, {27, "ALPHABLENDENABLE"},
        {19, "SRCBLEND"}, {20, "DESTBLEND"}, {171, "BLENDOP"}, {206, "SEPARATEALPHABLENDENABLE"}, {207, "SRCBLENDALPHA"},
        {208, "DESTBLENDALPHA"}, {209, "BLENDOPALPHA"}, {168, "COLORWRITEENABLE"}, {22, "CULLMODE"}, {28, "FOGENABLE"}, {194, "SRGBWRITEENABLE"}};
    n = wsprintfA(line, "\r\nrender states:\r\n");
    WriteFile(f, line, n, &put, nullptr);
    for (const auto& st : states) {
        DWORD v = 0;
        getRs(dev, st.id, &v);
        n = wsprintfA(line, "%s = %u\r\n", st.name, v);
        WriteFile(f, line, n, &put, nullptr);
    }
    // the vertex shader (its bytecode from the device), its constants that are not zero, the vertex layout and the first vertices
    typedef HRESULT(WINAPI * GetVsFn)(void*, void**);
    typedef HRESULT(WINAPI * GetFuncFn)(void*, void*, UINT*);
    typedef HRESULT(WINAPI * GetVsConstFn)(void*, UINT, float*, UINT);
    typedef ULONG(WINAPI * RelFn2)(void*);
    void** dvt = *(void***)dev;
    void* vs = nullptr;
    if (SUCCEEDED(((GetVsFn)dvt[93])(dev, &vs)) && vs) {
        UINT size = 0;
        ((GetFuncFn)(*(void***)vs)[4])(vs, nullptr, &size);
        DWORD* vcode = size && size < (1u << 20) ? (DWORD*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
        if (vcode && SUCCEEDED(((GetFuncFn)(*(void***)vs)[4])(vs, vcode, &size))) {
            n = wsprintfA(line, "\r\nvertex shader %08X:\r\n", (u32)(uintptr_t)vs);
            WriteFile(f, line, n, &put, nullptr);
            void* vbuf = nullptr;
            if (dis && SUCCEEDED(dis(vcode, FALSE, nullptr, &vbuf)) && vbuf) {
                void** bvt = *(void***)vbuf;
                typedef void*(WINAPI * PtrFn2)(void*);
                typedef DWORD(WINAPI * SizeFn2)(void*);
                const char* text = (const char*)((PtrFn2)bvt[3])(vbuf);
                DWORD tsize = ((SizeFn2)bvt[4])(vbuf);
                while (tsize && text[tsize - 1] == 0) --tsize;
                WriteFile(f, text, tsize, &put, nullptr);
                ((RelFn2)bvt[2])(vbuf);
            }
        }
        if (vcode) VirtualFree(vcode, 0, MEM_RELEASE);
        ((RelFn2)(*(void***)vs)[2])(vs);
    }
    static float vc[256][4];
    if (SUCCEEDED(((GetVsConstFn)dvt[95])(dev, 0, &vc[0][0], 256))) {
        n = wsprintfA(line, "\r\nVS constants that are not zero (thousandths):\r\n");
        WriteFile(f, line, n, &put, nullptr);
        for (int r = 0; r < 256; ++r) {
            if (vc[r][0] == 0.f && vc[r][1] == 0.f && vc[r][2] == 0.f && vc[r][3] == 0.f) continue;
            n = wsprintfA(line, "c%d %d %d %d %d\r\n", r, Milli(vc[r][0]), Milli(vc[r][1]), Milli(vc[r][2]), Milli(vc[r][3]));
            WriteFile(f, line, n, &put, nullptr);
        }
    }
    typedef HRESULT(WINAPI * GetDeclFn)(void*, void**);
    typedef HRESULT(WINAPI * DeclElemsFn)(void*, void*, UINT*);
    void* decl = nullptr;
    if (SUCCEEDED(((GetDeclFn)dvt[88])(dev, &decl)) && decl) {
        u8 el[64 * 8];
        UINT count = 64;
        if (SUCCEEDED(((DeclElemsFn)(*(void***)decl)[4])(decl, el, &count))) {
            n = wsprintfA(line, "\r\nvertex layout (stream offset type usage index):\r\n");
            WriteFile(f, line, n, &put, nullptr);
            for (UINT i = 0; i < count && i < 64; ++i) {
                const u16 stream = *(const u16*)(el + i * 8), offset = *(const u16*)(el + i * 8 + 2);
                if (stream == 0xFF) break;
                n = wsprintfA(line, "%u %u %u %u %u\r\n", stream, offset, el[i * 8 + 4], el[i * 8 + 6], el[i * 8 + 7]);
                WriteFile(f, line, n, &put, nullptr);
            }
        }
        ((RelFn2)(*(void***)decl)[2])(decl);
    }
    typedef HRESULT(WINAPI * GetStreamFn)(void*, UINT, void**, UINT*, UINT*);
    typedef HRESULT(WINAPI * VbLockFn)(void*, UINT, UINT, void**, DWORD);
    typedef HRESULT(WINAPI * VbUnlockFn)(void*);
    void* vb = nullptr;
    UINT vbOffset = 0, stride = 0;
    if (SUCCEEDED(((GetStreamFn)dvt[101])(dev, 0, &vb, &vbOffset, &stride)) && vb) {
        const UINT first = vbOffset + g_drawFirstVertex * stride, bytes = stride * 8;
        void* ptr = nullptr;
        if (stride && stride <= 256 && SUCCEEDED(((VbLockFn)(*(void***)vb)[11])(vb, first, bytes, &ptr, 0x10 /*READONLY*/)) && ptr) {
            n = wsprintfA(line, "\r\nfirst 8 vertices of stream 0 (stride %u, as floats in thousandths and as hex):\r\n", stride);
            WriteFile(f, line, n, &put, nullptr);
            for (int v = 0; v < 8; ++v) {
                const u32* w = (const u32*)((const u8*)ptr + v * stride);
                for (UINT k = 0; k < stride / 4; ++k) {
                    float fv;
                    __builtin_memcpy(&fv, &w[k], 4);
                    const bool plain = (fv > -1.0e6f && fv < 1.0e6f);
                    n = plain ? wsprintfA(line, "%d/%08X ", Milli(fv), w[k]) : wsprintfA(line, "-/%08X ", w[k]);
                    WriteFile(f, line, n, &put, nullptr);
                }
                WriteFile(f, "\r\n", 2, &put, nullptr);
            }
            ((VbUnlockFn)(*(void***)vb)[12])(vb);
        } else {
            n = wsprintfA(line, "\r\nstream 0: stride %u, the vertex buffer could not be read\r\n", stride);
            WriteFile(f, line, n, &put, nullptr);
        }
        ((RelFn2)(*(void***)vb)[2])(vb);
    }
    CloseHandle(f);
    Log("water: the draw from %08X explained in %S", from, path);
}

// [Debug] TraceWater=1: the draw scan (Numpad 3). The game's draws of a frame are grouped by where in DP.exe they come from and the
// pixel shader they use (a smoke made of many particles is one group). The scan leaves out one group per frame and measures the
// brightness of the middle of the picture (a 64x64 patch read back at Present, before the mod's own filters); every 8th frame
// leaves out nothing, for the baseline. Groups whose absence makes the middle brighter go to the log, the ten strongest are listed
// at the end whatever their size, and the three strongest are explained in UltimateFix\hunt_<caller>_<shader>.txt with their
// textures saved as PNG. Standing still keeps the picture the same from frame to frame.
struct DrawGroup {
    u32 from, shader, tex0, tex1;  // a smoke of many particles shares its textures; the scenery behind it uses other ones
};
struct ScanHit {
    int group, gain, draws;
    u32 from, shader, count, tex0, tex1;
    char tex[4][40];
};
constexpr int kMaxGroups = 4096;
constexpr int kGroupSlots = 8192;  // open hash: group index + 1, 0 empty
u16 g_groupSlot[kGroupSlots];
DrawGroup g_groups[kMaxGroups];
int g_groupCount = 0;
struct DrawScan {
    bool on, survey;       // survey: the coming frame lists the groups
    int next;              // the group to leave out next
    int skipping;          // the group left out in the frame being drawn now, -1 none
    int frames, measured;
    int base, baseCount;   // running baseline (sum of R+G+B, 0..765)
    int first;             // brightness with every draw, -1 unknown
    int allCount;          // groups measured
    int hits;
    int explain[3], explainCount, explainStep;
    DWORD lastNotice;
} g_scan;
ScanHit* g_effect = nullptr;  // per group (VirtualAlloc'd, kMaxGroups): its effect once measured, draws 0 = not measured
ScanHit g_skipInfo;        // the left-out group in this frame: its first draw, and how many were left out
int g_explainGroup = -1;   // explain the first draw of this group in the coming frame (not left out)
int g_huntDraws = 0;       // the game's draws so far in this frame
void* g_huntDev = nullptr;

int GroupOf(u32 from, u32 shader, u32 tex0, u32 tex1, bool add) {
    u32 h = (from * 2654435761u) ^ (shader * 40503u) ^ (tex0 * 2246822519u) ^ (tex1 * 3266489917u);
    for (int probe = 0; probe < kGroupSlots; ++probe, ++h) {
        u16& slot = g_groupSlot[h & (kGroupSlots - 1)];
        if (!slot) {
            if (!add || g_groupCount >= kMaxGroups) return -1;
            g_groups[g_groupCount] = {from, shader, tex0, tex1};
            slot = (u16)(++g_groupCount);
            return g_groupCount - 1;
        }
        const DrawGroup& g = g_groups[slot - 1];
        if (g.from == from && g.shader == shader && g.tex0 == tex0 && g.tex1 == tex1) return slot - 1;
    }
    return -1;
}

void SaveStagePng(void* tex, int stage, u32 from) {
    typedef HRESULT(WINAPI * SaveFn)(const wchar_t*, DWORD, void*, const void*);
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    SaveFn save = d3dx ? (SaveFn)(void*)GetProcAddress(d3dx, "D3DXSaveTextureToFileW") : nullptr;
    if (!save || !tex) return;
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%sUltimateFix\\hunt_%08X_%08X_s%d.png", g_paths.gameDir, from, (u32)(uintptr_t)g_curPs, stage);
    const HRESULT hr = save(path, 3 /*D3DXIFF_PNG*/, tex, nullptr);
    Log("hunt:   s%d saved as %S (%s)", stage, path, SUCCEEDED(hr) ? "ok" : "failed");
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

// true: leave this draw out
bool HuntSkips(void* dev, u32 from, UINT count) {
    static u32 selfLo = 0, selfHi = 0;
    if (!selfLo) {
        const u8* volatile base = (const u8*)&__ImageBase;
        selfLo = (u32)(uintptr_t)base;
        selfHi = selfLo + ((const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew))->OptionalHeader.SizeOfImage;
    }
    if (from >= selfLo && from < selfHi) return false;  // the mod's own drawing (menu, filters) is not counted
    if (!g_scan.on && g_explainGroup < 0) return false;
    g_huntDev = dev;
    ++g_huntDraws;
    const u32 shader = (u32)(uintptr_t)g_curPs, tex0 = (u32)(uintptr_t)g_stageTex[0], tex1 = (u32)(uintptr_t)g_stageTex[1];
    if (g_scan.survey) {
        GroupOf(from, shader, tex0, tex1, true);
        return false;
    }
    auto same = [&](const DrawGroup& g) { return g.from == from && g.shader == shader && g.tex0 == tex0 && g.tex1 == tex1; };
    if (g_explainGroup >= 0 && same(g_groups[g_explainGroup])) {
        g_explainGroup = -1;
        char t[4][48];
        for (int st = 0; st < 4; ++st) DescribeTexture(g_stageTex[st], t[st]);
        Log("hunt: explained: %u primitives from %08X, pixel shader %08X: s0 %s | s1 %s | s2 %s | s3 %s", count, from, shader, t[0], t[1], t[2], t[3]);
        wchar_t path[MAX_PATH + 64];
        wsprintfW(path, L"%sUltimateFix\\hunt_%08X_%08X.txt", g_paths.gameDir, from, shader);
        WriteDrawFile(dev, from, path);
        for (int st = 0; st < 4; ++st) SaveStagePng(g_stageTex[st], st, from);
        return false;
    }
    if (!g_scan.on || g_scan.skipping < 0) return false;
    if (!same(g_groups[g_scan.skipping])) return false;
    if (g_skipInfo.draws++ == 0) {
        g_skipInfo.from = from;
        g_skipInfo.shader = shader;
        g_skipInfo.count = count;
        g_skipInfo.tex0 = tex0;
        g_skipInfo.tex1 = tex1;
        for (int st = 0; st < 4; ++st) {
            char t[48];
            DescribeTexture(g_stageTex[st], t);
            lstrcpynA(g_skipInfo.tex[st], t, 40);
        }
    }
    return true;
}

// The brightness (R+G+B, 0..765) of the 64x64 middle of the back buffer, or -1
int MiddleBrightness(void* dev) {
    if (!dev) return -1;
    void** vt = *(void***)dev;
    typedef HRESULT(WINAPI * GetBbFn)(void*, UINT, UINT, DWORD, void**);
    typedef HRESULT(WINAPI * GetRtDataFn)(void*, void*, void*);
    typedef HRESULT(WINAPI * PlainFn)(void*, UINT, UINT, DWORD, DWORD, void**, void*);
    typedef HRESULT(WINAPI * SurfDescFn)(void*, u32*);
    typedef HRESULT(WINAPI * LockFn)(void*, void*, const RECT*, DWORD);
    typedef HRESULT(WINAPI * UnlockFn)(void*);
    typedef ULONG(WINAPI * RelFn)(void*);
    void *bb = nullptr, *rt = nullptr, *sys = nullptr;
    int result = -1;
    CreateSurfaceFn mkRt = r_CreateRenderTarget ? r_CreateRenderTarget : (CreateSurfaceFn)vt[kCreateRenderTarget];
    StretchRectFn stretch = r_StretchRect ? r_StretchRect : (StretchRectFn)vt[kStretchRect];
    if (SUCCEEDED(((GetBbFn)vt[18])(dev, 0, 0, 0, &bb)) && bb) {
        u32 d[8] = {};
        ((SurfDescFn)(*(void***)bb)[12])(bb, d);  // Format, Type, Usage, Pool, MultiSampleType, Quality, Width, Height
        const LONG w = (LONG)d[6], h = (LONG)d[7];
        const RECT src = {w / 2 - 32, h / 2 - 32, w / 2 + 32, h / 2 + 32};
        if (w > 64 && h > 64 && SUCCEEDED(mkRt(dev, 64, 64, 21 /*A8R8G8B8*/, 0, 0, FALSE, &rt, nullptr)) && rt &&
            SUCCEEDED(stretch(dev, bb, &src, rt, nullptr, 0 /*D3DTEXF_NONE*/)) &&
            SUCCEEDED(((PlainFn)vt[36])(dev, 64, 64, 21, 2 /*SYSTEMMEM*/, &sys, nullptr)) && sys &&
            SUCCEEDED(((GetRtDataFn)vt[32])(dev, rt, sys))) {
            struct { INT pitch; void* bits; } lr = {0, nullptr};
            if (SUCCEEDED(((LockFn)(*(void***)sys)[13])(sys, &lr, nullptr, 0x10 /*READONLY*/)) && lr.bits) {
                u64 sum = 0;
                for (int y = 0; y < 64; ++y) {
                    const u8* row = (const u8*)lr.bits + y * lr.pitch;
                    for (int x = 0; x < 64; ++x) sum += row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2];
                }
                result = (int)(sum / 4096);
                ((UnlockFn)(*(void***)sys)[14])(sys);
            }
        }
    }
    if (sys) ((RelFn)(*(void***)sys)[2])(sys);
    if (rt) ((RelFn)(*(void***)rt)[2])(rt);
    if (bb) ((RelFn)(*(void***)bb)[2])(bb);
    return result;
}

// Note: wsprintfA (Log) has no '+' flag; "%+d" shifted the arguments and a %s read a number as a string (a crash at the end of a scan).
void NoteEffect(const ScanHit& h) {
    if (!g_effect || h.group < 0 || h.group >= kMaxGroups) return;
    g_effect[h.group] = h;
    ++g_scan.allCount;
}

// The back buffer as the scan measures it, with the measured square in the middle, to UltimateFix\hunt_frame.png
void SaveMeasuredFrame(void* dev) {
    typedef HRESULT(WINAPI * SaveSurfFn)(const wchar_t*, DWORD, void*, const void*, const RECT*);
    typedef HRESULT(WINAPI * GetBbFn)(void*, UINT, UINT, DWORD, void**);
    typedef ULONG(WINAPI * RelFn)(void*);
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    SaveSurfFn save = d3dx ? (SaveSurfFn)(void*)GetProcAddress(d3dx, "D3DXSaveSurfaceToFileW") : nullptr;
    void* bb = nullptr;
    if (!save || !dev || FAILED(((GetBbFn)(*(void***)dev)[18])(dev, 0, 0, 0, &bb)) || !bb) return;
    wchar_t path[MAX_PATH + 32];
    wsprintfW(path, L"%sUltimateFix\\hunt_frame.png", g_paths.gameDir);
    const HRESULT hr = save(path, 3 /*PNG*/, bb, nullptr, nullptr);
    ((RelFn)(*(void***)bb)[2])(bb);
    Log("hunt: the measured picture saved as %S (%s); the measured square is its middle 64x64", path, SUCCEEDED(hr) ? "ok" : "failed");
}

void LogEffect(const char* what, int rank, const ScanHit& h) {
    Log("hunt: %s %d: %d without %d draws from %08X, shader %08X, s0 %08X s1 %08X (first: %u primitives): s0 %s | s1 %s | s2 %s | s3 %s", what, rank,
        h.gain, h.draws, h.from, h.shader, h.tex0, h.tex1, h.count, h.tex[0], h.tex[1], h.tex[2], h.tex[3]);
}

void ScanFinish() {
    g_scan.on = false;
    g_scan.skipping = -1;
    Log("hunt: scan done over %d groups (%d frames, %d measured), middle %d with everything, baseline %d; %d groups make it brighter by 12 or more",
        g_groupCount, g_scan.frames, g_scan.measured, g_scan.first, g_scan.base, g_scan.hits);
    // the ten that brighten the middle most, and the five that darken it most (these show that the measuring works)
    bool used[kMaxGroups];
    __builtin_memset(used, 0, sizeof(used));
    g_scan.explainCount = 0;
    for (int rank = 1; rank <= 10 && g_effect; ++rank) {
        int best = -1;
        for (int k = 0; k < g_groupCount; ++k)
            if (!used[k] && g_effect[k].draws && (best < 0 || g_effect[k].gain > g_effect[best].gain)) best = k;
        if (best < 0) break;
        used[best] = true;
        LogEffect("brighter", rank, g_effect[best]);
        if (g_scan.explainCount < 3 && g_effect[best].gain > 0) g_scan.explain[g_scan.explainCount++] = best;
    }
    for (int rank = 1; rank <= 5 && g_effect; ++rank) {
        int worst = -1;
        for (int k = 0; k < g_groupCount; ++k)
            if (!used[k] && g_effect[k].draws && (worst < 0 || g_effect[k].gain < g_effect[worst].gain)) worst = k;
        if (worst < 0) break;
        used[worst] = true;
        LogEffect("darker", rank, g_effect[worst]);
    }
    g_scan.explainStep = 0;
    wchar_t uk[220], en[220];
    if (g_scan.measured == 0) {
        wsprintfW(uk, L"Яскравість не вдалося виміряти. Пришли ufx.log.");
        wsprintfW(en, L"The brightness could not be measured. Send ufx.log.");
    } else {
        wsprintfW(uk, L"Груп малювань, що темнять центр: %d. Пришли ufx.log і файли UltimateFix\\hunt_*.", g_scan.hits);
        wsprintfW(en, L"Draw groups that darken the middle: %d. Send ufx.log and the UltimateFix\\hunt_* files.", g_scan.hits);
    }
    ShowNotice(L"Сканування завершено", L"Scan finished", uk, en, 12);
}

// Once per frame, from WaterFrame (at Present, the game's picture complete)
void HuntFrame(bool k3, bool, bool, bool) {
    const int draws = g_huntDraws;
    g_huntDraws = 0;
    if (g_scan.explainStep < g_scan.explainCount) {  // after a scan: explain the strongest groups, one per frame
        g_explainGroup = g_scan.explain[g_scan.explainStep++];
        return;
    }
    if (k3) {
        if (g_scan.on) {
            Log("hunt: scan stopped at group %d of %d", g_scan.next, g_groupCount);
            ScanFinish();
            return;
        }
        __builtin_memset(&g_scan, 0, sizeof(g_scan));
        __builtin_memset(g_groupSlot, 0, sizeof(g_groupSlot));
        if (!g_effect) g_effect = (ScanHit*)VirtualAlloc(nullptr, sizeof(ScanHit) * kMaxGroups, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (g_effect) __builtin_memset(g_effect, 0, sizeof(ScanHit) * kMaxGroups);
        g_groupCount = 0;
        g_scan.on = true;
        g_scan.survey = true;  // the coming frame lists the groups
        g_scan.skipping = -1;
        g_scan.first = -1;
        g_scan.lastNotice = GetTickCount();
        ShowNotice(L"Сканування", L"Scan", L"Не рухайтеся, туман у центрі екрана. Numpad 3 зупиняє.",
                   L"Do not move, the fog in the middle of the screen. Numpad 3 stops.", 5);
        return;
    }
    if (!g_scan.on) return;
    if (g_scan.survey) {
        g_scan.survey = false;
        Log("hunt: scan started: %d draws of the game in the frame, %d groups (caller + pixel shader + textures s0, s1)", draws, g_groupCount);
    }
    const int m = MiddleBrightness(g_huntDev);
    ++g_scan.frames;
    if (m >= 0) {
        ++g_scan.measured;
        if (g_scan.first < 0) {
            g_scan.first = m;
            Log("hunt: middle brightness %d (0..765) with every draw", m);
            SaveMeasuredFrame(g_huntDev);
        }
        if (g_scan.skipping < 0) {
            g_scan.base = g_scan.baseCount ? (g_scan.base * 3 + m) / 4 : m;
            ++g_scan.baseCount;
        } else if (g_scan.baseCount) {
            ScanHit h = g_skipInfo;
            h.group = g_scan.skipping;
            h.gain = m - g_scan.base;
            if (h.draws > 0) {
                NoteEffect(h);
                if (h.gain >= 12) {
                    ++g_scan.hits;
                    Log("hunt: group %d left out (%d draws): middle %d (baseline %d, brighter by %d): from %08X, shader %08X", h.group, h.draws, m, g_scan.base,
                        h.gain, h.from, h.shader);
                }
            }
        }
    } else if (g_scan.frames == 1) {
        Log("hunt: the middle of the picture could not be read (device %08X)", (u32)(uintptr_t)g_huntDev);
    }
    __builtin_memset(&g_skipInfo, 0, sizeof(g_skipInfo));
    if (g_scan.frames % 8 == 0 || g_scan.baseCount < 4) {
        g_scan.skipping = -1;
    } else {
        if (g_scan.next >= g_groupCount) {
            ScanFinish();
            return;
        }
        g_scan.skipping = g_scan.next++;
    }
    if (GetTickCount() - g_scan.lastNotice > 3000) {
        g_scan.lastNotice = GetTickCount();
        wchar_t uk[160], en[160];
        wsprintfW(uk, L"%d з %d груп, знайдено %d. Не рухайтеся.", g_scan.next, g_groupCount, g_scan.hits);
        wsprintfW(en, L"%d of %d groups, %d found. Do not move.", g_scan.next, g_groupCount, g_scan.hits);
        ShowNotice(L"Сканування", L"Scan", uk, en, 4);
    }
}

// [Debug] FrameCapture: Numpad 0 arms, the next frame (Present to Present) is written draw by draw to UltimateFix\capture_<time>\.
struct FrameCap {
    bool armed, on;
    HANDLE list;
    wchar_t dir[MAX_PATH];
    int draws, pngs;
    void* shaders[512];
    int shaderCount;
    void* textures[512];
    int textureCount;
} g_cap;
void* g_capDev = nullptr;  // the device of the last draw

void CapWrite(const char* text, int n) {
    DWORD put;
    if (g_cap.list != INVALID_HANDLE_VALUE && g_cap.list) WriteFile(g_cap.list, text, (DWORD)n, &put, nullptr);
}

// The disassembly of a shader (bytecode from the device object) into <dir>\<kind>_<ptr>.txt, once
void CapShader(void* shader, const char* kind) {
    if (!shader) return;
    for (int i = 0; i < g_cap.shaderCount; ++i) if (g_cap.shaders[i] == shader) return;
    if (g_cap.shaderCount < 512) g_cap.shaders[g_cap.shaderCount++] = shader;
    typedef HRESULT(WINAPI * GetFuncFn)(void*, void*, UINT*);
    typedef HRESULT(WINAPI * DisFn)(const DWORD*, BOOL, const char*, void**);
    typedef void*(WINAPI * PtrFn)(void*);
    typedef DWORD(WINAPI * SizeFn)(void*);
    typedef ULONG(WINAPI * RelFn)(void*);
    UINT size = 0;
    ((GetFuncFn)(*(void***)shader)[4])(shader, nullptr, &size);
    if (!size || size > (1u << 20)) return;
    DWORD* code = (DWORD*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!code) return;
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    DisFn dis = d3dx ? (DisFn)(void*)GetProcAddress(d3dx, "D3DXDisassembleShader") : nullptr;
    void* buf = nullptr;
    if (SUCCEEDED(((GetFuncFn)(*(void***)shader)[4])(shader, code, &size)) && dis && SUCCEEDED(dis(code, FALSE, nullptr, &buf)) && buf) {
        wchar_t path[MAX_PATH + 64];
        wsprintfW(path, L"%s\\%S_%08X.txt", g_cap.dir, kind, (u32)(uintptr_t)shader);
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            const char* text = (const char*)((PtrFn)(*(void***)buf)[3])(buf);
            DWORD n = ((SizeFn)(*(void***)buf)[4])(buf), put;
            while (n && text[n - 1] == 0) --n;
            WriteFile(f, text, n, &put, nullptr);
            CloseHandle(f);
        }
        ((RelFn)(*(void***)buf)[2])(buf);
    }
    VirtualFree(code, 0, MEM_RELEASE);
}

// A texture as <dir>\tex_<ptr>.png, once, 2D and at most 1024 on a side (bigger targets take D3DX seconds)
void CapTexture(void* tex) {
    if (!tex || g_cap.pngs >= 400) return;
    for (int i = 0; i < g_cap.textureCount; ++i) if (g_cap.textures[i] == tex) return;
    if (g_cap.textureCount < 512) g_cap.textures[g_cap.textureCount++] = tex;
    typedef DWORD(WINAPI * TypeFn)(void*);
    typedef HRESULT(WINAPI * DescFn)(void*, UINT, void*);
    typedef HRESULT(WINAPI * SaveFn)(const wchar_t*, DWORD, void*, const void*);
    void** vt = *(void***)tex;
    u32 d[8] = {0};
    if (((TypeFn)vt[10])(tex) != 3 || FAILED(((DescFn)vt[17])(tex, 0, d)) || d[6] > 1024 || d[7] > 1024) return;
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    SaveFn save = d3dx ? (SaveFn)(void*)GetProcAddress(d3dx, "D3DXSaveTextureToFileW") : nullptr;
    if (!save) return;
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%s\\tex_%08X.png", g_cap.dir, (u32)(uintptr_t)tex);
    save(path, 3 /*PNG*/, tex, nullptr);
    ++g_cap.pngs;
}

void CapDraw(void* dev, u32 from, const char* kind, DWORD prim, UINT count) {
    if (g_cap.draws >= 4000) return;
    void** dvt = *(void***)dev;
    typedef HRESULT(WINAPI * GetVsFn)(void*, void**);
    typedef HRESULT(WINAPI * GetRsFn)(void*, DWORD, DWORD*);
    typedef ULONG(WINAPI * RelFn)(void*);
    void* vs = nullptr;
    ((GetVsFn)dvt[93])(dev, &vs);
    GetRsFn getRs = (GetRsFn)dvt[58];
    DWORD st[9] = {};
    static const DWORD kIds[9] = {7 /*Z*/, 14 /*ZWRITE*/, 27 /*BLEND*/, 19 /*SRC*/, 20 /*DST*/, 15 /*ATEST*/, 24 /*AREF*/, 168 /*CWRITE*/, 22 /*CULL*/};
    for (int i = 0; i < 9; ++i) getRs(dev, kIds[i], &st[i]);
    char rt[48] = "?";
    if (g_rt0) {
        typedef HRESULT(WINAPI * SurfDescFn)(void*, u32*);
        u32 d[8] = {};
        if (SUCCEEDED(((SurfDescFn)(*(void***)g_rt0)[12])(g_rt0, d))) wsprintfA(rt, "%ux%u fmt %u", d[6], d[7], d[0]);
    }
    const DWORD* code = CurrentPsCode();
    char line[900];
    int n = wsprintfA(line, "%4d %s from %08X prim %u count %u | vs %08X ps %08X (bytecode %08X) | rt %08X %s | z %u zw %u blend %u %u/%u atest %u ref %u cw %X cull %u |",
                      g_cap.draws, kind, from, prim, count, (u32)(uintptr_t)vs, (u32)(uintptr_t)g_curPs, (u32)(uintptr_t)code,
                      (u32)(uintptr_t)g_rt0, rt, st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7], st[8]);
    for (int s = 0; s < 8; ++s) {
        char t[48];
        DescribeTexture(g_stageTex[s], t);
        n += wsprintfA(line + n, " s%d %08X %s;", s, (u32)(uintptr_t)g_stageTex[s], t);
        if (n > 820) break;
    }
    line[n++] = '\r';
    line[n++] = '\n';
    CapWrite(line, n);
    ++g_cap.draws;
    CapShader(vs, "vs");
    CapShader(g_curPs, "ps");
    for (int s = 0; s < 8; ++s) CapTexture(g_stageTex[s]);
    if (vs) ((RelFn)(*(void***)vs)[2])(vs);
}

// From WaterFrame at every Present: Numpad 0 arms; the frame after the arming Present is the one written
void CapFrame(void* dev, bool key) {
    if (g_cap.on) {  // the captured frame ends here: the picture, then close
        g_cap.on = false;
        typedef HRESULT(WINAPI * SaveSurfFn)(const wchar_t*, DWORD, void*, const void*, const RECT*);
        typedef HRESULT(WINAPI * GetBbFn)(void*, UINT, UINT, DWORD, void**);
        typedef ULONG(WINAPI * RelFn)(void*);
        HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
        SaveSurfFn save = d3dx ? (SaveSurfFn)(void*)GetProcAddress(d3dx, "D3DXSaveSurfaceToFileW") : nullptr;
        void* bb = nullptr;
        if (save && dev && SUCCEEDED(((GetBbFn)(*(void***)dev)[18])(dev, 0, 0, 0, &bb)) && bb) {
            wchar_t path[MAX_PATH + 32];
            wsprintfW(path, L"%s\\frame.png", g_cap.dir);
            save(path, 3, bb, nullptr, nullptr);
            ((RelFn)(*(void***)bb)[2])(bb);
        }
        if (g_cap.list && g_cap.list != INVALID_HANDLE_VALUE) CloseHandle(g_cap.list);
        g_cap.list = nullptr;
        Log("capture: %d draws, %d shaders, %d textures written to %S", g_cap.draws, g_cap.shaderCount, g_cap.pngs, g_cap.dir);
        ShowNotice(L"Кадр записано", L"Frame captured", g_cap.dir, g_cap.dir, 6);
        return;
    }
    if (g_cap.armed) {  // the frame from this Present to the next one
        g_cap.armed = false;
        SYSTEMTIME t;
        GetLocalTime(&t);
        wsprintfW(g_cap.dir, L"%sUltimateFix\\capture_%04u%02u%02u_%02u%02u%02u", g_paths.gameDir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                  t.wSecond);
        CreateDirectoryW(g_cap.dir, nullptr);
        wchar_t path[MAX_PATH + 32];
        wsprintfW(path, L"%s\\frame.txt", g_cap.dir);
        g_cap.list = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        g_cap.draws = g_cap.pngs = g_cap.shaderCount = g_cap.textureCount = 0;
        const char head[] = "draws of one frame in order; z/zw/blend etc. are render states; cw = colour write mask; sN = texture stage N\r\n";
        CapWrite(head, (int)sizeof(head) - 1);
        g_cap.on = true;
        return;
    }
    if (key) {
        g_cap.armed = true;
        Log("capture: the next frame will be written (Numpad 0)");
    }
}

HRESULT WINAPI H_DrawIp(void* dev, DWORD prim, INT base, UINT minIdx, UINT verts, UINT start, UINT count) __attribute__((noinline));
HRESULT WINAPI H_DrawIp(void* dev, DWORD prim, INT base, UINT minIdx, UINT verts, UINT start, UINT count) {
    const u32 from = Caller();
    if (g_cfg.traceWater && from == 0x6D6E1Bu) DumpDraw(dev, from);  // the prologue fog
    // FogFix: the smoke volumes (the prologue's doorways) are drawn by FUN_006D69E0 into the G-buffer like a solid wall: the
    // depth/normal shader (it writes oC1) keeps every pixel whose cloud alpha is 0.953 or more, and the cloud target (256x256)
    // has alpha 1 on PC, so the whole volume becomes a dark surface. That one draw is left out; nothing else is touched.
    if (g_cfg.fogFix && from == 0x6D6E1Bu && SmallSquareTarget(g_stageTex[0]) == 256 && CurrentPsWritesOC1()) {
        static int told = 0;
        if (!told++) Log("fog: a smoke volume left out of the G-buffer (it was drawn there as a solid dark wall)");
        return 0;
    }
    g_drawFirstVertex = (UINT)(base + (INT)minIdx);
    g_capDev = dev;
    if (g_cap.on) CapDraw(dev, from, "DIP", prim, count);
    if (g_cfg.traceWater && CurrentPsIsSmoke()) ExplainSmoke(dev, from);
    if (g_cfg.traceWater) {
        ExplainCloudReader(dev, from);
        ExplainCloudWriter(dev, from);
    }
    if (g_cfg.traceWater && HuntSkips(dev, from, count)) return 0;
    if (g_cfg.smokeFix) NoteGreyCloud(from);
    {
        HRESULT hr = 0;
        if (SmokeDraw(dev, [&] { return r_DrawIp(dev, prim, base, minIdx, verts, start, count); }, &hr)) return hr;
    }
    const bool w = NoteWaterDraw(count, from);
    return WaterDraw(dev, w, [&] { return r_DrawIp(dev, prim, base, minIdx, verts, start, count); });
}
HRESULT WINAPI H_DrawP(void* dev, DWORD prim, UINT start, UINT count) __attribute__((noinline));
HRESULT WINAPI H_DrawP(void* dev, DWORD prim, UINT start, UINT count) {
    const u32 from = Caller();
    g_drawFirstVertex = start;
    g_capDev = dev;
    if (g_cap.on) CapDraw(dev, from, "DP ", prim, count);
    if (g_cfg.traceWater && CurrentPsIsSmoke()) ExplainSmoke(dev, from);
    if (g_cfg.traceWater) {
        ExplainCloudReader(dev, from);
        ExplainCloudWriter(dev, from);
    }
    if (g_cfg.traceWater && HuntSkips(dev, from, count)) return 0;
    if (g_cfg.smokeFix) NoteGreyCloud(from);
    {
        HRESULT hr = 0;
        if (SmokeDraw(dev, [&] { return r_DrawP(dev, prim, start, count); }, &hr)) return hr;
    }
    const bool w = NoteWaterDraw(count, from);
    return WaterDraw(dev, w, [&] { return r_DrawP(dev, prim, start, count); });
}

void NotePsConst(UINT start, const float* data, UINT count) {
    if (!data) return;
    for (UINT i = 0; i < count; ++i)
        if (start + i < 32)
            for (int k = 0; k < 4; ++k) g_psCx[start + i][k] = data[i * 4 + k];
    for (UINT i = 0; i < count; ++i)
        if (start + i < 16)
            for (int k = 0; k < 4; ++k) g_psC[start + i][k] = data[i * 4 + k];
}
void NoteVsConst(UINT start, const float* data, UINT count) {
    if (data && start <= 243 && start + count > 243)
        for (int k = 0; k < 4; ++k) g_vsC243[k] = data[(243 - start) * 4 + k];
}

// The shadow pass hands its map size to the pixel shader as register c0 = {512 or 1024, 0, 0, 0}. When the
// maps are enlarged the value has to follow, or the filtering steps no longer match the texels.
HRESULT WINAPI H_SetPsConstF(void* dev, UINT start, const float* data, UINT count) {
    if (g_cfg.traceWater || g_cfg.waterTest) NotePsConst(start, data, count);
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
    if (g_cfg.traceWater || g_cfg.waterTest) NoteVsConst(start, data, count);
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
    if (stage == 0 && tex && g_cfg.glyphTheme) tex = GlyphAtlasFor(dev, tex);
    if (stage < 8) g_stageTex[stage] = tex;
    return r_SetTexture(dev, stage, tex);
}

bool g_waterHooked = false;  // the water hooks are on the device: Numpad 1 may switch the test

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
    if (g_cfg.traceWater || g_cfg.waterTest || g_cfg.fogFix || g_cfg.smokeFix || g_cfg.frameCapture) {
        const bool a = PatchSlot(vt, kSetPixelShaderConstantF, (void*)H_SetPsConstF, (void**)&r_SetPsConstF);
        const bool b = PatchSlot(vt, kSetVertexShaderConstantF, (void*)H_SetVsConstF, (void**)&r_SetVsConstF);
        const bool c = PatchSlot(vt, kSetTexture, (void*)H_SetTexture, (void**)&r_SetTexture);
        const bool d = PatchSlot(vt, kCreatePixelShader, (void*)H_CreatePs, (void**)&r_CreatePs);
        const bool e = PatchSlot(vt, kSetPixelShader, (void*)H_SetPs, (void**)&r_SetPs);
        const bool f = PatchSlot(vt, kDrawIndexedPrimitive, (void*)H_DrawIp, (void**)&r_DrawIp);
        const bool g = PatchSlot(vt, kDrawPrimitive, (void*)H_DrawP, (void**)&r_DrawP);
        if (g_cfg.traceWater || g_cfg.frameCapture) PatchSlot(vt, kSetRenderTarget, (void*)H_SetRt, (void**)&r_SetRt);
        g_waterHooked = a && f && g;
        if (g_cfg.traceWater || g_cfg.waterTest)
            Log("water: diagnostic hooked (%d%d%d%d%d%d%d): each distinct water draw goes to the log once; Numpad 1 switches WaterTest", a, b,
                c, d, e, f, g);
        else
            Log("fog: draw hooks in (%d%d%d): %s%s", c, d && e, f, g_cfg.smokeFix ? "the smoke gets the game's cloud material (SmokeFix)" : "",
                g_cfg.fogFix ? (g_cfg.smokeFix ? ", smoke volumes kept out of the G-buffer" : "smoke volumes kept out of the G-buffer") : "");
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
    if (SUCCEEDED(hr) && dev && *dev) {
        if (g_fullLayer) PatchDevice(*dev);
        return hr;
    }
    // The game throws the result away (0x6CC597) and crashes on the first use of the device (0x6E1F47) with no word to the player.
    // Say what happened instead, in the log and in a window, and end the game cleanly.
    const u32* p = (const u32*)pp;
    Log("d3d: CreateDevice FAILED with %08X (adapter %u, back buffer %ux%u format %u, windowed %u, refresh %u): the game cannot run",
        (u32)hr, adapter, p ? p[0] : 0, p ? p[1] : 0, p ? p[2] : 0, p ? p[8] : 0, p ? p[12] : 0);
    wchar_t text[900];
    if (g_cfg.language == 1)
        wsprintfW(text, L"Deadly Premonition could not create its Direct3D 9 device (error %08X), so the game cannot start.\n\n"
                        L"Try: restart the PC; update the video driver; if DXVK or another d3d9.dll is in the game folder, remove it; "
                        L"set [Graphics] OutputWidth=0 and RenderScale=100 in UltimateFix.ini.\n\nDetails are in UltimateFix\\ufx.log.", (u32)hr);
    else
        wsprintfW(text, L"Deadly Premonition не змогла створити графічний пристрій Direct3D 9 (помилка %08X), тому гра не запуститься.\n\n"
                        L"Спробуйте: перезапустити ПК; оновити драйвер відеокарти; якщо в теці гри є DXVK чи інший d3d9.dll, приберіть його; "
                        L"поставте в UltimateFix.ini [Graphics] OutputWidth=0 і RenderScale=100.\n\nПодробиці в UltimateFix\\ufx.log.", (u32)hr);
    MessageBoxW(focus, text, L"UltimateFIXDP", MB_OK | MB_ICONERROR | MB_TOPMOST);
    ExitProcess(1);
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

void DetectD3dLibrary() { NameD3dLibrary(); }

// The reflection probe (kSiteReflectionChoice): kind (-1 none, 0 mirror, 1 water), weight and blend of the frame's reflection
// source, and a frame counter. WaterFrame turns them into log lines.
volatile u32 g_reflRec[4];

// TraceWater: which draws get a special material slot (FUN_006E10F0; slot 0 of the table at A000A0 carries the smoke cloud), each
// distinct (caller, slot) once in the log. The mesh draw FUN_006D6E70 (the call returning to 006D73E1) keeps its material at
// [ebp-0BCh]: id at +0Eh (u16), flags at +14h, texture bytes at +44h/+45h, slot+1 at +46h. On PC no material asks for slot 0, so
// the smoke sheets never get the cloud. As a test (Numpad 6 switches it), the material seen right before the smoke sheet draws
// gets slot 0, and the game binds the cloud and picks its own shader for it.
struct MatInfo {
    u32 id, flags;
    int tex0, tex1, slotByte;
    bool valid;
} g_lastMat;
u32 g_smokeMatIds[8];
int g_smokeMatCount = 0;
bool g_forceSmokeSlot = false;  // test13: the game's shader for the slot is the deferred one, which does not read the cloud

extern "C" int __cdecl MaterialSlotWorker(u32 caller, int slot, u32 ebp) {
    if (caller == 0x6D73E1u && ebp) {
        const u8* mat = *(const u8* const*)(uintptr_t)(ebp - 0xBC);
        if (mat) {
            g_lastMat.id = *(const u16*)(mat + 0x0E);
            g_lastMat.flags = *(const u32*)(mat + 0x14);
            g_lastMat.tex0 = (signed char)mat[0x44];
            g_lastMat.tex1 = (signed char)mat[0x45];
            g_lastMat.slotByte = (signed char)mat[0x46];
            g_lastMat.valid = true;
            if (g_forceSmokeSlot)
                for (int i = 0; i < g_smokeMatCount; ++i)
                    if (g_smokeMatIds[i] == g_lastMat.id) {
                        static u32 told[8];
                        static int toldCount = 0;
                        bool seen = false;
                        for (int k = 0; k < toldCount; ++k) seen |= told[k] == g_lastMat.id;
                        if (!seen && toldCount < 8) {
                            told[toldCount++] = g_lastMat.id;
                            Log("smoke: material %u gets the cloud slot 0 (was %d)", g_lastMat.id, slot);
                        }
                        slot = 0;
                    }
        }
    } else if (caller == 0x6D73E1u) {
        g_lastMat.valid = false;
    }
    static u32 seen[48];
    static int count = 0;
    const u32 key = caller * 7u + (u32)(slot + 1);
    bool known = false;
    for (int i = 0; i < count; ++i) known |= seen[i] == key;
    if (!known && count < 48) {
        seen[count++] = key;
        Log("material slot: %d set from %08X%s", slot, caller, slot == 0 ? " (slot 0 = the smoke cloud material)" : "");
    }
    return slot;
}

// At a smoke sheet draw (TraceWater): the material set last is the sheets' one
void NoteSmokeMaterial() {
    if (!g_lastMat.valid) return;
    for (int i = 0; i < g_smokeMatCount; ++i) if (g_smokeMatIds[i] == g_lastMat.id) return;
    if (g_smokeMatCount >= 8) return;
    g_smokeMatIds[g_smokeMatCount++] = g_lastMat.id;
    Log("smoke: the sheets' material is %u (flags %08X, textures %d %d, slot byte %d)", g_lastMat.id, g_lastMat.flags, g_lastMat.tex0,
        g_lastMat.tex1, g_lastMat.slotByte);
}

void InstallMaterialSlotProbe() {
    const SiteDef& s = kSiteMaterialSlot;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("material slot: the setter at %08X differs from DP.exe 1.01b, no probe", s.va);
        return;
    }
    u8* stub = AllocStub(48);
    if (!stub) return;
    BuildSlotProbe(stub, (u32)(uintptr_t)stub, (u32)(uintptr_t)&MaterialSlotWorker);
    if (PatchJump(s.va, s.bytes, s.len, stub)) Log("material slot: probe installed (which draws get a special material)");
}

void InstallReflectionProbe() {
    const SiteDef& s = kSiteReflectionChoice;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("water: the reflection source check at %08X differs from DP.exe 1.01b, no reflection probe", s.va);
        return;
    }
    u8* stub = AllocStub(64);
    if (!stub) return;
    BuildReflectionProbe(stub, (u32)(uintptr_t)stub, (u32)(uintptr_t)g_reflRec, kReflectionChoiceBack);
    if (PatchJump(s.va, s.bytes, s.len, stub)) Log("water: reflection probe installed (which reflection passes the frame runs)");
}

// Once per frame (display.cpp): with the water hooks on, Numpad 1 switches the water test, so the same view can be compared.
void WaterFrame() {
    if (!g_waterHooked) return;
    if (g_cfg.frameCapture) {
        static bool held0 = false;
        DWORD pid0 = 0;
        HWND fg0 = GetForegroundWindow();
        if (fg0) GetWindowThreadProcessId(fg0, &pid0);
        const bool down = pid0 == GetCurrentProcessId() && !ui::IsOpen() && (GetAsyncKeyState(VK_NUMPAD0) & 0x8000);
        CapFrame(g_huntDev ? g_huntDev : g_capDev, down && !held0);
        held0 = down;
    }
    static DWORD s_start = 0;
    static bool s_counted = false;
    if (!s_start) s_start = GetTickCount();
    if (!s_counted && GetTickCount() - s_start > 10000) {
        s_counted = true;
        Log("water: %d water pixel shaders recognised (the game has 272); %d distinct water draws so far", g_waterShaderCount, g_waterKeyCount);
    }
    {   // the reflection source of the frame, every 2 s when it changed (at most 80 lines)
        static DWORD s_last = 0;
        static int s_lines = 0, s_prevKind = -2, s_prevFar = -1, s_prevNear = -1;
        static u32 s_prevCount = 0;
        const DWORD t = GetTickCount();
        if (s_lines < 80 && t - s_last > 2000 && g_reflRec[3] != s_prevCount) {
            s_last = t;
            s_prevCount = g_reflRec[3];
            const int kind = (int)g_reflRec[0];
            float weight, blend;
            u32 w = g_reflRec[1], b = g_reflRec[2];
            __builtin_memcpy(&weight, &w, 4);
            __builtin_memcpy(&blend, &b, 4);
            const int farPass = kind != -1 && weight > 0.f && blend > 0.01f;
            const int nearPass = kind != -1 && weight > 0.f && blend < 0.99f;
            if (kind != s_prevKind || farPass != s_prevFar || nearPass != s_prevNear) {
                ++s_lines;
                s_prevKind = kind; s_prevFar = farPass; s_prevNear = nearPass;
                Log("water: reflection source %s, weight %d, blend %d (thousandths): far pass %s, near pass %s",
                    kind == -1 ? "none" : kind == 1 ? "water" : "mirror", Milli(weight), Milli(blend), farPass ? "runs" : "no", nearPass ? "runs" : "no");
            }
        }
    }
    static bool was = false;
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (fg) GetWindowThreadProcessId(fg, &pid);
    const bool ours = pid == GetCurrentProcessId() && !ui::IsOpen();  // not while typing a number in F10
    const bool now = ours && (GetAsyncKeyState(VK_NUMPAD1) & 0x8000);
    if (g_cfg.traceWater) {
        static bool held[4];
        bool hit[4];
        for (int k = 0; k < 4; ++k) {
            const bool d = ours && (GetAsyncKeyState(VK_NUMPAD3 + k) & 0x8000);
            hit[k] = d && !held[k];
            held[k] = d;
        }
        HuntFrame(hit[0], hit[1], hit[2], hit[3]);
        if (hit[3]) {  // Numpad 6: the cloud slot for the smoke sheets on or off
            g_forceSmokeSlot = !g_forceSmokeSlot;
            Log("smoke: the cloud slot for the smoke sheets %s (Numpad 6)", g_forceSmokeSlot ? "on" : "off");
            ShowNotice(g_forceSmokeSlot ? L"Хмара на дим: увімкнено" : L"Хмара на дим: вимкнено",
                       g_forceSmokeSlot ? L"Cloud on the smoke: on" : L"Cloud on the smoke: off",
                       L"Numpad 6 перемикає. Порівняйте дим біля дверей.", L"Numpad 6 switches it. Compare the smoke at the door.", 4);
        }
    }
    if (now && !was) {
        g_cfg.waterTest = (g_cfg.waterTest + 1) % 3;
        Log("water: WaterTest %d (Numpad 1): c10.z %s", g_cfg.waterTest, g_cfg.waterTest == 1 ? "-0.75" : g_cfg.waterTest == 2 ? "0" : "as the game sets it");
        if (g_cfg.waterTest == 1)
            ShowNotice(L"Тест води 1 з 2: відбиття", L"Water test 1 of 2: reflection", L"c10.z = -0.75: більше відбиття. Numpad 1 далі.",
                       L"c10.z = -0.75: mostly reflection. Numpad 1 for the next.", 3);
        else if (g_cfg.waterTest == 2)
            ShowNotice(L"Тест води 2 з 2: за прозорістю", L"Water test 2 of 2: by alpha", L"c10.z = 0: текстура за власною прозорістю. Numpad 1 вимикає.",
                       L"c10.z = 0: the texture by its own alpha. Numpad 1 turns it off.", 3);
        else
            ShowNotice(L"Тест води: вимкнено", L"Water test: off", L"Вода як у грі. Numpad 1 вмикає.", L"Water as the game draws it. Numpad 1 turns the test on.", 3);
    }
    was = now;
}  // display.cpp, at the first Present

bool InstallD3dTrace(bool full) {
    if (g_cfg.traceWater) {
        InstallReflectionProbe();
        InstallMaterialSlotProbe();
    }
    g_fullLayer = full;
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
    if (full)
        Log("d3d: device layer installed (tracing %s, shadow constant %s)", g_cfg.traceResources ? "on" : "off", g_cfg.shadowScale > 100 ? "on" : "off");
    else
        Log("d3d: device creation watched (a failed CreateDevice is reported instead of a crash); the device layer is not needed");
    return true;
}

}  // namespace ufx
