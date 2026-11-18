// SMAA on the host: the lookup tables of src/smaa_lut.h and the shader texts of src/ui/smaa_shaders.h.
// The shader bodies are compiled here as C++ (tests/hlsl_host.h; the Makefile writes them out with tests/smaa_dump.cpp)
// and run on test pictures the way the video card would run them: 8-bit targets, the same filters, the same constants.
// What this proves: the technique, the tables and the texts agree with each other. What it cannot prove: that the HLSL
// compiler of d3dx9_43 accepts the texts, and how the result looks in the game.
//   test_smaa            runs the checks
//   test_smaa <folder>   also writes the test pictures there as .ppm (input, edges, weights, result) to look at
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "hlsl_host.h"
#include "smaa_lut.h"

namespace hlsl {
float4 c0, c1;
Sampler sColor, sEdges, sArea, sSearch, sWeights;
#define SMAA_BRANCH
namespace edge {
#include "smaa_edge_body.inc"
}
namespace blend {
#include "smaa_blend_body.inc"
}
#define SMAA_STEPS 4
namespace steps4 {
#include "smaa_weight_body.inc"
}
#undef SMAA_STEPS
#define SMAA_STEPS 8
namespace steps8 {
#include "smaa_weight_body.inc"
}
#undef SMAA_STEPS
#define SMAA_STEPS 16
namespace steps16 {
#include "smaa_weight_body.inc"
}
#undef SMAA_STEPS
}  // namespace hlsl

using hlsl::Image;
using namespace ufx;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { if (g_fail < 40) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } ++g_fail; } } while (0)

static uint32_t g_rng = 12345;
static uint32_t Rand(uint32_t n) { g_rng = g_rng * 1664525u + 1013904223u; return (g_rng >> 8) % n; }

// ---- the tables as the DLL uploads them ----
static Image g_area(kSmaaAreaSize, kSmaaAreaSize), g_search(kSmaaSearchSize, kSmaaSearchSize);

static void BuildTables() {
    for (int y = 0; y < kSmaaAreaSize; ++y)
        for (int x = 0; x < kSmaaAreaSize; ++x) SmaaAreaTexel(x, y, &g_area.at(x, y)[0], &g_area.at(x, y)[1]);
    for (int y = 0; y < kSmaaSearchSize; ++y)
        for (int x = 0; x < kSmaaSearchSize; ++x) SmaaSearchTexel(x, y, &g_search.at(x, y)[0], &g_search.at(x, y)[1]);
}

