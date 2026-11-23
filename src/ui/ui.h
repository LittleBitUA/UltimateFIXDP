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
// Draws `text` cut with "…" when it is wider than maxW. Returns the width drawn.
float DrawFitted(const wchar_t* text, float x, float y, u32 color, int px, float maxW, int weight = kRegular);
// Word wrap: '\n' starts a new line, and a line that would be wider than maxW breaks at the last space that fits. At most
// maxLines lines; when the text needs more, the last one ends with "…". Returns the number of lines; with draw = false
// it only counts them.
int DrawLines(const wchar_t* text, float x, float y, u32 color, int px, float lineH, float maxW, int maxLines, bool draw,
              int weight = kRegular);
// The same from line `skip` of the wrapped text on (a scrolled text keeps the wrap it has unscrolled), drawn from y. When the
// text goes on past the last line, that line ends with "…" and then `tail` (null = nothing) in tailColor: the description box
// says there how to read the rest. Returns the number of lines drawn, or with draw = false how many there would be.
int DrawLinesEx(const wchar_t* text, float x, float y, u32 color, int px, float lineH, float maxW, int skip, int maxLines, bool draw,
                const wchar_t* tail, u32 tailColor, int weight = kRegular);

// A picture read from a file (PNG, JPG, BMP, DDS through d3dx9_43), cached by its path; `width` and `height` get the size
// of the file, for cropping. Null when it cannot be read (tried once, logged once) and, for a frame, while another picture
// was read in the same overlay pass. The texture belongs to the overlay: no Release.
void* ImageTexture(const wchar_t* path, u32* width, u32* height);
// Draws a texture over x,y,w,h (layout units, on whole pixels like FillRect) with the texture coordinates u0,v0..u1,v1 and
// the colour and alpha of `tint`; `radius` rounds its corners (anti-aliased, over any background).
void DrawImage(void* texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 tint = 0xFFFFFFFF,
               float radius = 0.f);

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
// The open menu's last input came from the gamepad: a page's hint line then names the pad's buttons, drawn by DrawPadLine,
// whose text has the buttons as tokens drawn as small key caps in the pad's family: {A} {B} {X} {Y} {LB} {RB} {LT} {RT} {OPEN}.
bool PadHintsShown();
void DrawPadLine(const wchar_t* text, float x, float y, u32 argb, int px, float maxW);
void NetPage(float left, float top, float right, float bottom);   // multiplayer (net_page.cpp)
void NetPageInput(bool up, bool down, bool accept, bool click, float mx, float my);
bool NetPageTyping();                            // one of its text fields has the keyboard
void ModsPage(float left, float top, float right, float bottom);  // the mod manager page (mods_page.cpp)
void ModsPageInput(bool up, bool down, bool toggle, bool openFolder, bool raise, bool lower, bool click, float mx, float my);
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
