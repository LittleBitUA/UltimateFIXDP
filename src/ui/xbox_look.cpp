// The Xbox 360 look ([Graphics] XboxLook): the parts of the console's picture the PC version does not have.
//
// 1. Scene grading, in the final composite. FUN_006E9F60 hands the tone-map pixel shader a contrast pair (c11 = k from
//    ENV+18h, c12 = 1 - 2k), a chroma value (c13 from ENV+1Ch) and a colour offset (c14 from ENV+38h) every frame; the ENV
//    block (CRdPost+1F8h) gets them from a table by hour and weather set (FUN_0066DBA0, 16-bit values / 100), and
//    FUN_0072A1C0 / FUN_0072A380 can override single values. No PC shader reads them: in the blob of 32 tone-map variants
//    at BCEE68h the variants n and n+16 are byte for byte the same, so the fifth switch of the variant index (which the
//    game still sets, FUN_006EA8E0) selects nothing any more. Here the game's composite shader (variant 30, recognised by
//    its byte code) is swapped for one draw for our own copy of it with that tail put back.
//    What the tail looked like on the console is NOT known from our material (the console's shader microcode is not in the
//    dump we have). ZachFix describes it as contrast, then a mix with the luma (weights 0.30 0.59 0.11) by the chroma
//    value, then the colour offset added; the order and the luma weights are taken from that description, and the two
//    formulas that can be read in more than one way are settings (XboxContrastFormula, XboxChromaFormula).
//    The values are authored in hundredths. The table is loaded from a file, but FUN_005D8720 (a second writer of the
//    same ENV block) has two literal sets in its code; the formulas differ a lot there, so these are scenes to compare in:
//    - a flash the game starts at random intervals (FUN_005DF740 sets the factor at 1437514h to 1, it then decays; most
//      likely lightning): contrast blends towards 0.40 (5DA3C8h, double 40.0 at 7733F0h) and the exposure key towards
//      0.70 (5DA0A4h). With k = 0.40: formula 1 gives (x - 0.4) * 0.2, formula 2 x * 0.2 + 0.4, formula 3 (x - 0.4) * 5;
//    - one hard-coded state (5D8D18h..5D8E20h): contrast 0.30, chroma 0.80, colour offset +0.04 -0.04 -0.04, exposure
//      key 0.28. A chroma of 0.80 is nearly grey with chroma formula 1 and a mild fade with formula 2.
// 2. The TV gamma of the console on the finished picture (XboxGamma): sRGB decoded, BT.709 encoded. A general property of
//    the console's video output, not confirmed for this game.
// 3. Soft highlights (XboxShoulder): not a console feature, it only shares the shader.
// The exposure factors (XboxExposure) are two operand patches in graphics_tweaks.cpp and do not depend on any of this.
// 4. The game's own 4-tap filter ([Graphics] GameAliasFilter), which works with or without the Xbox look: variant 31 of the
//    same blob (sites.h, kCompositeAliasShader) is variant 30 plus four diagonal taps of the scene at uv +- c17 (g_vAlias),
//    averaged. The PC port never selects it, though it still sets c17 every frame. Without the grading the composite is drawn
//    with variant 31's own byte code from DP.exe (a shader object of ours, made from the game's bytes once: so nothing depends
//    on the game's table entry for index 31, and frames with another variant, the title among them, stay as they are); with
//    the grading our copy below gets the same taps. Mode 2 only sets c17 to a quarter of an output pixel for that draw.
//    Whether the Xbox 360 drew with it is not known (the console's capture has not been checked for it yet).
// 5. Smooth upscaling of the bloom and the depth of field ([Graphics] SmoothBloom), with or without the Xbox look: the
//    composite reads the bloom (s1) and the blurred scene (s3) from small buffers with one bilinear fetch each, which at a
//    high output resolution leaves the square steps of their texels in every halo and out-of-focus edge. Our copy reads
//    both with a cubic B-spline (four bilinear fetches); their sizes go into c23. Without the Xbox look the copy draws with
//    the neutral constants (no grading, no soft highlights, no colour offset), so the colours stay those of the PC.
//    Not a console feature: the Xbox showed the same buffers at 720p, where each of their texels covered fewer pixels.
// 6. Dither against colour banding ([Graphics] Dither), with or without the Xbox look: the composite turns the HDR scene
//    into the 8 bits of the back buffer in one step, so slow gradients (a night sky, fog, a dark room, the edge of a halo)
//    become flat steps one level apart. Our copy adds a fixed noise of +-0.5 of one level (interleaved gradient noise of
//    the pixel position, VPOS) right before that write: the steps break up into a pattern finer than the eye resolves.
//    The same noise goes into the TV gamma pass (2 above), which re-curves the 8-bit picture and quantises it again. The
//    noise does not move from frame to frame (moving noise reads as film grain), and pure black and pure white stay as
//    they are (half a level rounds back). Without the Xbox look the copy draws with the neutral constants, as for 5.
//
// Nothing here was run in the game: the HLSL below is compiled on the player's machine by d3dx9_43, and a compile error
// is written to the log and leaves the game's own shader in place.
#include "ui.h"
#include "d3d9.h"
#include "../sites.h"