// ---- 1. The area table ----
static void TestAreaTable() {
    int nonZero = 0;
    for (int a = 0; a < 4; ++a)
        for (int b = 0; b < 4; ++b)
            for (int back = 0; back < kSmaaAreaBlock; ++back)
                for (int fwd = 0; fwd < kSmaaAreaBlock; ++fwd) {
                    double other, own, o2, w2;
                    SmaaArea(a, b, back, fwd, &other, &own);
                    CHECK(other >= 0.0 && own >= 0.0 && other < 0.5 && own < 0.5 && other + own < 0.5,
                          "area out of range: ends %d %d, %d back, %d forward: %f %f", a, b, back, fwd, other, own);
                    // seen from the other end of the run the picture is the same
                    SmaaArea(b, a, fwd, back, &o2, &w2);
                    CHECK(fabs(other - o2) < 1e-12 && fabs(own - w2) < 1e-12, "area not mirror symmetric: ends %d %d, %d %d", a, b, back, fwd);
                    // seen from the other side of the run the two areas change places
                    static const int flip[4] = {kSmaaEndNone, kSmaaEndOwn, kSmaaEndOther, kSmaaEndBoth};
                    SmaaArea(flip[a], flip[b], back, fwd, &o2, &w2);
                    CHECK(fabs(other - w2) < 1e-12 && fabs(own - o2) < 1e-12, "area not side symmetric: ends %d %d, %d %d", a, b, back, fwd);
                    uint8_t t0, t1;
                    SmaaAreaTexel(a * kSmaaAreaBlock + back, b * kSmaaAreaBlock + fwd, &t0, &t1);
                    CHECK(t0 <= 127 && t1 <= 127 && fabs(t0 / 255.0 - other) <= 0.5 / 255 + 1e-9 && fabs(t1 / 255.0 - own) <= 0.5 / 255 + 1e-9,
                          "area texel differs from the area: ends %d %d, %d %d", a, b, back, fwd);
                    if (t0 || t1) ++nonZero;
                    if (a == kSmaaEndNone && b == kSmaaEndNone) CHECK(other == 0.0 && own == 0.0, "a run without crossing edges must stay");
                    // an end without a crossing edge leaves its half of the run alone
                    if (a == kSmaaEndNone && back < fwd) CHECK(other == 0.0 && own == 0.0, "far half of an L shape must stay (%d, %d %d)", b, back, fwd);
                    if (a == kSmaaEndOther && b == kSmaaEndOther) CHECK(own == 0.0, "a U shape towards the other side only cuts the other side");
                }
    CHECK(nonZero > 20000, "the area table is mostly empty (%d texels)", nonZero);
    // The whole staircase step: along a Z-shaped run of n pixels each triangle is 1/2 * n/2 * 1/2 = n/8.
    for (int n = 1; n <= 40; ++n) {
        double sumOther = 0, sumOwn = 0;
        for (int back = 0; back < n; ++back) {
            double other, own;
            SmaaArea(kSmaaEndOther, kSmaaEndOwn, back, n - 1 - back, &other, &own);
            sumOther += other;
            sumOwn += own;
            // the line falls from the other side to the own side
            if (back * 2 + 1 < n) CHECK(own == 0.0 && other > 0.0, "Z run %d, pixel %d: wrong side", n, back);
            if (back * 2 + 1 > n) CHECK(other == 0.0 && own > 0.0, "Z run %d, pixel %d: wrong side", n, back);
        }
        CHECK(fabs(sumOther - n / 8.0) < 1e-9 && fabs(sumOwn - n / 8.0) < 1e-9, "Z run %d: triangles %f %f, expected %f", n, sumOther, sumOwn, n / 8.0);
    }
}

// ---- 2. The search table against the definition of the walk ----
static void TestSearchTable() {
    int valid = 0;
    for (int level = 0; level <= 32; ++level) {
        const int bits = SmaaLevelBits(level);
        if (bits < 0) continue;
        ++valid;
        const int sum = ((bits & 1) ? 21 : 0) + ((bits & 2) ? 7 : 0) + ((bits & 4) ? 3 : 0) + ((bits & 8) ? 1 : 0);
        CHECK(sum == level, "level %d decodes to bits %d", level, bits);
    }
    CHECK(valid == 16, "%d valid levels, expected 16", valid);
    // the DLL has a weight shader for these walk lengths only (ui/smaa.cpp), and so has this test
    for (const SmaaPreset& q : kSmaaPresets) CHECK(q.steps == 4 || q.steps == 8 || q.steps == 16, "preset with %d steps", q.steps);
    for (int y = 0; y < kSmaaSearchSize; ++y)
        for (int x = 0; x < kSmaaSearchSize; ++x) {
            uint8_t b, f;
            SmaaSearchTexel(x, y, &b, &f);
            CHECK((b == 0 || b == 85 || b == 170) && (f == 0 || f == 85), "search texel %d %d out of range: %d %d", x, y, b, f);
            if (x >= kSmaaSearchLevels || y >= kSmaaSearchLevels) CHECK(b == 0 && f == 0, "padding of the search table must be empty");
        }
}

