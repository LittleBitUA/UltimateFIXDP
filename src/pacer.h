// Frame pacing logic without any Windows dependency, so the host can test it.
//
// Game time advances in whole steps of 1/60 s. After every frame the pacer tells how many steps passed (k):
// at least `minSteps` (1 = up to 60 frames per second, 2 = 30 like the console), at most 4. The frame is held
// back until `minSteps` steps after the previous aligned moment, so the game neither runs faster than real time
// (the PC code rounds the frame delta up from 0.5, i.e. 2x too fast at 120 Hz) nor produces a delta of zero.
// All arithmetic is integer (except the round-up of mode 1 at the end); ticks are QueryPerformanceCounter ticks.
#pragma once
#include <stdint.h>

namespace ufx {

struct PacerState {
    uint64_t freq = 0;     // ticks per second
    uint64_t anchor = 0;   // tick of the last re-anchor
    uint64_t n = 0;        // steps since the anchor
    uint64_t total = 0;    // steps since the pacer started (this is what the virtual clock counts)
    uint64_t stalls = 0;   // times a long stall was cut to 4 steps
    bool started = false;
};

constexpr int kPacerMaxSteps = 4;

inline void PacerStart(PacerState& s, uint64_t freq, uint64_t now) {
    s.freq = freq ? freq : 1;
    s.anchor = now;
    s.n = 0;
    s.started = true;
}

// Tick at which the next frame may begin.
inline uint64_t PacerTarget(const PacerState& s, int minSteps) {
    return s.anchor + (s.n + (uint64_t)minSteps) * s.freq / 60;
}

// Called after the wait, with the current tick. Returns the number of steps this frame lasted.
inline int PacerAdvance(PacerState& s, uint64_t now, int minSteps) {
    uint64_t target = PacerTarget(s, minSteps);
    uint64_t late = now > target ? now - target : 0;
    uint64_t extra = late * 60 / s.freq;
    uint64_t k = (uint64_t)minSteps + extra;
    if (k > (uint64_t)kPacerMaxSteps) {
        k = kPacerMaxSteps;
        s.anchor = now > target ? now : target;  // a long stall (Alt+Tab, loading): do not try to catch up
        s.n = 0;
        ++s.stalls;
    } else {
        s.n += k;
    }
    s.total += k;
    return (int)k;
}

// ---- Mode 1: the 60 frames/s limit, placed in front of the Present ----------------------------------------------------------
//
// Two ways to hold a frame, chosen from what the Present is seen to do:
//  * the grid (no vsync, or not proven): fixed points 1/60 s apart. Each Present waits for the next point, so the frames average
//    exactly 60 per second whatever the frame and Present times are. A frame that comes more than 1 ms late moves the grid to
//    itself: the next frames are not made shorter to catch up (the game rounds a short frame up to a whole step).
//  * vsync (the Present waits for the screen refresh and returns on it): the frame is held for a whole number of refreshes counted
//    from the previous return, hz/60 of them on average (144 Hz: 2,2,3,2,3), and its Present is issued half a refresh before the
//    refresh it should be shown on. This follows the screen's own clock, so a screen a hair faster than 60 Hz never drops a frame,
//    which a fixed grid would do each time it drifts across a refresh.
// The vsync way is checked against what it really gives: frames back sooner than their refreshes allow, returns that do not fall
// on the refresh grid, or an average above 60.6 frames/s mean the Present does not wait as assumed. Then it is back to the grid,
// and vsync is not tried again for a while.
struct LimiterState {
    uint64_t freq = 0;        // ticks per second
    uint32_t hz = 60;         // screen refresh rate
    uint64_t anchor = 0;      // the grid: tick of point 0
    uint64_t k = 0;           // the grid: points used since the anchor
    bool vsync = false;       // the Present waits for the refresh
    uint32_t acc = 0;         // vsync: + hz per frame, - 60 per refresh held (so hz/60 refreshes per frame on average)
    uint32_t n = 0;           // vsync: refreshes the current frame is held for (0 = not held)
    uint64_t target = 0;      // where the wait of the current frame ends (0 = no wait)
    uint64_t lastReturn = 0;  // the previous Present returned here (0 = none yet)
    uint32_t count = 0, early = 0, offGrid = 0;  // the check over a window of 60 frames
    uint64_t presentSum = 0, intervalSum = 0;
    uint32_t strikes = 0, holdoff = 0;      // vsync proven wrong: frames before it may be tried again
};

inline void LimiterStart(LimiterState& s, uint64_t freq) {
    s = LimiterState();
    s.freq = freq ? freq : 1;
}

inline void LimiterSetHz(LimiterState& s, uint32_t hz) {
    if (hz < 30 || hz > 500 || hz == s.hz) return;
    s.hz = hz;
    s.acc = 0;
    s.early = 0;
}

// The tick to wait for before this frame's Present (0 = no wait).
inline uint64_t LimiterTarget(LimiterState& s) {
    s.target = 0;
    s.n = 0;
    if (!s.lastReturn) return 0;  // the first frame
    if (s.vsync) {
        s.acc += s.hz;
        s.n = s.acc / 60;
        s.acc -= s.n * 60;
        if (s.n == 0) return 0;  // a screen below 60 Hz: the refresh itself is the limit
        s.target = s.lastReturn + (uint64_t)(2 * s.n - 1) * s.freq / (2 * (uint64_t)s.hz);
    } else {
        s.target = s.anchor + (s.k + 1) * s.freq / 60;
    }
    return s.target;
}

// After the wait, with the tick the Present is issued at.
inline void LimiterIssue(LimiterState& s, uint64_t at) {
    if (!s.vsync && s.target && at <= s.target + s.freq / 1000) {
        if (++s.k >= 3600) {  // move the anchor once a minute (exactly 60 s), the products stay small
            s.anchor += 60 * s.freq;
            s.k -= 3600;
        }
    } else {
        s.anchor = at;  // late, the first frame, or vsync: the grid starts at this Present
        s.k = 0;
    }
}

inline void LimiterLeaveVsync(LimiterState& s) {
    s.vsync = false;
    if (s.strikes < 6) ++s.strikes;
    s.holdoff = 600u << (s.strikes - 1);  // 10 s, then 20, 40 ... up to about 5 minutes at 60 frames/s
    s.count = s.early = s.offGrid = 0;
    s.presentSum = s.intervalSum = 0;
}

// After the Present, with the tick it returned at and how long it took.
inline void LimiterReturned(LimiterState& s, uint64_t at, uint64_t presentTicks) {
    if (s.lastReturn && at > s.lastReturn) {
        uint64_t interval = at - s.lastReturn;
        // Issued half a refresh before the n-th refresh, a Present that waits for it cannot come back sooner than n refreshes after
        // the previous return (a quarter of a refresh is left for the jitter).
        if (s.vsync && s.n && interval * 4 * s.hz < (uint64_t)(4 * s.n - 1) * s.freq) ++s.early;
        // Returns on the refreshes are a whole number of refreshes apart (15% of a refresh is left for the jitter).
        uint64_t hundredths = interval * s.hz * 100 / s.freq % 100;
        if (s.vsync && hundredths > 15 && hundredths < 85) ++s.offGrid;
        s.intervalSum += interval;
        s.presentSum += presentTicks;
        ++s.count;
    }
    s.lastReturn = at;
    if (s.vsync && s.early >= 3) {
        LimiterLeaveVsync(s);
        return;
    }
    if (s.count < 60) return;
    if (s.vsync) {
        if (s.offGrid * 4 > s.count || s.intervalSum * 100 * 60 < (uint64_t)s.count * s.freq * 99) {  // above 60.6 frames/s
            LimiterLeaveVsync(s);
            return;
        }
    } else if (s.holdoff) {
        s.holdoff = s.holdoff > s.count ? s.holdoff - s.count : 0;
    } else if (s.presentSum * 4 * s.hz > (uint64_t)s.count * s.freq) {  // the Present takes over a quarter of a refresh
        s.vsync = true;
        s.acc = 0;
    }
    s.count = s.early = s.offGrid = 0;
    s.presentSum = s.intervalSum = 0;
}

// The game rounds a frame delta d (in steps of 1/60 s) up to the next whole number when its fraction is 0.5 or more (WinMain,
// FUN_00700650), so a frame of 0.5..0.99 steps counts as a whole step: at 75 frames/s the game runs 1.25x too fast. Mode 1 hands the
// game the time it would round to anyway, previous + ceil(d)/60, and the next delta is measured from that value, so the game clock
// never drifts from the real one (it is at most about half a step ahead).
// Returns the whole steps to hand out after the previous value, or -1 when the real time can be handed out as it is.
inline int PacerRoundUpSteps(double d) {
    if (d <= 0.0) return 0;    // no time has passed since the value handed out last (it was a little ahead): hand it again
    if (d >= 7.0) return -1;   // a long frame (a load): the game cuts a delta of 8 or more by itself
    int whole = (int)d;
    if (d - (double)whole < 0.49) return -1;  // the game keeps this delta as it is (0.49: room for its float rounding)
    return whole + 1;
}

}  // namespace ufx
