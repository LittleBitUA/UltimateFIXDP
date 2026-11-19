// Small graphics settings that are plain byte changes in the game, adjustable while it runs.
#include "../base.h"
#include "../sites.h"

namespace ufx {
namespace {

bool g_anisoReady = false;
bool g_reflectionReady = false;
bool g_exposureReady = false;
alignas(8) const double kOne = 1.0;

}  // namespace

// Called at start and after every change in the menu.
void ApplyGraphicsTweaks() {
    if (g_anisoReady) {
        u8 value = (u8)g_cfg.anisotropy;  // the immediate of `push 8`
        WriteCode(kSiteAniso1.va + 1, &value, 1);
        WriteCode(kSiteAniso2.va + 1, &value, 1);
    }
    if (g_exposureReady) {
        const SiteDef* ex[2] = {&kSiteExposure85, &kSiteExposure75};
        for (const SiteDef* e : ex) {
            u32 addr = g_cfg.xboxExposure ? (u32)(uintptr_t)&kOne : (u32)e->bytes[2] | ((u32)e->bytes[3] << 8) |
                                                                      ((u32)e->bytes[4] << 16) | ((u32)e->bytes[5] << 24);
            u8 b[4] = {(u8)addr, (u8)(addr >> 8), (u8)(addr >> 16), (u8)(addr >> 24)};
            WriteCode(e->va + 2, b, 4);
        }
    }
    if (g_reflectionReady) {
        const SiteDef& s = kSiteReflectionBias;
        if (g_cfg.reflectionBiasFix)
            WriteCode(s.va, kReflectionBiasFixed, s.len);
        else
            WriteCode(s.va, s.bytes, s.len);
    }
}

// Sun shadow cascades (16-bit depth): the bias table at A00148h, per cascade {float constant, float slope}, for the outdoor
// mode. The game gives all three 2e-5 / 2.0, about one step of D16: the far cascades (50..200, 200..1000 units, one texel
// covers most of a unit) crawl with stripes as the camera moves. With the fix the middle and the far one get more.
namespace {
constexpr u32 kShadowBiasVa = 0x00A00148;
const u8 kShadowBiasOriginal[24] = {0xAC, 0xC5, 0xA7, 0x37, 0x00, 0x00, 0x00, 0x40, 0xAC, 0xC5, 0xA7, 0x37,
                                    0x00, 0x00, 0x00, 0x40, 0xAC, 0xC5, 0xA7, 0x37, 0x00, 0x00, 0x00, 0x40};
bool g_shadowReady = false;
}  // namespace

void ApplyShadowBias() {
    if (!g_shadowReady) return;
    volatile float* t = (volatile float*)(uintptr_t)kShadowBiasVa;
    const bool on = g_cfg.shadowBiasFix != 0;
    t[2] = on ? 6e-5f : 2e-5f;
    t[3] = on ? 2.5f : 2.0f;
    t[4] = on ? 1.5e-4f : 2e-5f;
    t[5] = on ? 3.0f : 2.0f;
}

bool InstallGraphicsTweaks() {
    bool any = false;
    if (BytesMatch(kShadowBiasVa, kShadowBiasOriginal, sizeof(kShadowBiasOriginal))) {
        g_shadowReady = true;
        ApplyShadowBias();
        Log("graphics: shadow cascade bias %s (switchable in the menu)", g_cfg.shadowBiasFix ? "raised on the far cascades" : "as the game");
        any = true;
    } else {
        Log("graphics: the shadow bias table differs from DP.exe 1.01b, left alone");
    }
    // Anisotropy sites also match when we already changed them: accept any value in the `push` immediate.
    const SiteDef* aniso[2] = {&kSiteAniso1, &kSiteAniso2};
    bool ok = true;
    for (const SiteDef* s : aniso) {
        const u8 tail[2] = {0x6A, 0x0A};
        if (*(const u8*)(uintptr_t)s->va != 0x6A || !BytesMatch(s->va + 2, tail, 2)) ok = false;
    }
    if (ok) {
        g_anisoReady = true;
        any = true;
        Log("graphics: anisotropy sites found, maximum %dx", g_cfg.anisotropy);
    } else {
        Log("graphics: anisotropy sites differ from DP.exe 1.01b, skipped");
    }
    if (BytesMatch(kSiteReflectionBias.va, kSiteReflectionBias.bytes, kSiteReflectionBias.len)) {
        g_reflectionReady = true;
        any = true;
    } else {
        Log("graphics: reflection bias site differs from DP.exe 1.01b, skipped");
    }
    if (BytesMatch(kSiteExposure85.va, kSiteExposure85.bytes, 6) && BytesMatch(kSiteExposure75.va, kSiteExposure75.bytes, 6)) {
        g_exposureReady = true;
        any = true;
    } else {
        Log("graphics: exposure sites differ from DP.exe 1.01b, skipped");
    }
    ApplyGraphicsTweaks();
    return any;
}

}  // namespace ufx