// The walk itself, one pixel at a time, straight from the definition. `marks` holds the edge picture (red = left
// boundary, green = top boundary); the run goes along x for horizontal = true.
static bool Mark(const Image& marks, int x, int y, int channel) { return marks.clamped(x, y)[channel] != 0; }
static int WalkBack(const Image& m, int x, int y, bool horizontal, int limit) {
    int n = 0;
    for (;;) {
        // a crossing edge on the low boundary of the current pixel, on either side of the run
        const bool crossed = horizontal ? (Mark(m, x - n, y, 0) || Mark(m, x - n, y - 1, 0)) : (Mark(m, x, y - n, 1) || Mark(m, x - 1, y - n, 1));
        if (crossed) break;
        const bool next = horizontal ? Mark(m, x - n - 1, y, 1) : Mark(m, x, y - n - 1, 0);
        if (!next) break;
        if (++n >= limit) break;
    }
    return n;
}
static int WalkForward(const Image& m, int x, int y, bool horizontal, int limit) {
    int n = 0;
    for (;;) {
        const bool crossed = horizontal ? (Mark(m, x + n + 1, y, 0) || Mark(m, x + n + 1, y - 1, 0)) : (Mark(m, x, y + n + 1, 1) || Mark(m, x - 1, y + n + 1, 1));
        const bool next = horizontal ? Mark(m, x + n + 1, y, 1) : Mark(m, x, y + n + 1, 0);
        if (crossed || !next) break;
        if (++n >= limit) break;
    }
    return n;
}

static void SetEdgeSamplers(const Image& marks) {
    hlsl::c0 = hlsl::float4(1.f / (float)marks.w, 1.f / (float)marks.h, 1.f, 0.f);
    hlsl::sEdges.image = &marks;
    hlsl::sEdges.linear = true;
    hlsl::sArea.image = &g_area;
    hlsl::sArea.linear = false;
    hlsl::sSearch.image = &g_search;
    hlsl::sSearch.linear = false;
}

template <class Back, class Forward>
static void CheckSearch(const Image& marks, int steps, Back back, Forward forward) {
    for (int y = 0; y < marks.h; ++y)
        for (int x = 0; x < marks.w; ++x) {
            const hlsl::float2 uv(((float)x + 0.5f) / (float)marks.w, ((float)y + 0.5f) / (float)marks.h);
            for (int horizontal = 0; horizontal < 2; ++horizontal) {
                if (!Mark(marks, x, y, horizontal ? 1 : 0)) continue;
                const hlsl::float2 axis(horizontal ? 1.f : 0.f, horizontal ? 0.f : 1.f);
                // after `steps` fetches the walk back knows 2 * steps - 1 pixels; the walk forward is cut to the same
                const int wantBack = WalkBack(marks, x, y, horizontal != 0, steps * 2 - 1);
                const int wantForward = WalkForward(marks, x, y, horizontal != 0, steps * 2 - 1);
                const int gotBack = (int)back(uv, axis), gotForward = (int)forward(uv, axis);
                CHECK(gotBack == wantBack, "%d steps, %s run at %d,%d: back %d, expected %d", steps, horizontal ? "horizontal" : "vertical", x, y, gotBack, wantBack);
                CHECK(gotForward == wantForward, "%d steps, %s run at %d,%d: forward %d, expected %d", steps, horizontal ? "horizontal" : "vertical", x, y, gotForward, wantForward);
            }
        }
}

