// Internal resolution of the 3D scene (supersampling).
//
// The game sizes every scene buffer from two float globals, the "logical resolution" (1280x720). The
// scene buffers are created once at start-up, by FUN_006D1610 (scene) and FUN_006E8930 (post-processing),
// and each buffer remembers its own size afterwards: viewports and the pixel-size shader constants are
// derived from the buffer being drawn into. So making the globals bigger only while those two functions
// run gives bigger buffers everywhere they matter, while the 2D interface, fades and loading screens keep
// drawing in their logical 1280x720. The scene is RenderScale percent of the back buffer: of the output resolution
// when that module is active (OutputWidth, by default the monitor width), otherwise of the game's 1280x720. The
// game's own final pass downsamples the finished scene into the back buffer (a free 4x supersampling at 200%),
// the 2D layer is drawn on top at the back buffer's size, and the display module presents it directly when the
// back buffer has the size of the window, or scales it into the window through its extra swap chain otherwise.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {

int g_sceneWidth = 0;
bool g_postBuffersScaled = false;  // the blur/bloom storage literals were enlarged (read by blur_look.cpp)

// RenderScale as it is really applied to a back buffer `baseW` pixels wide: at least 100, and never more than a
// 7680-pixel-wide scene. output_resolution.cpp sizes the final depth buffer with the same value.
int RenderScalePercent(int baseW) {
    int percent = g_cfg.renderScale < 100 ? 100 : g_cfg.renderScale;
    if (baseW > 0 && baseW * percent / 100 > 7680) {
        int limited = 7680 * 100 / baseW;
        percent = limited < 100 ? 100 : limited;
    }
    return percent;
}

static u32 FloatBits(int value) {
    float f = (float)value;
    u32 bits;
    u8* d = (u8*)&bits;
    const u8* s = (const u8*)&f;
    for (int i = 0; i < 4; ++i) d[i] = s[i];
    return bits;
}

bool InstallRenderScale() {
    if (g_cfg.renderScale <= 100 && g_outputW == 0) return false;
    const SiteDef& scene = kSiteSceneInitCall;
    const SiteDef& post = kSitePostInitCall;
    if (!BytesMatch(scene.va, scene.bytes, scene.len) || !BytesMatch(post.va, post.bytes, post.len)) {
        Log("render-scale: the start-up call sites differ from DP.exe 1.01b, skipped");
        return false;
    }
    // With the output resolution module the scene is sized from the output, otherwise from 1280x720.
    int baseW = g_outputW ? g_outputW : 1280;
    int baseH = g_outputH ? g_outputH : 720;
    int wanted = g_cfg.renderScale < 100 ? 100 : g_cfg.renderScale;
    int percent = RenderScalePercent(baseW);
    if (percent != wanted)
        Log("render-scale: %d%% of %d pixels would be %d wide (too large for video memory), limited to %d%%", wanted,
            baseW, baseW * wanted / 100, percent);
    int w = baseW * percent / 100;
    int h = baseH * percent / 100;

    // [0..3] return slot of the scene thunk, [4..7] of the post thunk, [8..11] width bits, [12..15] height bits
    u8* arena = AllocStub(256);
    if (!arena) {
        Log("render-scale: no memory for the thunks, skipped");
        return false;
    }
    u32* data = (u32*)arena;
    data[2] = FloatBits(w);
    data[3] = FloatBits(h);
    u32 dataVa = (u32)(uintptr_t)arena;

    u8* sceneThunk = arena + 16;
    u8* postThunk = arena + 16 + 80;
    BuildScaleThunk(sceneThunk, (u32)(uintptr_t)sceneThunk, kSceneInitFn, dataVa + 0, dataVa + 8, dataVa + 12,
                    kLogicalWidthVa, kLogicalHeightVa);
    BuildScaleThunk(postThunk, (u32)(uintptr_t)postThunk, kPostInitFn, dataVa + 4, dataVa + 8, dataVa + 12,
                    kLogicalWidthVa, kLogicalHeightVa);

    if (!PatchCall(scene.va, scene.bytes, scene.len, sceneThunk) || !PatchCall(post.va, post.bytes, post.len, postThunk)) {
        Log("render-scale: could not patch the call sites");
        return false;
    }
    if (g_cfg.scalePostBuffers) {
        const SiteDef* sites[4] = {&kSiteStorageH1, &kSiteStorageW1, &kSiteStorageH2, &kSiteStorageW2};
        const int base[4] = {504, 896, 252, 448};
        bool ok = true;
        for (const SiteDef* s : sites) ok = ok && BytesMatch(s->va, s->bytes, s->len);
        if (ok) {
            for (int i = 0; i < 4; ++i) {
                u32 v = (u32)((long long)base[i] * w / 1280);  // grows with the scene width
                u8 imm[4] = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)};
                WriteCode(sites[i]->va + 1, imm, 4);  // the immediate of `push imm32`
            }
            g_postBuffersScaled = true;
            Log("render-scale: blur/bloom storage buffers enlarged to %dx%d and %dx%d", 896 * w / 1280, 504 * w / 1280,
                448 * w / 1280, 252 * w / 1280);
        } else {
            Log("render-scale: the blur/bloom size literals differ from DP.exe 1.01b, left alone");
        }
    }
    g_sceneWidth = w;
    Log("render-scale: the 3D scene is built at %dx%d (%d%%)", w, h, percent);
    return true;
}

// Shadow map size. The viewports and pixel constants of a buffer come from the buffer itself, so changing the
// size at creation is all that is needed.
bool InstallShadowScale() {
    if (g_cfg.shadowScale <= 100) return false;
    for (const ShadowSite& site : kShadowSites) {
        const u8 expected[5] = {0x68, (u8)site.value, (u8)(site.value >> 8), 0x00, 0x00};
        if (!BytesMatch(site.va, expected, 5)) {
            Log("shadow-scale: the literal at %08X differs from DP.exe 1.01b, skipped", site.va);
            return false;
        }
    }
    for (const ShadowSite& site : kShadowSites) {
        u32 v = site.value * (u32)g_cfg.shadowScale / 100;
        u8 imm[4] = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)};
        WriteCode(site.va + 1, imm, 4);
    }
    Log("shadow-scale: shadow maps %dx%d and %dx%d", 512 * g_cfg.shadowScale / 100, 512 * g_cfg.shadowScale / 100,
        1024 * g_cfg.shadowScale / 100, 1024 * g_cfg.shadowScale / 100);
    return true;
}

}  // namespace ufx
