// SMAA 1x on the finished back buffer: the other choice next to the FXAA-like pass of overlay.cpp
// ([Graphics] AntialiasMode=1). Our own implementation of the published technique (Jimenez et al., "SMAA: Enhanced
// Subpixel Morphological Antialiasing", 2012) without its diagonal patterns; the shaders are in smaa_shaders.h, the
// two lookup tables are computed by ../smaa_lut.h when the pass first runs.
//
// Three full-screen passes every frame:
//   1. edges    copy of the picture -> edge target (red: edge on the left boundary of a pixel, green: on the top one)
//   2. weights  edge target + tables -> weight target (how much of each pixel the true border cuts off)
//   3. blend    copy of the picture + weight target -> back buffer
// The two targets are render-target textures of the back buffer's size (default pool: released before a Reset and
// made again on the next frame, also when the size changes; released as well while the FXAA-like pass is chosen).
// The tables are managed textures and the shaders are not bound to the pool, so they live through a Reset.
// Anything that fails (the HLSL compiler, a texture format) switches the pass off for the run with one log line, and
// AntialiasFrame goes back to its FXAA-like pass.
#include "ui.h"
#include "d3d9.h"
#include "smaa_shaders.h"
#include "../smaa_lut.h"

namespace ufx {
namespace ui {
namespace {

using namespace d3d;

// IDirect3DDevice9 methods the shared header has no name for, and the two of ID3DXBuffer / IDirect3DTexture9
enum {
    kTestCooperativeLevel = 3, kStretchRect = 34, kSetDepthStencil = 39, kGetDepthStencil = 40, kCreatePixelShader = 106,
    kSetPixelShaderConstantF = 109, kBufferPointer = 3, kBufferSize = 4, kTexGetSurfaceLevel = 18,
};
constexpr DWORD kFmtA8R8G8B8 = 21, kUsageRenderTarget = 1, kPoolDefault = 0, kPoolManaged = 1;
constexpr DWORD kFilterPoint = 1, kFilterLinear = 2, kAddressClamp = 3;

typedef ULONG(WINAPI* ReleaseFn)(void*);
typedef HRESULT(WINAPI* CompileFn)(const char*, UINT, const void*, void*, const char*, const char*, DWORD, void**, void**, void**);
typedef void*(WINAPI* BufPtrFn)(void*);
typedef DWORD(WINAPI* BufSizeFn)(void*);
typedef HRESULT(WINAPI* CreatePsFn)(void*, const DWORD*, void**);
typedef HRESULT(WINAPI* CreateTexFn)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**, void*);
typedef HRESULT(WINAPI* GetLevelFn)(void*, UINT, void**);
typedef HRESULT(WINAPI* LockFn)(void*, UINT, LockedRect*, const RECT*, DWORD);
typedef HRESULT(WINAPI* UnlockFn)(void*, UINT);
typedef HRESULT(WINAPI* StretchFn)(void*, void*, const RECT*, void*, const RECT*, DWORD);
typedef HRESULT(WINAPI* SetRtFn)(void*, DWORD, void*);
typedef HRESULT(WINAPI* GetDsFn)(void*, void**);
typedef HRESULT(WINAPI* SetDsFn)(void*, void*);
typedef HRESULT(WINAPI* SetPsFn)(void*, void*);
typedef HRESULT(WINAPI* SetTexFn)(void*, DWORD, void*);
typedef HRESULT(WINAPI* SetPsConstFn)(void*, UINT, const float*, UINT);
typedef HRESULT(WINAPI* SetViewportFn)(void*, const Viewport*);
typedef HRESULT(WINAPI* Dword2Fn)(void*, DWORD, DWORD);
typedef HRESULT(WINAPI* Dword3Fn)(void*, DWORD, DWORD, DWORD);

const char kEdgeSource[] = UFX_SMAA_EDGE_DECL UFX_SMAA_EDGE_BODY UFX_SMAA_EDGE_MAIN;
const char kBlendSource[] = UFX_SMAA_BLEND_DECL UFX_SMAA_BLEND_BODY UFX_SMAA_BLEND_MAIN;
// One text per walk length (SmaaPreset::steps): it is the bound of a loop, which the compiler has to know.
#define UFX_SMAA_WEIGHT_SOURCE UFX_SMAA_WEIGHT_DECL UFX_SMAA_WEIGHT_BODY UFX_SMAA_WEIGHT_MAIN
const char kWeightSource4[] = "#define SMAA_STEPS 4\n" UFX_SMAA_WEIGHT_SOURCE;
const char kWeightSource8[] = "#define SMAA_STEPS 8\n" UFX_SMAA_WEIGHT_SOURCE;
const char kWeightSource16[] = "#define SMAA_STEPS 16\n" UFX_SMAA_WEIGHT_SOURCE;
struct WeightSource {
    int steps;
    const char* text;
    size_t length;
};
const WeightSource kWeightSources[3] = {{4, kWeightSource4, sizeof(kWeightSource4) - 1},
                                        {8, kWeightSource8, sizeof(kWeightSource8) - 1},
                                        {16, kWeightSource16, sizeof(kWeightSource16) - 1}};

bool g_failed = false;           // for the rest of the run: AntialiasFrame uses its own pass
void* g_edgeShader = nullptr;    // IDirect3DPixelShader9
void* g_blendShader = nullptr;
// One weight shader per entry of kWeightSources, built when a quality first needs it and kept: going back and forth
// between the qualities in the menu compiles each text once, not on every change.
void* g_weightShaders[3] = {};
void* g_areaTex = nullptr;       // managed textures
void* g_searchTex = nullptr;
void* g_edgeTex = nullptr;       // render targets of the back buffer's size
void* g_edgeSurf = nullptr;
void* g_weightTex = nullptr;
void* g_weightSurf = nullptr;
UINT g_targetW = 0, g_targetH = 0;
int g_loggedQuality = -1;        // the quality the "pass running" line was written for; -1 = write it again

void SafeRelease(void*& object) {
    if (object) Method<ReleaseFn>(object, kRelease)(object);
    object = nullptr;
}

// The one line the log gets when the pass gives up. Returns what SmaaFrame returns then.
bool Fail(const char* what) {
    g_failed = true;
    Log("smaa: %s; edge smoothing falls back to the FXAA-like pass for this run", what);
    return false;
}

// A texture could not be made. A lost device refuses everything until the game has reset it: then only this frame
// is skipped (true: nothing for the FXAA-like pass to do either). Otherwise the pass gives up.
bool CannotCreate(void* dev, const char* what) {
    typedef HRESULT(WINAPI * Fn0)(void*);
    if (Method<Fn0>(dev, kTestCooperativeLevel)(dev) != 0) return true;
    return Fail(what);
}

// ps_3_0 only: the walk needs texture fetches inside loops and branches (tex2Dlod). The compiler's own message goes
// into the log: where these texts were written they could be run as C++ (tests/test_smaa.cpp) but not through the
// HLSL compiler of d3dx9_43.
bool BuildShader(void* dev, const char* name, const char* source, size_t length, void** shader) {
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    if (!d3dx) d3dx = LoadLibraryW(L"d3dx9_43.dll");
    CompileFn compile = d3dx ? (CompileFn)(void*)GetProcAddress(d3dx, "D3DXCompileShader") : nullptr;
    if (!compile) return Fail("D3DXCompileShader of d3dx9_43.dll was not found");
    void *code = nullptr, *errors = nullptr, *table = nullptr;
    const u64 started = TickNow();
    HRESULT hr = compile(source, (UINT)length, nullptr, nullptr, "main", "ps_3_0", 0, &code, &errors, &table);
    char timed[48];  // the weight texts differ only in their walk length: the length is part of the name
    wsprintfA(timed, "smaa %s", name);
    LogShaderTime(timed, SUCCEEDED(hr) && code ? "ps_3_0" : nullptr, started);
    char message[400] = "";
    if (errors) {  // also holds warnings when the shader compiled
        const char* text = (const char*)Method<BufPtrFn>(errors, kBufferPointer)(errors);
        const DWORD size = Method<BufSizeFn>(errors, kBufferSize)(errors);
        int n = 0;
        for (DWORD i = 0; text && i < size && text[i] && n < (int)sizeof(message) - 1; ++i)
            message[n++] = (text[i] == '\r' || text[i] == '\n') ? ' ' : text[i];
        message[n] = 0;
        Method<ReleaseFn>(errors, kRelease)(errors);
    }
    if (table) Method<ReleaseFn>(table, kRelease)(table);
    if (FAILED(hr) || !code) {
        if (code) Method<ReleaseFn>(code, kRelease)(code);
        g_failed = true;
        Log("smaa: the %s shader did not compile (%08X) %s; edge smoothing falls back to the FXAA-like pass for this run", name,
            (u32)hr, message);
        return false;
    }
    const DWORD* bytes = (const DWORD*)Method<BufPtrFn>(code, kBufferPointer)(code);
    hr = Method<CreatePsFn>(dev, kCreatePixelShader)(dev, bytes, shader);
    Method<ReleaseFn>(code, kRelease)(code);
    if (FAILED(hr) || !*shader) {
        *shader = nullptr;
        g_failed = true;
        Log("smaa: the video card refused the %s shader (%08X); edge smoothing falls back to the FXAA-like pass for this run", name,
            (u32)hr);
        return false;
    }
    return true;
}

// Fills a managed A8R8G8B8 texture, two table values per texel in red and green.
void* BuildTable(void* dev, int size, void (*texel)(int, int, uint8_t*, uint8_t*)) {
    void* tex = nullptr;
    if (FAILED(Method<CreateTexFn>(dev, kCreateTexture)(dev, (UINT)size, (UINT)size, 1, 0, kFmtA8R8G8B8, kPoolManaged, &tex, nullptr)) || !tex)
        return nullptr;
    LockedRect lr;
    if (FAILED(Method<LockFn>(tex, kTexLockRect)(tex, 0, &lr, nullptr, 0))) {
        Method<ReleaseFn>(tex, kRelease)(tex);
        return nullptr;
    }
    for (int y = 0; y < size; ++y) {
        u32* row = (u32*)((u8*)lr.bits + (size_t)y * lr.pitch);
        for (int x = 0; x < size; ++x) {
            uint8_t red, green;
            texel(x, y, &red, &green);
            row[x] = 0xFF000000u | ((u32)red << 16) | ((u32)green << 8);
        }
    }
    Method<UnlockFn>(tex, kTexUnlockRect)(tex, 0);
    return tex;
}

bool MakeTarget(void* dev, UINT w, UINT h, void** texture, void** surface) {
    if (FAILED(Method<CreateTexFn>(dev, kCreateTexture)(dev, w, h, 1, kUsageRenderTarget, kFmtA8R8G8B8, kPoolDefault, texture, nullptr)) ||
        !*texture)
        return false;
    return SUCCEEDED(Method<GetLevelFn>(*texture, kTexGetSurfaceLevel)(*texture, 0, surface)) && *surface;
}

void BindTexture(void* dev, DWORD stage, void* texture, DWORD filter) {
    Method<SetTexFn>(dev, kSetTexture)(dev, stage, texture);
    Dword3Fn set = Method<Dword3Fn>(dev, kSetSamplerState);
    set(dev, stage, SAMP_MINFILTER, filter);
    set(dev, stage, SAMP_MAGFILTER, filter);
    set(dev, stage, SAMP_MIPFILTER, 0);
    set(dev, stage, SAMP_ADDRESSU, kAddressClamp);
    set(dev, stage, SAMP_ADDRESSV, kAddressClamp);
    set(dev, stage, SAMP_SRGBTEXTURE, 0);
}

void DrawInto(void* dev, void* surface, void* shader, UINT w, UINT h) {
    Method<SetRtFn>(dev, kSetRenderTarget)(dev, 0, surface);
    Viewport vp = {0, 0, w, h, 0.f, 1.f};
    Method<SetViewportFn>(dev, kSetViewport)(dev, &vp);
    Method<SetPsFn>(dev, kSetPixelShader)(dev, shader);
    PostFullQuad(w, h);
}

}  // namespace

void ReleaseSmaaTargets() {
    SafeRelease(g_edgeSurf);
    SafeRelease(g_edgeTex);
    SafeRelease(g_weightSurf);
    SafeRelease(g_weightTex);
    g_targetW = g_targetH = 0;
}

bool SmaaFrame(void* dev, int percent) {
    if (g_failed) return false;
    if (percent <= 0) return true;
    const int quality = g_cfg.smaaQuality < 0 ? 0 : g_cfg.smaaQuality > 2 ? 2 : g_cfg.smaaQuality;
    const SmaaPreset& preset = kSmaaPresets[quality];

    if (!g_areaTex && !(g_areaTex = BuildTable(dev, kSmaaAreaSize, SmaaAreaTexel))) return CannotCreate(dev, "the area table texture could not be made");
    if (!g_searchTex && !(g_searchTex = BuildTable(dev, kSmaaSearchSize, SmaaSearchTexel)))
        return CannotCreate(dev, "the search table texture could not be made");
    if (!g_edgeShader && !BuildShader(dev, "edge", kEdgeSource, sizeof(kEdgeSource) - 1, &g_edgeShader)) return false;
    if (!g_blendShader && !BuildShader(dev, "blend", kBlendSource, sizeof(kBlendSource) - 1, &g_blendShader)) return false;
    int walk = 0;  // the entry of kWeightSources this quality uses
    for (int i = 0; i < 3; ++i)
        if (kWeightSources[i].steps == preset.steps) walk = i;
    if (!g_weightShaders[walk] &&
        !BuildShader(dev, walk == 0 ? "weight (walk 4)" : walk == 1 ? "weight (walk 8)" : "weight (walk 16)", kWeightSources[walk].text,
                     kWeightSources[walk].length, &g_weightShaders[walk]))
        return false;

    UINT w, h, fmt;
    void* back = PostBackBuffer(dev, &w, &h, &fmt);
    if (!back) return true;  // no back buffer this frame (a lost device): nothing to smooth, and nothing for FXAA either
    if (g_targetW != w || g_targetH != h) {
        ReleaseSmaaTargets();
        if (!MakeTarget(dev, w, h, &g_edgeTex, &g_edgeSurf) || !MakeTarget(dev, w, h, &g_weightTex, &g_weightSurf)) {
            ReleaseSmaaTargets();
            Method<ReleaseFn>(back, kRelease)(back);
            return CannotCreate(dev, "the two render targets of the back buffer's size could not be made");
        }
        g_targetW = w;
        g_targetH = h;
        g_loggedQuality = -1;
    }
    void *copyTex = nullptr, *copySurf = nullptr;
    if (!PostCopyTarget(dev, w, h, fmt, &copyTex, &copySurf)) {
        Method<ReleaseFn>(back, kRelease)(back);
        return CannotCreate(dev, "the copy of the back buffer could not be made");
    }
    HRESULT hr = Method<StretchFn>(dev, kStretchRect)(dev, back, nullptr, copySurf, nullptr, kFilterPoint);
    if (FAILED(hr)) {  // as in the other passes: skip the frame (the device may be lost), try again on the next
        Method<ReleaseFn>(back, kRelease)(back);
        return true;
    }
    // The state block of OverlayBegin does not hold the depth buffer. The passes do not use one, and none is bound
    // while they draw, so its size never has to fit our targets.
    void* oldDepth = nullptr;
    Method<GetDsFn>(dev, kGetDepthStencil)(dev, &oldDepth);
    if (!OverlayBegin(dev)) {
        if (oldDepth) Method<ReleaseFn>(oldDepth, kRelease)(oldDepth);
        Method<ReleaseFn>(back, kRelease)(back);
        return true;
    }
    Method<SetDsFn>(dev, kSetDepthStencil)(dev, nullptr);
    Method<Dword2Fn>(dev, kSetRenderState)(dev, RS_ALPHABLENDENABLE, 0);
    const float texel[4] = {1.0f / (float)w, 1.0f / (float)h, (float)(percent > 100 ? 100 : percent) / 100.0f, 0.f};
    const float edgeSetup[4] = {preset.threshold, preset.colorEdges, 0.f, 0.f};
    const float weightSetup[4] = {preset.cornerLoss, 0.f, 0.f, 0.f};
    SetPsConstFn setConst = Method<SetPsConstFn>(dev, kSetPixelShaderConstantF);
    setConst(dev, 0, texel, 1);

    setConst(dev, 1, edgeSetup, 1);
    BindTexture(dev, 0, copyTex, kFilterPoint);
    DrawInto(dev, g_edgeSurf, g_edgeShader, w, h);

    setConst(dev, 1, weightSetup, 1);
    BindTexture(dev, 0, g_edgeTex, kFilterLinear);  // the walk reads four marks per fetch through the filter
    BindTexture(dev, 1, g_areaTex, kFilterPoint);
    BindTexture(dev, 2, g_searchTex, kFilterPoint);
    DrawInto(dev, g_weightSurf, g_weightShaders[walk], w, h);

    BindTexture(dev, 0, copyTex, kFilterLinear);    // a fetch between two pixels is their mix
    BindTexture(dev, 1, g_weightTex, kFilterPoint);
    Method<SetTexFn>(dev, kSetTexture)(dev, 2, nullptr);
    DrawInto(dev, back, g_blendShader, w, h);

    Method<SetTexFn>(dev, kSetTexture)(dev, 0, nullptr);
    Method<SetTexFn>(dev, kSetTexture)(dev, 1, nullptr);
    OverlayEnd();  // the game's render target and the whole state are back
    Method<SetDsFn>(dev, kSetDepthStencil)(dev, oldDepth);
    if (oldDepth) Method<ReleaseFn>(oldDepth, kRelease)(oldDepth);
    Method<ReleaseFn>(back, kRelease)(back);
    if (g_loggedQuality != quality) {  // the first frame, new targets (a Reset, another size) or another quality
        g_loggedQuality = quality;
        Log("smaa: pass running on a %ux%u back buffer, quality %d (walk of %d pixels each way, %s edges)", w, h, quality,
            preset.steps * 2 - 1, preset.colorEdges > 0.f ? "colour" : "brightness");
    }
    return true;
}

}  // namespace ui
}  // namespace ufx
