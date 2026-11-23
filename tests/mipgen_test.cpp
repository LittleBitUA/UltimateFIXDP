// Host test of src/mipgen.h: c++ -std=c++17 -O2 tests/mipgen_test.cpp -o /tmp/mipgen_test && /tmp/mipgen_test file.dds [out.dds]
// For each DDS given: builds the chain, decodes level 1 again and compares it with the 2x2 average of the decoded top level.
#include "../src/mipgen.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace ufx::mip;
static void* A(u32 n) { return malloc(n); }
static void R(void* p) { free(p); }
static std::vector<u8> Decode(const u8* blk, int kind, u32 w, u32 h) {
    std::vector<u8> o(w * h * 4);
    if (kind == kRgb32) { o.assign(blk, blk + w * h * 4); return o; }
    const u32 bs = kind == kDxt1 ? 8 : 16;
    for (u32 by = 0; by < (h + 3) / 4; ++by)
        for (u32 bx = 0; bx < (w + 3) / 4; ++bx, blk += bs) {
            u8 px[16][4];
            DecodeColor(blk + (bs - 8), kind == kDxt1, px);
            if (kind == kDxt3) DecodeAlpha3(blk, px); else if (kind == kDxt5) DecodeAlpha5(blk, px);
            for (int i = 0; i < 16; ++i) {
                u32 x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h) for (int k = 0; k < 4; ++k) o[(y * w + x) * 4 + k] = px[i][k];
            }
        }
    return o;
}
int main(int argc, char** argv) {
    int bad = 0;
    for (int f = 1; f < argc; ++f) {
        FILE* fp = fopen(argv[f], "rb");
        if (!fp) { printf("%s: cannot open\n", argv[f]); return 2; }
        std::vector<u8> d;
        u8 buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, fp)) > 0) d.insert(d.end(), buf, buf + n);
        fclose(fp);
        const u32* h = (const u32*)d.data();
        u32 w = h[4], hh = h[3];
        int kind = KindOf(h);
        // only the top level goes in, as the game's one-level files
        u32 topSize = 128 + LevelBytes(kind ? kind : kDxt1, w, hh);
        if (!kind || d.size() < topSize) { printf("%s: kind %d, skipped\n", argv[f], kind); continue; }
        std::vector<u8> one(d.begin(), d.begin() + topSize);
        ((u32*)one.data())[7] = 1;
        u32 outSize = 0;
        auto t0 = std::chrono::steady_clock::now();
        u8* out = BuildChain(one.data(), (u32)one.size(), &outSize, A, R);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!out) { printf("%s: BuildChain gave null\n", argv[f]); ++bad; continue; }
        const u32* oh = (const u32*)out;
        std::vector<u8> top = Decode(out + 128, kind, w, hh);
        u32 w1 = w > 1 ? w / 2 : 1, h1 = hh > 1 ? hh / 2 : 1;
        std::vector<u8> l1 = Decode(out + topSize, kind, w1, h1);
        double se[2] = {0, 0};
        for (u32 y = 0; y < h1; ++y)
            for (u32 x = 0; x < w1; ++x)
                for (int k = 0; k < 4; ++k) {
                    int ref = (top[((y * 2) * w + x * 2) * 4 + k] + top[((y * 2) * w + x * 2 + 1) * 4 + k] + top[((y * 2 + 1) * w + x * 2) * 4 + k] +
                               top[((y * 2 + 1) * w + x * 2 + 1) * 4 + k] + 2) / 4;
                    int e = ref - l1[(y * w1 + x) * 4 + k];
                    se[k == 3] += e * e;
                }
        double psnrC = se[0] ? 10 * log10(255.0 * 255.0 * w1 * h1 * 3 / se[0]) : 99, psnrA = se[1] ? 10 * log10(255.0 * 255.0 * w1 * h1 / se[1]) : 99;
        printf("%s: kind %d %ux%u -> %u levels, %u -> %u bytes, %.2f ms; level 1 against the box average: colour %.1f dB, alpha %.1f dB\n", argv[f], kind, w, hh,
               oh[7], topSize, outSize, ms, psnrC, psnrA);
        if (psnrC < 27 || psnrA < 30) ++bad;  // a quick packer on a sign of sharp letters gives about 29 dB
        if (f == 1 && argc > 2 && argv[argc - 1][0] == '=') { FILE* o = fopen(argv[argc - 1] + 1, "wb"); fwrite(out, 1, outSize, o); fclose(o); --argc; }
        free(out);
    }
    printf(bad ? "FAILED: %d\n" : "all ok\n", bad);
    return bad;
}