static void TestSearchShader() {
    // random marks of several densities: long runs with few crossings, and noise
    static const int runPercent[4] = {97, 90, 70, 50}, crossPercent[4] = {2, 8, 20, 50};
    for (int round = 0; round < 8; ++round) {
        Image marks(96, 96);
        for (int y = 0; y < marks.h; ++y)
            for (int x = 0; x < marks.w; ++x) {
                // rows of mostly horizontal runs in the top half, columns of vertical runs in the bottom half
                const bool top = y < marks.h / 2;
                const int run = (int)Rand(100) < runPercent[round % 4] ? 255 : 0, crossing = (int)Rand(100) < crossPercent[round % 4] ? 255 : 0;
                marks.at(x, y)[0] = (uint8_t)(top ? crossing : run);
                marks.at(x, y)[1] = (uint8_t)(top ? run : crossing);
            }
        SetEdgeSamplers(marks);
        CheckSearch(marks, 4, hlsl::steps4::SmaaSearchBack, hlsl::steps4::SmaaSearchForward);
        CheckSearch(marks, 8, hlsl::steps8::SmaaSearchBack, hlsl::steps8::SmaaSearchForward);
        CheckSearch(marks, 16, hlsl::steps16::SmaaSearchBack, hlsl::steps16::SmaaSearchForward);
    }
}

// ---- 3. The whole pass on pictures ----
struct Result {
    Image edges, weights, out;
};

static Result Smaa(const Image& in, int quality, float strength = 1.f) {
    const SmaaPreset& q = kSmaaPresets[quality];
    Result r;
    r.edges = Image(in.w, in.h);
    r.weights = Image(in.w, in.h);
    r.out = Image(in.w, in.h);
    hlsl::c0 = hlsl::float4(1.f / (float)in.w, 1.f / (float)in.h, strength, 0.f);
    hlsl::c1 = hlsl::float4(q.threshold, q.colorEdges, 0.f, 0.f);
    hlsl::sColor.image = &in;
    hlsl::sColor.linear = false;
    hlsl::RunPass(r.edges, hlsl::edge::SmaaEdges);
    SetEdgeSamplers(r.edges);
    hlsl::c0.z = strength;
    hlsl::c1 = hlsl::float4(q.cornerLoss, 0.f, 0.f, 0.f);
    if (q.steps == 4) hlsl::RunPass(r.weights, hlsl::steps4::SmaaWeights);
    else if (q.steps == 8) hlsl::RunPass(r.weights, hlsl::steps8::SmaaWeights);
    else hlsl::RunPass(r.weights, hlsl::steps16::SmaaWeights);
    hlsl::sColor.linear = true;
    hlsl::sWeights.image = &r.weights;
    hlsl::sWeights.linear = false;
    hlsl::RunPass(r.out, hlsl::blend::SmaaBlend);
    return r;
}

// A picture of two colours split by `inside(x, y)` (in pixels): `samples` x `samples` points per pixel. 1 = what the game
// draws (one point in the middle, jagged), 16 = the smooth picture the pass should come close to.
template <class Inside>
static Image Draw(int w, int h, int samples, const uint8_t* colorIn, const uint8_t* colorOut, Inside inside) {
    Image im(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hits = 0;
            for (int j = 0; j < samples; ++j)
                for (int i = 0; i < samples; ++i)
                    if (inside((double)x + ((double)i + 0.5) / samples, (double)y + ((double)j + 0.5) / samples)) ++hits;
            const double k = (double)hits / (samples * samples);
            for (int c = 0; c < 3; ++c) im.at(x, y)[c] = (uint8_t)(int)(colorOut[c] + (colorIn[c] - colorOut[c]) * k + 0.5);
            im.at(x, y)[3] = 255;
        }
    return im;
}

static double Difference(const Image& a, const Image& b, int margin, int* worst = nullptr) {
    double sum = 0;
    int most = 0;
    for (int y = margin; y < a.h - margin; ++y)
        for (int x = margin; x < a.w - margin; ++x)
            for (int c = 0; c < 3; ++c) {
                const int d = abs((int)a.at(x, y)[c] - (int)b.at(x, y)[c]);
                sum += d;
                if (d > most) most = d;
            }
    if (worst) *worst = most;
    return sum / 255.0;
}

