// The slice of Direct3D 9 the overlay needs, called through the vtables of the game's own device.
#pragma once
#include "../base.h"

namespace ufx {
namespace d3d {

// IDirect3DDevice9 method indices
enum {
    kRelease = 2,
    kPresent = 17,
    kGetBackBuffer = 18,
    kCreateTexture = 23,
    kSetRenderTarget = 37,
    kGetRenderTarget = 38,
    kBeginScene = 41,
    kEndScene = 42,
    kSetViewport = 47,
    kSetRenderState = 57,
    kCreateStateBlock = 59,
    kSetTexture = 65,
    kSetTextureStageState = 67,
    kSetSamplerState = 69,
    kDrawPrimitiveUP = 83,
    kSetFVF = 89,
    kSetVertexShader = 92,
    kSetPixelShader = 107,
};
// IDirect3DTexture9 / IDirect3DStateBlock9
enum { kTexLockRect = 19, kTexUnlockRect = 20, kSbCapture = 4, kSbApply = 5 };

// render states
enum {
    RS_ZENABLE = 7, RS_FILLMODE = 8, RS_ZWRITEENABLE = 14, RS_ALPHATESTENABLE = 15, RS_SRCBLEND = 19,
    RS_DESTBLEND = 20, RS_CULLMODE = 22, RS_ALPHABLENDENABLE = 27, RS_FOGENABLE = 28, RS_SPECULARENABLE = 29,
    RS_STENCILENABLE = 52, RS_CLIPPING = 136, RS_LIGHTING = 137, RS_COLORWRITEENABLE = 168,
    RS_BLENDOP = 171, RS_SCISSORTESTENABLE = 174, RS_SRGBWRITEENABLE = 194, RS_SEPARATEALPHABLEND = 206,
};
// texture stage states and operations
enum { TSS_COLOROP = 1, TSS_COLORARG1 = 2, TSS_COLORARG2 = 3, TSS_ALPHAOP = 4, TSS_ALPHAARG1 = 5, TSS_ALPHAARG2 = 6,
       TSS_TEXCOORDINDEX = 11, TSS_TEXTURETRANSFORMFLAGS = 24 };
enum { TOP_DISABLE = 1, TOP_SELECTARG1 = 2, TOP_SELECTARG2 = 3, TOP_MODULATE = 4 };
enum { TA_DIFFUSE = 0, TA_CURRENT = 1, TA_TEXTURE = 2 };
// sampler states
enum { SAMP_ADDRESSU = 1, SAMP_ADDRESSV = 2, SAMP_MAGFILTER = 5, SAMP_MINFILTER = 6, SAMP_MIPFILTER = 7,
       SAMP_MIPMAPLODBIAS = 8, SAMP_MAXMIPLEVEL = 9, SAMP_SRGBTEXTURE = 11 };

constexpr u32 kFvfXyzrhwDiffuseTex1 = 0x4 | 0x40 | 0x100;
constexpr u32 kFvfXyzrhwDiffuseTex2 = 0x4 | 0x40 | 0x200;  // two sets of texture coordinates (a picture with rounded corners)

struct LockedRect {
    INT pitch;
    void* bits;
};
struct Viewport {
    DWORD x, y, width, height;
    float minZ, maxZ;
};

template <class F>
inline F Method(void* object, int index) {
    return (F)(*(void***)object)[index];
}

}  // namespace d3d
}  // namespace ufx
