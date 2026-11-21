// Draws rectangles and text on top of the finished frame, just before it is presented.
//
// Text is rendered with GDI into small textures (so every language the system fonts cover works,
// Ukrainian included) and drawn as textured quads with the fixed-function pipeline. The device state is
// saved with a state block and restored afterwards; the game never notices.
#include "ui.h"
#include "d3d9.h"
#include "theme.h"

namespace ufx {
namespace ui {
namespace {

using namespace d3d;

struct Vertex {
    float x, y, z, rhw;
    u32 color;
    float u, v;
};

struct TextEntry {
    void* texture;
    int width, height;
    int pixelHeight;
    int weight;  // kRegular or kSemibold: part of the key, the same string is cached once per weight
    int length;
    u32 hash;
    u32 lastUsed;  // the use clock at the last lookup: the least recently used entry is replaced when the cache is full
    wchar_t text[256];
};

// Room for every string of a menu page plus the panels. When it is full only the oldest entry is replaced, so the
// strings on screen are not rebuilt all at once (that used to make a hitch of dozens of GDI uploads in one frame).
constexpr int kMaxTexts = 256;
TextEntry g_texts[kMaxTexts];
int g_textCount = 0;
u32 g_useClock = 0;

void* g_dev = nullptr;
void* g_stateBlock = nullptr;  // the block OverlayBegin saved the game's state into
bool g_stateBlockOwned = false;  // a block of its own for this one pass: released in OverlayEnd
void* g_sbCache = nullptr;     // the reused D3DSBT_ALL block (captured again at every OverlayBegin), released before a Reset
void* g_sbDevice = nullptr;
void* g_oldTarget = nullptr;
bool g_inScene = false;
int g_imageLoads = 0;  // pictures read from files in this overlay pass (ImageTexture): one at most, so a page full of covers
                       // spreads its loading over frames instead of stalling one

typedef ULONG(WINAPI* ReleaseFn)(void*);
typedef HRESULT(WINAPI* DwordDwordFn)(void*, DWORD, DWORD);
typedef HRESULT(WINAPI* Dword3Fn)(void*, DWORD, DWORD, DWORD);

void SetRS(u32 state, u32 value) { Method<DwordDwordFn>(g_dev, kSetRenderState)(g_dev, state, value); }
void SetTSS(u32 type, u32 value) { Method<Dword3Fn>(g_dev, kSetTextureStageState)(g_dev, 0, type, value); }
void SetSampler(u32 type, u32 value) { Method<Dword3Fn>(g_dev, kSetSamplerState)(g_dev, 0, type, value); }

// One memory DC and one font per pixel size and weight, kept for the whole session (a new font for every string was slow).
HDC g_textDc = nullptr;
struct FontEntry {
    int px;
    int weight;
    HFONT font;
};
constexpr int kMaxFonts = 24;  // the menu uses about a dozen sizes, a few of them in both weights
FontEntry g_fonts[kMaxFonts];
int g_fontCount = 0;

HDC TextDc() {
    if (!g_textDc) g_textDc = CreateCompatibleDC(nullptr);
    return g_textDc;
}

HFONT MakeFont(int px, int weight, const wchar_t* face) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

float ScaleY();

// The semibold weight. GDI keeps the old four-style families, so Segoe UI itself has only Regular and Bold, and weight 600
// on it comes out Bold; the real semibold cut (seguisb.ttf, on every Windows since 7) is the family "Segoe UI Semibold".
// Where that face is missing (Wine and Proton ship no Segoe UI) GDI would quietly put another font in its place, so the
// face the DC really got is checked, and without it the heading is Segoe UI (or what stands in for it) at weight 600.
HFONT MakeSemibold(HDC dc, int px) {
    static bool logged = false;
    HFONT font = MakeFont(px, FW_SEMIBOLD, L"Segoe UI Semibold");
    bool real = false;
    if (font) {
        HGDIOBJ old = SelectObject(dc, font);
        wchar_t face[LF_FACESIZE];
        real = GetTextFaceW(dc, LF_FACESIZE, face) > 0 && lstrcmpiW(face, L"Segoe UI Semibold") == 0;
        SelectObject(dc, old);
        if (!real) {
            DeleteObject(font);
            font = MakeFont(px, FW_SEMIBOLD, L"Segoe UI");
        }
    }
    if (font && !logged) {  // the first heading font: the whole feature is on from here
        logged = true;
        const int scale = (int)(ScaleY() * 100.f + 0.5f);
        Log("overlay: text snapped to whole pixels, scale %d.%02d, headings in %s", scale / 100, scale % 100,
            real ? "Segoe UI Semibold" : "Segoe UI at weight 600 (no Segoe UI Semibold face)");
    }
    return font;
}

// The font is never left selected in the DC, so an old one can be deleted when the table is full.
HFONT FontFor(int px, int weight) {
    for (int i = 0; i < g_fontCount; ++i)
        if (g_fonts[i].px == px && g_fonts[i].weight == weight) return g_fonts[i].font;
    HDC dc = TextDc();
    HFONT font = weight == kSemibold && dc ? MakeSemibold(dc, px) : MakeFont(px, FW_NORMAL, L"Segoe UI");
    if (!font) return nullptr;
    int slot = g_fontCount < kMaxFonts ? g_fontCount++ : kMaxFonts - 1;
    if (slot == kMaxFonts - 1 && g_fonts[slot].font) DeleteObject(g_fonts[slot].font);
    g_fonts[slot].px = px;
    g_fonts[slot].weight = weight;
    g_fonts[slot].font = font;
    return font;
}

u32 HashText(const wchar_t* text, int* length) {
    u32 h = 2166136261u;  // FNV-1a: an exact (ordinal) key, not the locale-aware lstrcmpW
    int n = 0;
    for (; text[n]; ++n) h = (h ^ (u32)text[n]) * 16777619u;
    *length = n;
    return h;
}

// Renders `text` with GDI and uploads it as an A8R8G8B8 texture (white, alpha = coverage).
TextEntry* MakeText(const wchar_t* text, int len, u32 hash, int px, int weight) {
    if (len <= 0 || len >= 256) return nullptr;
    TextEntry* slot = nullptr;
    if (g_textCount < kMaxTexts) {
        slot = &g_texts[g_textCount];
    } else {  // full: replace the least recently used entry
        slot = &g_texts[0];
        for (int i = 1; i < kMaxTexts; ++i)
            if (g_texts[i].lastUsed < slot->lastUsed) slot = &g_texts[i];
        if (slot->texture) Method<ReleaseFn>(slot->texture, kRelease)(slot->texture);
        slot->texture = nullptr;
        slot->length = 0;  // an empty slot until the new texture is ready
        slot->text[0] = 0;
    }

    HDC dc = TextDc();
    HFONT font = dc ? FontFor(px, weight) : nullptr;
    if (!font) return nullptr;
    HGDIOBJ oldFont = SelectObject(dc, font);
    SIZE size;
    GetTextExtentPoint32W(dc, text, len, &size);
    int w = size.cx + 4, h = size.cy + 4;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    TextEntry* result = nullptr;
    if (bmp && bits) {
        HGDIOBJ oldBmp = SelectObject(dc, bmp);
        ZeroMemory(bits, (size_t)w * h * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 2, 2, text, len);
        GdiFlush();

        typedef HRESULT(WINAPI * CreateTextureFn)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**, void*);
        void* tex = nullptr;
        HRESULT hr = Method<CreateTextureFn>(g_dev, kCreateTexture)(g_dev, (UINT)w, (UINT)h, 1, 0, 21 /*A8R8G8B8*/,
                                                                     1 /*MANAGED*/, &tex, nullptr);
        if (SUCCEEDED(hr) && tex) {
            typedef HRESULT(WINAPI * LockFn)(void*, UINT, LockedRect*, const RECT*, DWORD);
            typedef HRESULT(WINAPI * UnlockFn)(void*, UINT);
            LockedRect lr;
            if (SUCCEEDED(Method<LockFn>(tex, kTexLockRect)(tex, 0, &lr, nullptr, 0))) {
                const u32* src = (const u32*)bits;
                for (int y = 0; y < h; ++y) {
                    u32* dst = (u32*)((u8*)lr.bits + (size_t)y * lr.pitch);
                    for (int x = 0; x < w; ++x) {
                        u32 a = src[y * w + x] & 0xFF;  // grey text on black: any channel is the coverage
                        dst[x] = (a << 24) | 0x00FFFFFF;
                    }
                }
                Method<UnlockFn>(tex, kTexUnlockRect)(tex, 0);
                if (slot == &g_texts[g_textCount]) ++g_textCount;
                TextEntry& e = *slot;
                e.texture = tex;
                e.width = w;
                e.height = h;
                e.pixelHeight = px;
                e.weight = weight;
                e.length = len;
                e.hash = hash;
                e.lastUsed = ++g_useClock;
                for (int i = 0; i <= len; ++i) e.text[i] = text[i];
                result = &e;
            } else {
                Method<ReleaseFn>(tex, kRelease)(tex);
            }
        }
        SelectObject(dc, oldBmp);
    }
    if (bmp) DeleteObject(bmp);
    SelectObject(dc, oldFont);
    return result;
}

TextEntry* GetText(const wchar_t* text, int px, int weight) {
    int len;
    u32 hash = HashText(text, &len);
    for (int i = 0; i < g_textCount; ++i) {
        TextEntry& e = g_texts[i];
        if (e.hash != hash || e.length != len || e.pixelHeight != px || e.weight != weight || !e.texture) continue;
        int k = 0;
        while (k < len && e.text[k] == text[k]) ++k;
        if (k == len) {
            e.lastUsed = ++g_useClock;
            return &e;
        }
    }
    return MakeText(text, len, hash, px, weight);
}

// Scale of the overlay relative to the 1280x720 layout: the back buffer may be bigger. The output is always 16:9, so both
// axes scale the same; the two functions stay apart in case that ever changes.
float ScaleX() { return g_outputW ? (float)g_outputW / 1280.f : 1.f; }
float ScaleY() { return g_outputW && g_outputH ? (float)g_outputH / 720.f : ScaleX(); }
int FontPx(int px) { return (int)((float)px * ScaleY() + 0.5f); }  // the font is rendered at the real size

// The fade of the menu's opening (SetOverlayFade): every overlay primitive multiplies its alpha by g_fadeAlpha and moves
// down by g_fadeOffset layout units. Applied where the shapes reach back buffer pixels (FillRect, DrawText, RoundPixels),
// so a shape drawn through another (a square fallback, a framed shape) is faded and moved once, and never in Quad: the
// final-image passes draw with it too.
float g_fadeAlpha = 1.f;
float g_fadeOffset = 0.f;

u32 Faded(u32 argb) {
    if (g_fadeAlpha >= 1.f) return argb;
    const int a = (int)((float)(argb >> 24) * g_fadeAlpha + 0.5f);
    return (argb & 0x00FFFFFFu) | ((u32)(a < 0 ? 0 : a > 255 ? 255 : a) << 24);
}

// The offset in whole back buffer pixels: the text stays on whole pixels while it rises (see DrawText).
float FadeOffsetPixels() { return g_fadeOffset != 0.f ? SnapWhole(g_fadeOffset * ScaleY()) : 0.f; }

// Draws a quad in back buffer pixels.
void Quad(float x0, float y0, float x1, float y1, u32 color, float u0, float v0, float u1, float v1) {
    // -0.5 aligns texels with pixels for pre-transformed vertices
    Vertex v[6] = {
        {x0 - 0.5f, y0 - 0.5f, 0.f, 1.f, color, u0, v0}, {x1 - 0.5f, y0 - 0.5f, 0.f, 1.f, color, u1, v0},
        {x0 - 0.5f, y1 - 0.5f, 0.f, 1.f, color, u0, v1}, {x1 - 0.5f, y0 - 0.5f, 0.f, 1.f, color, u1, v0},
        {x1 - 0.5f, y1 - 0.5f, 0.f, 1.f, color, u1, v1}, {x0 - 0.5f, y1 - 0.5f, 0.f, 1.f, color, u0, v1},
    };
    typedef HRESULT(WINAPI * DrawUpFn)(void*, DWORD, UINT, const void*, UINT);
    Method<DrawUpFn>(g_dev, kDrawPrimitiveUP)(g_dev, 4 /*TRIANGLELIST*/, 2, v, sizeof(Vertex));
}

void ReleaseStateBlockCache() {
    if (g_sbCache) Method<ReleaseFn>(g_sbCache, kRelease)(g_sbCache);
    g_sbCache = nullptr;
    g_sbDevice = nullptr;
}

// Saves the whole device state. A reused block is only captured again (creating and releasing a D3DSBT_ALL block
// for every pass cost CPU time every frame); `keep` = false gives the pass a block of its own, released at the end.
bool SaveState(void* dev, bool keep) {
    typedef HRESULT(WINAPI * CreateSbFn)(void*, DWORD, void**);
    typedef HRESULT(WINAPI * Fn0)(void*);
    g_stateBlock = nullptr;
    g_stateBlockOwned = false;
    if (keep && g_sbCache && g_sbDevice == dev) {
        if (SUCCEEDED(Method<Fn0>(g_sbCache, kSbCapture)(g_sbCache))) {
            g_stateBlock = g_sbCache;
            return true;
        }
    }
    if (keep) ReleaseStateBlockCache();  // another device, or the capture failed: start again
    void* block = nullptr;
    if (FAILED(Method<CreateSbFn>(dev, kCreateStateBlock)(dev, 1 /*D3DSBT_ALL*/, &block)) || !block) return false;
    g_stateBlock = block;
    if (keep) {
        g_sbCache = block;
        g_sbDevice = dev;
    } else {
        g_stateBlockOwned = true;
    }
    return true;
}

bool BeginOverlay(void* dev, bool keepStateBlock) {
    g_dev = dev;
    typedef HRESULT(WINAPI * GetRtFn)(void*, DWORD, void**);
    typedef HRESULT(WINAPI * SetRtFn)(void*, DWORD, void*);
    typedef HRESULT(WINAPI * GetBbFn)(void*, UINT, UINT, UINT, void**);
    typedef HRESULT(WINAPI * Fn0)(void*);
    typedef HRESULT(WINAPI * SetPtrFn)(void*, void*);
    typedef HRESULT(WINAPI * SetViewportFn)(void*, const Viewport*);

    if (!SaveState(dev, keepStateBlock)) return false;
    g_imageLoads = 0;

    g_oldTarget = nullptr;
    Method<GetRtFn>(dev, kGetRenderTarget)(dev, 0, &g_oldTarget);
    void* back = nullptr;
    if (SUCCEEDED(Method<GetBbFn>(dev, kGetBackBuffer)(dev, 0, 0, 0, &back)) && back) {
        Method<SetRtFn>(dev, kSetRenderTarget)(dev, 0, back);
        Method<ReleaseFn>(back, kRelease)(back);
    }
    g_inScene = SUCCEEDED(Method<Fn0>(dev, kBeginScene)(dev));

    Viewport vp = {0, 0, (DWORD)(g_outputW ? g_outputW : 1280), (DWORD)(g_outputH ? g_outputH : 720), 0.f, 1.f};
    Method<SetViewportFn>(dev, kSetViewport)(dev, &vp);
    Method<SetPtrFn>(dev, kSetVertexShader)(dev, nullptr);
    Method<SetPtrFn>(dev, kSetPixelShader)(dev, nullptr);
    typedef HRESULT(WINAPI * SetFvfFn)(void*, DWORD);
    Method<SetFvfFn>(dev, kSetFVF)(dev, kFvfXyzrhwDiffuseTex1);

    SetRS(RS_ZENABLE, 0);
    SetRS(RS_ZWRITEENABLE, 0);
    SetRS(RS_ALPHATESTENABLE, 0);
    SetRS(RS_CULLMODE, 1);
    SetRS(RS_ALPHABLENDENABLE, 1);
    SetRS(RS_SRCBLEND, 5);   // SRCALPHA
    SetRS(RS_DESTBLEND, 6);  // INVSRCALPHA
    SetRS(RS_BLENDOP, 1);
    SetRS(RS_SEPARATEALPHABLEND, 0);
    SetRS(RS_FOGENABLE, 0);
    SetRS(RS_SPECULARENABLE, 0);
    SetRS(RS_STENCILENABLE, 0);
    SetRS(RS_CLIPPING, 1);
    SetRS(RS_LIGHTING, 0);
    SetRS(RS_COLORWRITEENABLE, 0xF);
    SetRS(RS_SCISSORTESTENABLE, 0);
    SetRS(RS_SRGBWRITEENABLE, 0);
    SetRS(RS_FILLMODE, 3);
    SetSampler(SAMP_MINFILTER, 2);
    SetSampler(SAMP_MAGFILTER, 2);
    SetSampler(SAMP_MIPFILTER, 0);
    SetSampler(SAMP_ADDRESSU, 3);
    SetSampler(SAMP_ADDRESSV, 3);
    SetSampler(SAMP_SRGBTEXTURE, 0);
    Method<Dword3Fn>(dev, kSetTextureStageState)(dev, 1, TSS_COLOROP, TOP_DISABLE);
    Method<Dword3Fn>(dev, kSetTextureStageState)(dev, 1, TSS_ALPHAOP, TOP_DISABLE);
    return true;
}

}  // namespace

bool OverlayBegin(void* dev) { return BeginOverlay(dev, true); }

void OverlayEnd() {
    typedef HRESULT(WINAPI * Fn0)(void*);
    typedef HRESULT(WINAPI * SetRtFn)(void*, DWORD, void*);
    if (g_inScene) Method<Fn0>(g_dev, kEndScene)(g_dev);
    g_inScene = false;
    if (g_oldTarget) {
        Method<SetRtFn>(g_dev, kSetRenderTarget)(g_dev, 0, g_oldTarget);
        Method<ReleaseFn>(g_oldTarget, kRelease)(g_oldTarget);
        g_oldTarget = nullptr;
    }
    if (g_stateBlock) {
        Method<Fn0>(g_stateBlock, kSbApply)(g_stateBlock);
        if (g_stateBlockOwned) Method<ReleaseFn>(g_stateBlock, kRelease)(g_stateBlock);  // the reused one stays for the next pass
        g_stateBlock = nullptr;
        g_stateBlockOwned = false;
    }
}

// One axis of a rectangle in back buffer pixels, on whole pixels. A thin one (a hairline, a bar of a graph) keeps its rounded
// thickness, at least one pixel, wherever it starts: at scale 1.5 a 1-unit line otherwise came out 1 or 2 pixels thick by
// its position. A bigger one has both edges rounded, so two rectangles that meet in layout units meet on the screen too.
void SnapSpan(float from, float size, float scale, float* a, float* b) {
    if (size < 0.f) {  // a negative size spans back from `from`, as the unsnapped quad did
        from += size;
        size = -size;
    }
    const float start = SnapWhole(from * scale), length = size * scale;
    *a = start;
    if (length < 3.f) *b = start + (length < 1.f ? 1.f : SnapWhole(length));
    else *b = SnapWhole((from + size) * scale);
}

void FillRect(float x, float y, float w, float h, u32 argb) {
    if (w == 0.f || h == 0.f) return;  // an empty rectangle covered no pixel before either
    typedef HRESULT(WINAPI * SetTexFn)(void*, DWORD, void*);
    Method<SetTexFn>(g_dev, kSetTexture)(g_dev, 0, nullptr);
    SetTSS(TSS_COLOROP, TOP_SELECTARG1);
    SetTSS(TSS_COLORARG1, TA_DIFFUSE);
    SetTSS(TSS_ALPHAOP, TOP_SELECTARG1);
    SetTSS(TSS_ALPHAARG1, TA_DIFFUSE);
    float x0, x1, y0, y1;
    SnapSpan(x, w, ScaleX(), &x0, &x1);
    SnapSpan(y, h, ScaleY(), &y0, &y1);
    const float o = FadeOffsetPixels();
    Quad(x0, y0 + o, x1, y1 + o, Faded(argb), 0.f, 0.f, 1.f, 1.f);
}

void SetOverlayFade(float alpha, float offsetY) {
    g_fadeAlpha = alpha < 0.f ? 0.f : alpha > 1.f ? 1.f : alpha;
    g_fadeOffset = offsetY;
}

// ---- Rounded shapes ---------------------------------------------------------------------------------------------
// One white disk whose alpha is its coverage. A rounded rectangle is nine quads cut from it: the four corners take the four
// quarters of the disk, the edges and the middle its solid centre, so one texture serves every radius and the straight edges
// stay on whole pixels. The disk carries its own smaller levels, each drawn at its own size: a corner of 9 pixels samples a
// quarter of 32 texels, and with the big level alone the bilinear sampler would skip texels and the curve would step.
// MANAGED like the text textures, so a Reset does not lose it; it is still let go before one with the other overlay
// resources and made again at the next use.
namespace {
constexpr int kDiskSize = 64, kDiskLevels = 7;  // 64, 32, 16, 8, 4, 2, 1
void* g_disk = nullptr;
bool g_diskFailed = false;  // CreateTexture or LockRect failed: the shapes stay square for the session

float Sqrt(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }

bool MakeDisk() {
    typedef HRESULT(WINAPI * CreateTextureFn)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**, void*);
    typedef HRESULT(WINAPI * LockFn)(void*, UINT, LockedRect*, const RECT*, DWORD);
    typedef HRESULT(WINAPI * UnlockFn)(void*, UINT);
    void* tex = nullptr;
    HRESULT hr = Method<CreateTextureFn>(g_dev, kCreateTexture)(g_dev, kDiskSize, kDiskSize, kDiskLevels, 0, 21 /*A8R8G8B8*/,
                                                                 1 /*MANAGED*/, &tex, nullptr);
    if (FAILED(hr) || !tex) {
        Log("overlay: rounded shapes unavailable (CreateTexture %08X), square fallback", (u32)hr);
        return false;
    }
    for (int level = 0; level < kDiskLevels; ++level) {
        const int size = kDiskSize >> level;
        const float radius = (float)size * 0.5f;
        LockedRect lr;
        if (FAILED(Method<LockFn>(tex, kTexLockRect)(tex, (UINT)level, &lr, nullptr, 0))) {
            Method<ReleaseFn>(tex, kRelease)(tex);
            Log("overlay: rounded shapes unavailable (LockRect of level %d), square fallback", level);
            return false;
        }
        for (int y = 0; y < size; ++y) {
            u32* row = (u32*)((u8*)lr.bits + (size_t)y * lr.pitch);
            for (int x = 0; x < size; ++x) {
                const float dx = (float)x + 0.5f - radius, dy = (float)y + 0.5f - radius;
                float a = radius + 0.5f - Sqrt(dx * dx + dy * dy);  // the share of the texel inside the circle, to a good guess
                if (a < 0.f) a = 0.f;
                if (a > 1.f) a = 1.f;
                row[x] = ((u32)(a * 255.f + 0.5f) << 24) | 0x00FFFFFF;
            }
        }
        Method<UnlockFn>(tex, kTexUnlockRect)(tex, (UINT)level);
    }
    g_disk = tex;
    static bool logged = false;
    if (!logged) {
        logged = true;
        Log("overlay: rounded shapes ready (%dx%d disk, %d levels)", kDiskSize, kDiskSize, kDiskLevels);
    }
    return true;
}

void ReleaseDisk() {
    if (g_disk) Method<ReleaseFn>(g_disk, kRelease)(g_disk);
    g_disk = nullptr;
}

// Binds the disk with the colour from the vertex and alpha = coverage x vertex alpha (as DrawText), and trilinear filtering
// so the small corners take the small levels. False = no disk, draw square.
bool BeginRound() {
    if (!g_disk) {
        if (g_diskFailed) return false;
        if (!MakeDisk()) {
            g_diskFailed = true;
            return false;
        }
    }
    typedef HRESULT(WINAPI * SetTexFn)(void*, DWORD, void*);
    Method<SetTexFn>(g_dev, kSetTexture)(g_dev, 0, g_disk);
    SetTSS(TSS_COLOROP, TOP_SELECTARG2);
    SetTSS(TSS_COLORARG2, TA_DIFFUSE);
    SetTSS(TSS_ALPHAOP, TOP_MODULATE);
    SetTSS(TSS_ALPHAARG1, TA_TEXTURE);
    SetTSS(TSS_ALPHAARG2, TA_DIFFUSE);
    SetSampler(SAMP_MIPFILTER, 2);     // LINEAR between the levels
    SetSampler(SAMP_MIPMAPLODBIAS, 0);  // the game's own bias or level limit must not blur or sharpen the corners
    SetSampler(SAMP_MAXMIPLEVEL, 0);
    return true;
}

// The overlay's sampler as BeginOverlay set it (the text textures have one level; the state block restores the rest).
void EndRound() { SetSampler(SAMP_MIPFILTER, 0); }

// A rounded rectangle in back buffer pixels, x0..x1 and y0..y1 on whole pixels, corner radius r in pixels (any fraction).
void RoundPixels(float x0, float y0, float x1, float y1, float r, u32 argb) {
    if (x1 <= x0 || y1 <= y0) return;
    const float o = FadeOffsetPixels();  // the menu's fade-in (SetOverlayFade), once per shape
    y0 += o;
    y1 += o;
    argb = Faded(argb);
    const float half = (x1 - x0 < y1 - y0 ? x1 - x0 : y1 - y0) * 0.5f;
    if (r > half) r = half;
    if (r < 0.f) r = 0.f;
    // The columns and rows of the nine parts. A pill has no straight middle: its two inner lines are the same line, and the
    // empty parts are left out (two triangles sharing an edge never leave a gap or cover a pixel twice).
    float xs[4] = {x0, x0 + r, x1 - r, x1}, ys[4] = {y0, y0 + r, y1 - r, y1};
    if (xs[2] < xs[1]) xs[1] = xs[2] = (x0 + x1) * 0.5f;
    if (ys[2] < ys[1]) ys[1] = ys[2] = (y0 + y1) * 0.5f;
    const float uv[4] = {0.f, 0.5f, 0.5f, 1.f};  // the middle parts sample the solid centre of the disk
    Vertex v[54];
    UINT count = 0;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            if (xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j]) continue;
            const float a = xs[i] - 0.5f, b = xs[i + 1] - 0.5f, c = ys[j] - 0.5f, d = ys[j + 1] - 0.5f;  // as in Quad
            const float u0 = uv[i], u1 = uv[i + 1], v0 = uv[j], v1 = uv[j + 1];
            Vertex* q = &v[count];
            q[0] = {a, c, 0.f, 1.f, argb, u0, v0};
            q[1] = {b, c, 0.f, 1.f, argb, u1, v0};
            q[2] = {a, d, 0.f, 1.f, argb, u0, v1};
            q[3] = {b, c, 0.f, 1.f, argb, u1, v0};
            q[4] = {b, d, 0.f, 1.f, argb, u1, v1};
            q[5] = {a, d, 0.f, 1.f, argb, u0, v1};
            count += 6;
        }
    if (!count) return;
    typedef HRESULT(WINAPI * DrawUpFn)(void*, DWORD, UINT, const void*, UINT);
    Method<DrawUpFn>(g_dev, kDrawPrimitiveUP)(g_dev, 4 /*TRIANGLELIST*/, count / 3, v, sizeof(Vertex));
}

