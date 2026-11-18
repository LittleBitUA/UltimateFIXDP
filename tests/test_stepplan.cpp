// Simulates the step plan (whole physics steps at a high frame rate) with synthetic frame deltas.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stepplan.h"
using namespace ufx;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); ++g_fail; } } while (0)

static uint32_t g_rng = 4242;
static uint32_t Rand(uint32_t n) { g_rng = g_rng * 1664525u + 1013904223u; return (g_rng >> 8) % n; }

// The game's own handling of the delta (WinMain 00700650): (float)seconds * 60, rounded up from a fraction of 0.5.
static float GameDelta(double seconds) {
    float dt = (float)seconds * 60.0f;
    float fr = dt - (float)(int)dt;
    if (fr >= 0.5f) dt = (float)((int)dt + 1);
    return dt;
}

struct Run {
    long steps = 0;         // physics steps taken
    double gameTime = 0;    // sum of the deltas, in steps
    long shortFrames = 0;   // frames the plan handled
    long ticks = 0;         // what an (int)dt counter of the car counted
    double ramp = 0;        // what a 0.05-per-frame sound ramp moved
    int maxPerFrame = 0;
    bool overCap = false;   // a frame took more steps than the game's own rule gives it
    bool ahead = false;     // the steps ran ahead of the game time
    bool accBad = false;    // the gathered time left 0..1
};

// One frame as the hooks see it: the plan at the start of the world update, taken at the physics step call.
static void Frame(StepPlan& plan, float dt, Run& r, bool stepCallComes = true) {
    StepFrame f = StepPlanFrame(plan, dt);
    int steps = f.steps < 0 ? GameStepsFor(dt) : f.steps;
    if (steps > GameStepsFor(dt)) r.overCap = true;
    if (f.steps >= 0) ++r.shortFrames;
    r.ticks += (long)StepFrameTicks(f, dt);
    r.ramp += 0.05 * StepFrameRamp(f, dt);
    if (!stepCallComes) return;  // frozen: no step, and the frame's time is not taken
    plan.acc = f.accAfter;
    r.steps += steps;
    r.gameTime += dt;
    if (steps > r.maxPerFrame) r.maxPerFrame = steps;
    if (!(plan.acc >= 0.0f && plan.acc < 1.0f)) r.accBad = true;
    if (f.steps >= 0 && (double)r.steps > r.gameTime + 1e-3) r.ahead = true;
}

