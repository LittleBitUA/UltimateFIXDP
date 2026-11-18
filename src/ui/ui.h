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
void RequestScreenshot();                       // a PNG of the next frame (no menu in it)
void OpenPhotoPage();                           // opens the menu on the photo mode page
void PerfPostBegin(void* device);   // GPU time of the final-image passes (with the performance overlay)
void PerfPostEnd(void* device);
void PerfOverlayFrame(void* device, double fps, double frameMs, double presentMs, double physicsMs);
void FrameStatsPush(double frameMs);
void FrameStatsPanel(void* device, double totalMs, double gameMs, double physicsMs, double presentMs, double worstMs, double busyMs);
bool PhotoPageOpen();                           // the menu is open on that page
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
