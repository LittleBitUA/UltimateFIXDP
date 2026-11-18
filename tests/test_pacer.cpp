// Simulates the pacer with synthetic frame times (no real waiting).
#include <stdio.h>
#include <stdlib.h>
#include "pacer.h"
using namespace ufx;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); ++g_fail; } } while (0)

// One run: `work(i)` is how long frame i takes by itself (ticks) before the pacer wait; returns total steps.
struct Run { uint64_t elapsed, steps, stalls; int minK, maxK; };

template <class F>
static Run Simulate(uint64_t freq, int minSteps, int frames, F work) {
    PacerState s;
    uint64_t now = 1000000;  // arbitrary start
    uint64_t t0 = now;
    PacerStart(s, freq, now);
    Run r{0, 0, 0, 99, 0};
    for (int i = 0; i < frames; ++i) {
        now += work(i);
        uint64_t target = PacerTarget(s, minSteps);
        if (now < target) now = target;  // the wait
        int k = PacerAdvance(s, now, minSteps);
        if (k < r.minK) r.minK = k;
        if (k > r.maxK) r.maxK = k;
    }
    r.elapsed = now - t0;
    r.steps = s.total;
    r.stalls = s.stalls;
    return r;
}

// ---- mode 1: the limit in front of the Present ----
static uint32_t g_rng = 777;
static uint32_t Rand(uint32_t n) { g_rng = g_rng * 1664525u + 1013904223u; return (g_rng >> 8) % n; }

// The game's own handling of the delta (WinMain 00700650): (float)(t - prev) * 60, rounded up from a fraction of 0.5.
static double GameDelta(double handed) {
    float dt = (float)handed * 60.0f;
    float fr = dt - (float)(int)dt;
    if (fr >= 0.5f) dt = (float)((int)dt + 1);
    return dt;
}

struct LimRun { double fps, gameDrift; int hitches; bool vsyncUsed; };

// screenHz: the real refresh rate, reportedHz: what Windows says. vsync: the Present returns on the first refresh after it
// is issued; otherwise it returns after presentMs. workMs: the game's work from the return to the next Present (+-1 ms).
static LimRun SimLimiter(double screenHz, uint32_t reportedHz, bool vsync, double workMs, double presentMs, int frames) {
    const uint64_t freq = 10000000;
    const double refresh = (double)freq / screenHz;
    const double phase = (double)Rand(100000);
    LimiterState s;
    LimiterStart(s, freq);
    LimiterSetHz(s, reportedHz);
    uint64_t ret = 5000000, t0 = 0, last = ret;
    double handedPrev = 0, game = 0;
    LimRun r{0, 0, 0, false};
    const int warm = 900;
    // A dropped frame: longer than the longest hold the rate needs (144 Hz: 3 refreshes) plus half a refresh, at least 25 ms.
    double hitch = ((double)(int)(screenHz / 60.0 + 0.999) + 0.5) * refresh;
    if (hitch < freq * 0.025) hitch = freq * 0.025;
    for (int i = 0; i < frames; ++i) {
        // the game reads its clock at the start of the frame (mode 1 hands out the real time with the round-up carry)
        double read = (double)ret / (double)freq;
        if (i == warm) { t0 = ret; handedPrev = read; game = 0; }
        if (i > warm) {
            int k = PacerRoundUpSteps((read - handedPrev) * 60.0);
            double handed = k < 0 ? read : handedPrev + k / 60.0;
            game += GameDelta(handed - handedPrev);
            handedPrev = handed;
        }
        uint64_t now = ret + (uint64_t)((workMs - 1.0) * freq / 1000) + Rand((uint32_t)(2 * freq / 1000));
        uint64_t target = LimiterTarget(s);
        if (target > now) now = target + Rand(300);  // the wait ends a little late
        LimiterIssue(s, now);
        uint64_t back;
        if (vsync) {
            double j = __builtin_floor(((double)now - phase) / refresh) + 1.0;
            back = (uint64_t)(phase + j * refresh) + 200;  // the flip, then the call returns
        } else {
            back = now + (uint64_t)(presentMs * freq / 1000) + Rand(100);
        }
        LimiterReturned(s, back, back - now);
        if (s.vsync) r.vsyncUsed = true;
        if (i > warm && (double)(back - last) > hitch) ++r.hitches;
        last = back;
        ret = back;
    }
    double secs = (double)(ret - t0) / (double)freq;
    r.fps = (frames - warm) / secs;  // frames warm..frames-1, from the return before frame warm to the last one
    r.gameDrift = game / 60.0 - (handedPrev - (double)t0 / (double)freq);  // game seconds minus the real seconds
    if (r.gameDrift < 0) r.gameDrift = -r.gameDrift;
    return r;
}