int main() {
    const int rates[] = {60, 90, 120, 144, 180, 240};
    const int seconds = 60;

    // The plain arithmetic: every frame of a rate lasts 60/rate steps, with no rounding by the game. 60 steps per second on
    // average at every rate, at most one step in a short frame, never more than the game's own rule.
    for (int fps : rates) {
        StepPlan plan;
        Run r;
        float dt = 60.0f / (float)fps;
        for (int i = 0; i < fps * seconds; ++i) Frame(plan, dt, r);
        double perSecond = (double)r.steps / seconds;
        CHECK(labs(r.steps - 60L * seconds) <= 1, "%d frames/s: %ld steps in %d s", fps, r.steps, seconds);
        CHECK(!r.overCap && r.maxPerFrame <= 1, "%d frames/s: %d steps in one frame", fps, r.maxPerFrame);
        CHECK(!r.ahead && !r.accBad, "%d frames/s: steps ahead of time %d, gathered time out of range %d", fps, r.ahead, r.accBad);
        CHECK((fps == 60) == (r.shortFrames == 0), "%d frames/s: the plan handled %ld frames", fps, r.shortFrames);
        // A counter of whole ticks follows the steps (in the game it stands still at these rates: (int)dt is 0).
        CHECK(r.ticks == r.steps, "%d frames/s: tick counter %ld, steps %ld", fps, r.ticks, r.steps);
        // A sound ramp of 0.05 per frame moves 3.0 per second, as at 60 frames/s.
        CHECK(fabs(r.ramp / seconds - 3.0) < 0.01, "%d frames/s: sound ramp %.4f per second", fps, r.ramp / seconds);
        printf("%3d frames/s, delta %.4f: %.2f steps/s, at most %d per frame\n", fps, dt, perSecond, r.maxPerFrame);
    }

    // The same rates with the game's own rounding of the delta (the frame limit off): from 0.5 the game makes the delta a whole
    // step, so at 90 and 120 frames/s every frame is the game's own and its whole clock runs fast (the frame limit is what cures
    // that); above 120 the delta stays short and the plan holds 60 steps per second. Always: the steps equal the game time.
    for (int fps : rates) {
        StepPlan plan;
        Run r;
        double t = 0, prev = 0;
        for (int i = 0; i < fps * seconds; ++i) {
            t = (double)(i + 1) / fps;
            Frame(plan, GameDelta(t - prev), r);
            prev = t;
        }
        CHECK(fabs((double)r.steps - r.gameTime) < 1.0 + 1e-3, "%d frames/s (game delta): %ld steps, game time %.3f", fps, r.steps, r.gameTime);
        CHECK(!r.overCap && !r.ahead && !r.accBad, "%d frames/s (game delta): cap %d ahead %d range %d", fps, r.overCap, r.ahead, r.accBad);
        if (fps > 120) CHECK(fabs((double)r.steps / seconds - 60.0) < 0.1, "%d frames/s (game delta): %.3f steps/s", fps, (double)r.steps / seconds);
        if (fps <= 120) CHECK(r.shortFrames == 0, "%d frames/s (game delta): the plan touched %ld frames", fps, r.shortFrames);
        printf("%3d frames/s with the game's rounding: %.2f steps/s, game clock x%.2f\n", fps, (double)r.steps / seconds, r.gameTime / (60.0 * seconds));
    }

    // Uneven short frames (2..8 ms): the steps follow the time within one step and never run ahead of it.
    {
        StepPlan plan;
        Run r;
        bool behind = false;
        for (int i = 0; i < 200000; ++i) {
            float dt = (float)(2000 + Rand(6000)) * 60.0f / 1000000.0f;
            Frame(plan, dt, r);
            if (r.gameTime - (double)r.steps >= 1.0 + 1e-3) behind = true;
        }
        CHECK(!r.ahead && !behind && !r.accBad && r.maxPerFrame == 1, "uneven: ahead %d behind %d range %d max %d", r.ahead, behind, r.accBad, r.maxPerFrame);
        CHECK(r.ticks == r.steps, "uneven: tick counter %ld, steps %ld", r.ticks, r.steps);
        CHECK(fabs(r.ramp - 0.05 * r.gameTime) < 0.05, "uneven: sound ramp %.3f for %.3f steps of time", r.ramp, r.gameTime);
    }

    // The game's own frames: nothing is planned and the gathered time keeps every bit.
    {
        const float own[] = {1.0f, 1.02f, 1.3f, 1.49f, 1.5f, 2.0f, 2.4f, 3.0f, 4.0f, 8.0f, 0.0f, -0.0f, -1.0f, NAN, INFINITY};
        for (float dt : own) {
            StepPlan plan;
            plan.acc = 0.73f;
            StepFrame f = StepPlanFrame(plan, dt);
            CHECK(f.steps == -1 && memcmp(&f.accAfter, &plan.acc, sizeof(float)) == 0, "delta %g: steps %d, gathered %g", dt, f.steps, f.accAfter);
            float ticks = StepFrameTicks(f, dt);
            CHECK(memcmp(&ticks, &dt, sizeof(float)) == 0, "delta %g: the tick counters get %g", dt, ticks);
            CHECK(StepFrameRamp(f, dt) == 1.0f, "delta %g: sound ramp scale %g", dt, StepFrameRamp(f, dt));
        }
        CHECK(GameStepsFor(1.0f) == 1 && GameStepsFor(1.5f) == 1 && GameStepsFor(2.0f) == 2 && GameStepsFor(2.5f) == 2 &&
              GameStepsFor(3.5f) == 3 && GameStepsFor(3.6f) == 4 && GameStepsFor(8.0f) == 4, "the game's own rule");
    }

    // Short and whole frames mixed (a scene that falls from 180 to 30 frames/s and back): the whole frames keep the game's steps,
    // the short ones add theirs.
    {
        StepPlan plan;
        Run r;
        long ownSteps = 0;
        double shortTime = 0;
        for (int i = 0; i < 60000; ++i) {
            bool whole = (i / 500) % 2 == 1;
            float dt = whole ? 2.0f : 1.0f / 3.0f;
            if (whole) ownSteps += GameStepsFor(dt); else shortTime += dt;
            Frame(plan, dt, r);
        }
        CHECK(fabs((double)(r.steps - ownSteps) - shortTime) < 1.0 + 1e-2, "mixed: %ld steps, %ld the game's, %.3f short time", r.steps, ownSteps, shortTime);
        CHECK(!r.overCap && !r.accBad, "mixed: cap %d range %d", r.overCap, r.accBad);
    }

    // Frozen frames (the F10 menu with the pause on): the step call does not come, so the plan is not taken and comes again.
    {
        StepPlan plan;
        Run r;
        for (int i = 0; i < 7; ++i) Frame(plan, 0.3f, r);
        float before = plan.acc;
        long steps = r.steps;
        StepFrame want = StepPlanFrame(plan, 0.3f);
        for (int i = 0; i < 50; ++i) Frame(plan, 0.3f, r, false);
        StepFrame again = StepPlanFrame(plan, 0.3f);
        CHECK(plan.acc == before && r.steps == steps && again.steps == want.steps && again.accAfter == want.accAfter, "frozen: the plan moved");
    }

    // The stuck detector of the car counts 300 ticks (5 seconds). At 180 frames/s that is 900 frames; in the game it never ends.
    {
        StepPlan plan;
        Run r;
        int frames = 0;
        while (r.ticks < 300 && frames < 100000) { Frame(plan, 1.0f / 3.0f, r); ++frames; }
        CHECK(frames >= 897 && frames <= 903, "300 ticks at 180 frames/s took %d frames", frames);
    }

    printf("%s\n", g_fail ? "STEP PLAN TESTS FAILED" : "STEP PLAN TESTS PASSED");
    return g_fail ? 1 : 0;
}
