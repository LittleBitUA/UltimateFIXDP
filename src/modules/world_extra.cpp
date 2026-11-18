// World extras: object activation radius, mesh LOD distance, outer-cell detail, and the interior visibility
// bridge. All values live in our own data and are written live from the menu.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {
namespace {

float g_activation = 1000000.0f;  // (1000 * scale)^2, the operand of the active-list distance test
alignas(8) double g_lod = 25.0;   // LOD metric factor
volatile u8 g_visFlag = 0;        // bit 0 / bit 1: bridge the first / second visibility test

bool g_activationReady = false, g_lodReady = false, g_highReady = false, g_visReady = false;

bool PatchOperand(u32 va, const void* slot) {
    u32 addr = (u32)(uintptr_t)slot;
    u8 b[4] = {(u8)addr, (u8)(addr >> 8), (u8)(addr >> 16), (u8)(addr >> 24)};
    return WriteCode(va, b, 4);
}

}  // namespace

void ApplyWorldExtra() {
    if (g_activationReady) {
        float r = 1000.0f * (float)g_cfg.activationScale;
        g_activation = r * r;
    }
    if (g_lodReady) g_lod = 25.0 * (double)g_cfg.lodScale;
    if (g_highReady) {
        u8 v = g_cfg.highDetail ? 0 : 1;  // 1 = the game's reduced set for the outer ring
        WriteCode(kSiteHighDetail1.va + 1, &v, 1);
        WriteCode(kSiteHighDetail2.va + 1, &v, 1);
        WriteCode(kSiteHighDetail3.va + 6, &v, 1);
    }
    if (g_visReady) g_visFlag = (u8)(g_cfg.interiorVisibility == 0 ? 0 : g_cfg.interiorVisibility == 1 ? 1 : 3);
}

bool InstallWorldExtra() {
    int n = 0;
    if (BytesMatch(kSiteActivation.va, kSiteActivation.bytes, kSiteActivation.len) &&
        PatchOperand(kActivationOperandVa, &g_activation)) {
        g_activationReady = true;
        ++n;
    } else {
        Log("world: activation radius site differs from DP.exe 1.01b, skipped");
    }
    if (BytesMatch(kSiteLodFactor.va, kSiteLodFactor.bytes, kSiteLodFactor.len) && PatchOperand(kLodOperandVa, &g_lod)) {
        g_lodReady = true;
        ++n;
    } else {
        Log("world: LOD factor site differs from DP.exe 1.01b, skipped");
    }
    if (BytesMatch(kSiteHighDetail1.va, kSiteHighDetail1.bytes, 5) && BytesMatch(kSiteHighDetail2.va, kSiteHighDetail2.bytes, 5) &&
        BytesMatch(kSiteHighDetail3.va, kSiteHighDetail3.bytes, 10)) {
        g_highReady = true;
        ++n;
    } else {
        Log("world: high-detail sites differ from DP.exe 1.01b, skipped");
    }
    const SiteDef* vis[2] = {&kSiteVisibility1, &kSiteVisibility2};
    const u8 bits[2] = {1, 2};
    u8* stubs[2];
    const SiteDef& entry = kSiteVisibilityTestEntry;  // the bytes verify_sites checks against DP.exe
    bool ok = entry.va == kVisibilityTestFn && BytesMatch(entry.va, entry.bytes, entry.len);
    for (int i = 0; i < 2 && ok; ++i) {
        stubs[i] = AllocStub(32);
        ok = stubs[i] && BytesMatch(vis[i]->va, vis[i]->bytes, vis[i]->len);
    }
    if (ok) {
        for (int i = 0; i < 2; ++i) {
            BuildVisibilityBridge(stubs[i], (u32)(uintptr_t)stubs[i], (u32)(uintptr_t)&g_visFlag, bits[i], kVisibilityTestFn);
            if (!PatchCall(vis[i]->va, vis[i]->bytes, vis[i]->len, stubs[i])) ok = false;
        }
    }
    if (ok) {
        g_visReady = true;
        ++n;
    } else {
        Log("world: interior visibility sites differ from DP.exe 1.01b, skipped");
    }
    ApplyWorldExtra();
    Log("world: activation x%d, LOD x%d, high detail %s, interior visibility %d", g_cfg.activationScale, g_cfg.lodScale,
        g_cfg.highDetail ? "on" : "off", g_cfg.interiorVisibility);
    return n > 0;
}

}  // namespace ufx