static void LimiterTests() {
    const uint32_t rates[] = {60, 75, 100, 120, 144, 165, 180, 240};
    for (uint32_t hz : rates) {
        LimRun v = SimLimiter((double)hz, hz, true, 5.0, 0, 12000);
        CHECK(v.fps > 59.7 && v.fps < 60.3, "vsync %u Hz: %.3f frames/s", hz, v.fps);
        CHECK(v.hitches == 0, "vsync %u Hz: %d frames of 25 ms", hz, v.hitches);
        CHECK(v.gameDrift < 0.012, "vsync %u Hz: the game clock is %.4f s off the real one", hz, v.gameDrift);
        LimRun n = SimLimiter((double)hz, hz, false, 5.0, 1.0, 12000);
        CHECK(n.fps > 59.7 && n.fps < 60.3, "no vsync %u Hz: %.3f frames/s", hz, n.fps);
        CHECK(n.gameDrift < 0.012, "no vsync %u Hz: the game clock is %.4f s off the real one", hz, n.gameDrift);
        // No vsync, but the Present takes long (the video card is the limit): taken for vsync first, then proven wrong.
        LimRun g = SimLimiter((double)hz, hz, false, 4.0, 0.45 * 1000.0 / hz, 12000);
        CHECK(g.fps > 59.5 && g.fps < 60.5, "no vsync %u Hz, slow Present: %.3f frames/s", hz, g.fps);
        CHECK(g.gameDrift < 0.012, "no vsync %u Hz, slow Present: the game clock is %.4f s off", hz, g.gameDrift);
        if (hz >= 100) {  // below that, 4 ms of work and a Present of 0.8 refresh do not fit into 1/60 s at all
            LimRun h = SimLimiter((double)hz, hz, false, 4.0, 0.8 * 1000.0 / hz, 12000);
            CHECK(h.fps > 59.5 && h.fps < 60.5, "no vsync %u Hz, Present of 0.8 refresh: %.3f frames/s", hz, h.fps);
        }
        printf("limit at %u Hz: vsync %.3f frames/s (vsync hold %s), no vsync %.3f, slow Present %.3f\n", hz, v.fps,
               v.vsyncUsed ? "used" : "not needed", n.fps, g.fps);
    }
    // Screens a hair off their nominal rate.
    LimRun f = SimLimiter(60.05, 60, true, 5.0, 0, 12000);
    CHECK(f.hitches == 0 && f.fps > 59.9 && f.fps < 60.2, "60.05 Hz: %.3f frames/s, %d frames of 25 ms", f.fps, f.hitches);
    LimRun l = SimLimiter(59.94, 59, true, 5.0, 0, 12000);
    CHECK(l.hitches == 0 && l.fps > 59.8 && l.fps < 60.0, "59.94 Hz: %.3f frames/s, %d frames of 25 ms", l.fps, l.hitches);
    // Windows reports the wrong screen (60) while the game is on a 144 Hz one: still 60 on average.
    LimRun w = SimLimiter(144.0, 60, true, 5.0, 0, 12000);
    CHECK(w.fps > 59.5 && w.fps < 60.5, "144 Hz reported as 60: %.3f frames/s", w.fps);
    // A machine slower than 60 frames/s (25 ms of work): no limit, and the game clock still keeps the real time.
    LimRun s = SimLimiter(144.0, 144, true, 25.0, 0, 6000);
    CHECK(s.fps < 60.0 && s.gameDrift < 0.012, "slow machine: %.3f frames/s, game clock %.4f s off", s.fps, s.gameDrift);

    // The round-up carry alone: frames of 1/75 s. Without it the game runs 1.25x fast; with it it keeps the real time.
    double handedPrev = 0, game = 0, plain = 0, t = 0;
    for (int i = 0; i < 7500; ++i) {
        double prevT = t;
        t += 1.0 / 75.0;
        int k = PacerRoundUpSteps((t - handedPrev) * 60.0);
        double handed = k < 0 ? t : handedPrev + k / 60.0;
        game += GameDelta(handed - handedPrev);
        handedPrev = handed;
        plain += GameDelta(t - prevT);
    }
    // whole steps only: frames faster than 60 a second (no limit holding) get one step each until the clock is 2.5 steps ahead,
    // then wait one frame; the game clock stays within about 3.5 steps (58 ms) of the real one and never drifts
    CHECK(game / 60.0 - t < 0.06 && game / 60.0 - t > -0.001, "75 frames/s carry: game %.4f s, real %.4f s", game / 60.0, t);
    CHECK(plain / 60.0 > t * 1.2, "75 frames/s without the carry: game %.4f s, real %.4f s (expected 1.25x)", plain / 60.0, t);
}

