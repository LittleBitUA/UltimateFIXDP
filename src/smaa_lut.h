// The two lookup tables of our SMAA 1x pass (ui/smaa.cpp), computed at run time: nothing is shipped as data.
// Our own implementation of the published technique (Jimenez et al., "SMAA: Enhanced Subpixel Morphological
// Antialiasing", 2012); the layout of both tables is ours and only matches the shaders in ui/smaa_shaders.h.
// No Windows headers: tested on the host (tests/test_smaa.cpp).
//
// Terms. The edge picture marks, for every pixel, an edge on its left boundary and one on its top boundary. A "run"
// is a straight row of such marks (the top boundaries of neighbouring pixels of one row, or the left boundaries of
// neighbouring pixels of one column). A run ends where the marks stop or where a "crossing edge" meets it at a right
// angle. The pixel that carries the mark is on the "own" side of the run, its neighbour across the boundary on the
// "other" side. Everything below is written for a horizontal run; a vertical one is the same with x and y swapped.
#pragma once
#include <stdint.h>

namespace ufx {

// ---- Quality presets (SmaaQuality 0..2) --------------------------------------------------------------------------
struct SmaaPreset {
    float threshold;    // the smallest difference between two pixels (0..1) that counts as an edge
    float colorEdges;   // 1 = compare the colour channels, 0 = the brightness only (misses borders between equally bright colours)
    float cornerLoss;   // how much of its smoothing a real corner loses, 0..1 (0 = corners are not looked for)
    int steps;          // texture fetches of the walk in each direction, two pixels each: it reaches 2 * steps - 1 pixels
};
// Corners are looked for in every preset and lose all of their smoothing: the pass runs on the finished picture,
// interface and text included, where every box and letter would get rounded corners and a soft outline otherwise.
// On slanted borders and curves this costs nothing that can be measured (tests/test_smaa.cpp: the steps of a
// staircase are one pixel high along its long axis).
constexpr SmaaPreset kSmaaPresets[3] = {
    {0.15f, 0.f, 1.f, 4},   // low: 7 pixels each way, only the clearer borders, by brightness
    {0.10f, 1.f, 1.f, 8},   // medium: 15 pixels
    {0.10f, 1.f, 1.f, 16},  // high: 31 pixels
};

// ---- Area table --------------------------------------------------------------------------------------------------
// 4 x 4 blocks of 64 x 64 texels. The block is chosen by what crosses the run at its two ends, the texel inside it by
// the distance (in pixels) from the pixel to each end.
constexpr int kSmaaAreaBlock = 64;  // distances 0..63; anything longer uses the last texel
constexpr int kSmaaAreaSize = 256;

// What crosses the run at one of its ends.
enum SmaaEnd {
    kSmaaEndNone = 0,
    kSmaaEndOther = 1,  // an edge that leaves the run towards the other side
    kSmaaEndOwn = 2,    // an edge that leaves it towards the own side
    kSmaaEndBoth = 3,   // both: the run meets a line that goes straight through
};

// The idea of the technique: a jagged border is a staircase, and the true border is the straight line through the
// middles of the risers. So an end where an edge leaves towards the other side lifts the line half a pixel to that
// side, an end where it leaves towards the own side drops it half a pixel, and in the middle of the run the line sits
// on the run itself. For a run of `len` pixels with the pixel `back` pixels from its low end:
//   height(t) = heightA * (1 - 2t/len)   for t <= len/2
//               heightB * (2t/len - 1)   for t >= len/2            (t = 0 at the low end, len at the high end)
// A Z shape (other, own) gives one straight line, a U shape (own, own) two half lines, an L shape (one end without a
// crossing edge) leaves its far half untouched. Where the line is on the other side, the strip between it and the run
// belongs to the own side's colour: that strip's share of the other side's pixel is `areaOther`. Where it is on the
// own side, the strip's share of the own pixel is `areaOwn` (it takes the other side's colour).
// An end with both crossing edges could go either way: each side gets half. An end with both and nothing at the far
// end is a T junction of three colours, not a staircase: left alone.
inline void SmaaArea(int endA, int endB, int back, int forward, double* areaOther, double* areaOwn) {
    *areaOther = 0.0;
    *areaOwn = 0.0;
    if (endA == kSmaaEndNone && endB == kSmaaEndNone) return;
    if ((endA == kSmaaEndBoth && endB == kSmaaEndNone) || (endA == kSmaaEndNone && endB == kSmaaEndBoth)) return;
    const double len = (double)(back + forward + 1), mid = len * 0.5;
    const double t0 = (double)back, t1 = t0 + 1.0;  // the pixel's span along the run
    // the integral of the height magnitude over the part of the pixel in each half
    double low = 0.0, high = 0.0;
    if (t0 < mid) {
        const double b = t1 < mid ? t1 : mid;
        low = 0.5 * ((b - t0) - (b * b - t0 * t0) / len);
    }
    if (t1 > mid) {
        const double a = t0 > mid ? t0 : mid;
        high = 0.5 * ((t1 * t1 - a * a) / len - (t1 - a));
    }
    const int ends[2] = {endA, endB};
    const double part[2] = {low, high};
    for (int i = 0; i < 2; ++i) {
        if (ends[i] == kSmaaEndOther) *areaOther += part[i];
        else if (ends[i] == kSmaaEndOwn) *areaOwn += part[i];
        else if (ends[i] == kSmaaEndBoth) {
            *areaOther += part[i] * 0.5;
            *areaOwn += part[i] * 0.5;
        }
    }
}

// One texel of the area table: x = endA * 64 + back, y = endB * 64 + forward. The areas (0 .. 0.5) are stored
// as value * 255.
inline void SmaaAreaTexel(int x, int y, uint8_t* areaOther, uint8_t* areaOwn) {
    double other, own;
    SmaaArea(x / kSmaaAreaBlock, y / kSmaaAreaBlock, x % kSmaaAreaBlock, y % kSmaaAreaBlock, &other, &own);
    *areaOther = (uint8_t)(int)(other * 255.0 + 0.5);
    *areaOwn = (uint8_t)(int)(own * 255.0 + 0.5);
}

// ---- Search table ------------------------------------------------------------------------------------------------
// The weight shader walks along a run two pixels per texture fetch: one bilinear fetch placed a quarter of a pixel
// towards the nearer column and an eighth of a pixel towards the own row reads four marks at once, weighted
//   near column, own row 21/32;  far column, own row 7/32;  near column, other row 3/32;  far column, other row 1/32.
// Every subset of these four weights has its own sum, so the fetched value (a "level", sum * 32 = 0..32) tells
// exactly which of the four marks are set. The walk goes on while both run marks of the own row are set and no
// crossing mark is; the fetch that stops it is decoded with this table into how many of its two pixels still belong
// to the run.
constexpr int kSmaaSearchLevels = 33;
constexpr int kSmaaSearchSize = 64;  // the texture: a power of two, the rest is padding

// Level -> the four marks (bit 0 near/own, bit 1 far/own, bit 2 near/other, bit 3 far/other); -1 for a sum no subset has.
inline int SmaaLevelBits(int level) {
    if (level < 0 || level > 32) return -1;
    int bits = 0, rest = level;
    if (rest >= 21) { bits |= 1; rest -= 21; }
    if (rest >= 7) { bits |= 2; rest -= 7; }
    if (rest >= 3) { bits |= 4; rest -= 3; }
    if (rest >= 1) { bits |= 8; rest -= 1; }
    return rest == 0 ? bits : -1;
}

// A level that cannot come from exact filtering (a video card with coarse filter weights) reads as the closest valid one.
inline int SmaaNearestLevelBits(int level) {
    for (int off = 0; off <= 32; ++off) {
        int bits = SmaaLevelBits(level - off);
        if (bits >= 0) return bits;
        bits = SmaaLevelBits(level + off);
        if (bits >= 0) return bits;
    }
    return 0;
}

// The pixel the walk comes from is on the run. Walking backwards (left / up) the near pixel of the pair is the next
// one and a crossing mark sits on the far boundary of its own pixel:
//   0 = the near pixel is not on the run; 1 = the run ends behind the near pixel; 2 = behind the far pixel.
// Walking forwards (right / down) a crossing mark sits on the boundary in front of its own pixel:
//   0 = the near pixel is cut off or not on the run; 1 = the far pixel is.
// The pattern that lets the walk go on reads 0 in both: the walk only ends on it when it runs out of steps, and then
// nothing of the next pair is known.
// crossLevel / runLevel: the fetched value of the crossing-edge channel and of the run's own channel, times 32.
inline void SmaaSearchSteps(int crossLevel, int runLevel, int* backward, int* forward) {
    const int cross = SmaaNearestLevelBits(crossLevel), run = SmaaNearestLevelBits(runLevel);
    const bool runNear = (run & 1) != 0, runFar = (run & 2) != 0;
    const bool crossNear = (cross & (1 | 4)) != 0, crossFar = (cross & (2 | 8)) != 0;
    const bool goesOn = runNear && runFar && !crossNear && !crossFar;
    *backward = (!runNear || goesOn) ? 0 : (crossNear || !runFar) ? 1 : 2;
    *forward = (crossNear || !runNear || goesOn) ? 0 : 1;
}

// One texel of the search table (x = crossing level, y = run level): the step counts stored as count * 85.
inline void SmaaSearchTexel(int x, int y, uint8_t* backward, uint8_t* forward) {
    int b = 0, f = 0;
    if (x < kSmaaSearchLevels && y < kSmaaSearchLevels) SmaaSearchSteps(x, y, &b, &f);
    *backward = (uint8_t)(b * 85);
    *forward = (uint8_t)(f * 85);
}

}  // namespace ufx
