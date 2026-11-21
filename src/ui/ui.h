// Overlay drawing used by the in-game menu (internal to the ui module).
#pragma once
#include "../base.h"

namespace ufx {
namespace ui {

// Saves the device state and prepares drawing into the back buffer in 1280x720 layout units. Returns false on
// failure, in which case nothing was changed and OverlayEnd must not be called.
bool OverlayBegin(void* device);
void OverlayEnd();

// Font weights for DrawText: body text stays Regular; titles and section headings use Segoe UI Semibold, so they read as
// headings by their weight and not only by their size and colour.
constexpr int kRegular = 400;   // FW_NORMAL
constexpr int kSemibold = 600;  // FW_SEMIBOLD

// The nearest whole number (halves go up, negative values too). Without the C runtime there is no roundf; the overlay uses
// it to put text and edges on whole back buffer pixels, and the menu to keep a dragged panel on whole layout units.
inline float SnapWhole(float v) {
    int i = (int)v;              // truncates towards zero
    if ((float)i > v) --i;       // floor for negative values
    return (float)(v - (float)i >= 0.5f ? i + 1 : i);
}

void FillRect(float x, float y, float w, float h, u32 argb);
// A rectangle with rounded corners of radius r (layout units; at most half the shorter side, so r = h / 2 is a pill). The
// straight edges lie on whole pixels like FillRect's, the corners are anti-aliased. Without the disk texture it is drawn
// square by FillRect.
void FillRound(float x, float y, float w, float h, float r, u32 argb);
// The same with a border of `frameWidth` layout units (whole pixels, at least one) in `frame` and the inside in `fill`.
void FillRoundFramed(float x, float y, float w, float h, float r, u32 fill, u32 frame, float frameWidth = 1.f);
// The on/off switch of the menu and the mods page: a pill (red when on, grey when off) with a round white knob, its top-left
// corner at x,y. The knob is centred in back buffer pixels, so it sits evenly in the track at every scale. `travel` is how
// far the knob has come since the switch flipped (0 = still at the old side, 1 = at rest on its side); the track colour
// follows it.
constexpr float kSwitchW = 40, kSwitchH = 24;
constexpr float kSwitchSeconds = 0.12f;  // how long the knob takes to slide across ([Menu] Animations)
void Switch(float x, float y, bool on, float travel = 1.f);
// The menu's fade-in: every primitive here multiplies its alpha by `alpha` (0..1) and is drawn `offsetY` layout units lower
// (in whole pixels, so text stays crisp while it rises). Only the menu sets it, around its own Draw, and puts 1, 0 back right
// after: the notice and the performance panels drawn later in the same frame must not inherit it.
void SetOverlayFade(float alpha, float offsetY);
// Draws text with its top-left corner at x,y. Returns the width in layout units. One string is at most 255 characters
// (longer ones are not drawn): long texts are wrapped by the caller.
float DrawText(const wchar_t* text, float x, float y, u32 argb, int pixelHeight, int weight = kRegular);
float TextWidth(const wchar_t* text, int pixelHeight, int weight = kRegular);
// How many of the first `length` characters fit in `maxWidth` layout units (measured only, nothing is drawn or cached).
int TextFit(const wchar_t* text, int length, float maxWidth, int pixelHeight, int weight = kRegular);

// Quiet motion of the menu ([Menu] Animations; the clock lives in menu.cpp and advances once per drawn menu frame).
bool MotionOn();                                // the setting is on
long long MotionStamp();                        // the QueryPerformanceCounter tick now, for when a switch flipped
float MotionProgress(long long from, float seconds);  // eased 0..1 since `from` over `seconds`; 1 when motion is off
// A selection highlight that glides to its row. `target` is the row's position relative to something that moves with the
// panel (the top of the list), so a dragged panel never leaves the highlight behind. It jumps without gliding when it was
// not drawn in the previous menu frame (a page shown again, the menu just opened), when `context` changed (another page, the
// list scrolled) or when `index` moved by more than 6 rows. Returns where to draw it this frame.
struct Glide {
    float pos;
    int index, context;
    unsigned frame;  // the menu frame it was last drawn in
};
float GlideTo(Glide& glide, float target, int index, int context);

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
// game's composite, it is swapped for ours and true is returned; End then puts the shader and the two constants back.
struct LookSaved {
    void* shader;        // the game's pixel shader (a reference is held until End)
    float constants[8];  // c20 and c21 as they were
};
bool XboxLookBegin(void* device, LookSaved* saved);
void XboxLookEnd(void* device, const LookSaved* saved);
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
