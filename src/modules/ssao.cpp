// Ambient occlusion hook: the final composite of the game draws one quad (FUN_006E9F60, a single DrawPrimitive through
// the device vtable). The call is redirected to a function with the same signature that first lets the overlay module
// darken the HDR scene, then makes the game's own draw call. The Xbox 360 look (ui/xbox_look.cpp) uses the same place:
// for this one draw it swaps the game's tone-map pixel shader for the mod's copy with the colour grading.
#include "../base.h"
#include "../sites.h"
#include "../ui/ui.h"

namespace ufx {
bool WorldActive();  // combat_strafe.cpp
namespace {

// DrawPrimitive: this, type, start, count (stdcall, vtable slot 81).
HRESULT WINAPI CompositeDrawHook(void* dev, DWORD type, UINT start, UINT count) {
    const bool world = WorldActive();
    if (g_cfg.ambientOcclusion > 0 && world) ui::AmbientOcclusionPass(dev, g_cfg.ambientOcclusion, g_cfg.aoRadius);
    ui::LookSaved look;
    const bool graded = g_cfg.xboxLook && world && ui::XboxLookBegin(dev, &look);
    void** vtable = *(void***)dev;
    HRESULT hr = ((HRESULT(WINAPI*)(void*, DWORD, UINT, UINT))vtable[81])(dev, type, start, count);
    if (graded) ui::XboxLookEnd(dev, &look);
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
    Log("xboxlook: uses the same hook (now %s; scene grading %d%%, TV gamma %d%%, soft highlights %d%%)", g_cfg.xboxLook ? "on" : "off",
        g_cfg.xboxGrade, g_cfg.xboxGamma, g_cfg.xboxShoulder);
    return true;
}

}  // namespace ufx
