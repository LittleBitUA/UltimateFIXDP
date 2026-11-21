// Draws rectangles and text on top of the finished frame, just before it is presented.
//
// Text is rendered with GDI into small textures (so every language the system fonts cover works,
// Ukrainian included) and drawn as textured quads with the fixed-function pipeline. The device state is
// saved with a state block and restored afterwards; the game never notices.
#include "ui.h"
#include "d3d9.h"

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

typedef ULONG(WINAPI* ReleaseFn)(void*);
typedef HRESULT(WINAPI* DwordDwordFn)(void*, DWORD, DWORD);
typedef HRESULT(WINAPI* Dword3Fn)(void*, DWORD, DWORD, DWORD);

void SetRS(u32 state, u32 value) { Method<DwordDwordFn>(g_dev, kSetRenderState)(g_dev, state, value); }
void SetTSS(u32 type, u32 value) { Method<Dword3Fn>(g_dev, kSetTextureStageState)(g_dev, 0, type, value); }
void SetSampler(u32 type, u32 value) { Method<Dword3Fn>(g_dev, kSetSamplerState)(g_dev, 0, type, value); }

// One memory DC and one font per pixel size, kept for the whole session (a new font for every string was slow).
HDC g_textDc = nullptr;
struct FontEntry {
    int px;
    HFONT font;
};
constexpr int kMaxFonts = 16;
FontEntry g_fonts[kMaxFonts];
int g_fontCount = 0;

HDC TextDc() {
    if (!g_textDc) g_textDc = CreateCompatibleDC(nullptr);
    return g_textDc;
}

