// Ambient occlusion hook: the final composite of the game draws one quad (FUN_006E9F60, a single DrawPrimitive through
// the device vtable). The call is redirected to a function with the same signature that first lets the overlay module
// darken the HDR scene, then makes the game's own draw call. The Xbox 360 look (ui/xbox_look.cpp) uses the same place:
// for this one draw it swaps the game's tone-map pixel shader for the mod's copy with the colour grading. The game's own
// 4-tap filter (GameAliasFilter) too: with the grading it is in the mod's copy, without it the game's variant 31 draws.
// The smooth upscaling of the bloom and the depth of field (SmoothBloom) and the dither against banding (Dither) are the
// same copy, ungraded when XboxLook is off.
#include "../base.h"
#include "../sites.h"
#include "../ui/ui.h"

namespace ufx {
bool WorldActive();  // combat_strafe.cpp
namespace {

typedef HRESULT(WINAPI* GetPsConstFn)(void*, UINT, float*, UINT);
typedef HRESULT(WINAPI* SetPsConstFn)(void*, UINT, const float*, UINT);

// 2 to the power of tenths: 2^(t/10) for t in -30..30 without a C runtime.
float Pow2Tenths(int t) {
    const float step = 1.0717735f;  // 2^0.1
    float v = 1.0f;
    if (t > 0) for (int i = 0; i < t; ++i) v *= step;
    else for (int i = 0; i < -t; ++i) v /= step;
    return v;
}

// The game's own bloom and exposure, as the composite uses them: c9.x = bloom force, c10.x = the exposure key (the scene is
// multiplied by key / average luminance). Changed for this one draw and put back after it.
bool TuneComposite(void* dev, float saved[8]) {
    if (g_cfg.bloomStrength == 100 && g_cfg.exposureComp == 0) return false;
    void** vt = *(void***)dev;
    if (FAILED(((GetPsConstFn)vt[110])(dev, 9, saved, 2))) return false;
    float c[8];
    for (int i = 0; i < 8; ++i) c[i] = saved[i];
    c[0] *= (float)g_cfg.bloomStrength / 100.0f;
    c[4] *= Pow2Tenths(g_cfg.exposureComp);
    return SUCCEEDED(((SetPsConstFn)vt[109])(dev, 9, c, 2));
}

// DrawPrimitive: this, type, start, count (stdcall, vtable slot 81).
HRESULT WINAPI CompositeDrawHook(void* dev, DWORD type, UINT start, UINT count) {
    const bool world = WorldActive();
    float tuned[8];
    // the look of the picture (bloom, exposure, the Xbox 360 grading) belongs to every composite, cutscenes and scripted scenes
    // too, where the player is not updated; only the occlusion waits for the world (it must stay off in the main menu)
    const bool tune = TuneComposite(dev, tuned);
    if (g_cfg.ambientOcclusion > 0 && world) ui::AmbientOcclusionPass(dev, g_cfg.ambientOcclusion, g_cfg.aoRadius);
    ui::LookSaved look;
    // the mod's copy of the composite: for the grading, or (with neutral constants) only for the smooth bloom or the dither
    const bool copied = (g_cfg.xboxLook || g_cfg.smoothBloom || g_cfg.dither) && ui::XboxLookBegin(dev, &look);
    const bool aliased = !copied && g_cfg.gameAlias > 0 && ui::GameAliasBegin(dev, &look);
    void** vtable = *(void***)dev;
    HRESULT hr = ((HRESULT(WINAPI*)(void*, DWORD, UINT, UINT))vtable[81])(dev, type, start, count);
    if (copied) ui::XboxLookEnd(dev, &look);
    if (aliased) ui::GameAliasEnd(dev, &look);
    if (tune) ((SetPsConstFn)vtable[109])(dev, 9, tuned, 2);
    // after the game's own state is back: the passes save and restore all of it around themselves
    if (SUCCEEDED(hr)) ui::PostFiltersAfterComposite(dev);
    return hr;
}

}  // namespace

bool InstallAmbientOcclusion() {
    const SiteDef& s = kSiteCompositeDraw;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("ssao: the composite draw at %08X differs from DP.exe 1.01b, skipped (the scene grading of the Xbox 360 look too)", s.va);
        return false;
    }
    if (!PatchCall(s.va, s.bytes, s.len, (const void*)CompositeDrawHook)) {
        Log("ssao: could not patch the composite draw (no SSAO, no scene grading of the Xbox 360 look)");
        return false;
    }
    Log("ssao: hooked the final composite (strength %d%%, radius %d px)", g_cfg.ambientOcclusion, g_cfg.aoRadius);
    Log("xboxlook: uses the same hook (now %s; scene grading %d%%, TV gamma %d%%, soft highlights %d%%; smooth bloom %s, dither %s)",
        g_cfg.xboxLook ? "on" : "off", g_cfg.xboxGrade, g_cfg.xboxGamma, g_cfg.xboxShoulder, g_cfg.smoothBloom ? "on" : "off",
        g_cfg.dither ? "on" : "off");
    return true;
}

}  // namespace ufx