// A border of `units` layout units in whole pixels, at least one.
float BorderPixels(float units) {
    const float p = SnapWhole(units * ScaleY());
    return p < 1.f ? 1.f : p;
}
}  // namespace

void FillRound(float x, float y, float w, float h, float r, u32 argb) {
    if (w == 0.f || h == 0.f) return;
    if (r <= 0.f || !BeginRound()) {
        FillRect(x, y, w, h, argb);
        return;
    }
    float x0, x1, y0, y1;
    SnapSpan(x, w, ScaleX(), &x0, &x1);
    SnapSpan(y, h, ScaleY(), &y0, &y1);
    RoundPixels(x0, y0, x1, y1, r * ScaleY(), argb);
    EndRound();
}

void FillRoundFramed(float x, float y, float w, float h, float r, u32 fill, u32 frame, float frameWidth) {
    if (w == 0.f || h == 0.f) return;
    if (r <= 0.f || !BeginRound()) {  // square: the frame as a rectangle under the inside
        FillRect(x, y, w, h, frame);
        const float b = BorderPixels(frameWidth) / ScaleX();
        FillRect(x + b, y + b, w - 2 * b, h - 2 * b, fill);
        return;
    }
    float x0, x1, y0, y1;
    SnapSpan(x, w, ScaleX(), &x0, &x1);
    SnapSpan(y, h, ScaleY(), &y0, &y1);
    const float rp = r * ScaleY(), b = BorderPixels(frameWidth);
    RoundPixels(x0, y0, x1, y1, rp, frame);
    RoundPixels(x0 + b, y0 + b, x1 - b, y1 - b, rp > b ? rp - b : 0.f, fill);
    EndRound();
}

