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
bool g_d3dWrapper = false;  // a d3d9.dll that is neither DXVK nor the Windows one (DPfix, ReShade, dgVoodoo...)
namespace {

enum { kSetTexture = 65, kSetVertexShaderConstantF = 94, kSetPixelShaderConstantF = 109, kCreateDevice = 16, kCreateTexture = 23, kCreateRenderTarget = 28, kCreateDepthStencil = 29,
       kStretchRect = 34, kSetViewport = 47, kDrawPrimitive = 81, kDrawIndexedPrimitive = 82, kCreatePixelShader = 106, kSetPixelShader = 107 };

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

// Every pixel shader with its bytecode, so one draw can be explained (DumpDraw), and whether it writes a second colour target
// (oC1: the normal of the G-buffer pass).
void* g_psObj[2048];
const DWORD* g_psCode[2048];
bool g_psOC1[2048];
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
    if (SUCCEEDED(hr) && out && *out && g_psCount < 2048) { g_psObj[g_psCount] = *out; g_psCode[g_psCount] = code; g_psOC1[g_psCount++] = WritesOC1(code); }
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
void DumpDraw(void* dev, u32 from) {
    static u32 done[8];
    static int doneCount = 0;
    for (int i = 0; i < doneCount; ++i) if (done[i] == from) return;
    if (doneCount >= 8) return;
    done[doneCount++] = from;
    const DWORD* code = nullptr;
    for (int i = 0; i < g_psCount && !code; ++i) if (g_psObj[i] == g_curPs) code = g_psCode[i];
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%sUltimateFix\\draw_%08X.txt", g_paths.gameDir, from);
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
    CloseHandle(f);
    Log("water: the draw from %08X explained in %S", from, path);
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
    const bool w = NoteWaterDraw(count, from);
    return WaterDraw(dev, w, [&] { return r_DrawIp(dev, prim, base, minIdx, verts, start, count); });
}
HRESULT WINAPI H_DrawP(void* dev, DWORD prim, UINT start, UINT count) __attribute__((noinline));
HRESULT WINAPI H_DrawP(void* dev, DWORD prim, UINT start, UINT count) {
    const bool w = NoteWaterDraw(count, Caller());
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
    if (g_cfg.traceWater || g_cfg.waterTest || g_cfg.fogFix) {
        const bool a = PatchSlot(vt, kSetPixelShaderConstantF, (void*)H_SetPsConstF, (void**)&r_SetPsConstF);
        const bool b = PatchSlot(vt, kSetVertexShaderConstantF, (void*)H_SetVsConstF, (void**)&r_SetVsConstF);
        const bool c = PatchSlot(vt, kSetTexture, (void*)H_SetTexture, (void**)&r_SetTexture);
        const bool d = PatchSlot(vt, kCreatePixelShader, (void*)H_CreatePs, (void**)&r_CreatePs);
        const bool e = PatchSlot(vt, kSetPixelShader, (void*)H_SetPs, (void**)&r_SetPs);
        const bool f = PatchSlot(vt, kDrawIndexedPrimitive, (void*)H_DrawIp, (void**)&r_DrawIp);
        const bool g = PatchSlot(vt, kDrawPrimitive, (void*)H_DrawP, (void**)&r_DrawP);
        g_waterHooked = a && f && g;
        if (g_cfg.traceWater || g_cfg.waterTest)
            Log("water: diagnostic hooked (%d%d%d%d%d%d%d): each distinct water draw goes to the log once; Numpad 1 switches WaterTest", a, b,
                c, d, e, f, g);
        else
            Log("fog: draw hooks in (%d%d%d): smoke volumes are kept out of the G-buffer", c, d && e, f);
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
    const bool now = pid == GetCurrentProcessId() && !ui::IsOpen() && (GetAsyncKeyState(VK_NUMPAD1) & 0x8000);  // not while typing a number in F10
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
    if (g_cfg.traceWater) InstallReflectionProbe();
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
