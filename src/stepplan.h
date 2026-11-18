// Whole physics steps at a high frame rate, without any Windows dependency, so the host can test it.
//
// The game steps PhysX from the frame delta dt (in steps of 1/60 s): FUN_006EACE0 takes one fixed step of 1/60 s for a delta up
// to 1.5, two up to 2.5, three up to 3.5, else four. A frame never gets less than one step, so a frame shorter than a step still
// moves the physics by a whole one. The game rounds a delta of 0.5..0.99 up to 1 itself (WinMain, FUN_00700650), which leaves the
// deltas below 0.5: above 120 frames/s the physics runs frames/60 times too fast (a car at 180 frames/s is three times as fast)
// while everything that moves by `speed * dt` keeps real time. The console cannot get there: its delta is a whole number of screen
// refreshes, at least 1.
//
// The plan gives the short frames the console's whole steps: their time is gathered, and a frame gets a step only when a whole one
// has passed. A frame of a whole step or more, and a frame in which no time passed (a pause), is not the plan's business: the game
// handles it exactly as before, and the gathered time is not touched.
#pragma once

namespace ufx {

struct StepPlan {
    float acc = 0.0f;  // game time of the short frames that no step has covered yet, in steps (0 <= acc < 1)
};

struct StepFrame {
    int steps;       // -1 = the frame is the game's own, 0 = no physics step in this frame, 1 = one whole step
    float accAfter;  // StepPlan::acc once the step call of the frame has run
};

// The steps the game itself takes for a frame delta (FUN_006EACE0, 006EAD15..006EAD8B).
inline int GameStepsFor(float dt) {
    return dt <= 1.5f ? 1 : dt <= 2.5f ? 2 : dt <= 3.5f ? 3 : 4;
}

// The plan for a frame of delta `dt`. The state is not changed: the caller takes `accAfter` when the step call of the frame
// really comes (it does not while the game is frozen), so the hooks before that call and the call itself see one plan.
inline StepFrame StepPlanFrame(const StepPlan& s, float dt) {
    StepFrame f = {-1, s.acc};
    if (!(dt > 0.0f && dt < 1.0f)) return f;  // a whole step or more, no time at all, or not a number
    volatile float sum = s.acc + dt;  // rounded to single precision here, whatever precision the x87 unit computes in
    float a = sum;
    f.steps = a >= 1.0f ? 1 : 0;
    f.accAfter = f.steps ? a - 1.0f : a;
    return f;
}

// What the car's whole-tick counters (`(int)dt` in the game code, 0 for every short frame) count in a frame: the steps of the
// plan, so they advance one tick per 1/60 s. In the game's own frames it is the delta itself.
inline float StepFrameTicks(const StepFrame& f, float dt) {
    return f.steps < 0 ? dt : (float)f.steps;
}

// The scale of the car's sound ramps, which the game moves by a constant per frame (0.05 for the tyre squeal): a short frame takes
// its share of a step, so the ramp keeps the speed it has at 60 frames/s. In the game's own frames it is 1.
inline float StepFrameRamp(const StepFrame& f, float dt) {
    return f.steps < 0 ? 1.0f : dt;
}

}  // namespace ufx