// A picture as a textured quad, on whole pixels like FillRect. With a radius its corners are rounded by the disk of the
// rounded shapes in the second texture stage (its coverage multiplies the picture's alpha), so the corners are anti-aliased
// and show whatever lies under them, a panel of any opacity too. Everything this changes is put back for the primitives
// above (the vertex format, stage 1, the mip filter, the bound textures); the rest the overlay's state block restores.
namespace {
struct ImageVertex {
    float x, y, z, rhw;
    u32 color;
    float u, v;    // the picture
    float u2, v2;  // the disk
};
void SetStage(DWORD stage, u32 type, u32 value) { Method<Dword3Fn>(g_dev, kSetTextureStageState)(g_dev, stage, type, value); }
void SetStageSampler(DWORD stage, u32 type, u32 value) { Method<Dword3Fn>(g_dev, kSetSamplerState)(g_dev, stage, type, value); }
}  // namespace

void DrawImage(void* texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 tint, float radius) {
    if (!texture || w == 0.f || h == 0.f) return;
    typedef HRESULT(WINAPI * SetTexFn)(void*, DWORD, void*);
    typedef HRESULT(WINAPI * SetFvfFn)(void*, DWORD);
    typedef HRESULT(WINAPI * DrawUpFn)(void*, DWORD, UINT, const void*, UINT);
    float x0, x1, y0, y1;
    SnapSpan(x, w, ScaleX(), &x0, &x1);
    SnapSpan(y, h, ScaleY(), &y0, &y1);
    const float o = FadeOffsetPixels();
    y0 += o;
    y1 += o;
    tint = Faded(tint);
    bool round = radius > 0.f;
    if (round && !g_disk && (g_diskFailed || !MakeDisk())) {
        g_diskFailed = true;
        round = false;
    }
    Method<SetTexFn>(g_dev, kSetTexture)(g_dev, 0, texture);
    SetTSS(TSS_COLOROP, TOP_MODULATE);  // the picture's colour and alpha times the tint
    SetTSS(TSS_COLORARG1, TA_TEXTURE);
    SetTSS(TSS_COLORARG2, TA_DIFFUSE);
    SetTSS(TSS_ALPHAOP, TOP_MODULATE);
    SetTSS(TSS_ALPHAARG1, TA_TEXTURE);
    SetTSS(TSS_ALPHAARG2, TA_DIFFUSE);
    SetSampler(SAMP_MIPFILTER, 2);  // a cover drawn as a thumbnail takes its small levels
    SetSampler(SAMP_MIPMAPLODBIAS, 0);
    SetSampler(SAMP_MAXMIPLEVEL, 0);
    if (!round) {
        Quad(x0, y0, x1, y1, tint, u0, v0, u1, v1);
    } else {
        float r = SnapWhole(radius * ScaleY());
        const float half = (x1 - x0 < y1 - y0 ? x1 - x0 : y1 - y0) * 0.5f;
        if (r > half) r = half;
        const float xs[4] = {x0, x0 + r, x1 - r, x1}, ys[4] = {y0, y0 + r, y1 - r, y1};
        const float disk[4] = {0.f, 0.5f, 0.5f, 1.f};
        float us[4], vs[4];
        for (int i = 0; i < 4; ++i) {
            us[i] = u0 + (u1 - u0) * (xs[i] - x0) / (x1 - x0);
            vs[i] = v0 + (v1 - v0) * (ys[i] - y0) / (y1 - y0);
        }
        ImageVertex v[54];
        UINT count = 0;
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                if (xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j]) continue;
                const float a = xs[i] - 0.5f, b = xs[i + 1] - 0.5f, c = ys[j] - 0.5f, d = ys[j + 1] - 0.5f;  // as in Quad
                ImageVertex* q = &v[count];
                q[0] = {a, c, 0.f, 1.f, tint, us[i], vs[j], disk[i], disk[j]};
                q[1] = {b, c, 0.f, 1.f, tint, us[i + 1], vs[j], disk[i + 1], disk[j]};
                q[2] = {a, d, 0.f, 1.f, tint, us[i], vs[j + 1], disk[i], disk[j + 1]};
                q[3] = q[1];
                q[4] = {b, d, 0.f, 1.f, tint, us[i + 1], vs[j + 1], disk[i + 1], disk[j + 1]};
                q[5] = q[2];
                count += 6;
            }
        // stage 1: the colour as it is, the alpha times the disk's coverage; stage 2 ends the cascade
        Method<SetTexFn>(g_dev, kSetTexture)(g_dev, 1, g_disk);
        SetStage(1, TSS_COLOROP, TOP_SELECTARG2);
        SetStage(1, TSS_COLORARG2, TA_CURRENT);
        SetStage(1, TSS_ALPHAOP, TOP_MODULATE);
        SetStage(1, TSS_ALPHAARG1, TA_TEXTURE);
        SetStage(1, TSS_ALPHAARG2, TA_CURRENT);
        SetStage(1, TSS_TEXCOORDINDEX, 1);
        SetStage(1, TSS_TEXTURETRANSFORMFLAGS, 0);
        SetStage(2, TSS_COLOROP, TOP_DISABLE);
        SetStage(2, TSS_ALPHAOP, TOP_DISABLE);
        SetStageSampler(1, SAMP_MINFILTER, 2);
        SetStageSampler(1, SAMP_MAGFILTER, 2);
        SetStageSampler(1, SAMP_MIPFILTER, 2);
        SetStageSampler(1, SAMP_ADDRESSU, 3);
        SetStageSampler(1, SAMP_ADDRESSV, 3);
        SetStageSampler(1, SAMP_MIPMAPLODBIAS, 0);
        SetStageSampler(1, SAMP_MAXMIPLEVEL, 0);
        SetStageSampler(1, SAMP_SRGBTEXTURE, 0);
        Method<SetFvfFn>(g_dev, kSetFVF)(g_dev, kFvfXyzrhwDiffuseTex2);
        if (count) Method<DrawUpFn>(g_dev, kDrawPrimitiveUP)(g_dev, 4 /*TRIANGLELIST*/, count / 3, v, sizeof(ImageVertex));
        Method<SetFvfFn>(g_dev, kSetFVF)(g_dev, kFvfXyzrhwDiffuseTex1);
        SetStage(1, TSS_COLOROP, TOP_DISABLE);
        SetStage(1, TSS_ALPHAOP, TOP_DISABLE);
        Method<SetTexFn>(g_dev, kSetTexture)(g_dev, 1, nullptr);
    }
    Method<SetTexFn>(g_dev, kSetTexture)(g_dev, 0, nullptr);
    SetSampler(SAMP_MIPFILTER, 0);
}

namespace {
// a + (b - a) * t for every channel of two colours, t 0..1
u32 MixColor(u32 a, u32 b, float t) {
    if (t <= 0.f) return a;
    if (t >= 1.f) return b;
    u32 out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float ca = (float)((a >> shift) & 0xFF), cb = (float)((b >> shift) & 0xFF);
        out |= (u32)(int)(ca + (cb - ca) * t + 0.5f) << shift;
    }
    return out;
}
}  // namespace

void Switch(float x, float y, bool on, float travel) {
    float x0, x1, y0, y1;
    SnapSpan(x, kSwitchW, ScaleX(), &x0, &x1);
    SnapSpan(y, kSwitchH, ScaleY(), &y0, &y1);
    // the knob is inset 3 units (18 of 24 at 1x, as the site's switch), in whole pixels on every side
    const float inset = SnapWhole(3.f * ScaleY()), knob = (y1 - y0) - 2.f * inset;
    // Where the knob is between its two rests (0 = off, 1 = on): `travel` is how far it has come from the other side since
    // the flip. The knob stays on whole pixels and the track colour follows it from grey to red.
    if (travel < 0.f) travel = 0.f;
    if (travel > 1.f) travel = 1.f;
    const float at = on ? travel : 1.f - travel;
    const float offX = x0 + inset, onX = x1 - inset - knob;
    const float kx = SnapWhole(offX + (onX - offX) * at);
    const u32 track = MixColor(theme::kLine2, theme::kAccent, at);
    if (!BeginRound()) {  // square, as the switch was before
        FillRect(x, y, kSwitchW, kSwitchH, track);
        FillRect(kx / ScaleX(), (y0 + inset) / ScaleY(), knob / ScaleX(), knob / ScaleY(), theme::kWhite);
        return;
    }
    RoundPixels(x0, y0, x1, y1, (y1 - y0) * 0.5f, track);
    RoundPixels(kx, y0 + inset, kx + knob, y0 + inset + knob, knob * 0.5f, theme::kWhite);
    EndRound();
}

float TextWidth(const wchar_t* text, int px, int weight) {
    TextEntry* e = GetText(text, FontPx(px), weight);
    return e ? (float)(e->width - 4) / ScaleX() : 0.f;
}

// Measured with GDI only (no texture is made), so a long text can be tried at many break points cheaply.
int TextFit(const wchar_t* text, int length, float maxWidth, int px, int weight) {
    if (length <= 0) return 0;
    HDC dc = TextDc();
    HFONT font = dc ? FontFor(FontPx(px), weight) : nullptr;
    if (!font) return length;
    HGDIOBJ old = SelectObject(dc, font);
    int fit = 0;
    SIZE size;
    int room = (int)(maxWidth * ScaleX());
    if (!GetTextExtentExPointW(dc, text, length, room > 0 ? room : 0, &fit, nullptr, &size)) fit = length;
    SelectObject(dc, old);
    return fit;
}

float DrawText(const wchar_t* text, float x, float y, u32 argb, int px, int weight) {
    TextEntry* e = GetText(text, FontPx(px), weight);  // render the font at the real size
    if (!e) return 0.f;
    typedef HRESULT(WINAPI * SetTexFn)(void*, DWORD, void*);
    Method<SetTexFn>(g_dev, kSetTexture)(g_dev, 0, e->texture);
    SetTSS(TSS_COLOROP, TOP_SELECTARG2);  // colour from the vertex
    SetTSS(TSS_COLORARG2, TA_DIFFUSE);
    SetTSS(TSS_ALPHAOP, TOP_MODULATE);  // coverage from the texture times the vertex alpha
    SetTSS(TSS_ALPHAARG1, TA_TEXTURE);
    SetTSS(TSS_ALPHAARG2, TA_DIFFUSE);
    // On whole pixels: each texel of the GDI bitmap lands on exactly one pixel. A fractional start (every odd layout unit at
    // scale 1.5, any centred label, a panel dragged by the mouse) made the bilinear sampler smear the glyphs over two pixels.
    float px0 = SnapWhole(x * ScaleX()) - 2.f, py0 = SnapWhole(y * ScaleY()) - 2.f + FadeOffsetPixels();
    Quad(px0, py0, px0 + (float)e->width, py0 + (float)e->height, Faded(argb), 0.f, 0.f, 1.f, 1.f);
    return (float)(e->width - 4) / ScaleX();
}


