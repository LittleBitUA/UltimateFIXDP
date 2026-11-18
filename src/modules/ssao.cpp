// Ambient occlusion hook: the final composite of the game draws one quad (FUN_006E9F60, a single DrawPrimitive through
// the device vtable). The call is redirected to a function with the same signature that first lets the overlay module
// darken the HDR scene, then makes the game's own draw call.
#include "../base.h"
#include "../sites.h"
#include "../ui/ui.h"

namespace ufx {
bool WorldActive();  // combat_strafe.cpp
namespace {

// DrawPrimitive: this, type, start, count (stdcall, vtable slot 81).
HRESULT WINAPI CompositeDrawHook(void* dev, DWORD type, UINT start, UINT count) {
    if (g_cfg.ambientOcclusion > 0 && WorldActive()) ui::AmbientOcclusionPass(dev, g_cfg.ambientOcclusion, g_cfg.aoRadius);
    void** vtable = *(void***)dev;
    return ((HRESULT(WINAPI*)(void*, DWORD, UINT, UINT))vtable[81])(dev, type, start, count);
}

}  // namespace

bool InstallAmbientOcclusion() {
    const SiteDef& s = kSiteCompositeDraw;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("ssao: the composite draw at %08X differs from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    if (!PatchCall(s.va, s.bytes, s.len, (const void*)CompositeDrawHook)) {
        Log("ssao: could not patch the composite draw");
        return false;
    }
    Log("ssao: hooked the final composite (strength %d%%, radius %d px)", g_cfg.ambientOcclusion, g_cfg.aoRadius);
    return true;
}

}  // namespace ufx