namespace ufx {
bool WorldActive();  // combat_strafe.cpp
namespace ui {
namespace {

using namespace d3d;

// IDirect3DDevice9 methods the overlay's list (d3d9.h) does not name, and IDirect3DPixelShader9::GetFunction. The game
// itself calls SetPixelShaderConstantF through [vtable+1B4h] (FUN_006E2360), which is slot 109.
enum { kGetPixelShader = 108, kSetPixelShaderConstantF = 109, kGetPixelShaderConstantF = 110, kShaderGetFunction = 4 };
// More of them for the game's 4-tap filter: the shader made from the game's bytes, and what its log line reports.
enum { kCreatePixelShader = 106, kGetViewport = 48, kGetTexture = 64, kGetSamplerState = 68, kTexGetLevelDesc = 17 };
// The register of the filter's offset (g_vAlias in the game's variant 31).
enum { kAliasRegister = 17 };
// The registers read back in one call: the game's c9..c16, then up to our own c20, c21 and c23 (the texture sizes of
// the smooth upscaling; c22 is read only to keep it one call).
enum { kFirstRegister = 9, kOurRegister = 20, kSizeRegister = 23, kRegisterCount = 15 };
// IDirect3DResource9::GetType (D3DRTYPE_TEXTURE = 3), and the two stages the smooth upscaling reads.
enum { kResourceGetType = 10, kTypeTexture = 3, kBloomStage = 1, kBlurStage = 3 };
// IDirect3DSurface9::GetDesc, for the one log line of the dither (the format of the target the composite writes).
enum { kSurfaceGetDesc = 12 };

typedef ULONG(WINAPI* ReleaseFn)(void*);
typedef HRESULT(WINAPI* GetShaderFn)(void*, void**);
typedef HRESULT(WINAPI* SetShaderFn)(void*, void*);
typedef HRESULT(WINAPI* GetFunctionFn)(void*, void*, UINT*);
typedef HRESULT(WINAPI* GetConstFn)(void*, UINT, float*, UINT);
typedef HRESULT(WINAPI* SetConstFn)(void*, UINT, const float*, UINT);
typedef HRESULT(WINAPI* CreatePsFn)(void*, const DWORD*, void**);
typedef HRESULT(WINAPI* GetViewportFn)(void*, Viewport*);
typedef HRESULT(WINAPI* GetTextureFn)(void*, DWORD, void**);
typedef HRESULT(WINAPI* GetSamplerFn)(void*, DWORD, DWORD, DWORD*);
typedef HRESULT(WINAPI* LevelDescFn)(void*, UINT, u32*);
typedef DWORD(WINAPI* GetTypeFn)(void*);
typedef HRESULT(WINAPI* SetSamplerFn)(void*, DWORD, DWORD, DWORD);
typedef HRESULT(WINAPI* GetTargetFn)(void*, DWORD, void**);
typedef HRESULT(WINAPI* SurfaceDescFn)(void*, u32*);

// The game's shader, statement for statement (disassembled from BD4638h), up to `col = col * gain + bloom`: depth of
// field mix by the packed depth, the bluish night tone by the average luminance, exposure, bloom. The alpha is what the
// game writes (scene alpha times the gain plus the bloom alpha). Then the tail: soft highlights, contrast as one
// multiply-add (c20.xy, worked out on the CPU for the chosen formula), saturation, colour offset; with DITHER the noise.
const char kLookSource[] =
    "sampler s0 : register(s0);\n"   // HDR scene
    "sampler s1 : register(s1);\n"   // bloom
    "sampler s2 : register(s2);\n"   // 1x1 average luminance
    "sampler s3 : register(s3);\n"   // blurred scene
    "sampler s4 : register(s4);\n"   // packed depth
    "float4 c9 : register(c9);\n"    // bloom force
    "float4 c10 : register(c10);\n"  // exposure key
    "float4 c15 : register(c15);\n"  // depth of field distances
    "float4 c16 : register(c16);\n"  // smallest blur
    "float4 c17 : register(c17);\n"  // xy: offset of the game's 4-tap filter (read only with GAME_ALIAS)
    "float4 c20 : register(c20);\n"  // x contrast scale, y contrast offset, z saturation, w soft highlights 0..1
    "float4 c21 : register(c21);\n"  // rgb colour offset
    "float4 c23 : register(c23);\n"  // xy: size of the bloom texture (s1), zw: of the blurred scene (s3), in texels (SMOOTH_BLOOM)
    "#ifdef SMOOTH_BLOOM\n"
    // A cubic B-spline over the 4x4 texels around uv, from four bilinear fetches: each pair of neighbouring weights is
    // folded into one fetch placed between the two texels (the known fast-bicubic way). Unlike the single bilinear fetch
    // it has no kink at the texel borders, so a magnified small buffer shows no squares. The weights add up to 1 (the
    // brightness stays); the spline does not pass through the texels, so it is a little wider than the bilinear read.
    // D3D9 texel centres are at (i + 0.5) / size; the sampler must filter linearly (XboxLookBegin sees to that).
    "float4 Smooth(sampler s, float2 uv, float2 size) {\n"
    "  float2 p = uv * size - 0.5;\n"
    "  float2 f = frac(p);\n"
    "  float2 i = p - f;\n"
    "  float2 f2 = f * f;\n"
    "  float2 f3 = f2 * f;\n"
    "  float2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;\n"
    "  float2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;\n"
    "  float2 w3 = f3 / 6.0;\n"
    "  float2 w2 = 1.0 - w0 - w1 - w3;\n"
    "  float2 g0 = w0 + w1;\n"  // texels i - 1 and i; never below 1/6
    "  float2 g1 = w2 + w3;\n"  // texels i + 1 and i + 2; never below 1/6
    "  float2 t0 = (i - 0.5 + w1 / g0) / size;\n"
    "  float2 t1 = (i + 1.5 + w3 / g1) / size;\n"
    "  return g0.y * (g0.x * tex2D(s, t0) + g1.x * tex2D(s, float2(t1.x, t0.y))) +\n"
    "         g1.y * (g0.x * tex2D(s, float2(t0.x, t1.y)) + g1.x * tex2D(s, t1));\n"
    "}\n"
    "#endif\n"
    "#ifdef DITHER\n"
    // Interleaved gradient noise (Jimenez 2014) of the pixel, 0..1: neighbouring pixels differ a lot, so the pattern has
    // almost no low frequencies, and it is fixed on the screen. VPOS holds the pixel's position in the render target.
    "float Ign(float2 pixel) {\n"
    "  return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));\n"
    "}\n"
    "#endif\n"
    "float4 main(float2 uv : TEXCOORD0\n"
    "#ifdef DITHER\n"
    "            , float2 vpos : VPOS\n"
    "#endif\n"
    "            ) : COLOR {\n"
    "  float d = saturate(dot(tex2D(s4, uv).rgb, float3(65535.0, 255.0, 1.0)) * 0.0039215689) * 255.0;\n"
    "  float2 nf = c15.xz - d * c15.yw;\n"
    "  float blur = max(saturate(max(nf.x, 1.0 - nf.y)), c16.x);\n"
    "#ifdef GAME_ALIAS\n"
    // the game's own 4-tap filter, as in variant 31 (BD4A20h): the same four taps in the same order, averaged, alpha included
    "  float4 sharp = (tex2D(s0, uv - c17.xy) + tex2D(s0, uv + c17.xy * float2(1.0, -1.0)) +\n"
    "                  tex2D(s0, uv + c17.xy * float2(-1.0, 1.0)) + tex2D(s0, uv + c17.xy)) * 0.25;\n"
    "#else\n"
    "  float4 sharp = tex2D(s0, uv);\n"
    "#endif\n"
    "#ifdef SMOOTH_BLOOM\n"
    "  float3 dof = Smooth(s3, uv, c23.zw).rgb;\n"
    "  float4 glow = Smooth(s1, uv, c23.xy);\n"
    "#else\n"
    "  float3 dof = tex2D(s3, uv).rgb;\n"
    "  float4 glow = tex2D(s1, uv);\n"
    "#endif\n"
    "  float3 col = lerp(sharp.rgb, dof, blur);\n"
    "  float lum = tex2D(s2, float2(0.5, 0.5)).x;\n"
    "  float night = saturate(1.0 - (lum + 1.5) * 0.24390244);\n"
    "  col = lerp(col, dot(col, float3(0.2125, 0.7154, 0.0721)) * float3(1.05, 0.97, 1.27), night);\n"
    "  float gain = c10.x / (lum + 0.001);\n"
    "  float4 bloom = glow * c9.x;\n"
    "  col = col * gain + bloom.rgb;\n"
    // soft highlights: above 0.6 the luma follows 0.6 + 0.4 * (1 - exp(-(l - 0.6) / 0.4)), which continues with the same
    // slope and never reaches 1; the colour is scaled by the same factor, so the hue stays
    "  float l = max(dot(col, float3(0.2125, 0.7154, 0.0721)), 0.0001);\n"
    "  float soft = min(l, 0.6) + 0.4 * (1.0 - exp(max(l - 0.6, 0.0) * -2.5));\n"
    "  col = col * lerp(1.0, soft / l, c20.w);\n"
    "  col = saturate(col * c20.x + c20.y);\n"
    "  float grey = dot(col, float3(0.30, 0.59, 0.11));\n"
    "  col = grey + (col - grey) * c20.z;\n"
    "  col = col + c21.rgb;\n"
    "#ifdef DITHER\n"
    // +-0.5 of one 8-bit level, the same for the three channels (no coloured noise); the alpha is left as the game writes it
    "  col = col + (Ign(vpos) - 0.5) * 0.0039215689;\n"
    "#endif\n"
    "  return float4(col, sharp.a * gain + bloom.a);\n"
    "}\n";
// The same text compiled with GAME_ALIAS defined is the copy for the game's 4-tap filter, with SMOOTH_BLOOM the one with
// the smooth upscaling, with DITHER the one with the dither. The eight copies are indexed by these bits and built when
// first needed; the bits go by importance (XboxLookBegin gives up the lowest first when a copy cannot be built).
const char kAliasDefine[] = "#define GAME_ALIAS 1\n";
const char kSmoothDefine[] = "#define SMOOTH_BLOOM 1\n";
const char kDitherDefine[] = "#define DITHER 1\n";
enum { kCopySmooth = 1, kCopyDither = 2, kCopyAlias = 4, kCopyKinds = 8 };
char g_copySource[sizeof(kAliasDefine) - 1 + sizeof(kSmoothDefine) - 1 + sizeof(kDitherDefine) - 1 + sizeof(kLookSource)];
// The game draws the composite with a vs_3_0 vertex shader, and Direct3D 9 pairs that only with a ps_3_0 pixel shader.
const char* const kProfilesLook[] = {"ps_3_0", nullptr};

// Per channel: the sRGB curve of a PC monitor undone, the BT.709 curve of a TV applied. c0.z = strength. The curve
// stretches the dark levels of the 8-bit picture apart, so its result gets the composite's dither again, scaled by c1.x
// (one 8-bit level with Dither=1, 0 without). ps_2_x has no VPOS: the pixel is uv / texel size (RunPass draws the copy
// 1:1 over the back buffer, c0.xy = 1 / its size).
const char kGammaSource[] =
    "sampler s0 : register(s0);\n"
    "float4 c0 : register(c0);\n"
    "float4 c1 : register(c1);\n"  // x: dither amount (1/255 or 0)
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float3 c = tex2D(s0, uv).rgb;\n"
    "  float3 lin = lerp(c / 12.92, pow(abs((c + 0.055) / 1.055), 2.4), step(0.04045, c));\n"
    "  float3 tv = lerp(lin * 4.5, 1.099 * pow(max(lin, 0.000001), 0.45) - 0.099, step(0.018, lin));\n"
    "  float2 pixel = floor(uv / c0.xy);\n"
    "  float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715)))) - 0.5;\n"
    "  return float4(lerp(c, tv, c0.z) + noise * c1.x, 1.0);\n"
    "}\n";
const char* const kProfilesGamma[] = {"ps_2_b", "ps_2_a", "ps_2_0", nullptr};

void* g_copies[kCopyKinds];     // IDirect3DPixelShader9: our copies of the composite (kCopyAlias | kCopySmooth)
bool g_copyFailed[kCopyKinds];
bool g_lookFailed = false;
bool g_lookLogged = false;
void* g_aliasShader = nullptr;      // the game's variant 31, made from its bytes in DP.exe
bool g_aliasFailed = false;
void* g_gammaShader = nullptr;
bool g_gammaFailed = false;
bool g_gammaLogged = false;

bool Finite(float v) {
    union {
        float f;
        u32 bits;
    } b = {v};
    return (b.bits & 0x7F800000u) != 0x7F800000u;
}

// Game data goes straight into shader constants: a NaN or a wild value must not get there.
float Clamp(float v, float lo, float hi) {
    if (!Finite(v) || v < lo) return lo;
    return v > hi ? hi : v;
}

int Thousandths(float v) {
    v = Clamp(v, -100000.f, 100000.f);
    return (int)(v * 1000.f + (v < 0.f ? -0.5f : 0.5f));
}

const float* Register(const float* block, int index) { return block + (index - kFirstRegister) * 4; }

// The bound pixel shader is the game's composite (either copy of it): 1000 bytes with the known hash.
u32 g_otherSeen[8];
int g_otherCount = 0;

void NoteOtherComposite(void* shader) {
    UINT size = 0;
    GetFunctionFn getFunction = Method<GetFunctionFn>(shader, kShaderGetFunction);
    if (FAILED(getFunction(shader, nullptr, &size)) || size == 0 || size > 16384) return;
    u8* code = (u8*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!code) return;
    if (SUCCEEDED(getFunction(shader, code, &size))) {
        const u32 h = Fnv1a(code, size);
        bool known = false;
        for (int i = 0; i < g_otherCount; ++i) known = known || g_otherSeen[i] == h;
        if (!known && h == 0xD730D051u) {  // the game's plain copy (texld oC0, v0, s0): frames with no post effects, e.g. the title
            g_otherSeen[g_otherCount < 8 ? g_otherCount++ : 7] = h;
            Log("xboxlook: a frame with the game's plain copy instead of the tone map (no post effects there in the game either)");
            known = true;
        }
        if (!known && g_otherCount < 8 && !g_cfg.xboxLook) {
            // the smooth upscaling or the dither alone (on by default) only notes it: the files are for the Xbox look's support
            g_otherSeen[g_otherCount++] = h;
            Log("composite: this scene's composite is another shader (%u bytes, hash %08X), drawn as the game does", size, h);
        } else if (!known && g_otherCount < 8) {
            g_otherSeen[g_otherCount++] = h;
            wchar_t path[MAX_PATH + 40];
            wsprintfW(path, L"%scomposite_%08x.bin", g_paths.dataDir, h);
            HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD put = 0;
            if (f != INVALID_HANDLE_VALUE) {
                WriteFile(f, code, size, &put, nullptr);
                CloseHandle(f);
            }
            Log("xboxlook: this scene's composite is another shader (%u bytes, hash %08X), drawn as the game does; saved as %S", size, h, path);
        }
    }
    VirtualFree(code, 0, MEM_RELEASE);
}

// Which of the game's composites is bound: 30 (the one the PC draws), 31 (the same with the game's 4-tap filter), else 0.
int CompositeVariant(void* shader) {
    u8 code[kCompositeAliasShaderSize];
    UINT size = 0;
    GetFunctionFn getFunction = Method<GetFunctionFn>(shader, kShaderGetFunction);
    if (FAILED(getFunction(shader, nullptr, &size)) || (size != kCompositeShaderSize && size != kCompositeAliasShaderSize)) return 0;
    if (FAILED(getFunction(shader, code, &size))) return 0;
    if (size == kCompositeShaderSize && Fnv1a(code, size) == kCompositeShaderFnv) return 30;
    if (size == kCompositeAliasShaderSize && Fnv1a(code, size) == kCompositeAliasShaderFnv) return 31;
    return 0;
}

int Rounded(float v) { return (int)(Clamp(v, 0.f, 100000.f) + 0.5f); }

// The offset of the 4-tap filter for this one draw. Mode 1 keeps the game's c17 (0.5 / the size the game takes, set every
// frame by FUN_006EAA40). Mode 2 puts a quarter of an output pixel there: with RenderScale each tap's bilinear footprint then
// lies inside the output pixel and the four together take more of the scene's pixels behind it: with linear sampling exactly
// the 2x2 box at 200% and the 4x4 box at 400%, at other scales a weighted footprint about as wide as the output pixel (not
// an exact box), and at 100% only a slight softening. zw are kept as the game wrote them.
void AliasOffsetBegin(void* dev, LookSaved* saved, bool graded) {
    saved->offset = false;
    float game[4] = {0.f, 0.f, 0.f, 0.f};
    const bool read = SUCCEEDED(Method<GetConstFn>(dev, kGetPixelShaderConstantF)(dev, kAliasRegister, game, 1));
    Viewport vp = {};
    const bool sized = SUCCEEDED(Method<GetViewportFn>(dev, kGetViewport)(dev, &vp)) && vp.width && vp.height;
    if (g_cfg.gameAlias == 2 && read && sized) {
        const float ours[4] = {0.25f / (float)vp.width, 0.25f / (float)vp.height, game[2], game[3]};
        if (SUCCEEDED(Method<SetConstFn>(dev, kSetPixelShaderConstantF)(dev, kAliasRegister, ours, 1))) {
            saved->offset = true;
            for (int i = 0; i < 4; ++i) saved->aliasOffset[i] = game[i];
        }
    }
    // One line whenever the way it is drawn changes (mode, path, offset), at most 12 per run: the game's c17 as the size it
    // is half a pixel of, the scene texture and how the game samples it (D3DTEXF: 1 point, 2 linear).
    static int lastKey = -1, lines = 0;
    const int key = g_cfg.gameAlias * 8 + (graded ? (g_cfg.xboxLook ? 4 : 2) : 0) + (saved->offset ? 1 : 0);
    if (key == lastKey || lines >= 12) return;
    lastKey = key;
    ++lines;
    u32 desc[8] = {};  // Format, Type, Usage, Pool, MultiSampleType, MultiSampleQuality, Width, Height
    void* scene = nullptr;
    if (SUCCEEDED(Method<GetTextureFn>(dev, kGetTexture)(dev, 0, &scene)) && scene) {
        Method<LevelDescFn>(scene, kTexGetLevelDesc)(scene, 0, desc);
        Method<ReleaseFn>(scene, kRelease)(scene);
    }
    DWORD minFilter = 0, magFilter = 0;
    Method<GetSamplerFn>(dev, kGetSamplerState)(dev, 0, SAMP_MINFILTER, &minFilter);
    Method<GetSamplerFn>(dev, kGetSamplerState)(dev, 0, SAMP_MAGFILTER, &magFilter);
    const int gameW = read && game[0] > 0.f ? Rounded(0.5f / game[0]) : 0, gameH = read && game[1] > 0.f ? Rounded(0.5f / game[1]) : 0;
    Log("composite: the game's own 4-tap filter is on (mode %d, %s), offset %s; the game's c17 is half a pixel of %dx%d, "
        "scene %ux%u, output %ux%u, s0 filter min %u mag %u",
        g_cfg.gameAlias,
        graded ? (g_cfg.xboxLook ? "the mod's copy with the Xbox grading" : "the mod's copy without grading, for the smooth bloom or the dither")
               : "the game's variant 31",
        saved->offset ? "a quarter of an output pixel" : (g_cfg.gameAlias == 2 ? "the game's (the mod's could not be set)" : "the game's"),
        gameW, gameH, desc[6], desc[7], (u32)vp.width, (u32)vp.height, (u32)minFilter, (u32)magFilter);
}

void AliasOffsetEnd(void* dev, const LookSaved* saved) {
    if (saved->offset) Method<SetConstFn>(dev, kSetPixelShaderConstantF)(dev, kAliasRegister, saved->aliasOffset, 1);
}

// c20 and c21 for our shader from the game's c11..c14 and the settings. The strength scales the parameters, so 0 gives
// the neutral set (scale 1, offset 0, saturation 1, nothing added). With the Xbox look off (the copy draws only for the
// smooth upscaling or the dither) the whole set is neutral, soft highlights and colour offset included: the PC colours.
void LookConstants(const float* game, float* out) {
    if (!g_cfg.xboxLook) {
        const float neutral[8] = {1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        for (int i = 0; i < 8; ++i) out[i] = neutral[i];
        return;
    }
    const float strength = (float)g_cfg.xboxGrade / 100.f;
    const float k = Clamp(Register(game, 11)[0], -1.f, 1.f);
    const float p = Clamp(Register(game, 12)[0], -1.f, 3.f);  // 1 - 2k as the game worked it out
    const float chroma = Clamp(Register(game, 13)[0], -4.f, 4.f);
    const float* add = Register(game, 14);
    float scale = 1.f, offset = 0.f, saturation = 1.f;
    if (g_cfg.xboxContrast == 1) {  // (x - k) * p
        scale = p;
        offset = -k * p;
    } else if (g_cfg.xboxContrast == 2) {  // x * p + k
        scale = p;
        offset = k;
    } else if (g_cfg.xboxContrast == 3) {  // (x - k) / p; near k = 0.5 the divisor would vanish
        const float divisor = p < 0.05f ? 0.05f : p;
        scale = 1.f / divisor;
        offset = -k / divisor;
    }
    if (g_cfg.xboxChroma == 1) saturation = 1.f - chroma;
    else if (g_cfg.xboxChroma == 2) saturation = chroma;
    out[0] = 1.f + (Clamp(scale, 0.f, 8.f) - 1.f) * strength;
    out[1] = Clamp(offset, -4.f, 4.f) * strength;
    out[2] = 1.f + (Clamp(saturation, 0.f, 4.f) - 1.f) * strength;
    out[3] = (float)g_cfg.xboxShoulder / 100.f;
    for (int i = 0; i < 3; ++i) out[4 + i] = Clamp(add[i], -1.f, 1.f) * strength;
    out[7] = 0.f;
}

// The game's numbers in the log, in thousandths: they are what decides between the formulas (is the chroma near 0 or near
// 1, is the contrast positive). They drift with the hour, so a line is written when they moved, at most every 5 seconds
// and 150 times per run.
void TraceEnv(const float* game) {
    static float last[5];
    static DWORD lastTick = 0;
    static int lines = 0;
    const float now[5] = {Register(game, 11)[0], Register(game, 13)[0], Register(game, 14)[0], Register(game, 14)[1], Register(game, 14)[2]};
    if (lines >= 150) return;
    bool moved = lines == 0;
    for (int i = 0; i < 5; ++i) {
        const float delta = Clamp(now[i], -100000.f, 100000.f) - last[i];
        if (delta > 0.01f || delta < -0.01f) moved = true;
    }
    const DWORD tick = GetTickCount();
    if (!moved || (lines && tick - lastTick < 5000)) return;
    for (int i = 0; i < 5; ++i) last[i] = Clamp(now[i], -100000.f, 100000.f);
    lastTick = tick;
    ++lines;
    Log("xboxlook: ENV in thousandths: contrast %d (pitch %d), chroma %d, colour offset %d %d %d %d, exposure %d, bloom %d",
        Thousandths(now[0]), Thousandths(Register(game, 12)[0]), Thousandths(now[1]), Thousandths(now[2]), Thousandths(now[3]),
        Thousandths(now[4]), Thousandths(Register(game, 14)[3]), Thousandths(Register(game, 10)[0]), Thousandths(Register(game, 9)[0]));
}

// One of our copies of the composite (kCopyAlias | kCopySmooth), built when first needed; null when it cannot be built.
void* CopyShader(void* dev, int kind) {
    if (g_copies[kind] || g_copyFailed[kind]) return g_copies[kind];
    char* p = g_copySource;
    if (kind & kCopyAlias) {
        __builtin_memcpy(p, kAliasDefine, sizeof(kAliasDefine) - 1);
        p += sizeof(kAliasDefine) - 1;
    }
    if (kind & kCopySmooth) {
        __builtin_memcpy(p, kSmoothDefine, sizeof(kSmoothDefine) - 1);
        p += sizeof(kSmoothDefine) - 1;
    }
    if (kind & kCopyDither) {
        __builtin_memcpy(p, kDitherDefine, sizeof(kDitherDefine) - 1);
        p += sizeof(kDitherDefine) - 1;
    }
    __builtin_memcpy(p, kLookSource, sizeof(kLookSource));  // with its terminating zero
    const size_t length = (size_t)(p - g_copySource) + sizeof(kLookSource) - 1;
    // Each kind is its own compile, so the timing line names the defines it was built with.
    char name[64];
    wsprintfA(name, "xboxlook copy%s%s%s", (kind & kCopyAlias) ? " +alias" : "", (kind & kCopySmooth) ? " +smooth" : "",
              (kind & kCopyDither) ? " +dither" : "");
    if (!BuildPixelShader(dev, g_copySource, length, &g_copies[kind], kProfilesLook, name)) {
        g_copies[kind] = nullptr;
        g_copyFailed[kind] = true;
        if (kind)
            Log("xboxlook: the copy of the composite%s%s%s could not be built (d3dx9_43, ps_3_0)",
                (kind & kCopyAlias) ? " with the game's 4-tap filter" : "", (kind & kCopySmooth) ? " with the smooth upscaling" : "",
                (kind & kCopyDither) ? " with the dither" : "");
    }
    return g_copies[kind];
}

// The sizes of the bloom (stage 1) and the blurred scene (stage 3) for the smooth upscaling, read from the textures the game
// bound for this draw (with ScalePostBuffers or KeepBlurLook they are not the 720p sizes). False when one of them is
// missing or not a plain 2D texture: that draw is not smoothed.
bool SmoothSizes(void* dev, float* sizes, u32* dims) {
    const DWORD stages[2] = {kBloomStage, kBlurStage};
    for (int i = 0; i < 2; ++i) {
        void* tex = nullptr;
        if (FAILED(Method<GetTextureFn>(dev, kGetTexture)(dev, stages[i], &tex)) || !tex) return false;
        u32 desc[8] = {};  // Format, Type, Usage, Pool, MultiSampleType, MultiSampleQuality, Width, Height
        const bool plain = Method<GetTypeFn>(tex, kResourceGetType)(tex) == kTypeTexture &&
                           SUCCEEDED(Method<LevelDescFn>(tex, kTexGetLevelDesc)(tex, 0, desc));
        Method<ReleaseFn>(tex, kRelease)(tex);
        if (!plain || desc[6] == 0 || desc[7] == 0 || desc[6] > 16384 || desc[7] > 16384) return false;
        dims[i * 2] = desc[6];
        dims[i * 2 + 1] = desc[7];
        sizes[i * 2] = (float)desc[6];
        sizes[i * 2 + 1] = (float)desc[7];
    }
    return true;
}

// The spline weighs each pair of texels through the bilinear filter, so a stage the game samples with points (or with no
// filter) gets linear filtering for this one draw; XboxLookEnd puts back what was changed. Linear and anisotropic stay.
const DWORD kSmoothStages[2] = {kBloomStage, kBlurStage};
const DWORD kSmoothFilters[2] = {SAMP_MINFILTER, SAMP_MAGFILTER};

void SmoothFiltersBegin(void* dev, LookSaved* saved) {
    saved->filterSet = 0;
    for (int i = 0; i < 4; ++i) {
        const DWORD stage = kSmoothStages[i >> 1], state = kSmoothFilters[i & 1];
        DWORD value = 2;
        if (FAILED(Method<GetSamplerFn>(dev, kGetSamplerState)(dev, stage, state, &value))) value = 2;
        saved->filters[i] = value;
        if (value < 2 && SUCCEEDED(Method<SetSamplerFn>(dev, kSetSamplerState)(dev, stage, state, 2 /*LINEAR*/)))
            saved->filterSet |= 1u << i;
    }
}

void SmoothFiltersEnd(void* dev, const LookSaved* saved) {
    for (int i = 0; i < 4; ++i)
        if (saved->filterSet & (1u << i))
            Method<SetSamplerFn>(dev, kSetSamplerState)(dev, kSmoothStages[i >> 1], kSmoothFilters[i & 1], saved->filters[i]);
}

// One line the first time the dither is drawn: the target the composite writes (D3DFMT 21 A8R8G8B8 and 22 X8R8G8B8 are the
// 8-bit ones the dither is for; on a 16-bit float target it changes nothing visible) and how the copy draws.
void DitherLog(void* dev) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    u32 desc[8] = {};  // Format, Type, Usage, Pool, MultiSampleType, MultiSampleQuality, Width, Height
    void* target = nullptr;
    if (SUCCEEDED(Method<GetTargetFn>(dev, kGetRenderTarget)(dev, 0, &target)) && target) {
        Method<SurfaceDescFn>(target, kSurfaceGetDesc)(target, desc);
        Method<ReleaseFn>(target, kRelease)(target);
    }
    Log("composite: dither against banding on (fixed interleaved gradient noise, +-0.5 of an 8-bit level); target %ux%u "
        "format %u; drawn with the mod's copy of the composite (%s)",
        desc[6], desc[7], desc[0], g_cfg.xboxLook ? "with the Xbox grading" : "no grading: the PC colours");
}

// One line when the smooth upscaling starts and whenever the buffer sizes change (a new resolution), at most 8 per run.
// The game's filters as D3DTEXF: 1 point, 2 linear, 3 anisotropic.
void SmoothLog(const u32* dims, const LookSaved* saved) {
    static u32 last[4];
    static int lines = 0;
    bool same = lines > 0;
    for (int i = 0; i < 4; ++i) same = same && last[i] == dims[i];
    if (same || lines >= 8) return;
    for (int i = 0; i < 4; ++i) last[i] = dims[i];
    ++lines;
    Log("composite: smooth upscaling on (cubic B-spline, 4 fetches each): bloom s1 %ux%u, blur s3 %ux%u; the game's filters s1 "
        "min %u mag %u, s3 min %u mag %u%s; drawn with the mod's copy of the composite (%s)",
        dims[0], dims[1], dims[2], dims[3], (u32)saved->filters[0], (u32)saved->filters[1], (u32)saved->filters[2],
        (u32)saved->filters[3], saved->filterSet ? " (point ones set to linear for this draw)" : "",
        g_cfg.xboxLook ? "with the Xbox grading" : "no grading: the PC colours");
}

}  // namespace

// Also with every strength at 0: the shader then gives the game's own picture (neutral constants), which is the way to
// check the copy against the original in the game, and the ENV numbers still reach the log. With the Xbox look off the
// copy draws only for the smooth upscaling (SmoothBloom) or the dither (Dither), with the neutral constants.
bool XboxLookBegin(void* dev, LookSaved* saved) {
    saved->offset = false;
    saved->smooth = false;
    saved->filterSet = 0;
    const bool grade = g_cfg.xboxLook != 0;
    if (g_lookFailed || (!grade && !g_cfg.smoothBloom && !g_cfg.dither)) return false;
    void* shader = nullptr;
    if (FAILED(Method<GetShaderFn>(dev, kGetPixelShader)(dev, &shader)) || !shader) return false;
    const int variant = CompositeVariant(shader);
    if (!variant) {
        // Another of the game's 32 tone-map variants (other post effects in this scene), or another mod's shader. Each one
        // is reported once and, with the Xbox look, its byte code saved as UltimateFix\composite_<hash>.bin, so it can be
        // supported later.
        NoteOtherComposite(shader);
        Method<ReleaseFn>(shader, kRelease)(shader);
        return false;
    }
    float sizes[4];
    u32 dims[4];
    bool smooth = g_cfg.smoothBloom && SmoothSizes(dev, sizes, dims);
    static bool unsized = false;
    if (g_cfg.smoothBloom && !smooth && !unsized) {
        unsized = true;
        Log("composite: the bloom (s1) or the blur (s3) of a composite is not a plain 2D texture, that one is not smoothed");
    }
    bool dither = g_cfg.dither != 0;
    if (!grade && !smooth && !dither) {
        Method<ReleaseFn>(shader, kRelease)(shader);
        return false;
    }
    // the game's 4-tap filter is on, or the game drew with it itself: the copy with the same taps
    bool alias = variant == 31 || g_cfg.gameAlias > 0;
    const int wanted = (alias ? kCopyAlias : 0) | (smooth ? kCopySmooth : 0) | (dither ? kCopyDither : 0);
    // The most of what is wanted that builds: the subsets of the wanted bits from the largest number down, so the smoothing
    // is given up first, then the dither, the filter last (the plain copy is the last try). Without the grading the filter
    // is never given up (the game's own variant 31 draws it then, ssao.cpp) and the smoothing or the dither must remain,
    // or the copy has nothing to do and the game's own shader draws.
    void* copy = nullptr;
    int kind = wanted;
    for (int sub = wanted;; sub = (sub - 1) & wanted) {
        const bool useful = grade || ((sub & kCopyAlias) == (wanted & kCopyAlias) && (sub & (kCopySmooth | kCopyDither)) != 0);
        if (useful && (copy = CopyShader(dev, sub)) != nullptr) {
            kind = sub;
            break;
        }
        if (sub == 0) break;
    }
    if (!copy && !grade) {
        Method<ReleaseFn>(shader, kRelease)(shader);
        return false;
    }
    alias = (kind & kCopyAlias) != 0;
    smooth = (kind & kCopySmooth) != 0;
    dither = (kind & kCopyDither) != 0;
    float game[kRegisterCount * 4];
    const char* why = nullptr;
    if (!copy)
        why = "the shader could not be built (d3dx9_43, ps_3_0)";
    else if (FAILED(Method<GetConstFn>(dev, kGetPixelShaderConstantF)(dev, kFirstRegister, game, kRegisterCount)))
        why = "the shader constants of the game could not be read";
    if (!why) {
        float ours[8];
        LookConstants(game, ours);
        Method<SetConstFn>(dev, kSetPixelShaderConstantF)(dev, kOurRegister, ours, 2);
        if (smooth) Method<SetConstFn>(dev, kSetPixelShaderConstantF)(dev, kSizeRegister, sizes, 1);
        if (FAILED(Method<SetShaderFn>(dev, kSetPixelShader)(dev, copy))) {
            Method<SetConstFn>(dev, kSetPixelShaderConstantF)(dev, kOurRegister, Register(game, kOurRegister), 2);
            if (smooth) Method<SetConstFn>(dev, kSetPixelShaderConstantF)(dev, kSizeRegister, Register(game, kSizeRegister), 1);
            why = "the device refused the shader";
        }
    }
    if (why) {
        g_lookFailed = true;
        Log("xboxlook: %s, the mod's copy of the composite is off: no scene grading, no smooth bloom (the game's own shader draws)", why);
        Method<ReleaseFn>(shader, kRelease)(shader);
        return false;
    }
    if (grade) TraceEnv(game);
    saved->shader = shader;
    for (int i = 0; i < 8; ++i) saved->constants[i] = Register(game, kOurRegister)[i];
    if (smooth) {
        saved->smooth = true;
        for (int i = 0; i < 4; ++i) saved->sizeRegister[i] = Register(game, kSizeRegister)[i];
        SmoothFiltersBegin(dev, saved);
        SmoothLog(dims, saved);
    }
    if (alias) AliasOffsetBegin(dev, saved, true);
    if (dither) DitherLog(dev);
    if (grade && !g_lookLogged) {
        g_lookLogged = true;
        Log("xboxlook: the final composite is drawn with the mod's shader (grading %d%%, contrast formula %d, chroma formula %d, "
            "soft highlights %d%%)", g_cfg.xboxGrade, g_cfg.xboxContrast, g_cfg.xboxChroma, g_cfg.xboxShoulder);
    }
    return true;
}

// The game keeps its own record of the bound shader (FUN_006DD990) and skips a SetPixelShader it thinks is not needed:
// the device must hold the game's shader again when its code goes on.
void XboxLookEnd(void* dev, const LookSaved* saved) {
    Method<SetShaderFn>(dev, kSetPixelShader)(dev, saved->shader);
    Method<SetConstFn>(dev, kSetPixelShaderConstantF)(dev, kOurRegister, saved->constants, 2);
    if (saved->smooth) {
        Method<SetConstFn>(dev, kSetPixelShaderConstantF)(dev, kSizeRegister, saved->sizeRegister, 1);
        SmoothFiltersEnd(dev, saved);
    }
    AliasOffsetEnd(dev, saved);
    Method<ReleaseFn>(saved->shader, kRelease)(saved->shader);
}

// The game's 4-tap filter without the grading (XboxLook off, or its copy could not run): for this one draw the game's own
// variant 31 in place of variant 30. Only variant 30 is swapped; the game's state is put back by GameAliasEnd.
bool GameAliasBegin(void* dev, LookSaved* saved) {
    saved->offset = false;
    if (g_cfg.gameAlias <= 0 || g_aliasFailed) return false;
    void* shader = nullptr;
    if (FAILED(Method<GetShaderFn>(dev, kGetPixelShader)(dev, &shader)) || !shader) return false;
    if (CompositeVariant(shader) != 30) {
        Method<ReleaseFn>(shader, kRelease)(shader);
        return false;
    }
    const char* why = nullptr;
    if (!g_aliasShader) {
        const SiteDef& s = kCompositeAliasShader;
        if (!BytesMatch(s.va, s.bytes, s.len) || Fnv1a((const u8*)(uintptr_t)s.va, kCompositeAliasShaderSize) != kCompositeAliasShaderFnv)
            why = "its byte code at BD4A20h differs from DP.exe 1.01b";
        else if (FAILED(Method<CreatePsFn>(dev, kCreatePixelShader)(dev, (const DWORD*)(uintptr_t)s.va, &g_aliasShader)) || !g_aliasShader) {
            g_aliasShader = nullptr;
            why = "the device refused to create it";
        }
    }
    if (!why && FAILED(Method<SetShaderFn>(dev, kSetPixelShader)(dev, g_aliasShader))) why = "the device refused to bind it";
    if (why) {
        g_aliasFailed = true;
        Log("composite: the game's own 4-tap filter (variant 31) is off for this run: %s", why);
        Method<ReleaseFn>(shader, kRelease)(shader);
        return false;
    }
    saved->shader = shader;
    AliasOffsetBegin(dev, saved, false);
    return true;
}

void GameAliasEnd(void* dev, const LookSaved* saved) {
    Method<SetShaderFn>(dev, kSetPixelShader)(dev, saved->shader);
    AliasOffsetEnd(dev, saved);
    Method<ReleaseFn>(saved->shader, kRelease)(saved->shader);
}

// On the finished picture, the game's interface included (the console's video output did not tell them apart); the
// mod's own menu and panels are drawn later and stay as they are. The same world test as the composite: not in the main menu.
void XboxGammaFrame(void* dev) {
    if (!g_cfg.xboxLook || g_cfg.xboxGamma <= 0 || g_gammaFailed || !WorldActive()) return;
    if (!g_gammaShader && !BuildPixelShader(dev, kGammaSource, sizeof(kGammaSource) - 1, &g_gammaShader, kProfilesGamma, "xboxlook gamma")) {
        g_gammaFailed = true;
        Log("xboxlook gamma: the shader could not be built (d3dx9_43), the TV gamma is off");
        return;
    }
    // c1 (and c2, FinalImagePass sets two registers): the dither amount; the pass's state block puts both back
    const float extra[8] = {g_cfg.dither ? 0.0039215689f : 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    FinalImagePass(dev, &g_gammaShader, &g_gammaFailed, "xboxlook gamma", kGammaSource, sizeof(kGammaSource) - 1, kProfilesGamma,
                   g_cfg.xboxGamma, extra);
    static int loggedDither = -1;
    if ((!g_gammaLogged || loggedDither != g_cfg.dither) && !g_gammaFailed) {
        g_gammaLogged = true;
        loggedDither = g_cfg.dither;
        Log("xboxlook gamma: the TV gamma pass is on for the finished picture (%d%%), dither %s", g_cfg.xboxGamma,
            g_cfg.dither ? "on" : "off");
    }
}

}  // namespace ui
}  // namespace ufx