static Image FlipX(const Image& in) {
    Image o(in.w, in.h);
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x)
            for (int c = 0; c < 4; ++c) o.at(x, y)[c] = in.at(in.w - 1 - x, y)[c];
    return o;
}
static Image FlipY(const Image& in) {
    Image o(in.w, in.h);
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x)
            for (int c = 0; c < 4; ++c) o.at(x, y)[c] = in.at(x, in.h - 1 - y)[c];
    return o;
}
static Image Transpose(const Image& in) {
    Image o(in.h, in.w);
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x)
            for (int c = 0; c < 4; ++c) o.at(y, x)[c] = in.at(x, y)[c];
    return o;
}

static std::string g_dumpDir;
static void Dump(const char* name, const char* kind, const Image& im) {
    if (g_dumpDir.empty()) return;
    const std::string path = g_dumpDir + "/" + name + "_" + kind + ".ppm";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", im.w, im.h);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) fwrite(im.at(x, y), 1, 3, f);
    fclose(f);
}

static const uint8_t kLight[3] = {230, 225, 210}, kDark[3] = {30, 40, 60};

struct Shape {
    const char* name;
    bool (*inside)(double, double);
};
// 160 x 160 pictures; the shapes stay 40 pixels away from the border (a run that reaches the border of the picture is
// read through the clamped texture addressing, which is not the same on the left and on the right).
static const Shape kShapes[] = {
    {"disc", [](double x, double y) { return (x - 80.3) * (x - 80.3) + (y - 79.7) * (y - 79.7) < 30.5 * 30.5; }},
    {"ring", [](double x, double y) { double r2 = (x - 80.3) * (x - 80.3) + (y - 79.7) * (y - 79.7); return r2 < 36 * 36 && r2 > 34.6 * 34.6; }},
    {"tilted box", [](double x, double y) { double u = (x - 80) * 0.966 + (y - 80) * 0.259, v = -(x - 80) * 0.259 + (y - 80) * 0.966; return fabs(u) < 30 && fabs(v) < 18; }},
    {"wires", [](double x, double y) {
         if (x < 44 || x > 116 || y < 44 || y > 116) return false;
         double v = y - 55 - (x - 44) * 0.07, w = y - 80 - (x - 44) * 0.3, z = x - 70 - (y - 44) * 0.12;
         return fabs(v) < 0.5 || fabs(w) < 0.75 || fabs(z) < 1.0;
     }},
    {"fan", [](double x, double y) {
         double r2 = (x - 80) * (x - 80) + (y - 80) * (y - 80);
         return fmod(atan2(y - 80, x - 80) + 10, 0.3927) < 0.19635 && r2 > 36 && r2 < 38 * 38;
     }},
};