// ---- Final-image passes -----------------------------------------------------------------------------------------
namespace {

void* g_sharpShader = nullptr;   // IDirect3DPixelShader9
void* g_fxaaShader = nullptr;
bool g_fxaaFailed = false;
void* g_copyTex = nullptr;       // IDirect3DTexture9, render target, same size and format as the back buffer
void* g_copySurf = nullptr;
UINT g_copyW = 0, g_copyH = 0, g_copyFmt = 0;
bool g_sharpFailed = false;
void* g_rcasShader = nullptr;   // SharpenMode=1
bool g_rcasFailed = false;

const char kSharpenSource[] =
    "sampler s0 : register(s0);\n"
    "float4 c0 : register(c0);\n"  // xy = texel size, z = amount
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float3 c = tex2D(s0, uv).rgb;\n"
    "  float3 n = tex2D(s0, uv + float2(0.0, -c0.y)).rgb;\n"
    "  float3 s = tex2D(s0, uv + float2(0.0, c0.y)).rgb;\n"
    "  float3 w = tex2D(s0, uv + float2(-c0.x, 0.0)).rgb;\n"
    "  float3 e = tex2D(s0, uv + float2(c0.x, 0.0)).rgb;\n"
    "  float3 avg = (n + s + w + e) * 0.25;\n"
    "  return float4(saturate(c + (c - avg) * c0.z), 1.0);\n"
    "}\n";

// Contrast-adaptive sharpening ([Graphics] SharpenMode=1): our own HLSL of the RCAS formula published by AMD in
// FidelityFX FSR 1 (ffx_fsr1.h, FsrRcasF, MIT). The unsharp mask above pushes every edge by the same amount, so a dark
// branch against the sky gets a bright rim that clips to white. RCAS picks one negative lobe weight for the 4-neighbour
// cross: the largest that, from the ring's min and max, can never push any channel out of 0..1, capped at -0.1875
// (AMD's FSR_RCAS_LIMIT) and scaled by c1.x. A contrasty edge, or one near black or white, leaves little room, so it gets
// a small push and no clipped rim; soft low-contrast detail gets the full strength. The denoise term lowers the lobe by
// up to half where the centre stands alone against its ring (grain, dither, single bright pixels), so noise is not
// sharpened into speckles.
// The 1/256 terms are ours: AMD divides by 4*max (0 on pure black, e.g. letterbox bars) and by 4*min-4 (0 on pure
// white), and a division by zero there would give NaN pixels. Both ratios are 0 in those cases anyway.
const char kRcasSource[] =
    "sampler s0 : register(s0);\n"
    "float4 c0 : register(c0);\n"  // xy = texel size
    "float4 c1 : register(c1);\n"  // x = strength: 1 at 100 %, half of it for every 50 points less
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float3 b = tex2D(s0, uv + float2(0.0, -c0.y)).rgb;\n"
    "  float3 d = tex2D(s0, uv + float2(-c0.x, 0.0)).rgb;\n"
    "  float3 e = tex2D(s0, uv).rgb;\n"
    "  float3 f = tex2D(s0, uv + float2(c0.x, 0.0)).rgb;\n"
    "  float3 h = tex2D(s0, uv + float2(0.0, c0.y)).rgb;\n"
    "  float3 mn4 = min(min(b, d), min(f, h));\n"
    "  float3 mx4 = max(max(b, d), max(f, h));\n"
    "  float3 hitMin = min(mn4, e) / (4.0 * mx4 + 0.00390625);\n"
    "  float3 hitMax = (1.0 - max(mx4, e)) / (4.0 * mn4 - 4.00390625);\n"
    "  float3 l = max(-hitMin, hitMax);\n"
    "  float lobe = max(-0.1875, min(max(max(l.r, l.g), l.b), 0.0)) * c1.x;\n"
    "  float3 w = float3(0.5, 1.0, 0.5);\n"  // luma times 2, as AMD's
    "  float bL = dot(b, w), dL = dot(d, w), eL = dot(e, w), fL = dot(f, w), hL = dot(h, w);\n"
    "  float range = max(max(max(bL, dL), max(fL, hL)), eL) - min(min(min(bL, dL), min(fL, hL)), eL);\n"
    "  float nz = saturate(abs(0.25 * (bL + dL + fL + hL) - eL) / max(range, 0.00390625));\n"
    "  lobe *= 1.0 - 0.5 * nz;\n"
    "  return float4((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0), 1.0);\n"
    "}\n";

// Edge-directional smoothing in the spirit of FXAA (our own implementation of the well-known idea): the blur direction
// follows the luma gradient of the 2x2 neighbourhood, and the result is kept only if it stays inside the local range.
const char kFxaaSource[] =
    "sampler s0 : register(s0);\n"
    "float4 c0 : register(c0);\n"  // xy = texel size, z = strength
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float2 t = c0.xy;\n"
    "  float3 lw = float3(0.299, 0.587, 0.114);\n"
    "  float3 rM = tex2D(s0, uv).rgb;\n"
    "  float lNW = dot(tex2D(s0, uv + float2(-t.x, -t.y)).rgb, lw);\n"
    "  float lNE = dot(tex2D(s0, uv + float2(t.x, -t.y)).rgb, lw);\n"
    "  float lSW = dot(tex2D(s0, uv + float2(-t.x, t.y)).rgb, lw);\n"
    "  float lSE = dot(tex2D(s0, uv + float2(t.x, t.y)).rgb, lw);\n"
    "  float lM = dot(rM, lw);\n"
    "  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n"
    "  float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n"
    "  float2 dir;\n"
    "  dir.x = -((lNW + lNE) - (lSW + lSE));\n"
    "  dir.y = ((lNW + lSW) - (lNE + lSE));\n"
    "  float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);\n"
    "  dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -8.0, 8.0) * t;\n"
    "  float3 rA = 0.5 * (tex2D(s0, uv + dir * -0.16667).rgb + tex2D(s0, uv + dir * 0.16667).rgb);\n"
    "  float3 rB = rA * 0.5 + 0.25 * (tex2D(s0, uv + dir * -0.5).rgb + tex2D(s0, uv + dir * 0.5).rgb);\n"
    "  float lB = dot(rB, lw);\n"
    "  float3 res = (lB < lMin || lB > lMax) ? rA : rB;\n"
    "  return float4(lerp(rM, res, c0.z), 1.0);\n"
    "}\n";

typedef HRESULT(WINAPI* CompileFn)(const char*, UINT, const void*, void*, const char*, const char*, DWORD, void**, void**, void**);
typedef void*(WINAPI* BufPtrFn)(void*);
typedef HRESULT(WINAPI* GetBbFn2)(void*, UINT, UINT, UINT, void**);
typedef HRESULT(WINAPI* GetDescFn)(void*, u32*);
typedef HRESULT(WINAPI* CreateTexFn)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**, void*);
typedef HRESULT(WINAPI* GetLevelFn)(void*, UINT, void**);
typedef HRESULT(WINAPI* StretchFn)(void*, void*, const RECT*, void*, const RECT*, DWORD);
typedef HRESULT(WINAPI* CreatePsFn)(void*, const DWORD*, void**);
typedef HRESULT(WINAPI* SetPsFn)(void*, void*);
typedef HRESULT(WINAPI* SetTexFn2)(void*, DWORD, void*);
typedef HRESULT(WINAPI* SetPsConstFn)(void*, UINT, const float*, UINT);

// With `logName` the compiler's message for a profile that failed is written to the log (it is plain text). The time the
// render thread waited (every profile tried, the shader object made) goes to the log under `timeName`, or `logName`.
bool BuildShader(void* dev, const char* source, size_t length, void** shader, const char* const* profiles, const char* logName = nullptr,
                 const char* timeName = nullptr) {
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    if (!d3dx) d3dx = LoadLibraryW(L"d3dx9_43.dll");
    CompileFn compile = d3dx ? (CompileFn)(void*)GetProcAddress(d3dx, "D3DXCompileShader") : nullptr;
    if (!compile) return false;
    const char* name = timeName ? timeName : logName ? logName : "(unnamed)";
    const u64 started = TickNow();
    for (; *profiles; ++profiles) {
        void *code = nullptr, *errors = nullptr, *table = nullptr;
        HRESULT hr = compile(source, (UINT)length, nullptr, nullptr, "main", *profiles, 0, &code, &errors, &table);
        if (errors && logName && FAILED(hr)) Log("%s: %s: %.400s", logName, *profiles, (const char*)Method<BufPtrFn>(errors, 3)(errors));
        if (errors) Method<ReleaseFn>(errors, kRelease)(errors);
        if (table) Method<ReleaseFn>(table, kRelease)(table);
        if (FAILED(hr) || !code) continue;
        const DWORD* bytes = (const DWORD*)Method<BufPtrFn>(code, 3)(code);
        hr = Method<CreatePsFn>(dev, 106)(dev, bytes, shader);
        Method<ReleaseFn>(code, kRelease)(code);
        if (SUCCEEDED(hr) && *shader) {
            LogShaderTime(name, *profiles, started);
            return true;
        }
    }
    LogShaderTime(name, nullptr, started);
    return false;
}

void ReleaseCopy() {
    if (g_copySurf) Method<ReleaseFn>(g_copySurf, kRelease)(g_copySurf);
    if (g_copyTex) Method<ReleaseFn>(g_copyTex, kRelease)(g_copyTex);
    g_copySurf = g_copyTex = nullptr;
}

// Gets the back buffer and its description (width, height, format). The caller releases the surface.
void* BackBufferInfo(void* dev, UINT* w, UINT* h, UINT* fmt) {
    void* back = nullptr;
    if (FAILED(Method<GetBbFn2>(dev, kGetBackBuffer)(dev, 0, 0, 0, &back)) || !back) return nullptr;
    u32 d[8] = {};  // Format, Type, Usage, Pool, MultiSampleType, MultiSampleQuality, Width, Height
    Method<GetDescFn>(back, 12)(back, d);
    *fmt = d[0];
    *w = d[6];
    *h = d[7];
    return back;
}

}  // namespace

// ---- Performance statistics: FPS, frame time, how long Present waits (a GPU-bound game waits there) and the GPU time of our
// own passes, measured with D3D9 timestamp queries. Switched on with [Debug] PerfOverlay or the F10 option.
namespace {

struct GpuTimer {
    void* disjoint[3];
    void* t0[3];
    void* t1[3];
    bool pending[3];
    int slot;
    bool failed;
    double ms;  // the last finished measurement
};
// Zero-initialised aggregates: no constructor, so nothing has to run before DllEntry (this DLL has no C runtime to run one).
// g_timerUnder: the passes that ran after the game's composite (FiltersUnderHud); the panels show it added to g_timerPost.
GpuTimer g_timerAo = {}, g_timerPost = {}, g_timerUnder = {};
bool g_underTimed = false;  // g_timerUnder was issued since the last Present (otherwise its last value is dropped there)
unsigned long long g_gpuFreq = 0;

typedef HRESULT(WINAPI* CreateQueryFn)(void*, DWORD, void**);
typedef HRESULT(WINAPI* IssueFn)(void*, DWORD);
typedef HRESULT(WINAPI* GetDataFn)(void*, void*, DWORD, DWORD);

bool TimerCreate(void* dev, GpuTimer& t) {
    for (int i = 0; i < 3; ++i)
        if (FAILED(Method<CreateQueryFn>(dev, 118)(dev, 11 /*TIMESTAMPDISJOINT*/, &t.disjoint[i])) || !t.disjoint[i] ||
            FAILED(Method<CreateQueryFn>(dev, 118)(dev, 10 /*TIMESTAMP*/, &t.t0[i])) || !t.t0[i] ||
            FAILED(Method<CreateQueryFn>(dev, 118)(dev, 10, &t.t1[i])) || !t.t1[i])
            return false;
    return true;
}

void TimerBegin(void* dev, GpuTimer& t) {
    if (t.failed) return;
    if (!t.disjoint[0] && !TimerCreate(dev, t)) { t.failed = true; return; }
    Method<IssueFn>(t.disjoint[t.slot], 6)(t.disjoint[t.slot], 2 /*BEGIN*/);
    Method<IssueFn>(t.t0[t.slot], 6)(t.t0[t.slot], 1 /*END*/);
}

void TimerEnd(void* dev, GpuTimer& t) {
    (void)dev;
    if (t.failed || !t.disjoint[0]) return;
    Method<IssueFn>(t.t1[t.slot], 6)(t.t1[t.slot], 1);
    Method<IssueFn>(t.disjoint[t.slot], 6)(t.disjoint[t.slot], 1);
    t.pending[t.slot] = true;
    t.slot = (t.slot + 1) % 3;
    // read the oldest finished slot without waiting
    for (int k = 0; k < 3; ++k) {
        int i = (t.slot + k) % 3;
        if (!t.pending[i] || i == (t.slot + 2) % 3) continue;  // not the one just issued
        struct { u32 disjointFlag; } d = {0};
        unsigned long long a = 0, b = 0;
        if (Method<GetDataFn>(t.disjoint[i], 7)(t.disjoint[i], &d, 4, 0) != 0) continue;
        if (Method<GetDataFn>(t.t0[i], 7)(t.t0[i], &a, 8, 0) != 0 || Method<GetDataFn>(t.t1[i], 7)(t.t1[i], &b, 8, 0) != 0) continue;
        t.pending[i] = false;
        if (!d.disjointFlag && g_gpuFreq && b >= a) t.ms = (double)(b - a) * 1000.0 / (double)g_gpuFreq;
    }
}

void EnsureFreq(void* dev) {
    static void* freqQuery = nullptr;
    static bool tried = false;
    if (g_gpuFreq || tried) return;
    tried = true;
    if (FAILED(Method<CreateQueryFn>(dev, 118)(dev, 12 /*TIMESTAMPFREQ*/, &freqQuery)) || !freqQuery) return;
    Method<IssueFn>(freqQuery, 6)(freqQuery, 1);
    unsigned long long f = 0;
    for (int i = 0; i < 1000; ++i)
        if (Method<GetDataFn>(freqQuery, 7)(freqQuery, &f, 8, 1) == 0) { g_gpuFreq = f; break; }
}

}  // namespace

void PerfPostBegin(void* dev) {
    if (!g_cfg.perfOverlay && !g_cfg.frameStats) return;
    EnsureFreq(dev);
    TimerBegin(dev, g_timerPost);
}
void PerfPostEnd(void* dev) {
    if (!g_cfg.perfOverlay && !g_cfg.frameStats) return;
    TimerEnd(dev, g_timerPost);
    if (!g_underTimed) g_timerUnder.ms = 0;  // a frame with everything at Present: no old time of the composite side in the sum
    g_underTimed = false;
}

namespace {
// The box of both performance panels: the start card's rounded shape in the site's colours instead of the old square black.
// It stays see-through, as the old one was, since it sits over the game all the time. FillRoundFramed lays the inside over
// the frame's shape, so the frame's alpha adds under the inside too: 0x60 for a hairline that shows over a dark scene, 0xB0
// for the inside, together a little more cover than the old box's 0.69 (1 - (1 - 0x60/255) * (1 - 0xB0/255), about 0.81).
void PerfPanelBox(float x, float y, float w, float h) {
    FillRoundFramed(x, y, w, h, 8.f, theme::Alpha(theme::kBg, 0xB0), theme::Alpha(theme::kLine2, 0x60));
}
}  // namespace

void PerfOverlayFrame(void* dev, double fps, double frameMs, double presentMs, double physicsMs) {
    if (!g_cfg.perfOverlay) return;
    static u64 lastUpdate = 0;
    // line2 was one line of about 75 characters, wider than its 520-unit box in Ukrainian; now it is two short lines (the CPU
    // side, then the mod's GPU passes) in a box as wide as its text.
    static wchar_t line1[96] = L"", line2[96] = L"", line3[96] = L"";
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (!lastUpdate || (u64)(now.QuadPart - (long long)lastUpdate) > (u64)freq.QuadPart / 2) {  // the text changes twice a second
        lastUpdate = (u64)now.QuadPart;
        const int f10 = (int)(frameMs * 10.0) % 10, p10 = (int)(physicsMs * 10.0) % 10, w10 = (int)(presentMs * 10.0) % 10;
        const double post = g_timerPost.ms + g_timerUnder.ms;  // the final-image passes at Present and under the interface
        const int a10 = (int)(g_timerAo.ms * 10.0) % 10, g10 = (int)(post * 10.0) % 10;
        static int logTick = 0;
        if (++logTick % 10 == 0 || g_cfg.language == 1) {  // the log is always in English
            wsprintfW(line1, L"%d FPS   %d.%d ms", (int)(fps + 0.5), (int)frameMs, f10);
            wsprintfW(line2, L"physics %d.%d ms  Present wait %d.%d ms", (int)physicsMs, p10, (int)presentMs, w10);
            wsprintfW(line3, L"mod GPU: SSAO %d.%d, post %d.%d ms", (int)g_timerAo.ms, a10, (int)post, g10);
            if (logTick % 10 == 0) Log("perf: %S | %S  %S", line1, line2, line3);  // the same log line as before the split
        }
        if (g_cfg.language != 1) {
            wsprintfW(line1, L"%d FPS   %d,%d мс", (int)(fps + 0.5), (int)frameMs, f10);
            wsprintfW(line2, L"фізика %d,%d мс  очікування Present %d,%d мс", (int)physicsMs, p10, (int)presentMs, w10);
            wsprintfW(line3, L"GPU мода: SSAO %d,%d, пост %d,%d мс", (int)g_timerAo.ms, a10, (int)post, g10);
        }
    }
    if (!OverlayBegin(dev)) return;
    // The box follows its widest line but only grows (in 8-unit steps), so it does not twitch twice a second as the digits
    // change; a switch of the language starts it over.
    static float boxW = 0.f;
    static int boxLang = -1;
    if (boxLang != g_cfg.language) {
        boxLang = g_cfg.language;
        boxW = 0.f;
    }
    float need = TextWidth(line1, 16);
    const float w2 = TextWidth(line2, 13), w3 = TextWidth(line3, 13);
    if (w2 > need) need = w2;
    if (w3 > need) need = w3;
    need = (float)(((int)need + 24 + 7) / 8 * 8);
    if (need > boxW) boxW = need;
    PerfPanelBox(8.f, 8.f, boxW, 68.f);
    DrawText(line1, 20, 14, theme::kWhite, 16);
    DrawText(line2, 20, 35, theme::kMuted, 13);
    DrawText(line3, 20, 52, theme::kMuted, 13);
    OverlayEnd();
}

