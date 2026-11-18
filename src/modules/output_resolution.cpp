// Back buffer of the real output size, and a 2D layer that stays sharp.
//
// The game draws its 2D layer (interface, menus, fades) in "logical" 1280x720 pixels and turns them into
// clip-space coordinates with a vertex-shader constant (c254) built from the logical rectangle, so it fills any
// viewport. The logical rectangle is the pair of float globals the scene code also reads. This module makes the
// back buffer outW x outH and keeps two states apart:
//   * H state (2D, composite, fades): the real D3D viewport is the whole outW x outH back buffer, the logical
//     rectangle stays 1280x720;
//   * S state (scene): untouched, sized by the render-scale module.
// Everything is done by replacing call targets and a few instructions with our own functions (see sites.h and
// docs/pc_analysis/09_output_resolution_cp3.md). Nothing is applied unless every site matches.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"
#include "../ui/d3d9.h"

namespace ufx {

int g_outputW = 0;
int g_outputH = 0;
int RenderScalePercent(int baseW);  // render_scale.cpp: the render scale as it is really applied

namespace {

using namespace d3d;

typedef HRESULT(WINAPI* SetViewportFn)(void*, const Viewport*);
typedef HRESULT(WINAPI* GetRtFn)(void*, DWORD, void**);
typedef HRESULT(WINAPI* GetBbFn)(void*, UINT, UINT, UINT, void**);
typedef ULONG(WINAPI* ReleaseFn)(void*);
typedef void(__cdecl* SetLogicalRectFn)(int, int, int, int, char);
typedef void(__cdecl* VoidFn)();

void* Device() { return *(void**)(uintptr_t)kDeviceVa; }

int Scaled(int v) { return (v * g_outputW + 640) / 1280; }  // logical pixels to output pixels, rounded

// True when the render target currently bound is the back buffer.
bool OnBackbuffer(void* dev) {
    void* rt = nullptr;
    void* bb = nullptr;
    Method<GetRtFn>(dev, kGetRenderTarget)(dev, 0, &rt);
    Method<GetBbFn>(dev, kGetBackBuffer)(dev, 0, 0, 0, &bb);
    bool same = rt && bb && rt == bb;
    if (rt) Method<ReleaseFn>(rt, kRelease)(rt);
    if (bb) Method<ReleaseFn>(bb, kRelease)(bb);
    return same;
}

void RealSetViewport(void* dev, int x, int y, int w, int h, float minZ, float maxZ) {
    Viewport vp = {(DWORD)x, (DWORD)y, (DWORD)w, (DWORD)h, minZ, maxZ};
    Method<SetViewportFn>(dev, kSetViewport)(dev, &vp);
}

// H state: the whole output as viewport, logical rectangle 1280x720 (this also rebuilds c254).
void HudState() {
    void* dev = Device();
    if (!dev) return;
    RealSetViewport(dev, 0, 0, g_outputW, g_outputH, 0.f, 1.f);
    ((SetLogicalRectFn)(uintptr_t)kLogicalRectFn)(0, 0, 1280, 720, 1);
}

// FUN_005B49E0 at the start of a frame; afterwards the 2D state is in force until the scene sets its own.
void __cdecl FrameBegin() {
    ((VoidFn)(uintptr_t)kFrameBeginFn)();
    if (*(volatile u32*)(uintptr_t)0x148BBC4 == 0) HudState();  // windowed path only
}

union FloatBits {
    float f;
    u32 u;
};

// Replaces FUN_0044ADA0(x, y, w, h, minZ, maxZ) for the explicit 2D viewports: the logical rectangle is scaled
// to output pixels, but only while the back buffer is the target.
void __cdecl LogicalViewport(int x, int y, int w, int h, u32 minZBits, u32 maxZBits) {
    void* dev = Device();
    FloatBits mn, mx;
    mn.u = minZBits;
    mx.u = maxZBits;
    if (dev && OnBackbuffer(dev)) {
        int px = Scaled(x), py = Scaled(y), pw = Scaled(w), ph = Scaled(h);
        if (px + pw > g_outputW) pw = g_outputW - px;
        if (py + ph > g_outputH) ph = g_outputH - py;
        RealSetViewport(dev, px, py, pw, ph, mn.f, mx.f);
    } else if (dev) {
        RealSetViewport(dev, x, y, w, h, mn.f, mx.f);
    }
    ((SetLogicalRectFn)(uintptr_t)kLogicalRectFn)(x, y, w, h, 1);
}

// The viewport restored after a pop of the render-target stack: back on the back buffer it is the H state.
void __cdecl PopViewport(int x, int y, int w, int h, u32 minZBits, u32 maxZBits) {
    void* dev = Device();
    if (dev && OnBackbuffer(dev)) {
        HudState();
        return;
    }
    FloatBits mn, mx;
    mn.u = minZBits;
    mx.u = maxZBits;
    if (dev) RealSetViewport(dev, x, y, w, h, mn.f, mx.f);
    ((SetLogicalRectFn)(uintptr_t)kLogicalRectFn)(x, y, w, h, 1);
}

// FUN_005F8430: full 1280x720 viewport.
void __cdecl ResetViewport() {
    void* dev = Device();
    if (dev && OnBackbuffer(dev)) {
        HudState();
        return;
    }
    ((VoidFn)(uintptr_t)kViewportResetFn)();
}

// The map code loads the SetViewport method of the device from its vtable and calls it; we hand it this instead.
HRESULT WINAPI SetViewportScaled(void* dev, const Viewport* vp) {
    Viewport out = *vp;
    if (OnBackbuffer(dev)) {
        out.x = (DWORD)Scaled((int)vp->x);
        out.y = (DWORD)Scaled((int)vp->y);
        out.width = (DWORD)Scaled((int)vp->width);
        out.height = (DWORD)Scaled((int)vp->height);
        if (out.x + out.width > (DWORD)g_outputW) out.width = (DWORD)g_outputW - out.x;
        if (out.y + out.height > (DWORD)g_outputH) out.height = (DWORD)g_outputH - out.y;
    }
    return Method<SetViewportFn>(dev, kSetViewport)(dev, &out);
}

bool Matches(const SiteDef& s) { return BytesMatch(s.va, s.bytes, s.len); }

void PutImm32(u32 va, u32 value) {
    u8 imm[4] = {(u8)value, (u8)(value >> 8), (u8)(value >> 16), (u8)(value >> 24)};
    WriteCode(va, imm, 4);
}

}  // namespace

bool InstallOutputResolution() {
    int w = g_cfg.outputWidth;
    if (w <= 0 || w == 1280) return false;
    // The output is always 16:9: the 2D layer scales every coordinate by the width ratio (Scaled), the HUD maps the
    // whole back buffer to a logical 1280x720 and the game's projection stays 16:9, so another height would stretch
    // the picture and put the 2D viewports in the wrong place.
    int h = w * 9 / 16;
    if (g_cfg.outputHeight > 0 && g_cfg.outputHeight != h)
        Log("output: OutputHeight=%d ignored, the output keeps 16:9 (%dx%d)", g_cfg.outputHeight, w, h);
    if (w > 8192 || h > 8192 || w < 640 || h < 360) {
        Log("output: %dx%d is outside the supported range, skipped", w, h);
        return false;
    }

    const SiteDef* all[] = {&kOutPresentParams, &kOutDepthH, &kOutDepthW, &kOutFrameBegin1, &kOutFrameBegin2,
                            &kOutPopViewport, &kOutHudDepthBind, &kOutViewport1, &kOutViewport2, &kOutViewport3,
                            &kOutReset1, &kOutReset2, &kOutMap1, &kOutMap2, &kOutMap3, &kOutMap4};
    for (const SiteDef* s : all) {
        if (!Matches(*s)) {
            Log("output: bytes at %08X (%s) differ from DP.exe 1.01b, the output resolution is not applied", s->va,
                s->name);
            return false;
        }
    }

    // Scene buffers: size of the output times the render scale, limited exactly as render_scale.cpp limits it; the
    // depth buffer of the final pass must be at least as big as the scene buffer it is paired with, and no bigger.
    const int percent = RenderScalePercent(w);
    int sceneW = w * percent / 100;
    int sceneH = h * percent / 100;
    u32 depthW = (u32)(sceneW > w ? sceneW : w);
    u32 depthH = (u32)(sceneH > h ? sceneH : h);

    u8* arena = AllocStub(256);
    if (!arena) {
        Log("output: no memory for the stubs, not applied");
        return false;
    }
    u8* backThunk = arena;                 // 49 bytes
    u8* hudStub = arena + 64;              // 17 bytes
    g_outputW = w;
    g_outputH = h;

    BuildBackbufferThunk(backThunk, (u32)(uintptr_t)backThunk, kFullscreenFlagVa, (u32)w, (u32)h, kResetParamsVa,
                         kOutPresentParamsReturn);
    BuildHudStub(hudStub, (u32)(uintptr_t)hudStub, kHudDepthSlotVa, (u32)(uintptr_t)HudState);

    bool ok = PatchJump(kOutPresentParams.va, kOutPresentParams.bytes, kOutPresentParams.len, backThunk);
    PutImm32(kOutDepthH.va + 1, depthH);
    PutImm32(kOutDepthW.va + 1, depthW);
    ok = ok && PatchCall(kOutFrameBegin1.va, kOutFrameBegin1.bytes, 5, (const void*)FrameBegin);
    ok = ok && PatchCall(kOutFrameBegin2.va, kOutFrameBegin2.bytes, 5, (const void*)FrameBegin);
    ok = ok && PatchCall(kOutPopViewport.va, kOutPopViewport.bytes, 5, (const void*)PopViewport);
    ok = ok && PatchCall(kOutHudDepthBind.va, kOutHudDepthBind.bytes, 6, hudStub);  // `call stub` + nop
    ok = ok && PatchCall(kOutViewport1.va, kOutViewport1.bytes, 5, (const void*)LogicalViewport);
    ok = ok && PatchCall(kOutViewport2.va, kOutViewport2.bytes, 5, (const void*)LogicalViewport);
    ok = ok && PatchCall(kOutViewport3.va, kOutViewport3.bytes, 5, (const void*)LogicalViewport);
    ok = ok && PatchCall(kOutReset1.va, kOutReset1.bytes, 5, (const void*)ResetViewport);
    ok = ok && PatchCall(kOutReset2.va, kOutReset2.bytes, 5, (const void*)ResetViewport);

    // The map loads a method pointer into a register and calls it: `mov reg, [reg+0BCh]` becomes `mov reg, imm32`.
    struct Map {
        const SiteDef* site;
        u8 opcode;  // B8 = mov eax, BA = mov edx
    };
    const Map maps[4] = {{&kOutMap1, 0xBA}, {&kOutMap2, 0xB8}, {&kOutMap3, 0xB8}, {&kOutMap4, 0xB8}};
    for (const Map& m : maps) {
        u32 fn = (u32)(uintptr_t)SetViewportScaled;
        u8 code[6] = {m.opcode, (u8)fn, (u8)(fn >> 8), (u8)(fn >> 16), (u8)(fn >> 24), 0x90};
        ok = ok && WriteCode(m.site->va, code, 6);
    }
    if (!ok) {
        Log("output: a patch could not be written, the game may be left in an inconsistent state");
        return false;
    }
    Log("output: back buffer %dx%d, 2D layer in the real resolution; final depth buffer %ux%u", w, h, depthW, depthH);
    return true;
}

}  // namespace ufx