// The font is never left selected in the DC, so an old one can be deleted when the table is full.
HFONT FontFor(int px) {
    for (int i = 0; i < g_fontCount; ++i)
        if (g_fonts[i].px == px) return g_fonts[i].font;
    HFONT font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!font) return nullptr;
    int slot = g_fontCount < kMaxFonts ? g_fontCount++ : kMaxFonts - 1;
    if (slot == kMaxFonts - 1 && g_fonts[slot].font) DeleteObject(g_fonts[slot].font);
    g_fonts[slot].px = px;
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
TextEntry* MakeText(const wchar_t* text, int len, u32 hash, int px) {
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
    HFONT font = dc ? FontFor(px) : nullptr;
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

TextEntry* GetText(const wchar_t* text, int px) {
    int len;
    u32 hash = HashText(text, &len);
    for (int i = 0; i < g_textCount; ++i) {
        TextEntry& e = g_texts[i];
        if (e.hash != hash || e.length != len || e.pixelHeight != px || !e.texture) continue;
        int k = 0;
        while (k < len && e.text[k] == text[k]) ++k;
        if (k == len) {
            e.lastUsed = ++g_useClock;
            return &e;
        }
    }
    return MakeText(text, len, hash, px);
}

// Scale of the overlay relative to the 1280x720 layout: the back buffer may be bigger. The output is always 16:9, so both
// axes scale the same; the two functions stay apart in case that ever changes.
float ScaleX() { return g_outputW ? (float)g_outputW / 1280.f : 1.f; }
float ScaleY() { return g_outputW && g_outputH ? (float)g_outputH / 720.f : ScaleX(); }
int FontPx(int px) { return (int)((float)px * ScaleY() + 0.5f); }  // the font is rendered at the real size

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

void FillRect(float x, float y, float w, float h, u32 argb) {
    typedef HRESULT(WINAPI * SetTexFn)(void*, DWORD, void*);
    Method<SetTexFn>(g_dev, kSetTexture)(g_dev, 0, nullptr);
    SetTSS(TSS_COLOROP, TOP_SELECTARG1);
    SetTSS(TSS_COLORARG1, TA_DIFFUSE);
    SetTSS(TSS_ALPHAOP, TOP_SELECTARG1);
    SetTSS(TSS_ALPHAARG1, TA_DIFFUSE);
    float kx = ScaleX(), ky = ScaleY();
    Quad(x * kx, y * ky, (x + w) * kx, (y + h) * ky, argb, 0.f, 0.f, 1.f, 1.f);
}

float TextWidth(const wchar_t* text, int px) {
    TextEntry* e = GetText(text, FontPx(px));
    return e ? (float)(e->width - 4) / ScaleX() : 0.f;
}

// Measured with GDI only (no texture is made), so a long text can be tried at many break points cheaply.
int TextFit(const wchar_t* text, int length, float maxWidth, int px) {
    if (length <= 0) return 0;
    HDC dc = TextDc();
    HFONT font = dc ? FontFor(FontPx(px)) : nullptr;
    if (!font) return length;
    HGDIOBJ old = SelectObject(dc, font);
    int fit = 0;
    SIZE size;
    int room = (int)(maxWidth * ScaleX());
    if (!GetTextExtentExPointW(dc, text, length, room > 0 ? room : 0, &fit, nullptr, &size)) fit = length;
    SelectObject(dc, old);
    return fit;
}

float DrawText(const wchar_t* text, float x, float y, u32 argb, int px) {
    TextEntry* e = GetText(text, FontPx(px));  // render the font at the real size
    if (!e) return 0.f;
    typedef HRESULT(WINAPI * SetTexFn)(void*, DWORD, void*);
    Method<SetTexFn>(g_dev, kSetTexture)(g_dev, 0, e->texture);
    SetTSS(TSS_COLOROP, TOP_SELECTARG2);  // colour from the vertex
    SetTSS(TSS_COLORARG2, TA_DIFFUSE);
    SetTSS(TSS_ALPHAOP, TOP_MODULATE);  // coverage from the texture times the vertex alpha
    SetTSS(TSS_ALPHAARG1, TA_TEXTURE);
    SetTSS(TSS_ALPHAARG2, TA_DIFFUSE);
    float px0 = x * ScaleX() - 2.f, py0 = y * ScaleY() - 2.f;
    Quad(px0, py0, px0 + (float)e->width, py0 + (float)e->height, argb, 0.f, 0.f, 1.f, 1.f);
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

// With `logName` the compiler's message for a profile that failed is written to the log (it is plain text).
bool BuildShader(void* dev, const char* source, size_t length, void** shader, const char* const* profiles, const char* logName = nullptr) {
    HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
    if (!d3dx) d3dx = LoadLibraryW(L"d3dx9_43.dll");
    CompileFn compile = d3dx ? (CompileFn)(void*)GetProcAddress(d3dx, "D3DXCompileShader") : nullptr;
    if (!compile) return false;
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
        if (SUCCEEDED(hr) && *shader) return true;
    }
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

void PerfOverlayFrame(void* dev, double fps, double frameMs, double presentMs, double physicsMs) {
    if (!g_cfg.perfOverlay) return;
    static u64 lastUpdate = 0;
    static wchar_t line1[96] = L"", line2[128] = L"";
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
            wsprintfW(line2, L"physics %d.%d ms  Present wait %d.%d ms  mod GPU: SSAO %d.%d, post %d.%d ms", (int)physicsMs, p10,
                      (int)presentMs, w10, (int)g_timerAo.ms, a10, (int)post, g10);
            if (logTick % 10 == 0) Log("perf: %S | %S", line1, line2);
        }
        if (g_cfg.language != 1) {
            wsprintfW(line1, L"%d FPS   %d,%d мс", (int)(fps + 0.5), (int)frameMs, f10);
            wsprintfW(line2, L"фізика %d,%d мс  очікування Present %d,%d мс  GPU мода: SSAO %d,%d, пост %d,%d мс", (int)physicsMs, p10,
                      (int)presentMs, w10, (int)g_timerAo.ms, a10, (int)post, g10);
        }
    }
    if (!OverlayBegin(dev)) return;
    FillRect(8, 8, 520, 44, 0xB0000000);
    DrawText(line1, 14, 10, 0xFFFFFFFF, 16);
    DrawText(line2, 14, 30, 0xFFC4C4CE, 13);
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
    FillRect(x, y, w, 196.f, 0xB0000000);
    static const wchar_t* const namesUk[7] = {L"Всього:", L"Гра (CPU):", L"  з них фізика:", L"Present:", L"Найгірший:", L"Проходи мода:", L"Потік гри працює:"};
    static const wchar_t* const namesEn[7] = {L"Total:", L"Game (CPU):", L"  of it physics:", L"Present:", L"Worst:", L"Mod passes:", L"Game thread busy:"};
    const wchar_t* const* names = g_cfg.language == 1 ? namesEn : namesUk;
    for (int i = 0; i < 7; ++i) {
        DrawText(names[i], x + 8.f, y + 6.f + 17.f * (float)i, i == 2 ? 0xFFA8A8B4 : 0xFFE8E8F0, 13);
        float tw = TextWidth(t[i], 13);
        DrawText(t[i], x + w - 8.f - tw, y + 6.f + 17.f * (float)i, i == 0 ? 0xFFFFFFFF : 0xFFC4C4CE, 13);
    }
    wchar_t foot[64];
    const bool en = g_cfg.language == 1;
    const wchar_t* api = g_usingDxvk ? L"DXVK" : g_d3dWrapper ? (en ? L"D3D9 (wrapper)" : L"D3D9 (обгортка)") : L"D3D9";
    wsprintfW(foot, en ? L"%d FPS   %d Hz   %s" : L"%d FPS   %d Гц   %s", totalMs > 0.0 ? (int)(1000.0 / totalMs + 0.5) : 0, hz, api);
    DrawText(foot, x + 8.f, y + 6.f + 17.f * 7.f, 0xFFA8A8B4, 12);
    // graph of the last 120 frames: the scale is 2 x 16.7 ms (60 frames/s) or the worst frame; a thin line marks 16.7 ms
    double scale = 33.4;
    for (int i = 0; i < 120; ++i)
        if (g_frameHist[i] * 1.05 > scale) scale = g_frameHist[i] * 1.05;
    const float gx = x + 8.f, gy = y + 186.f, gh = 38.f, bw = (w - 16.f) / 120.f;
    FillRect(gx, gy - gh, w - 16.f, gh, 0x40000000);
    for (int i = 0; i < 120; ++i) {
        double v = g_frameHist[(g_frameHistPos + i) % 120];
        float h = (float)(v / scale) * gh;
        if (h > gh) h = gh;
        if (h < 1.f) h = 1.f;
        FillRect(gx + (float)i * bw, gy - h, bw > 1.f ? bw - 0.4f : bw, h, v > totalMs * 1.6 + 2.0 ? 0xFFE05050 : 0xFFB8B8C4);
    }
    FillRect(gx, gy - (float)(16.7 / scale) * gh, w - 16.f, 1.f, 0x90FFFFFF);
    OverlayEnd();
}

// Before a Reset: the copy is a default-pool texture, and the reused state block holds references to the game's
// textures, so both must go (they are made again on the next frame).
namespace {
void ReleaseAoTargets();  // the horizon-based occlusion's two targets (below)
}  // namespace

void ReleasePostResources() {
    ReleaseCopy();
    ReleaseAoTargets();
    ReleaseSmaaTargets();
    ReleaseStateBlockCache();
}

namespace {

// Copies the finished picture and draws it back through `shader` (c0 = texel size and the amount).
void RunPass(void* dev, void** shader, bool* failed, const char* name, const char* source, size_t length,
             const char* const* profiles, int percent, const float* extra = nullptr, int extraRegs = 2) {
    if (percent <= 0 || *failed) return;
    UINT w, h, fmt;
    void* back = BackBufferInfo(dev, &w, &h, &fmt);
    if (!back) return;
    if (!*shader && !BuildShader(dev, source, length, shader, profiles)) {
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
    if (!g_aoShader && !BuildShader(dev, kAoSource, sizeof(kAoSource) - 1, &g_aoShader, kProfilesAo)) {
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