namespace {
double g_frameHist[120];
int g_frameHistPos = 0;

// "24,92" like the Windows performance overlay.
void FmtMs(wchar_t* out, double v) {
    if (v < 0.0) v = 0.0;
    int whole = (int)v, frac = (int)((v - whole) * 100.0 + 0.5);
    if (frac >= 100) { ++whole; frac = 0; }
    if (g_cfg.language == 1) wsprintfW(out, L"%d.%02d ms", whole, frac);
    else wsprintfW(out, L"%d,%02d мс", whole, frac);
}
}  // namespace

void FrameStatsPush(double frameMs) {
    g_frameHist[g_frameHistPos] = frameMs;
    g_frameHistPos = (g_frameHistPos + 1) % 120;
}

// A separate panel (top right): where the frame time goes. Total = from one Present to the next; Game = the CPU work of the game
// between two Presents (logic, physics and the draw calls it submits); Physics = the PhysX step inside it; Present = the time the
// call itself took (driver, waiting for the video card and for the screen refresh); mod GPU = our own passes on the video card;
// game thread working = the CPU time the game thread really used per frame (not waiting): about the whole frame = the processor is the
// limit; far below it = the thread waits (for the video card inside the game, for the screen).
void FrameStatsPanel(void* dev, double totalMs, double gameMs, double physicsMs, double presentMs, double worstMs, double busyMs) {
    if (!g_cfg.frameStats) return;
    static u64 lastUpdate = 0;
    static wchar_t t[7][32];
    static int hz = 0;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (!lastUpdate || (u64)(now.QuadPart - (long long)lastUpdate) > (u64)freq.QuadPart / 2) {
        lastUpdate = (u64)now.QuadPart;
        FmtMs(t[0], totalMs);
        FmtMs(t[1], gameMs);
        FmtMs(t[2], physicsMs);
        FmtMs(t[3], presentMs);
        FmtMs(t[4], worstMs);
        FmtMs(t[5], g_timerAo.ms + g_timerPost.ms + g_timerUnder.ms);
        FmtMs(t[6], busyMs);
        hz = DisplayRefreshHz();  // the monitor the game window is on, cached by the frame pacer
    }
    if (!OverlayBegin(dev)) return;
    const float w = 214.f, x = 1280.f - w - 8.f, y = 8.f;
    PerfPanelBox(x, y, w, 196.f);
    static const wchar_t* const namesUk[7] = {L"Всього:", L"Гра (CPU):", L"  з них фізика:", L"Present:", L"Найгірший:", L"Проходи мода:", L"Потік гри працює:"};
    static const wchar_t* const namesEn[7] = {L"Total:", L"Game (CPU):", L"  of it physics:", L"Present:", L"Worst:", L"Mod passes:", L"Game thread busy:"};
    const wchar_t* const* names = g_cfg.language == 1 ? namesEn : namesUk;
    for (int i = 0; i < 7; ++i) {
        DrawText(names[i], x + 8.f, y + 6.f + 17.f * (float)i, i == 2 ? theme::kFaint : theme::kText, 13);
        float tw = TextWidth(t[i], 13);
        DrawText(t[i], x + w - 8.f - tw, y + 6.f + 17.f * (float)i, i == 0 ? theme::kWhite : theme::kMuted, 13);
    }
    wchar_t foot[64];
    const bool en = g_cfg.language == 1;
    const wchar_t* api = g_usingDxvk ? L"DXVK" : g_d3dWrapper ? (en ? L"D3D9 (wrapper)" : L"D3D9 (обгортка)") : L"D3D9";
    wsprintfW(foot, en ? L"%d FPS   %d Hz   %s" : L"%d FPS   %d Гц   %s", totalMs > 0.0 ? (int)(1000.0 / totalMs + 0.5) : 0, hz, api);
    DrawText(foot, x + 8.f, y + 6.f + 17.f * 7.f, theme::kFaint, 12);
    // graph of the last 120 frames: the scale is 2 x 16.7 ms (60 frames/s) or the worst frame; a thin line marks 16.7 ms
    double scale = 33.4;
    for (int i = 0; i < 120; ++i)
        if (g_frameHist[i] * 1.05 > scale) scale = g_frameHist[i] * 1.05;
    const float gx = x + 8.f, gy = y + 186.f, gh = 38.f, bw = (w - 16.f) / 120.f;
    FillRound(gx, gy - gh, w - 16.f, gh, 3.f, theme::Alpha(theme::kBg, 0x80));
    for (int i = 0; i < 120; ++i) {
        double v = g_frameHist[(g_frameHistPos + i) % 120];
        float h = (float)(v / scale) * gh;
        if (h > gh) h = gh;
        if (h < 1.f) h = 1.f;
        FillRect(gx + (float)i * bw, gy - h, bw > 1.f ? bw - 0.4f : bw, h, v > totalMs * 1.6 + 2.0 ? theme::kAccentHi : theme::kMuted);
    }
    FillRect(gx, gy - (float)(16.7 / scale) * gh, w - 16.f, 1.f, theme::Alpha(theme::kText, 0x90));
    OverlayEnd();
}

// Moved here from menu.cpp so the mods page wraps its descriptions the same way; `weight` lets a heading wrap too.
float DrawFitted(const wchar_t* text, float x, float y, u32 color, int px, float maxW, int weight) {
    int len = lstrlenW(text);
    int n = TextFit(text, len, maxW, px, weight);
    if (n >= len && len < 256) return DrawText(text, x, y, color, px, weight);
    n = TextFit(text, len, maxW - TextWidth(L"…", px, weight), px, weight);
    if (n > 250) n = 250;
    while (n > 0 && text[n - 1] == L' ') --n;
    wchar_t cut[256];
    for (int i = 0; i < n; ++i) cut[i] = text[i];
    cut[n] = L'…';
    cut[n + 1] = 0;
    return DrawText(cut, x, y, color, px, weight);
}

int DrawLines(const wchar_t* text, float x, float y, u32 color, int px, float lineH, float maxW, int maxLines, bool draw, int weight) {
    int lines = 0;
    const wchar_t* p = text;
    for (;;) {
        const wchar_t* end = p;
        while (*end && *end != L'\n') ++end;
        do {
            int len = (int)(end - p);
            int n = TextFit(p, len > 250 ? 250 : len, maxW, px, weight);
            if (n < len) {
                int b = n;
                while (b > 0 && p[b] != L' ') --b;
                if (b > 0) n = b;
                else if (n == 0) n = 1;  // one word wider than the line: cut it
            }
            const wchar_t* next = p + n;
            while (next < end && *next == L' ') ++next;
            bool more = next < end || *end != 0;
            if (lines == maxLines - 1 && more) {  // the last line there is room for, and text is left: end it with "…"
                if (draw) {
                    int m = TextFit(p, len > 250 ? 250 : len, maxW - TextWidth(L"…", px, weight), px, weight);
                    if (m < len) {  // cut inside the paragraph: end at a word break
                        int b = m;
                        while (b > 0 && p[b] != L' ') --b;
                        if (b > 0) m = b;
                    }
                    while (m > 0 && p[m - 1] == L' ') --m;
                    wchar_t line[256];
                    for (int i = 0; i < m; ++i) line[i] = p[i];
                    line[m] = L'…';
                    line[m + 1] = 0;
                    DrawText(line, x, y + lineH * (float)lines, color, px, weight);
                }
                return lines + 1;
            }
            if (draw && n > 0) {
                wchar_t line[256];
                for (int i = 0; i < n; ++i) line[i] = p[i];
                line[n] = 0;
                DrawText(line, x, y + lineH * (float)lines, color, px, weight);
            }
            ++lines;
            p = next;
        } while (p < end);
        if (*end == 0) break;
        p = end + 1;
    }
    return lines;
}

// ---- Pictures from files ----------------------------------------------------------------------------------------
// The covers of the mods page. d3dx9_43 reads the file (PNG, JPG, BMP, DDS) into a MANAGED texture with its whole mip
// chain, so a 36-unit thumbnail of a 350-pixel cover is as smooth as the cover itself. A picture bigger than needed is
// scaled on load to 512 texels on its shorter side (a 4000-pixel photo would take 120 MB with its copy in system memory);
// the proportions stay, the page crops it to a square by its texture coordinates. Cached by path for the session; a file
// that cannot be read is tried once and logged once. Released before a Reset like the disk, read again at the next use.
namespace {
struct ImageEntry {
    wchar_t path[MAX_PATH];
    void* texture;
    void* device;   // the device it was made on: another device (the game made a new one) means load again
    u32 width, height;  // of the file
    bool failed;
    bool logged;    // the first load is logged, a load after a Reset is not
};
constexpr int kMaxImages = 40;  // a cover per mod (32 at most) and some room
ImageEntry g_images[kMaxImages];
int g_imageCount = 0;

struct ImageInfo {  // D3DXIMAGE_INFO
    UINT width, height, depth, mipLevels;
    DWORD format, resourceType, fileFormat;
};

void PathUtf8(const wchar_t* path, char* out, int capacity) {
    if (WideCharToMultiByte(CP_UTF8, 0, path, -1, out, capacity, nullptr, nullptr) <= 0) out[0] = 0;
}

bool ReadPicture(ImageEntry& e) {
    typedef HRESULT(WINAPI * InfoFn)(const wchar_t*, ImageInfo*);
    typedef HRESULT(WINAPI * FromFileFn)(void*, const wchar_t*, UINT, UINT, UINT, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, void*,
                                         void*, void**);
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    if (!d3dx) d3dx = LoadLibraryW(L"d3dx9_43.dll");
    InfoFn info = d3dx ? (InfoFn)(void*)GetProcAddress(d3dx, "D3DXGetImageInfoFromFileW") : nullptr;
    FromFileFn load = d3dx ? (FromFileFn)(void*)GetProcAddress(d3dx, "D3DXCreateTextureFromFileExW") : nullptr;
    char name[MAX_PATH * 3];
    PathUtf8(e.path, name, sizeof name);
    if (!info || !load) {
        Log("overlay: picture %s not loaded: d3dx9_43.dll has no D3DXCreateTextureFromFileExW", name);
        return false;
    }
    ImageInfo ii = {};
    HRESULT hr = info(e.path, &ii);
    if (FAILED(hr) || !ii.width || !ii.height) {
        Log("overlay: picture %s could not be read (%08X): not a PNG, JPG, BMP or DDS?", name, (u32)hr);
        return false;
    }
    UINT w = ii.width, h = ii.height;
    const UINT shorter = w < h ? w : h, kMost = 512;
    if (shorter > kMost) {
        w = (UINT)((unsigned long long)w * kMost / shorter);
        h = (UINT)((unsigned long long)h * kMost / shorter);
        if (w > 4096) w = 4096;  // a panorama: its middle is all the page shows
        if (h > 4096) h = 4096;
    }
    void* tex = nullptr;
    hr = load(g_dev, e.path, w, h, 0 /*the whole mip chain*/, 0, 21 /*A8R8G8B8*/, 1 /*MANAGED*/, 0xFFFFFFFFu /*D3DX_DEFAULT*/,
              5 /*D3DX_FILTER_BOX for the levels*/, 0, nullptr, nullptr, &tex);
    if (FAILED(hr) || !tex) {
        Log("overlay: picture %s could not be loaded (%08X)", name, (u32)hr);
        return false;
    }
    e.texture = tex;
    e.device = g_dev;
    e.width = ii.width;
    e.height = ii.height;
    if (!e.logged) {
        e.logged = true;
        Log("overlay: picture %s loaded (%ux%u, a %ux%u texture)", name, ii.width, ii.height, w, h);
    }
    return true;
}

void ReleaseImages() {
    for (int i = 0; i < g_imageCount; ++i) {
        if (g_images[i].texture) Method<ReleaseFn>(g_images[i].texture, kRelease)(g_images[i].texture);
        g_images[i].texture = nullptr;
    }
}
}  // namespace

void* ImageTexture(const wchar_t* path, u32* width, u32* height) {
    if (!path || !path[0] || lstrlenW(path) >= MAX_PATH || !g_dev) return nullptr;
    ImageEntry* e = nullptr;
    for (int i = 0; i < g_imageCount && !e; ++i)
        if (lstrcmpiW(g_images[i].path, path) == 0) e = &g_images[i];
    if (!e) {
        if (g_imageCount >= kMaxImages) return nullptr;
        e = &g_images[g_imageCount++];
        lstrcpyW(e->path, path);
        e->texture = e->device = nullptr;
        e->width = e->height = 0;
        e->failed = e->logged = false;
    }
    if (e->failed) return nullptr;
    if (e->texture && e->device != g_dev) {  // made on a device that is gone: never released through it, just forgotten
        e->texture = nullptr;
    }
    if (!e->texture) {
        if (g_imageLoads >= 1) return nullptr;  // the next frame
        ++g_imageLoads;
        if (!ReadPicture(*e)) {
            e->failed = true;
            return nullptr;
        }
    }
    if (width) *width = e->width;
    if (height) *height = e->height;
    return e->texture;
}

// Before a Reset: the copy is a default-pool texture, and the reused state block holds references to the game's
// textures, so both must go (they are made again on the next frame). The disk of the rounded shapes and the pictures go too,
// though managed textures would survive: they are made again at their next use.
namespace {
void ReleaseAoTargets();  // the horizon-based occlusion's two targets (below)
}  // namespace

void ReleasePostResources() {
    ReleaseCopy();
    ReleaseAoTargets();
    ReleaseSmaaTargets();
    ReleaseStateBlockCache();
    ReleaseDisk();
    ReleaseImages();
}

