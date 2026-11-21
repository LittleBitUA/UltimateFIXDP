// Overlay drawing used by the in-game menu (internal to the ui module).
#pragma once
#include "../base.h"

namespace ufx {
namespace ui {

// Saves the device state and prepares drawing into the back buffer in 1280x720 layout units. Returns false on
// failure, in which case nothing was changed and OverlayEnd must not be called.
bool OverlayBegin(void* device);
void OverlayEnd();

void FillRect(float x, float y, float w, float h, u32 argb);
// Draws text with its top-left corner at x,y. Returns the width in layout units. One string is at most 255 characters
// (longer ones are not drawn): long texts are wrapped by the caller.
float DrawText(const wchar_t* text, float x, float y, u32 argb, int pixelHeight);
float TextWidth(const wchar_t* text, int pixelHeight);
// How many of the first `length` characters fit in `maxWidth` layout units (measured only, nothing is drawn or cached).
int TextFit(const wchar_t* text, int length, float maxWidth, int pixelHeight);

// Final-image passes, run on the finished back buffer right before the menu is drawn.
void SharpenFrame(void* device, int percent);   // unsharp mask, 0 = off
void AntialiasFrame(void* device, int percent); // edge-directional smoothing (FXAA-like), 0 = off
void AmbientOcclusionPass(void* device, int strengthPercent, int radiusPixels);  // in front of the final composite
void ColorFrame(void* device);                  // photo filters: brightness, contrast, saturation, tint, vignette
// The same helpers for passes that live in other files: a pixel shader from HLSL (the first profile that compiles; with
// `logName` the compiler's message is logged when a profile fails), and one pass over the finished back buffer (c0 = texel
// size and percent / 100, `extra` = c1 and c2 or null).
bool BuildPixelShader(void* device, const char* source, size_t length, void** shader, const char* const* profiles, const char* logName);
void FinalImagePass(void* device, void** shader, bool* failed, const char* name, const char* source, size_t length,
                    const char* const* profiles, int percent, const float* extra);
// Xbox 360 look (xbox_look.cpp). Begin runs right before the game's final composite draw: when the bound pixel shader is the
// game's composite, it is swapped for ours and true is returned; End then puts the shader and the constants back. The same
// pair draws the smooth upscaling of the bloom and the depth of field ([Graphics] SmoothBloom) and the dither against banding
// ([Graphics] Dither), with the Xbox look off too.
struct LookSaved {
    void* shader;        // the game's pixel shader (a reference is held until End)
    float constants[8];  // c20 and c21 as they were
    bool offset;         // c17 was changed for the game's 4-tap filter (GameAliasFilter=2)
    float aliasOffset[4];  // c17 as it was
    bool smooth;           // c23 was set for the smooth upscaling (SmoothBloom)
    float sizeRegister[4];  // c23 as it was
    DWORD filters[4];      // min and mag filter of stages 1 and 3 as they were
    u32 filterSet;         // which of them were set to linear for the draw (bit i = filters[i])
};
bool XboxLookBegin(void* device, LookSaved* saved);
void XboxLookEnd(void* device, const LookSaved* saved);
// The game's own 4-tap filter without the grading ([Graphics] GameAliasFilter): the same Begin/End pair, for the case where
// XboxLookBegin did not take the draw. Begin swaps the game's variant 30 for its variant 31; End puts the shader and c17 back.
bool GameAliasBegin(void* device, LookSaved* saved);
void GameAliasEnd(void* device, const LookSaved* saved);
void XboxGammaFrame(void* device);              // display gamma of the console on the finished picture
void RequestScreenshot();                       // a PNG of the next frame (no menu in it)
void OpenPhotoPage();                           // opens the menu on the photo mode page
void PerfPostBegin(void* device);   // GPU time of the final-image passes (with the performance overlay)
void PerfPostEnd(void* device);
void PerfOverlayFrame(void* device, double fps, double frameMs, double presentMs, double physicsMs);
void FrameStatsPush(double frameMs);
void FrameStatsPanel(void* device, double totalMs, double gameMs, double physicsMs, double presentMs, double worstMs, double busyMs);
bool PhotoPageOpen();                           // the menu is open on that page
void ResidentsPage(float left, float top, float right, float bottom);  // the schedule viewer, drawn into the menu's content area (residents.cpp)
void ResidentsScroll(int rows);                 // its list: positive = down
void ModsPage(float left, float top, float right, float bottom);  // the mod manager page (mods_page.cpp)
void ModsPageInput(bool up, bool down, bool toggle, bool raise, bool lower, bool click, float mx, float my);
void ScreenshotFrame(void* device);              // saves a PNG when the screenshot key is pressed
void ReleasePostResources();                     // before the device is reset (also frees the reused state block)

// SMAA 1x (smaa.cpp): the other choice for AntialiasFrame. Returns false when it cannot run (a shader that does not
// compile, a texture that cannot be made): it says so in the log once, and the caller uses its FXAA-like pass instead.
bool SmaaFrame(void* device, int percent);
void ReleaseSmaaTargets();                       // its render targets: before the device is reset, and while FXAA is chosen

// Building blocks of overlay.cpp for a final-image pass in another file.
void* PostBackBuffer(void* device, UINT* width, UINT* height, UINT* format);  // the caller releases the surface
// The copy of the back buffer the final-image passes share (a render target of the same size and format, made on
// demand). The pointers belong to overlay.cpp: no Release. False = it cannot be created.
bool PostCopyTarget(void* device, UINT width, UINT height, UINT format, void** texture, void** surface);
void PostFullQuad(UINT width, UINT height);      // between OverlayBegin and OverlayEnd: texture coordinates 0..1 over the target

}  // namespace ui
}  // namespace ufx