// The frames the limit of mode 1 holds for whole refreshes (the first refresh at or after each 1/60 s slot) on screens of
// 61..139 Hz: every frame must get a whole number of steps (92 counters of the game take (int)dt), none of zero, and the game
// clock must stay within 2.5 steps of the real one.
void WholeStepTests() {
    const double rates[] = {61, 75, 85, 90, 100, 120, 139};
    for (double hz : rates) {
        const double refresh = 1.0 / hz;
        double prev = 0, handedPrev = 0, worst = 0;
        int fractional = 0, zero = 0;
        for (int f = 1; f < 20000; ++f) {
            double t = __builtin_ceil((f / 60.0) / refresh - 1e-9) * refresh;
            if (f % 7 == 0) t += 0.03;  // a slow frame now and then
            if (t < prev + refresh) t = prev + refresh;
            prev = t;
            int k = PacerRoundUpSteps((t - handedPrev) * 60.0);
            double handed = k < 0 ? t : handedPrev + k / 60.0;
            double steps = (handed - handedPrev) * 60.0;
            if (k < 0 && __builtin_fabs(steps - __builtin_floor(steps + 0.5)) > 1e-6) ++fractional;
            if (k == 0) ++zero;
            double ahead = (handed - t) * 60.0;
            if (ahead > worst) worst = ahead;
            handedPrev = handed;
        }
        CHECK(fractional == 0 && zero == 0 && worst < 2.5, "%.0f Hz held for whole refreshes: %d fractional and %d zero steps, %.2f steps ahead at most",
              hz, fractional, zero, worst);
    }
}

int main() {
    LimiterTests();
    WholeStepTests();
    const uint64_t freq = 10000000;  // 10 MHz QPC
    const uint64_t ms = freq / 1000;

    // Fast machine: 5 ms of work per frame at 60: always one step, game time == real time.
    Run a = Simulate(freq, 1, 6000, [&](int) { return 5 * ms; });
    CHECK(a.minK == 1 && a.maxK == 1, "fast: k %d..%d", a.minK, a.maxK);
    double rs = (double)a.elapsed / freq * 60.0;
    CHECK(rs - a.steps < 1.0 && a.steps <= rs + 0.001, "fast: steps %llu vs real %.3f", (unsigned long long)a.steps, rs);

    // Jitter 1..30 ms: steps follow real time within one step, and never run ahead of it.
    uint32_t seed = 12345;
    Run b = Simulate(freq, 1, 20000, [&](int) { seed = seed * 1664525u + 1013904223u; return (1 + (seed >> 16) % 30) * ms; });
    rs = (double)b.elapsed / freq * 60.0;
    CHECK(b.steps <= rs + 0.001 && rs - b.steps < 8.0, "jitter: steps %llu vs real %.3f (stalls %llu)",
          (unsigned long long)b.steps, rs, (unsigned long long)b.stalls);
    CHECK(b.maxK <= 4 && b.minK >= 1, "jitter: k range");

    // 144 Hz display, vsync: each frame takes 6.944 ms of work + we wait; same invariants.
    Run c = Simulate(freq, 1, 14400, [&](int) { return freq / 144; });
    rs = (double)c.elapsed / freq * 60.0;
    CHECK(c.steps <= rs + 0.001 && rs - c.steps < 1.0, "144Hz: steps %llu vs %.3f", (unsigned long long)c.steps, rs);

    // Console mode: at least 2 steps per frame, 30 frames per second.
    Run d = Simulate(freq, 2, 3000, [&](int) { return 4 * ms; });
    CHECK(d.minK == 2 && d.maxK == 2, "30 fps: k %d..%d", d.minK, d.maxK);
    rs = (double)d.elapsed / freq * 60.0;
    CHECK(d.steps <= rs + 0.001 && rs - d.steps < 2.0, "30 fps: steps %llu vs %.3f", (unsigned long long)d.steps, rs);

    // A 5 second stall (Alt+Tab): one frame of 4 steps, then normal pacing again, no catching up.
    int stallFrame = 100;
    Run e = Simulate(freq, 1, 200, [&](int i) { return i == stallFrame ? 5000 * ms : 8 * ms; });
    CHECK(e.stalls == 1 && e.maxK == 4, "stall: stalls %llu maxK %d", (unsigned long long)e.stalls, e.maxK);
    CHECK(e.steps < 200 + 4, "stall: steps %llu", (unsigned long long)e.steps);

    // A slow machine (25 ms per frame = 40 fps): k is 1 or 2, average matches real time.
    Run f = Simulate(freq, 1, 4000, [&](int) { return 25 * ms; });
    rs = (double)f.elapsed / freq * 60.0;
    CHECK(f.minK >= 1 && f.maxK <= 2 && f.steps <= rs + 0.001 && rs - f.steps < 1.5, "slow: k %d..%d steps %llu vs %.3f",
          f.minK, f.maxK, (unsigned long long)f.steps, rs);

    printf("%s\n", g_fail ? "PACER TESTS FAILED" : "PACER TESTS PASSED");
    return g_fail ? 1 : 0;
}