namespace {

// Copies the finished picture and draws it back through `shader` (c0 = texel size and the amount).
void RunPass(void* dev, void** shader, bool* failed, const char* name, const char* source, size_t length,
             const char* const* profiles, int percent, const float* extra = nullptr, int extraRegs = 2) {
    if (percent <= 0 || *failed) return;
    UINT w, h, fmt;
    void* back = BackBufferInfo(dev, &w, &h, &fmt);
    if (!back) return;
    if (!*shader && !BuildShader(dev, source, length, shader, profiles, nullptr, name)) {
        *failed = true;
        Log("%s: the shader could not be built (d3dx9_43), the pass is off", name);
        Method<ReleaseFn>(back, kRelease)(back);
        return;
    }
    if (!g_copyTex || g_copyW != w || g_copyH != h || g_copyFmt != fmt) {
        ReleaseCopy();
        if (FAILED(Method<CreateTexFn>(dev, kCreateTexture)(dev, w, h, 1, 1 /*RENDERTARGET*/, fmt, 0 /*DEFAULT*/, &g_copyTex,
                                                            nullptr)) ||
            !g_copyTex || FAILED(Method<GetLevelFn>(g_copyTex, 18)(g_copyTex, 0, &g_copySurf)) || !g_copySurf) {
            ReleaseCopy();
            *failed = true;
            Log("%s: could not create the copy of the back buffer (%ux%u format %u), the pass is off", name, w, h, fmt);
            Method<ReleaseFn>(back, kRelease)(back);
            return;
        }
        g_copyW = w; g_copyH = h; g_copyFmt = fmt;
        Log("%s: pass ready on a %ux%u back buffer", name, w, h);
    }
    HRESULT hr = Method<StretchFn>(dev, 34)(dev, back, nullptr, g_copySurf, nullptr, 1 /*POINT*/);
    Method<ReleaseFn>(back, kRelease)(back);
    if (FAILED(hr)) return;
    if (!OverlayBegin(dev)) return;
    Method<SetPsFn>(dev, kSetPixelShader)(dev, *shader);
    Method<SetTexFn2>(dev, kSetTexture)(dev, 0, g_copyTex);
    SetRS(RS_ALPHABLENDENABLE, 0);
    SetSampler(SAMP_MINFILTER, 1);  // POINT: exact texels (the shaders do their own filtering)
    SetSampler(SAMP_MAGFILTER, 1);
    const float c[4] = {1.0f / (float)w, 1.0f / (float)h, (float)(percent > 100 ? 100 : percent) / 100.0f, 0.f};
    Method<SetPsConstFn>(dev, 109)(dev, 0, c, 1);
    if (extra) Method<SetPsConstFn>(dev, 109)(dev, 1, extra, extraRegs);  // c1, c2, ...
    Viewport vp = {0, 0, w, h, 0.f, 1.f};
    typedef HRESULT(WINAPI * SetViewportFn2)(void*, const Viewport*);
    Method<SetViewportFn2>(dev, kSetViewport)(dev, &vp);
    Quad(0.f, 0.f, (float)w, (float)h, 0xFFFFFFFF, 0.f, 0.f, 1.f, 1.f);
    Method<SetTexFn2>(dev, kSetTexture)(dev, 0, nullptr);
    OverlayEnd();
}

void* g_colorShader = nullptr;
bool g_colorFailed = false;
const char kColorSource[] =
    "sampler s0 : register(s0);\n"
    "float4 c0 : register(c0);\n"  // xy texel size
    "float4 c1 : register(c1);\n"  // rgb white balance (temperature and tint), w vignette
    "float4 c2 : register(c2);\n"  // x brightness, y contrast, z saturation, w sepia 0..1
    "float4 c3 : register(c3);\n"  // x fade 0..1, y grain 0..1, z chromatic aberration in texels, w letterbox bar height 0..0.5
    "float4 c4 : register(c4);\n"  // x a seed that changes every frame (grain)
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float2 d = uv - 0.5;\n"
    "  float2 ca = d * c3.z * c0.xy * 2.0;\n"   // colour fringes grow towards the corners
    "  float3 c;\n"
    "  c.r = tex2D(s0, uv + ca).r;\n"
    "  c.g = tex2D(s0, uv).g;\n"
    "  c.b = tex2D(s0, uv - ca).b;\n"
    "  float l = dot(c, float3(0.299, 0.587, 0.114));\n"
    "  c = lerp(float3(l, l, l), c, c2.z);\n"
    "  c = (c - 0.5) * c2.y + 0.5 + c2.x;\n"
    "  c = c * c1.rgb;\n"
    "  float3 sep = float3(dot(c, float3(0.393, 0.769, 0.189)), dot(c, float3(0.349, 0.686, 0.168)), dot(c, float3(0.272, 0.534, 0.131)));\n"
    "  c = lerp(c, sep, c2.w);\n"
    "  c = lerp(c, c * 0.82 + 0.1, c3.x);\n"    // faded film: lifted blacks, dimmer whites
    "  float n = frac(sin(dot(uv * 1000.0 + c4.x, float2(12.9898, 78.233))) * 43758.5453);\n"
    "  c += (n - 0.5) * c3.y * 0.22;\n"
    "  c = c * (1.0 - c1.w * dot(d, d) * 2.8);\n"
    "  float bar = step(0.5 - c3.w, abs(d.y));\n"
    "  c = lerp(c, float3(0, 0, 0), bar);\n"
    "  return float4(saturate(c), 1.0);\n"
    "}\n";
const char* const kProfilesColor[] = {"ps_2_b", "ps_2_a", "ps_3_0", "ps_2_0", nullptr};

const char* const kProfilesSharp[] = {"ps_2_0", nullptr};
// RCAS is about 55 arithmetic slots, too close to the 64 of ps_2_0, so it gets the larger 2.x profiles only.
const char* const kProfilesRcas[] = {"ps_2_b", "ps_2_a", "ps_3_0", nullptr};
const char* const kProfilesFxaa[] = {"ps_2_b", "ps_2_a", "ps_2_0", nullptr};

}  // namespace

bool BuildPixelShader(void* dev, const char* source, size_t length, void** shader, const char* const* profiles, const char* logName) {
    return BuildShader(dev, source, length, shader, profiles, logName);
}

void FinalImagePass(void* dev, void** shader, bool* failed, const char* name, const char* source, size_t length,
                    const char* const* profiles, int percent, const float* extra) {
    RunPass(dev, shader, failed, name, source, length, profiles, percent, extra);
}

void ColorFrame(void* dev) {
    // photo mode (the camera flies, or its page is open for a preview): its sliders; otherwise the in-game look, if one is chosen
    int v[11];
    if (g_cfg.photoActive || PhotoPageOpen()) {
        const int* src[11] = {&g_cfg.photoBright, &g_cfg.photoContrast, &g_cfg.photoSat, &g_cfg.photoVignette, &g_cfg.photoTemp, &g_cfg.photoTint,
                              &g_cfg.photoSepia, &g_cfg.photoFade, &g_cfg.photoGrain, &g_cfg.photoAberration, &g_cfg.photoBars};
        for (int i = 0; i < 11; ++i) v[i] = *src[i];
    } else if (!ui::GameLookValues(v)) {
        return;
    }
    bool any = false;
    for (int i = 0; i < 11; ++i) any = any || v[i] != 0;
    if (!any) return;
    static u32 frame = 0;
    ++frame;
    const float t = (float)v[4] / 100.0f, m = (float)v[5] / 100.0f;  // warm +, magenta +
    const float extra[16] = {1.0f + 0.12f * t + 0.04f * m, 1.0f - 0.08f * m, 1.0f - 0.12f * t + 0.04f * m, (float)v[3] / 100.0f,
                             (float)v[0] / 200.0f, 1.0f + (float)v[1] / 100.0f, 1.0f + (float)v[2] / 100.0f, (float)v[6] / 100.0f,
                             (float)v[7] / 100.0f, (float)v[8] / 100.0f, (float)v[9] / 10.0f, (float)v[10] / 100.0f * 0.12f,
                             (float)(frame % 997), 0.f, 0.f, 0.f};
    RunPass(dev, &g_colorShader, &g_colorFailed, "photo filter", kColorSource, sizeof(kColorSource) - 1, kProfilesColor, 100, extra, 4);
}

void SharpenFrame(void* dev, int percent) {
    if (g_cfg.sharpenMode == 1 && !g_rcasFailed && percent > 0) {
        // AMD's sharpness is in stops (0 = strongest, each stop halves the lobe): 100 % = 0 stops, 50 % = 1 stop. The
        // default 50 then lifts a soft, low-contrast edge by about as much as the unsharp mask at 50 did.
        const float extra[4] = {Pow2Tenths(-(100 - (percent > 100 ? 100 : percent)) / 5), 0.f, 0.f, 0.f};
        RunPass(dev, &g_rcasShader, &g_rcasFailed, "sharpen (rcas)", kRcasSource, sizeof(kRcasSource) - 1, kProfilesRcas, percent,
                extra, 1);
        if (!g_rcasFailed) {
            static bool logged = false;
            if (!logged && g_rcasShader) {
                logged = true;
                Log("sharpen: contrast-adaptive (AMD FidelityFX RCAS) pass active");
            }
            return;
        }
        // The shader (or the copy of the back buffer) could not be made, and RunPass said why. The old pass takes over
        // from this frame on; the setting goes back in memory only, so the menu row shows what really sharpens (the ini
        // keeps the player's choice for the next start), as AntialiasFrame does with SMAA.
        g_cfg.sharpenMode = 0;
        Log("sharpen: falling back to the unsharp mask");
    }
    RunPass(dev, &g_sharpShader, &g_sharpFailed, "sharpen", kSharpenSource, sizeof(kSharpenSource) - 1, kProfilesSharp, percent);
}

void AntialiasFrame(void* dev, int percent) {
    if (g_cfg.antialiasMode == 1) {
        if (SmaaFrame(dev, percent)) return;
        // SMAA cannot run (its one log line says why) and the pass below takes over. The setting goes back in memory
        // only: the menu row then shows what really smooths the picture, and the ini keeps the player's choice for
        // the next start.
        g_cfg.antialiasMode = 0;
    }
    ReleaseSmaaTargets();  // two textures of the back buffer's size are not kept while the other method works
    RunPass(dev, &g_fxaaShader, &g_fxaaFailed, "antialias", kFxaaSource, sizeof(kFxaaSource) - 1, kProfilesFxaa, percent);
}

// ---- Filters under the interface ([Graphics] FiltersUnderHud) ----------------------------------------------------
// The game's final composite (the hook in ssao.cpp) writes the finished 3D picture into the back buffer, and only then
// does the game draw its 2D layer over it: subtitles, icons, the map, its menus. Run there, smoothing, sharpening and the
// in-game look change the 3D picture alone: SMAA/FXAA no longer soften the letters, sharpening puts no halo around them,
// a vignette or grain does not land on the interface (the console never filtered it either). Present then leaves out
// what already ran. A frame without such a composite (the title, loading screens, videos), or one whose composite drew
// into another target, keeps all of it at Present as before. The TV gamma stands for the screen and stays at Present,
// and so do the photo mode's own sliders (the HUD is hidden in photo mode, and its look stays as it was). The passes take the same
// way as the occlusion pass inside the game's frame: BeginScene fails there, so no EndScene is sent, and the whole
// device state goes back through the state block of OverlayBegin (SMAA also puts the depth buffer back).
namespace {
bool g_underFilters = false;  // smoothing and sharpening ran after a composite since the last Present
bool g_underLook = false;     // the in-game look too (not in photo mode)
int g_underCount = 0;         // composites into the back buffer since the last Present
int g_underMiss = 0;          // why a composite since the last Present did not take them: 1 another target, 2 another thread
UINT g_underMissW = 0, g_underMissH = 0;  // that target's size
UINT g_underBackW = 0, g_underBackH = 0;  // the back buffer the passes last ran on
}  // namespace

void PostFiltersAfterComposite(void* dev) {
    if (!g_cfg.filtersUnderHud) return;
    // Present runs the passes on the window thread only; a composite drawn elsewhere leaves them there
    HWND window = GameWindowHandle();
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) {
        g_underMiss = 2;
        return;
    }
    typedef HRESULT(WINAPI * GetRtFn3)(void*, DWORD, void**);
    void* target = nullptr;
    Method<GetRtFn3>(dev, kGetRenderTarget)(dev, 0, &target);
    UINT bw = 0, bh = 0, bf = 0;
    void* back = BackBufferInfo(dev, &bw, &bh, &bf);
    const bool onBack = target && back && target == back;
    if (!onBack && target) {
        u32 d[8] = {};  // Format, Type, Usage, Pool, MultiSampleType, MultiSampleQuality, Width, Height
        Method<GetDescFn>(target, 12)(target, d);
        g_underMissW = d[6];
        g_underMissH = d[7];
    }
    if (target) Method<ReleaseFn>(target, kRelease)(target);
    if (back) Method<ReleaseFn>(back, kRelease)(back);
    if (!onBack) {  // a composite into a smaller buffer (or no back buffer this frame): nothing for the passes here
        if (!target) g_underMissW = g_underMissH = 0;
        g_underMiss = 1;
        return;
    }
    const bool timed = g_cfg.perfOverlay || g_cfg.frameStats;
    if (timed) {
        EnsureFreq(dev);
        TimerBegin(dev, g_timerUnder);
    }
    const bool photo = g_cfg.photoActive || PhotoPageOpen();  // the photo sliders stay at Present (ColorFrame picks them there)
    if (g_cfg.antialias > 0) AntialiasFrame(dev, g_cfg.antialias);
    if (!photo) ColorFrame(dev);
    if (g_cfg.sharpen > 0) SharpenFrame(dev, g_cfg.sharpen);
    if (timed) {
        TimerEnd(dev, g_timerUnder);
        g_underTimed = true;
    }
    g_underFilters = true;
    g_underLook = !photo;
    g_underBackW = bw;
    g_underBackH = bh;
    ++g_underCount;
}

bool TakePostFiltersUnderHud(bool* lookDone) {
    const bool filters = g_underFilters;
    *lookDone = g_underLook;
    // where the passes ran, in the log whenever it changes (title, loading screens and play take turns, so it is capped)
    const int path = !g_cfg.filtersUnderHud ? 0 : filters ? 1 : g_underMiss == 1 ? 3 : g_underMiss == 2 ? 4 : 2;
    static int lastPath = -1, lines = 0;
    if (path != lastPath && lines < 40) {
        ++lines;
        if (path == 0) Log("post filters: at Present on the whole frame (FiltersUnderHud=0)");
        else if (path == 1) Log("post filters: drawn under the interface (after the composite, back buffer %ux%u)", g_underBackW, g_underBackH);
        else if (path == 2) Log("post filters: at Present (no composite this frame)");
        else if (path == 3) Log("post filters: at Present (the composite target is not the back buffer: %ux%u)", g_underMissW, g_underMissH);
        else Log("post filters: at Present (the composite was drawn on another thread than the window's)");
    }
    lastPath = path;
    static bool twiceLogged = false;
    if (g_underCount > 1 && !twiceLogged) {
        twiceLogged = true;
        Log("post filters: %d composites into the back buffer in one frame, the passes ran after each", g_underCount);
    }
    g_underFilters = g_underLook = false;
    g_underCount = g_underMiss = 0;
    return filters;
}

