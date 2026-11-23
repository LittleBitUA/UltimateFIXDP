// A DDS texture with one level is given its whole chain of smaller levels in memory, without the video card.
//
// Why here and not through D3DX: asked for "all levels", D3DX decodes, filters and packs every level and talks to the device
// for each one. The game's device is shared between threads, and every such call from the loader thread waits while the picture's
// thread sits in Present. A player's log (issue #3, a 60 Hz screen) had 11.3 of the 13 seconds of a loading screen in that: about
// three frames per texture. With the chain built here the texture goes to D3DX once, exactly like a file that came with its levels.
//
// Formats: DXT1 (with its one-bit transparency), DXT3, DXT5 and plain 32-bit. Anything else gives null and the caller keeps its
// old way. The levels are a 2x2 box average, as D3DX_FILTER_BOX; the colour of a block is packed between the two ends of the
// block's colour box (the quick way of real-time DXT packers: fine for the smaller levels, which are seen far away).
// No Windows headers and no C runtime: tests/mipgen_test.cpp runs it on the host.
#pragma once
#include <stdint.h>

namespace ufx {
namespace mip {

typedef uint8_t u8;
typedef uint32_t u32;

inline void From565(u32 c, u8* o) {
    const u32 r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    o[0] = (u8)((r << 3) | (r >> 2));
    o[1] = (u8)((g << 2) | (g >> 4));
    o[2] = (u8)((b << 3) | (b >> 2));
}
inline u32 To565(const int* c) {
    const int r = c[0] < 0 ? 0 : c[0] > 255 ? 255 : c[0], g = c[1] < 0 ? 0 : c[1] > 255 ? 255 : c[1], b = c[2] < 0 ? 0 : c[2] > 255 ? 255 : c[2];
    return (u32)((((r * 31 + 127) / 255) << 11) | (((g * 63 + 127) / 255) << 5) | ((b * 31 + 127) / 255));
}

// The colour half of a block (8 bytes) into 16 pixels of r, g, b, a. `oneBit`: DXT1, where c0 <= c1 means three colours and a
// transparent one.
inline void DecodeColor(const u8* b, bool oneBit, u8 (*px)[4]) {
    const u32 c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
    u8 pal[4][4];
    From565(c0, pal[0]);
    From565(c1, pal[1]);
    pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
    if (!oneBit || c0 > c1) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (u8)((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = (u8)((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
    } else {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (u8)((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = 0;
        }
        pal[3][3] = 0;
    }
    const u32 bits = b[4] | (b[5] << 8) | (b[6] << 16) | ((u32)b[7] << 24);
    for (int i = 0; i < 16; ++i) {
        const u8* p = pal[(bits >> (i * 2)) & 3];
        px[i][0] = p[0];
        px[i][1] = p[1];
        px[i][2] = p[2];
        px[i][3] = p[3];
    }
}
inline void DecodeAlpha3(const u8* b, u8 (*px)[4]) {  // DXT3: four bits each
    for (int i = 0; i < 16; ++i) px[i][3] = (u8)(((b[i >> 1] >> ((i & 1) * 4)) & 15) * 17);
}
inline void DecodeAlpha5(const u8* b, u8 (*px)[4]) {  // DXT5: two ends and three bits each
    int a[8];
    a[0] = b[0];
    a[1] = b[1];
    if (a[0] > a[1]) {
        for (int k = 1; k < 7; ++k) a[k + 1] = ((7 - k) * a[0] + k * a[1] + 3) / 7;
    } else {
        for (int k = 1; k < 5; ++k) a[k + 1] = ((5 - k) * a[0] + k * a[1] + 2) / 5;
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t bits = 0;
    for (int k = 0; k < 6; ++k) bits |= (uint64_t)b[2 + k] << (8 * k);
    for (int i = 0; i < 16; ++i) px[i][3] = (u8)a[(bits >> (i * 3)) & 7];
}

// 16 pixels into the colour half of a block. With `oneBit`, pixels with an alpha under 128 become the transparent index.
inline void EncodeColor(const u8 (*px)[4], bool oneBit, u8* out) {
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    bool holes = false;
    int solid = 0;
    for (int i = 0; i < 16; ++i) {
        if (oneBit && px[i][3] < 128) {
            holes = true;
            continue;
        }
        ++solid;
        for (int k = 0; k < 3; ++k) {
            if (px[i][k] < lo[k]) lo[k] = px[i][k];
            if (px[i][k] > hi[k]) hi[k] = px[i][k];
        }
    }
    if (!solid) {  // all of it transparent
        out[0] = out[1] = out[2] = out[3] = 0;
        out[4] = out[5] = out[6] = out[7] = 0xFF;
        return;
    }
    // The ends of the box, taken along the diagonal the colours really lie on: red against green and blue tells which corners.
    int cov[2] = {0, 0};
    const int mid[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
    for (int i = 0; i < 16; ++i) {
        if (oneBit && px[i][3] < 128) continue;
        cov[0] += (px[i][0] - mid[0]) * (px[i][1] - mid[1]);
        cov[1] += (px[i][2] - mid[2]) * (px[i][1] - mid[1]);
    }
    int a[3] = {hi[0], hi[1], hi[2]}, z[3] = {lo[0], lo[1], lo[2]};
    if (cov[0] < 0) { a[0] = lo[0]; z[0] = hi[0]; }
    if (cov[1] < 0) { a[2] = lo[2]; z[2] = hi[2]; }
    if (!holes) {  // a little inside the box: the two middle colours then cover more of it
        for (int k = 0; k < 3; ++k) {
            const int inset = (a[k] - z[k]) / 16;
            a[k] -= inset;
            z[k] += inset;
        }
    }
    u32 c0 = To565(a), c1 = To565(z);
    u8 pal[4][3];
    int count = 4;
    if (holes) {  // three colours and the transparent one: c0 <= c1
        if (c0 > c1) { const u32 t = c0; c0 = c1; c1 = t; }
        From565(c0, pal[0]);
        From565(c1, pal[1]);
        for (int k = 0; k < 3; ++k) pal[2][k] = (u8)((pal[0][k] + pal[1][k]) / 2);
        count = 3;
    } else {
        if (c0 < c1) { const u32 t = c0; c0 = c1; c1 = t; }
        From565(c0, pal[0]);
        From565(c1, pal[1]);
        if (c0 == c1) {
            count = 1;  // one colour: index 0 in every mode
        } else {
            for (int k = 0; k < 3; ++k) {
                pal[2][k] = (u8)((2 * pal[0][k] + pal[1][k] + 1) / 3);
                pal[3][k] = (u8)((pal[0][k] + 2 * pal[1][k] + 1) / 3);
            }
        }
    }
    u32 bits = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        if (oneBit && px[i][3] < 128) {
            best = 3;
        } else {
            int bestD = 1 << 30;
            for (int p = 0; p < count; ++p) {
                const int dr = px[i][0] - pal[p][0], dg = px[i][1] - pal[p][1], db = px[i][2] - pal[p][2];
                const int d = dr * dr + dg * dg + db * db;
                if (d < bestD) { bestD = d; best = p; }
            }
        }
        bits |= (u32)best << (i * 2);
    }
    out[0] = (u8)c0;
    out[1] = (u8)(c0 >> 8);
    out[2] = (u8)c1;
    out[3] = (u8)(c1 >> 8);
    out[4] = (u8)bits;
    out[5] = (u8)(bits >> 8);
    out[6] = (u8)(bits >> 16);
    out[7] = (u8)(bits >> 24);
}
inline void EncodeAlpha3(const u8 (*px)[4], u8* out) {
    for (int i = 0; i < 8; ++i) out[i] = 0;
    for (int i = 0; i < 16; ++i) out[i >> 1] |= (u8)(((px[i][3] + 8) / 17) << ((i & 1) * 4));
}
inline void EncodeAlpha5(const u8 (*px)[4], u8* out) {
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; ++i) {
        if (px[i][3] < lo) lo = px[i][3];
        if (px[i][3] > hi) hi = px[i][3];
    }
    out[0] = (u8)hi;
    out[1] = (u8)lo;
    uint64_t bits = 0;
    if (hi > lo) {
        int a[8];
        a[0] = hi;
        a[1] = lo;
        for (int k = 1; k < 7; ++k) a[k + 1] = ((7 - k) * hi + k * lo + 3) / 7;
        for (int i = 0; i < 16; ++i) {
            int best = 0, bestD = 1 << 30;
            for (int p = 0; p < 8; ++p) {
                const int d = px[i][3] > a[p] ? px[i][3] - a[p] : a[p] - px[i][3];
                if (d < bestD) { bestD = d; best = p; }
            }
            bits |= (uint64_t)best << (i * 3);
        }
    }
    for (int k = 0; k < 6; ++k) out[2 + k] = (u8)(bits >> (8 * k));
}

enum Kind { kNone = 0, kDxt1 = 1, kDxt3 = 3, kDxt5 = 5, kRgb32 = 32 };

// What the header says: the kind of the picture, 0 when it is not one this file packs.
inline int KindOf(const u32* dds) {
    const u32 flags = dds[20], fourcc = dds[21];
    if (flags & 4) return fourcc == 0x31545844u ? kDxt1 : fourcc == 0x33545844u ? kDxt3 : fourcc == 0x35545844u ? kDxt5 : kNone;
    return (flags & 0x40) && dds[22] == 32 ? kRgb32 : kNone;
}
inline u32 LevelBytes(int kind, u32 w, u32 h) {
    if (kind == kRgb32) return w * h * 4;
    return ((w + 3) / 4) * ((h + 3) / 4) * (kind == kDxt1 ? 8u : 16u);
}

// The DDS `data` of `size` bytes with one level -> a new DDS with every level down to 1x1, in memory from `alloc` (the caller
// frees it with `release`); `outSize` gets its size. Null: not a picture this packs, or no memory.
inline u8* BuildChain(const void* data, u32 size, u32* outSize, void* (*alloc)(u32), void (*release)(void*)) {
    const u32* dds = (const u32*)data;
    if (size < 128 || dds[0] != 0x20534444u || dds[1] != 124) return nullptr;
    const u32 h0 = dds[3], w0 = dds[4];
    const int kind = KindOf(dds);
    if (!kind || !w0 || !h0 || w0 > 4096 || h0 > 4096 || (kind != kRgb32 && ((w0 | h0) & 3))) return nullptr;
    const u32 top = LevelBytes(kind, w0, h0);
    if (size < 128 + top) return nullptr;
    u32 levels = 1, total = 128 + top;
    for (u32 w = w0, h = h0; w > 1 || h > 1;) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        total += LevelBytes(kind, w, h);
        ++levels;
    }
    u8* out = (u8*)alloc(total);
    u8* a = (u8*)alloc(w0 * h0 * 4);
    u8* b = (u8*)alloc(w0 * h0);  // the level below the top one: a quarter of it
    if (!out || !a || !b) {
        if (out) release(out);
        if (a) release(a);
        if (b) release(b);
        return nullptr;
    }
    const u8* src = (const u8*)data;
    for (u32 i = 0; i < 128 + top; ++i) out[i] = src[i];
    u32* head = (u32*)out;
    head[2] |= 0x20000u;   // DDSD_MIPMAPCOUNT
    head[7] = levels;
    head[27] |= 0x400008u;  // DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    // the top level as pixels
    if (kind == kRgb32) {
        for (u32 i = 0; i < w0 * h0 * 4; ++i) a[i] = src[128 + i];
    } else {
        const u32 bs = kind == kDxt1 ? 8 : 16;
        const u8* blk = src + 128;
        for (u32 by = 0; by < h0 / 4; ++by)
            for (u32 bx = 0; bx < w0 / 4; ++bx, blk += bs) {
                u8 px[16][4];
                DecodeColor(blk + (bs - 8), kind == kDxt1, px);
                if (kind == kDxt3) DecodeAlpha3(blk, px);
                else if (kind == kDxt5) DecodeAlpha5(blk, px);
                for (int i = 0; i < 16; ++i) {
                    u8* d = a + ((by * 4 + (u32)(i >> 2)) * w0 + bx * 4 + (u32)(i & 3)) * 4;
                    d[0] = px[i][0];
                    d[1] = px[i][1];
                    d[2] = px[i][2];
                    d[3] = px[i][3];
                }
            }
    }
    u8* cur = a;
    u8* next = b;
    u8* dst = out + 128 + top;
    for (u32 w = w0, h = h0; w > 1 || h > 1;) {
        const u32 nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        const u32 sx = w > 1 ? 1 : 0, sy = h > 1 ? 1 : 0;
        for (u32 y = 0; y < nh; ++y)
            for (u32 x = 0; x < nw; ++x) {
                const u8* p00 = cur + ((y << sy) * w + (x << sx)) * 4;
                const u8* p01 = p00 + sx * 4;
                const u8* p10 = p00 + sy * w * 4;
                const u8* p11 = p10 + sx * 4;
                u8* d = next + (y * nw + x) * 4;
                for (int k = 0; k < 4; ++k) d[k] = (u8)((p00[k] + p01[k] + p10[k] + p11[k] + 2) >> 2);
            }
        if (kind == kRgb32) {
            for (u32 i = 0; i < nw * nh * 4; ++i) dst[i] = next[i];
        } else {
            const u32 bs = kind == kDxt1 ? 8 : 16;
            u8* blk = dst;
            for (u32 by = 0; by < (nh + 3) / 4; ++by)
                for (u32 bx = 0; bx < (nw + 3) / 4; ++bx, blk += bs) {
                    u8 px[16][4];
                    for (int i = 0; i < 16; ++i) {  // past the edge of a small level: its last pixel again
                        u32 x = bx * 4 + (u32)(i & 3), y = by * 4 + (u32)(i >> 2);
                        if (x >= nw) x = nw - 1;
                        if (y >= nh) y = nh - 1;
                        const u8* s = next + (y * nw + x) * 4;
                        px[i][0] = s[0];
                        px[i][1] = s[1];
                        px[i][2] = s[2];
                        px[i][3] = s[3];
                    }
                    if (kind == kDxt3) EncodeAlpha3(px, blk);
                    else if (kind == kDxt5) EncodeAlpha5(px, blk);
                    EncodeColor(px, kind == kDxt1, blk + (bs - 8));
                }
        }
        dst += LevelBytes(kind, nw, nh);
        u8* t = cur;
        cur = next;
        next = t;
        w = nw;
        h = nh;
    }
    release(a);
    release(b);
    *outSize = total;
    return out;
}

}  // namespace mip
}  // namespace ufx
