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
void ResidentsPage(float left, float top, float right, float bottom);  // the schedule viewer, drawn into the menu's content area (residents.cpp)
void ResidentsScroll(int rows);                 // its list: positive = down
void ScreenshotFrame(void* device);              // saves a PNG when the screenshot key is pressed
void ReleasePostResources();                     // before the device is reset (also frees the reused state block)

}  // namespace ui
}  // namespace ufx