// ---- The same building blocks for a final-image pass that lives in its own file (smaa.cpp) ------------------------
void* PostBackBuffer(void* dev, UINT* w, UINT* h, UINT* fmt) { return BackBufferInfo(dev, w, h, fmt); }

bool PostCopyTarget(void* dev, UINT w, UINT h, UINT fmt, void** texture, void** surface) {
    if (!g_copyTex || g_copyW != w || g_copyH != h || g_copyFmt != fmt) {
        ReleaseCopy();
        if (FAILED(Method<CreateTexFn>(dev, kCreateTexture)(dev, w, h, 1, 1 /*RENDERTARGET*/, fmt, 0 /*DEFAULT*/, &g_copyTex,
                                                            nullptr)) ||
            !g_copyTex || FAILED(Method<GetLevelFn>(g_copyTex, 18)(g_copyTex, 0, &g_copySurf)) || !g_copySurf) {
            ReleaseCopy();
            return false;
        }
        g_copyW = w; g_copyH = h; g_copyFmt = fmt;
    }
    *texture = g_copyTex;
    *surface = g_copySurf;
    return true;
}

void PostFullQuad(UINT w, UINT h) { Quad(0.f, 0.f, (float)w, (float)h, 0xFFFFFFFF, 0.f, 0.f, 1.f, 1.f); }

// ---- Ambient occlusion -----------------------------------------------------------------------------------------
// Runs inside the game's frame, in front of the final composite draw (the one that reads the HDR scene as texture 0 and
// the packed depth as texture 4). It darkens the HDR scene where nearby geometry is in front of a pixel: the depth of
// every sample is compared with the plane through the pixel (slope corrected, so flat walls and floors stay clean).
// The packed depth is the game's own G-buffer (high byte, low byte, fraction in R, G, B).
namespace {

void* g_aoShader = nullptr;
bool g_aoFailed = false;
bool g_aoLogged = false;

const char kAoSource[] =
    "sampler s0 : register(s0);\n"
    "float4 c0 : register(c0);\n"  // xy = texel size, z = strength 0..1, w = radius in pixels
    "float4 c1 : register(c1);\n"  // xy = size in pixels
    "float Dep(float2 uv) {\n"
    "  float3 c = tex2D(s0, uv).rgb;\n"
    "  return floor(c.r * 256.0 + 0.5) * 256.0 + floor(c.g * 256.0 + 0.5) + c.b;\n"
    "}\n"
    "#define TAP(ox, oy) { float2 o = float2((ox) * r.x - (oy) * r.y, (ox) * r.y + (oy) * r.x) * c0.w;"
    " float2 q = uv + o * c0.xy; float ds = Dep(q); float rel = max(dc + dot(g, o) - ds - 1.6, 0.0) / dc;"
    " float2 e = abs(q - 0.5); float inside = (e.x < 0.5 && e.y < 0.5) ? 1.0 : 0.0;"
    " occ += inside * smoothstep(0.004, 0.03, rel) * (1.0 - smoothstep(0.10, 0.30, rel)); }\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float dc = max(Dep(uv), 1.0);\n"
    "  float k = max(c0.w * 0.5, 3.0);\n"  // slope from a wide baseline: the packed depth steps by one unit, a 1-pixel difference is noise
    "  float dr = Dep(uv + float2(c0.x * k, 0.0)); float dl = Dep(uv - float2(c0.x * k, 0.0));\n"
    "  float dd = Dep(uv + float2(0.0, c0.y * k)); float du = Dep(uv - float2(0.0, c0.y * k));\n"
    "  float2 g;\n"
    "  g.x = (abs(dr - dc) < abs(dc - dl) ? (dr - dc) : (dc - dl)) / k;\n"
    "  g.y = (abs(dd - dc) < abs(dc - du) ? (dd - dc) : (dc - du)) / k;\n"
    "  float2 p = uv * c1.xy;\n"
    "  float a = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715)))) * 6.2831853;\n"
    "  float2 r = float2(cos(a), sin(a));\n"
    "  float occ = 0.0;\n"
    "  TAP(0.35, 0.0) TAP(0.0, 0.35) TAP(-0.35, 0.0) TAP(0.0, -0.35)\n"
    "  TAP(0.5, 0.5) TAP(-0.5, 0.5) TAP(-0.5, -0.5) TAP(0.5, -0.5)\n"
    "  TAP(1.0, 0.0) TAP(0.0, 1.0) TAP(-1.0, 0.0) TAP(0.0, -1.0)\n"
    "  float ao = saturate(1.0 - c0.z * occ * 0.2);\n"
    "  ao = dc > 30000.0 ? 1.0 : ao;\n"
    "  return float4(ao, ao, ao, 1.0);\n"
    "}\n";

const char* const kProfilesAo[] = {"ps_2_b", "ps_2_a", "ps_3_0", nullptr};

// Horizon-based occlusion (AoMode 1): 8 directions turned per pixel, 4 steps each. For every direction the highest horizon above
// the surface's tangent plane counts (its sine), faded with the distance, so a corner or the ground under a car darkens and an
// open wall does not. The result goes into a texture of its own and is blurred with depth-aware weights in two passes before it
// darkens the scene, so the turned directions do not show as a pattern. c1.z: the width of one pixel at depth 1 (the camera's
// vertical view of about 45 degrees: 2 tan(22.5) / height).
void* g_hbaoShader = nullptr;
void* g_aoBlurShader = nullptr;
bool g_hbaoFailed = false;
void* g_aoTex[2] = {nullptr, nullptr};
void* g_aoSurf[2] = {nullptr, nullptr};
UINT g_aoTexW = 0, g_aoTexH = 0;
const char kHbaoSource[] =
    "sampler s0 : register(s0);\n"
    "float4 c0 : register(c0);\n"  // xy = texel size, z = strength 0..1, w = radius in pixels
    "float4 c1 : register(c1);\n"  // xy = size in pixels, z = width of a pixel at depth 1
    "float Dep(float2 uv) {\n"
    "  float3 c = tex2Dlod(s0, float4(uv, 0, 0)).rgb;\n"
    "  return floor(c.r * 256.0 + 0.5) * 256.0 + floor(c.g * 256.0 + 0.5) + c.b;\n"
    "}\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float dc = max(Dep(uv), 1.0);\n"
    "  if (dc > 30000.0) return float4(1, 1, 1, 1);\n"
    // the surface slope from the next texels, taken from the side that continues the surface: each side is tested by how well
    // its two texels extrapolate to the centre. Near a corner of two walls the nearer one-sided difference could belong to the
    // other wall; the pixels next to the crease then got a tilted plane, saw no occlusion and drew a white strip along it.
    "  float2 tx = float2(c0.x, 0.0), ty = float2(0.0, c0.y);\n"
    "  float dr1 = Dep(uv + tx), dr2 = Dep(uv + 2.0 * tx), dl1 = Dep(uv - tx), dl2 = Dep(uv - 2.0 * tx);\n"
    "  float dd1 = Dep(uv + ty), dd2 = Dep(uv + 2.0 * ty), du1 = Dep(uv - ty), du2 = Dep(uv - 2.0 * ty);\n"
    "  float2 g;\n"
    "  g.x = abs(2.0 * dr1 - dr2 - dc) < abs(2.0 * dl1 - dl2 - dc) ? (dr1 - dc) : (dc - dl1);\n"
    "  g.y = abs(2.0 * dd1 - dd2 - dc) < abs(2.0 * du1 - du2 - dc) ? (dd1 - dc) : (dc - du1);\n"
    "  float2 p = uv * c1.xy;\n"
    "  float n = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));\n"
    "  float a0 = n * 6.2831853;\n"
    "  float jit = frac(n * 7.0);\n"
    "  float worldR = c0.w * dc * c1.z;\n"
    "  float occ = 0.0;\n"
    "  for (int i = 0; i < 8; ++i) {\n"
    "    float ang = a0 + i * 0.78539816;\n"
    "    float2 dir = float2(cos(ang), sin(ang));\n"
    "    float best = 0.0;\n"
    "    for (int j = 1; j <= 4; ++j) {\n"
    "      float t = (j - jit) * 0.25 * c0.w;\n"
    "      float2 o = dir * t;\n"
    "      float2 q = uv + o * c0.xy;\n"
    // beyond the frame the texture clamps to its edge row: those samples are not geometry and drew lines along the edges
    "      if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) continue;\n"
    "      float ds = Dep(q);\n"
    "      float dz = dc + dot(g, o) - ds - (1.0 + dc * 0.002);\n"   // above the tangent plane; 1 unit for the packing's steps
    "      float lat = t * dc * c1.z;\n"
    "      float len2 = dz * dz + lat * lat;\n"
    "      float s = dz / sqrt(len2 + 1e-4);\n"
    "      float fall = saturate(1.0 - len2 / (worldR * worldR + 1e-4));\n"
    "      best = max(best, saturate((s - 0.1) / 0.9) * fall);\n"
    "    }\n"
    "    occ += best;\n"
    "  }\n"
    "  float ao = saturate(1.0 - c0.z * occ * 0.2);\n"
    "  return float4(ao, ao, ao, 1.0);\n"
    "}\n";
// 9 taps along c2.xy (one texel step), weighted by distance and by how close the depth is to the centre's.
const char kAoBlurSource[] =
    "sampler s0 : register(s0);\n"  // occlusion
    "sampler s1 : register(s1);\n"  // depth
    "float4 c0 : register(c0);\n"
    "float4 c2 : register(c2);\n"
    "float Dep(float2 uv) {\n"
    "  float3 c = tex2Dlod(s1, float4(uv, 0, 0)).rgb;\n"
    "  return floor(c.r * 256.0 + 0.5) * 256.0 + floor(c.g * 256.0 + 0.5) + c.b;\n"
    "}\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float dc = max(Dep(uv), 1.0);\n"
    "  float sum = 0.0, wsum = 0.0;\n"
    "  for (int k = -4; k <= 4; ++k) {\n"
    "    float2 q = uv + c2.xy * (k * 1.5);\n"
    "    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) continue;\n"
    "    float a = tex2Dlod(s0, float4(q, 0, 0)).r;\n"
    "    float d = Dep(q);\n"
    "    float w = exp(-k * k * 0.125) * saturate(1.0 - abs(d - dc) / (dc * 0.03 + 2.0));\n"
    "    sum += a * w; wsum += w;\n"
    "  }\n"
    "  float ao = sum / max(wsum, 1e-4);\n"
    "  return float4(ao, ao, ao, 1.0);\n"
    "}\n";
const char* const kProfilesHbao[] = {"ps_3_0", nullptr};

void ReleaseAoTargets() {
    for (int i = 0; i < 2; ++i) {
        if (g_aoSurf[i]) Method<ReleaseFn>(g_aoSurf[i], kRelease)(g_aoSurf[i]);
        if (g_aoTex[i]) Method<ReleaseFn>(g_aoTex[i], kRelease)(g_aoTex[i]);
        g_aoSurf[i] = g_aoTex[i] = nullptr;
    }
    g_aoTexW = g_aoTexH = 0;
}

bool AoTargets(void* dev, UINT w, UINT h) {
    if (g_aoTex[0] && g_aoTex[1] && g_aoTexW == w && g_aoTexH == h) return true;
    ReleaseAoTargets();
    for (int i = 0; i < 2; ++i) {
        if (FAILED(Method<CreateTexFn>(dev, kCreateTexture)(dev, w, h, 1, 1 /*RENDERTARGET*/, 21 /*A8R8G8B8*/, 0 /*DEFAULT*/, &g_aoTex[i], nullptr)) ||
            !g_aoTex[i] || FAILED(Method<GetLevelFn>(g_aoTex[i], 18)(g_aoTex[i], 0, &g_aoSurf[i])) || !g_aoSurf[i]) {
            ReleaseAoTargets();
            return false;
        }
    }
    g_aoTexW = w;
    g_aoTexH = h;
    return true;
}

}  // namespace