static void TestPictures() {
    const double kPi = 3.14159265358979;
    // Straight borders at many angles. "Error" is the distance to the same border drawn with 16 x 16 points per pixel.
    // The limits are what this implementation reaches, with room to spare: they are there to notice a change for the
    // worse, not a statement about the picture in the game.
    static const double angles[] = {2, 4, 7, 11, 18, 27, 34, 45, 56, 63, 72, 79, 83, 86, 88, 94, 101, 117, 135, 153, 169, 176};
    static const double limit[3] = {0.45, 0.38, 0.38};
    for (int quality = 0; quality < 3; ++quality) {
        double before = 0, after = 0;
        for (double deg : angles) {
            const double nx = cos(deg * kPi / 180), ny = sin(deg * kPi / 180);
            auto half = [&](double x, double y) { return (x - 48.2) * nx + (y - 47.6) * ny > 0; };
            const Image jagged = Draw(96, 96, 1, kLight, kDark, half), smooth = Draw(96, 96, 16, kLight, kDark, half);
            const Result r = Smaa(jagged, quality);
            const double b = Difference(jagged, smooth, 8), a = Difference(r.out, smooth, 8);
            before += b;
            after += a;
            CHECK(a < b, "quality %d, border at %.0f degrees: error %.2f -> %.2f, not better", quality, deg, b, a);
            // close to an axis is where jagged borders are worst and where the technique is at its best
            const double fromAxis = fabs(fmod(deg + 45, 90) - 45);
            if (quality == 2 && fromAxis <= 11) CHECK(a < b * 0.25, "border at %.0f degrees: error %.2f -> %.2f, expected under a quarter", deg, b, a);
            if (quality == 2) {
                char name[32];
                snprintf(name, sizeof(name), "border%03d", (int)deg);
                Dump(name, "in", jagged);
                Dump(name, "edges", r.edges);
                Dump(name, "weights", r.weights);
                Dump(name, "out", r.out);
                Dump(name, "smooth", smooth);
            }
        }
        printf("quality %d: error of 22 straight borders %.0f -> %.0f (%.0f%%)\n", quality, before, after, 100 * after / before);
        CHECK(after < before * limit[quality], "quality %d: borders error %.1f -> %.1f, expected under %.0f%%", quality, before, after, limit[quality] * 100);
    }

    // Shapes: closer to the smooth picture, and the same result when the picture is mirrored or turned (which swaps the
    // walk back with the walk forward, the own side with the other side, and rows with columns).
    for (const Shape& sh : kShapes) {
        const Image jagged = Draw(160, 160, 1, kLight, kDark, sh.inside), smooth = Draw(160, 160, 16, kLight, kDark, sh.inside);
        for (int quality = 0; quality < 3; ++quality) {
            const Result r = Smaa(jagged, quality);
            const double b = Difference(jagged, smooth, 0), a = Difference(r.out, smooth, 0);
            CHECK(a < b * 0.6, "quality %d, %s: error %.2f -> %.2f, expected under 60%%", quality, sh.name, b, a);
            int worst = 0;
            Difference(Smaa(FlipX(jagged), quality).out, FlipX(r.out), 0, &worst);
            CHECK(worst <= 1, "quality %d, %s: mirrored left-right differs by %d", quality, sh.name, worst);
            Difference(Smaa(FlipY(jagged), quality).out, FlipY(r.out), 0, &worst);
            CHECK(worst <= 1, "quality %d, %s: mirrored top-bottom differs by %d", quality, sh.name, worst);
            Difference(Smaa(Transpose(jagged), quality).out, Transpose(r.out), 0, &worst);
            CHECK(worst <= 1, "quality %d, %s: transposed differs by %d", quality, sh.name, worst);
            if (quality == 2) {
                Dump(sh.name, "in", jagged);
                Dump(sh.name, "out", r.out);
                Dump(sh.name, "smooth", smooth);
            }
        }
    }

    // A box along the axes has nothing to smooth. The technique alone would see its corners as the ends of long steps
    // and round them; the corner check of the presets leaves the box exactly as it was (interface, text).
    {
        auto box = [](double x, double y) { return x > 50 && x < 110 && y > 60 && y < 100; };
        const Image in = Draw(160, 160, 1, kLight, kDark, box);
        for (int quality = 0; quality < 3; ++quality) {
            const Result r = Smaa(in, quality);
            int worst = 0;
            Difference(r.out, in, 0, &worst);
            CHECK(worst == 0, "quality %d: an axis-aligned box changed by %d of 200", quality, worst);
            if (quality == 2) {
                Dump("box", "in", in);
                Dump("box", "out", r.out);
            }
        }
    }

    // Noise below the threshold and smooth gradients are not edges: the picture comes back untouched.
    {
        Image in(96, 96);
        for (int y = 0; y < in.h; ++y)
            for (int x = 0; x < in.w; ++x) {
                for (int c = 0; c < 3; ++c) in.at(x, y)[c] = (uint8_t)(60 + x + y / 2 + (int)Rand(12));
                in.at(x, y)[3] = 255;
            }
        for (int quality = 0; quality < 3; ++quality) {
            const Result r = Smaa(in, quality);
            int worst = 0;
            Difference(r.out, in, 0, &worst);
            CHECK(worst == 0, "quality %d: a picture without edges changed by %d", quality, worst);
        }
    }

    // Any picture: a pixel only ever moves towards its four neighbours, and only where an edge was found.
    for (int round = 0; round < 6; ++round) {
        Image in(64, 64);
        for (int y = 0; y < in.h; ++y)
            for (int x = 0; x < in.w; ++x) {
                const int block = round < 3 ? 1 : 3;  // single-pixel noise, then blobs
                g_rng = (uint32_t)((x / block) * 7919 + (y / block) * 104729 + round * 31);
                Rand(2);
                for (int c = 0; c < 3; ++c) in.at(x, y)[c] = (uint8_t)(Rand(3) * 127);
                in.at(x, y)[3] = 255;
            }
        const Result r = Smaa(in, round % 3);
        for (int y = 1; y < in.h - 1; ++y)
            for (int x = 1; x < in.w - 1; ++x) {
                const uint8_t* w = r.weights.at(x, y);
                const uint8_t* e = r.edges.at(x, y);
                CHECK((e[1] || (!w[0] && !w[1])) && (e[0] || (!w[2] && !w[3])), "round %d: weights without an edge at %d,%d", round, x, y);
                for (int c = 0; c < 3; ++c) {
                    int lo = 255, hi = 0;
                    static const int dx[5] = {0, -1, 1, 0, 0}, dy[5] = {0, 0, 0, -1, 1};
                    for (int k = 0; k < 5; ++k) {
                        const int v = in.at(x + dx[k], y + dy[k])[c];
                        lo = v < lo ? v : lo;
                        hi = v > hi ? v : hi;
                    }
                    const int v = r.out.at(x, y)[c];
                    CHECK(v >= lo - 1 && v <= hi + 1, "round %d: pixel %d,%d left the range of its neighbours (%d, range %d..%d)", round, x, y, v, lo, hi);
                }
            }
    }

    // The strength (the Antialias percent): 0 leaves the picture alone, a half moves every pixel about half as far.
    {
        const Image in = Draw(160, 160, 1, kLight, kDark, kShapes[0].inside);
        const Result full = Smaa(in, 2, 1.f), half = Smaa(in, 2, 0.5f), none = Smaa(in, 2, 0.f);
        int worst = 0;
        Difference(none.out, in, 0, &worst);
        CHECK(worst == 0, "strength 0 changed the picture by %d", worst);
        for (int y = 0; y < in.h; ++y)
            for (int x = 0; x < in.w; ++x) {
                const int f = full.out.at(x, y)[0] - in.at(x, y)[0], h = half.out.at(x, y)[0] - in.at(x, y)[0];
                CHECK(abs(h * 2 - f) <= 3, "strength 50%% at %d,%d: moved %d, the full strength %d", x, y, h, f);
            }
    }

    // Red against green of the same brightness: only the presets that compare colours see the border.
    {
        static const uint8_t red[3] = {200, 0, 0}, green[3] = {0, 59, 0};  // 0.2126 * 200 = 0.7152 * 59
        const Image in = Draw(96, 96, 1, red, green, [](double x, double y) { return y - 48.3 - (x - 48) * 0.2 > 0; });
        for (int quality = 0; quality < 3; ++quality) {
            const Result r = Smaa(in, quality);
            int worst = 0;
            Difference(r.out, in, 8, &worst);
            if (kSmaaPresets[quality].colorEdges > 0.f) CHECK(worst > 40, "quality %d: a border between two colours of one brightness was not smoothed", quality);
            else CHECK(worst == 0, "quality %d: the brightness-only preset changed a picture of one brightness", quality);
        }
    }
}

int main(int argc, char** argv) {
    if (argc > 1) g_dumpDir = argv[1];
    BuildTables();
    TestAreaTable();
    TestSearchTable();
    TestSearchShader();
    TestPictures();
    printf("%s\n", g_fail ? "SMAA TEST FAILED" : "SMAA TEST PASSED");
    return g_fail ? 1 : 0;
}
