// Keeps the on-screen size of the game's blur, bloom and depth of field with a bigger 3D scene.
//
// The gaussian offsets are `tap / buffer size` in UV units, so a buffer twice as wide halves the blur on the screen.
// Dividing the size the table builder is given by the scene scale gives back the original offsets. The scale is the
// width of the scene over 1280. Storage buffers that were left at their original size are skipped. Whether they were
// enlarged is decided once at start-up (render_scale.cpp), so the live ScalePostBuffers setting is not read here: the
// menu changes it at once, but the buffers only change after a restart.
#include "../base.h"
#include "../sites.h"

namespace ufx {
extern bool g_postBuffersScaled;  // render_scale.cpp
namespace {

typedef void(__cdecl* GaussFn)(int table, int width, int height, int taps, float sigma);
const GaussFn RealGauss = (GaussFn)(uintptr_t)kGaussTableFn;

// The table holds a fixed number of taps. Spread `scene / 1280` times wider they no longer overlap: at 300 % of a 1920
// output (scale 4.5) the taps sit 4.5 texels apart and the "blur" is a few shifted copies of the picture, the doubled edges
// of the depth of field. Up to twice the spacing the bilinear filtering of every tap still covers the gaps, so the
// compensation stops there: on a very large scene the blur is a little narrower than at 1280, never doubled.
constexpr int kMaxSpread = 2;

int Compensate(int size) {
    if (size <= 0 || g_sceneWidth <= 1280) return size;
    if (!g_postBuffersScaled && (size == 896 || size == 504 || size == 448 || size == 252)) return size;
    int v = (int)((long long)size * 1280 / g_sceneWidth);
    const int floor = size / kMaxSpread;
    if (v < floor) v = floor;
    return v < 1 ? 1 : v;
}

void __cdecl GaussHook(int table, int width, int height, int taps, float sigma) {
    RealGauss(table, Compensate(width), Compensate(height), taps, sigma);
}

}  // namespace

bool InstallBlurLook() {
    if (g_sceneWidth <= 1280) {
        Log("blur look: the scene is not scaled, nothing to compensate");
        return false;
    }
    for (const SiteDef& s : kGaussCalls)
        if (!BytesMatch(s.va, s.bytes, s.len)) {
            Log("blur look: the call at %08X differs from DP.exe 1.01b, skipped", s.va);
            return false;
        }
    for (const SiteDef& s : kGaussCalls)
        if (!PatchCall(s.va, s.bytes, s.len, (const void*)GaussHook)) {
            Log("blur look: could not patch the call at %08X", s.va);
            return false;
        }
    Log("blur look: blur sizes kept as at 1280 wide (scene %d wide)", g_sceneWidth);
    return true;
}

}  // namespace ufx