// The horizon-based pass with its blur (AoMode 1). False = it cannot run (shader, targets): the plain pass takes over.
bool HbaoPass(void* dev, void* hdr, void* depth, UINT w, UINT h, int strengthPercent, int radiusPixels) {
    typedef HRESULT(WINAPI * GetSurfFn)(void*, UINT, void**);
    typedef HRESULT(WINAPI * GetDsFn)(void*, void**);
    typedef HRESULT(WINAPI * SetDsFn)(void*, void*);
    typedef HRESULT(WINAPI * SetRtFn2)(void*, DWORD, void*);
    typedef HRESULT(WINAPI * SetViewportFn2)(void*, const Viewport*);
    if ((!g_hbaoShader && !BuildShader(dev, kHbaoSource, sizeof(kHbaoSource) - 1, &g_hbaoShader, kProfilesHbao, "ssao (horizon)")) ||
        (!g_aoBlurShader && !BuildShader(dev, kAoBlurSource, sizeof(kAoBlurSource) - 1, &g_aoBlurShader, kProfilesHbao, "ssao (blur)"))) {
        g_hbaoFailed = true;
        Log("ssao: the horizon-based shaders could not be built, the plain pass is used");
        return false;
    }
    // The occlusion is worked out on at most 1080 rows: on a scene of 5760x3240 (render scale 300%) a full-size pass cost
    // 11 ms. The depth is still read at full size; the result is stretched over the scene with bilinear filtering.
    const UINT div = (h + 1079) / 1080;
    const UINT aw = (w + div - 1) / div, ah = (h + div - 1) / div;
    if (!AoTargets(dev, aw, ah)) {
        g_hbaoFailed = true;
        Log("ssao: two %ux%u textures for the horizon-based pass could not be made, the plain pass is used", aw, ah);
        return false;
    }
    void* hdrSurf = nullptr;
    if (FAILED(Method<GetSurfFn>(hdr, 18)(hdr, 0, &hdrSurf)) || !hdrSurf) return false;
    void* oldDs = nullptr;
    Method<GetDsFn>(dev, 40)(dev, &oldDs);
    if (!BeginOverlay(dev, false)) {
        if (oldDs) Method<ReleaseFn>(oldDs, kRelease)(oldDs);
        Method<ReleaseFn>(hdrSurf, kRelease)(hdrSurf);
        return false;
    }
    const float radius = (float)radiusPixels * (float)h / 720.0f;
    const float c0[4] = {1.0f / (float)w, 1.0f / (float)h, (float)(strengthPercent > 100 ? 100 : strengthPercent) / 100.0f, radius};
    const float c1[4] = {(float)w, (float)h, 0.8284271f / (float)h, 0.f};
    Viewport vp = {0, 0, w, h, 0.f, 1.f}, avp = {0, 0, aw, ah, 0.f, 1.f};
    Method<SetDsFn>(dev, 39)(dev, nullptr);
    for (u32 stage = 0; stage < 2; ++stage) {
        Method<Dword3Fn>(dev, kSetSamplerState)(dev, stage, SAMP_MINFILTER, 1);  // POINT: exact depth and occlusion values
        Method<Dword3Fn>(dev, kSetSamplerState)(dev, stage, SAMP_MAGFILTER, 1);
        Method<Dword3Fn>(dev, kSetSamplerState)(dev, stage, 1 /*ADDRESSU*/, 3 /*CLAMP*/);
        Method<Dword3Fn>(dev, kSetSamplerState)(dev, stage, 2 /*ADDRESSV*/, 3);
    }
    SetRS(RS_COLORWRITEENABLE, 7);
    if (g_cfg.perfOverlay || g_cfg.frameStats) { EnsureFreq(dev); TimerBegin(dev, g_timerAo); }
    // 1: the occlusion into texture 0
    SetRS(RS_ALPHABLENDENABLE, 0);
    Method<SetRtFn2>(dev, kSetRenderTarget)(dev, 0, g_aoSurf[0]);
    Method<SetViewportFn2>(dev, kSetViewport)(dev, &avp);
    Method<SetPsFn>(dev, kSetPixelShader)(dev, g_hbaoShader);
    Method<SetTexFn2>(dev, kSetTexture)(dev, 0, depth);
    Method<SetPsConstFn>(dev, 109)(dev, 0, c0, 1);
    Method<SetPsConstFn>(dev, 109)(dev, 1, c1, 1);
    Quad(0.f, 0.f, (float)aw, (float)ah, 0xFFFFFFFF, 0.f, 0.f, 1.f, 1.f);
    // 2: blurred across into texture 1 (one step = one texel of the occlusion textures)
    const float across[4] = {1.0f / (float)aw, 0.f, 0.f, 0.f}, down[4] = {0.f, 1.0f / (float)ah, 0.f, 0.f};
    Method<SetRtFn2>(dev, kSetRenderTarget)(dev, 0, g_aoSurf[1]);
    Method<SetViewportFn2>(dev, kSetViewport)(dev, &avp);
    Method<SetPsFn>(dev, kSetPixelShader)(dev, g_aoBlurShader);
    Method<SetTexFn2>(dev, kSetTexture)(dev, 0, g_aoTex[0]);
    Method<SetTexFn2>(dev, kSetTexture)(dev, 1, depth);
    Method<SetPsConstFn>(dev, 109)(dev, 2, across, 1);
    Quad(0.f, 0.f, (float)aw, (float)ah, 0xFFFFFFFF, 0.f, 0.f, 1.f, 1.f);
    // 3: blurred down, multiplied into the scene
    Method<SetRtFn2>(dev, kSetRenderTarget)(dev, 0, hdrSurf);
    Method<SetViewportFn2>(dev, kSetViewport)(dev, &vp);
    Method<SetTexFn2>(dev, kSetTexture)(dev, 0, g_aoTex[1]);
    Method<SetPsConstFn>(dev, 109)(dev, 2, down, 1);
    if (div > 1) {  // the smaller occlusion stretched smoothly over the scene
        Method<Dword3Fn>(dev, kSetSamplerState)(dev, 0, SAMP_MINFILTER, 2 /*LINEAR*/);
        Method<Dword3Fn>(dev, kSetSamplerState)(dev, 0, SAMP_MAGFILTER, 2);
    }
    SetRS(RS_ALPHABLENDENABLE, g_cfg.aoView ? 0 : 1);  // the check view: the occlusion alone in place of the scene
    SetRS(RS_SRCBLEND, 9);   // DESTCOLOR: result = scene * occlusion
    SetRS(RS_DESTBLEND, 1);  // ZERO
    Quad(0.f, 0.f, (float)w, (float)h, 0xFFFFFFFF, 0.f, 0.f, 1.f, 1.f);
    if (g_cfg.perfOverlay || g_cfg.frameStats) TimerEnd(dev, g_timerAo);
    Method<SetTexFn2>(dev, kSetTexture)(dev, 0, nullptr);
    Method<SetTexFn2>(dev, kSetTexture)(dev, 1, nullptr);
    OverlayEnd();  // puts the render target and the whole state back
    Method<SetDsFn>(dev, 39)(dev, oldDs);
    if (oldDs) Method<ReleaseFn>(oldDs, kRelease)(oldDs);
    Method<ReleaseFn>(hdrSurf, kRelease)(hdrSurf);
    static bool logged = false;
    if (!logged) {
        logged = true;
        Log("ssao: horizon-based pass with a depth-aware blur running on a %ux%u scene, occlusion worked out at %ux%u (strength %d%%, radius %d px)",
            w, h, aw, ah, strengthPercent, radiusPixels);
    }
    return true;
}

void AmbientOcclusionPass(void* dev, int strengthPercent, int radiusPixels) {
    if (strengthPercent <= 0 || g_aoFailed) return;
    typedef HRESULT(WINAPI * GetTexFn)(void*, DWORD, void**);
    typedef HRESULT(WINAPI * GetLevelDescFn)(void*, UINT, u32*);
    typedef HRESULT(WINAPI * GetSurfFn)(void*, UINT, void**);
    typedef HRESULT(WINAPI * GetRtFn2)(void*, DWORD, void**);
    typedef HRESULT(WINAPI * GetDsFn)(void*, void**);
    typedef HRESULT(WINAPI * SetDsFn)(void*, void*);
    typedef HRESULT(WINAPI * SetRtFn2)(void*, DWORD, void*);

    void *hdr = nullptr, *depth = nullptr, *target = nullptr;
    Method<GetTexFn>(dev, 64)(dev, 0, &hdr);
    Method<GetTexFn>(dev, 64)(dev, 4, &depth);
    Method<GetRtFn2>(dev, kGetRenderTarget)(dev, 0, &target);
    auto releaseAll = [&]() {
        if (hdr) Method<ReleaseFn>(hdr, kRelease)(hdr);
        if (depth) Method<ReleaseFn>(depth, kRelease)(depth);
        if (target) Method<ReleaseFn>(target, kRelease)(target);
    };
    if (!hdr || !depth || !target) { releaseAll(); return; }
    u32 hd[8] = {}, dd[8] = {}, td[8] = {};  // Format, Type, Usage, Pool, MultiSampleType, MultiSampleQuality, Width, Height
    Method<GetLevelDescFn>(hdr, 17)(hdr, 0, hd);
    Method<GetLevelDescFn>(depth, 17)(depth, 0, dd);
    Method<GetDescFn>(target, 12)(target, td);
    UINT bw, bh, bf;
    void* back = BackBufferInfo(dev, &bw, &bh, &bf);
    if (back) Method<ReleaseFn>(back, kRelease)(back);
    // the main composite only: the target is as big as the back buffer, the scene is a 16-bit float texture, the
    // depth an 8-bit one of the same size (other composites go into small buffers)
    if (!back || td[6] != bw || td[7] != bh || hd[0] != 113 || dd[0] != 21 || hd[6] != dd[6] || hd[7] != dd[7] ||
        !(hd[2] & 1)) {
        releaseAll();
        return;
    }
    if (g_cfg.aoMode == 1 && !g_hbaoFailed && HbaoPass(dev, hdr, depth, hd[6], hd[7], strengthPercent, radiusPixels)) {
        releaseAll();
        return;
    }
    if (radiusPixels > 20) radiusPixels = 20;  // the plain pass: 12 taps and no blur show their pattern on a wider ring
    if (!g_aoShader && !BuildShader(dev, kAoSource, sizeof(kAoSource) - 1, &g_aoShader, kProfilesAo, nullptr, "ssao (plain)")) {
        g_aoFailed = true;
        Log("ssao: the shader could not be built (d3dx9_43), the pass is off");
        releaseAll();
        return;
    }
    void* hdrSurf = nullptr;
    if (FAILED(Method<GetSurfFn>(hdr, 18)(hdr, 0, &hdrSurf)) || !hdrSurf) { releaseAll(); return; }
    void* oldDs = nullptr;
    Method<GetDsFn>(dev, 40)(dev, &oldDs);
    if (!BeginOverlay(dev, false)) {  // a block of its own: this pass runs inside the game's frame, outside the Present hook
        if (oldDs) Method<ReleaseFn>(oldDs, kRelease)(oldDs);
        Method<ReleaseFn>(hdrSurf, kRelease)(hdrSurf);
        releaseAll();
        return;
    }
    UINT w = hd[6], h = hd[7];
    Method<SetRtFn2>(dev, kSetRenderTarget)(dev, 0, hdrSurf);
    Method<SetDsFn>(dev, 39)(dev, nullptr);
    Method<SetPsFn>(dev, kSetPixelShader)(dev, g_aoShader);
    Method<SetTexFn2>(dev, kSetTexture)(dev, 0, depth);
    SetRS(RS_ALPHABLENDENABLE, g_cfg.aoView ? 0 : 1);
    SetRS(RS_SRCBLEND, 9);   // DESTCOLOR: result = scene * occlusion
    SetRS(RS_DESTBLEND, 1);  // ZERO
    SetRS(RS_COLORWRITEENABLE, 7);
    SetSampler(SAMP_MINFILTER, 1);  // POINT: exact depth values
    SetSampler(SAMP_MAGFILTER, 1);
    float radius = (float)radiusPixels * (float)h / 720.0f;
    const float c0[4] = {1.0f / (float)w, 1.0f / (float)h, (float)(strengthPercent > 100 ? 100 : strengthPercent) / 100.0f, radius};
    const float c1[4] = {(float)w, (float)h, 0.f, 0.f};
    Method<SetPsConstFn>(dev, 109)(dev, 0, c0, 1);
    Method<SetPsConstFn>(dev, 109)(dev, 1, c1, 1);
    Viewport vp = {0, 0, w, h, 0.f, 1.f};
    typedef HRESULT(WINAPI * SetViewportFn2)(void*, const Viewport*);
    Method<SetViewportFn2>(dev, kSetViewport)(dev, &vp);
    if (g_cfg.perfOverlay || g_cfg.frameStats) { EnsureFreq(dev); TimerBegin(dev, g_timerAo); }
    Quad(0.f, 0.f, (float)w, (float)h, 0xFFFFFFFF, 0.f, 0.f, 1.f, 1.f);
    if (g_cfg.perfOverlay || g_cfg.frameStats) TimerEnd(dev, g_timerAo);
    OverlayEnd();  // puts the render target and the whole state back
    Method<SetDsFn>(dev, 39)(dev, oldDs);
    if (oldDs) Method<ReleaseFn>(oldDs, kRelease)(oldDs);
    Method<ReleaseFn>(hdrSurf, kRelease)(hdrSurf);
    releaseAll();
    if (!g_aoLogged) {
        g_aoLogged = true;
        Log("ssao: pass running on a %ux%u scene (strength %d%%, radius %d px)", w, h, strengthPercent, radiusPixels);
    }
}

static volatile bool g_shotRequested = false;
static wchar_t g_shotSuffix[48] = L"";  // added to the next file name (a batch names its filter)
static int g_hudlessFrames = 0;         // Shift + the screenshot key: the game's HUD is hidden for a few frames, then the shot
void RequestScreenshot() { g_shotRequested = true; }
void RequestScreenshotNamed(const wchar_t* suffix) {
    lstrcpynW(g_shotSuffix, suffix ? suffix : L"", 48);
    g_shotRequested = true;
}
bool ScreenshotHidesHud() { return g_hudlessFrames > 0; }

void ScreenshotFrame(void* dev) {
    static bool wasDown = false;
    bool down = g_cfg.screenshotKey && (GetAsyncKeyState(g_cfg.screenshotKey) & 0x8000) != 0;
    bool press = (down && !wasDown) || g_shotRequested;
    if (down && !wasDown && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {  // without the HUD: hide it first, shoot two frames later
        g_hudlessFrames = 3;
        press = false;
        lstrcpyW(g_shotSuffix, L"_nohud");
    }
    g_shotRequested = false;
    wasDown = down;
    if (g_hudlessFrames > 0 && --g_hudlessFrames == 0) press = true;
    if (!press) return;
    UINT w, h, fmt;
    void* back = BackBufferInfo(dev, &w, &h, &fmt);
    if (!back) return;
    typedef HRESULT(WINAPI * CreatePlainFn)(void*, UINT, UINT, DWORD, DWORD, void**, void*);
    typedef HRESULT(WINAPI * GetRtDataFn)(void*, void*, void*);
    typedef HRESULT(WINAPI * SaveFn)(const wchar_t*, DWORD, void*, const void*, const RECT*);
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    SaveFn save = d3dx ? (SaveFn)(void*)GetProcAddress(d3dx, "D3DXSaveSurfaceToFileW") : nullptr;
    void* sys = nullptr;
    bool ok = false;
    if (save && SUCCEEDED(Method<CreatePlainFn>(dev, 36)(dev, w, h, fmt, 2 /*SYSTEMMEM*/, &sys, nullptr)) && sys &&
        SUCCEEDED(Method<GetRtDataFn>(dev, 32)(dev, back, sys))) {
        wchar_t dir[MAX_PATH], path[MAX_PATH + 40];
        lstrcpyW(dir, g_paths.dataDir);
        lstrcatW(dir, L"screenshots");
        CreateDirectoryW(dir, nullptr);
        SYSTEMTIME t;
        GetLocalTime(&t);
        wsprintfW(path, L"%s\\dp_%04d%02d%02d_%02d%02d%02d_%03d%s.png", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                  t.wMilliseconds, g_shotSuffix);
        g_shotSuffix[0] = 0;
        ok = SUCCEEDED(save(path, 3 /*D3DXIFF_PNG*/, sys, nullptr, nullptr));
        Log("screenshot: %S %s", path, ok ? "saved" : "FAILED");
    }
    if (sys) Method<ReleaseFn>(sys, kRelease)(sys);
    Method<ReleaseFn>(back, kRelease)(back);
    if (!ok && !save) Log("screenshot: D3DXSaveSurfaceToFileW not found");
}

}  // namespace ui
}  // namespace ufx
