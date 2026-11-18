// A small stand-in for the part of HLSL that the shader bodies in src/ui/smaa_shaders.h use, so the same text compiles
// as C++ on the host: vector types, a few intrinsics and tex2Dlod on 8-bit pictures with Direct3D 9 addressing (texel
// centres at (i + 0.5) / size, clamp at the borders, bilinear weights with 8 fractional bits like a video card).
// Everything lives in namespace hlsl: the bodies are included inside it, so `abs`, `max`... are these, not the C ones.
#pragma once
#include <math.h>
#include <stdint.h>
#include <vector>

namespace hlsl {

struct float2 {
    float x, y;
    float2() = default;
    float2(float a, float b) : x(a), y(b) {}
};
struct float3 {
    float x, y, z;
    float3() = default;
    float3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct float4 {
    union {
        struct { float x, y, z, w; };
        float2 xy;
        float3 xyz;
    };
    float4() = default;
    float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    float4(float2 v, float c, float d) : x(v.x), y(v.y), z(c), w(d) {}
};

inline float2 operator+(float2 a, float2 b) { return float2(a.x + b.x, a.y + b.y); }
inline float2 operator-(float2 a, float2 b) { return float2(a.x - b.x, a.y - b.y); }
inline float2 operator*(float2 a, float2 b) { return float2(a.x * b.x, a.y * b.y); }
inline float2 operator*(float2 a, float s) { return float2(a.x * s, a.y * s); }
inline float2 operator*(float s, float2 a) { return float2(a.x * s, a.y * s); }
inline float2 operator/(float2 a, float s) { return float2(a.x / s, a.y / s); }
inline float2 operator+(float2 a, float s) { return float2(a.x + s, a.y + s); }
inline float3 operator-(float3 a, float3 b) { return float3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline float4 operator+(float4 a, float4 b) { return float4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline float4 operator*(float4 a, float s) { return float4(a.x * s, a.y * s, a.z * s, a.w * s); }
inline float4 operator/(float4 a, float s) { return float4(a.x / s, a.y / s, a.z / s, a.w / s); }

inline float abs(float v) { return fabsf(v); }
inline float3 abs(float3 v) { return float3(fabsf(v.x), fabsf(v.y), fabsf(v.z)); }
inline float max(float a, float b) { return a > b ? a : b; }
inline float min(float a, float b) { return a < b ? a : b; }
inline float floor(float v) { return floorf(v); }
inline float step(float edge, float v) { return v >= edge ? 1.f : 0.f; }
inline float saturate(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float dot(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }
inline float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// An A8R8G8B8 picture: 4 bytes per pixel in the order red, green, blue, alpha (what .x .y .z .w read).
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    Image() = default;
    Image(int width, int height) : w(width), h(height), px((size_t)width * height * 4, 0) {}
    uint8_t* at(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
    const uint8_t* clamped(int x, int y) const {
        x = x < 0 ? 0 : x >= w ? w - 1 : x;
        y = y < 0 ? 0 : y >= h ? h - 1 : y;
        return at(x, y);
    }
};

struct Sampler {
    const Image* image = nullptr;
    bool linear = false;
};

inline float4 tex2Dlod(const Sampler& s, float4 uv) {
    const Image& im = *s.image;
    const float fx = uv.x * (float)im.w - 0.5f, fy = uv.y * (float)im.h - 0.5f;
    if (!s.linear) {
        const uint8_t* p = im.clamped((int)floorf(fx + 0.5f), (int)floorf(fy + 0.5f));
        return float4(p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, p[3] / 255.f);
    }
    const int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    const float wx = floorf((fx - (float)x0) * 256.f + 0.5f) / 256.f, wy = floorf((fy - (float)y0) * 256.f + 0.5f) / 256.f;
    const uint8_t *a = im.clamped(x0, y0), *b = im.clamped(x0 + 1, y0), *c = im.clamped(x0, y0 + 1), *d = im.clamped(x0 + 1, y0 + 1);
    float out[4];
    for (int i = 0; i < 4; ++i)
        out[i] = ((a[i] * (1.f - wx) + b[i] * wx) * (1.f - wy) + (c[i] * (1.f - wx) + d[i] * wx) * wy) / 255.f;
    return float4(out[0], out[1], out[2], out[3]);
}

// Runs a pixel shader over a whole render target of 8 bits per channel (the output is rounded like a video card does).
template <class Shader>
inline void RunPass(Image& target, Shader shader) {
    for (int y = 0; y < target.h; ++y)
        for (int x = 0; x < target.w; ++x) {
            const float4 v = shader(float2(((float)x + 0.5f) / (float)target.w, ((float)y + 0.5f) / (float)target.h));
            const float c[4] = {v.x, v.y, v.z, v.w};
            uint8_t* p = target.at(x, y);
            for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(int)(saturate(c[i]) * 255.f + 0.5f);
        }
}

}  // namespace hlsl
