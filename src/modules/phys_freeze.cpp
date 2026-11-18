// The PhysX step of the game. It stands still while the F10 menu is open (and in the photo mode), and frames shorter than one
// step of 1/60 s share whole steps instead of taking one each (HighFpsPhysics).
//
// The game steps its PhysX scene from the main thread, from FUN_006EB3C0 called by the object update phase. Freezing the game
// clock alone leaves the physics running (a car keeps rolling), so the call itself is skipped while a flag is set.
//
// Whole steps (the arithmetic and the reason are in stepplan.h): the plan of a frame is made when the world update begins and is
// the same for everything in that update. In a frame without a step the physics call and the car's control are skipped; in a
// frame with a step both run as they do in a frame of exactly one step. What the car code counts in whole ticks outside its
// control function takes the plan's steps through a stub, and its sound ramps read their step from variables of this file. A
// frame of a whole step or more (60 frames/s and below, the frame limit, the whole-step clocks) gets no plan: every hook and
// the stub then pass straight through, and the variables hold the game's own values.
#include "../base.h"
#include "../sites.h"
#include "../stepplan.h"
#include "../stubs.h"

namespace ufx {
namespace {
volatile bool g_frozen = false;  // the call is replaced only when the bytes matched, so the flag alone does nothing otherwise
double g_physAccumMs = 0.0;  // time spent in the PhysX step since the overlay last took it
bool g_stepHooked = false;   // the call of the physics step goes through PhysicsStepHook

StepPlan g_plan;
StepFrame g_frame = {-1, 0.0f};  // the plan of the world update that is running; -1 outside of one and while the plan is off
float g_frameDt = 0.0f;          // the frame delta that plan was made from (the game can rewrite its delta during the update)
bool g_planReady = false;        // the world update, the car's control and the car's movement are hooked: the plan may act
bool g_warmUpOwed = false;       // a warm-up was asked for in a frame without a step: the next step carries it
float g_tickDt = 0.0f;           // what the car's (int)dt counters take in place of the frame delta while a plan is in force
double g_gameRamp[2];            // the game's own steps of the car's sound ramps (0.05 and 0.01)
double g_ramp[2];                // what those ramps read in place of them
bool g_shortSeen = false;        // for the log: a short frame was planned at least once
u32 g_shortFrames = 0, g_shortSteps = 0;
double g_shortTime = 0.0;        // game time of the short frames counted above, in steps

typedef void(__attribute__((thiscall)) * StepFn)(void* self, float dt);  // FUN_006EB3C0 is a thiscall: ECX is kept in a local and used later
typedef void(__attribute__((thiscall)) * WorldUpdateFn)(void* self, float dt);  // FUN_006C5FF0
typedef void(__attribute__((thiscall)) * CarFn)(void* car);  // FUN_0054AAE0 and FUN_005502C0

// The plan of a short frame was carried out (its physics call came): once in the log when it first happens, then a line per
// 18000 such frames (100 seconds at 180 frames/s) with what they added up to, so a log shows 60 steps per second of game time.
void NoteShortFrame(float dt, int steps) {
    if (!g_shortSeen) {
        g_shortSeen = true;
        Log("physics steps: a frame of %d/1000 of a step: frames shorter than one step share whole steps from now on (HighFpsPhysics)",
            (int)(dt * 1000.0f));
    }
    ++g_shortFrames;
    g_shortSteps += (u32)steps;
    g_shortTime += (double)dt;
    if (g_shortFrames < 18000) return;
    int perSecond10 = (int)((double)g_shortSteps * 600.0 / g_shortTime);  // the time is in steps of 1/60 s, and above zero here
    Log("physics steps: %u short frames (%d ms of game time) took %u whole steps: %d.%d per second", g_shortFrames,
        (int)(g_shortTime * 1000.0 / 60.0), g_shortSteps, perSecond10 / 10, perSecond10 % 10);
    g_shortFrames = g_shortSteps = 0;
    g_shortTime = 0.0;
}

// Replaces the call of the physics step (thiscall: ECX = the physics object, one float argument = the frame step): skipped while
// frozen, otherwise timed. Same convention as the original, so the callee pops its argument.
void __attribute__((thiscall)) PhysicsStepHook(void* self, float dt) {
    if (g_frozen) return;
    // A planned (short) frame: its time is taken into the plan here, where its step would run, so a frozen frame leaves the plan
    // as it was. Without a step the call is skipped like in a freeze: nothing is queued, so the simulate and fetch calls that
    // follow find no scene. With a step the game gets a delta of exactly one step, which is what it gets at 60 frames/s.
    volatile u8* warmUp = (volatile u8*)(uintptr_t)kPhysicsWarmUpFlagVa;
    if (g_frame.steps >= 0) {
        g_plan.acc = g_frame.accAfter;
        NoteShortFrame(g_frameDt, g_frame.steps);
        if (g_frame.steps == 0) {
            // The game reads the warm-up request right after this call and clears it in this frame, stepped or not.
            if (*warmUp) g_warmUpOwed = true;
            return;
        }
        dt = (float)g_frame.steps;
    }
    if (g_warmUpOwed) {
        g_warmUpOwed = false;
        *warmUp = 1;
    }
    // The game picks 1, 2, 3 or 4 fixed physics steps per frame from this step (below 1.5 / 2.5 / 3.5 ticks). Each extra step makes
    // the frame longer, which asks for still more steps next frame: a spiral that ends near 20 frames/s. A cap on the step keeps
    // the number of steps (and so the physics time per frame) bounded.
    if (g_cfg.physicsSteps >= 1 && g_cfg.physicsSteps <= 3) {
        float limit = g_cfg.physicsSteps == 1 ? 1.4f : g_cfg.physicsSteps == 2 ? 2.4f : 3.4f;
        if (dt > limit) dt = limit;
    }
    LARGE_INTEGER a, b, f;
    QueryPerformanceCounter(&a);
    ((StepFn)(uintptr_t)kPhysicsStepFn)(self, dt);
    QueryPerformanceCounter(&b);
    QueryPerformanceFrequency(&f);
    g_physAccumMs += (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
}

// Replaces both calls of the world update in the tick (thiscall: ECX = the world, one float argument = the frame delta, ret 4):
// the plan of the frame is made before any object runs, and the variables the stub and the patched operands read are set for
// this update.
void __attribute__((thiscall)) WorldUpdateHook(void* self, float dt) {
    StepFrame f = {-1, 0.0f};
    if (g_planReady && g_cfg.highFpsPhysics)
        f = StepPlanFrame(g_plan, dt);
    else
        g_plan.acc = 0.0f;  // off: nothing is carried into the next time it is switched on
    g_frame = f;
    g_frameDt = dt;
    g_tickDt = StepFrameTicks(f, dt);
    for (int i = 0; i < 2; ++i) g_ramp[i] = f.steps < 0 ? g_gameRamp[i] : g_gameRamp[i] * (double)StepFrameRamp(f, dt);
    ((WorldUpdateFn)(uintptr_t)kWorldUpdateFn)(self, dt);
    // A car message sent outside of a world update is the game's own: no plan, and the sound ramps take the game's steps.
    g_frame.steps = -1;
    for (int i = 0; i < 2; ++i) g_ramp[i] = g_gameRamp[i];
}

// Replaces the call of the car's control (thiscall: ECX = the car, no stack argument). It runs before the physics step and works
// per call: the wheel torques are `torque * dt`, the push down on the car is added, at least 0.1 fuel is burnt, the tick counters
// take (int)dt. So in a short frame without a step it is skipped (the wheels keep what the last step's call gave them), and in
// one with a step it sees a delta of exactly one step, as at 60 frames/s. The frame delta is a global the whole game reads; only
// the main thread writes it, and it is put back before anything else runs.
void __attribute__((thiscall)) CarDriveHook(void* car) {
    int steps = g_frame.steps;
    if (steps == 0) return;
    CarFn drive = (CarFn)(uintptr_t)kCarDriveFn;
    if (steps < 0) {
        drive(car);
        return;
    }
    volatile float* delta = (volatile float*)(uintptr_t)kFrameDeltaVa;
    float real = *delta;
    *delta = (float)steps;
    drive(car);
    *delta = real;
}

// Replaces the call of the car's update after the physics step (thiscall: ECX = the car, no stack argument). That update stores
// `position - previous position` as the car's movement of the frame. In a short frame without a step the car has not moved, so
// the game would store zero there, and the readers of the next frame would see a standing car: the odometer and the fuel (which
// run in the frames with a step, right after such a frame), and the player states' test of whether the car moves. The movement
// of the last step is kept instead, which is what every frame sees at 60 frames/s. When the position did change (the game placed
// the car itself), the game's own values stand.
void __attribute__((thiscall)) CarAfterStepHook(void* car) {
    CarFn update = (CarFn)(uintptr_t)kCarAfterStepFn;
    if (g_frame.steps != 0) {
        update(car);
        return;
    }
    volatile u32* position = (volatile u32*)((u8*)car + kCarPositionOffset);  // raw bits: a copy must not go through the x87 unit
    volatile u32* movement = (volatile u32*)((u8*)car + kCarMovementOffset);
    u32 wasAt[4], moved[4];
    for (int i = 0; i < 4; ++i) {
        wasAt[i] = position[i];
        moved[i] = movement[i];
    }
    update(car);
    for (int i = 0; i < 4; ++i)
        if (position[i] != wasAt[i]) return;
    for (int i = 0; i < 4; ++i) movement[i] = moved[i];
}

// Points the 32-bit address operand of the instruction at `s` (its bytes 2..5) at a variable of the mod.
bool PointOperand(const SiteDef& s, const void* variable) {
    u32 at = (u32)(uintptr_t)variable;
    const u8 code[6] = {s.bytes[0], s.bytes[1], (u8)at, (u8)(at >> 8), (u8)(at >> 16), (u8)(at >> 24)};
    return WriteCode(s.va, code, sizeof(code));
}

bool SiteMatches(const SiteDef& s) {
    if (BytesMatch(s.va, s.bytes, s.len)) return true;
    Log("physics steps: %s at %08X differs from DP.exe 1.01b, skipped", s.name, s.va);
    return false;
}
}

// Milliseconds per frame spent in the PhysX step since the last call (for the performance overlay).
double TakePhysicsMs(int frames) {
    double v = frames > 0 ? g_physAccumMs / frames : 0.0;
    g_physAccumMs = 0.0;
    return v;
}

void SetPhysicsFrozen(bool frozen) {
    g_frozen = frozen;
}

bool InstallPhysicsFreeze() {
    const SiteDef& s = kSitePhysicsStepCall;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("physics freeze: the call at %08X differs from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    g_frozen = false;
    if (!PatchCall(s.va, s.bytes, s.len, (const void*)PhysicsStepHook)) {  // straight to the thiscall hook, no stub needed
        Log("physics freeze: could not patch the call");
        return false;
    }
    g_stepHooked = true;
    Log("physics freeze: the PhysX step stands still while the menu or the photo mode freezes time");
    return true;
}

// The hooks of the whole-step plan. They are installed whatever HighFpsPhysics says, so the menu can switch it at once; with it
// off, and in every frame of a whole step or more, they change nothing. Nothing is patched unless every site matches, and the
// plan cannot act before the three calls it needs are in place.
bool InstallHighFpsPhysics() {
    if (!g_stepHooked) {
        Log("physics steps: the physics step call is not hooked, skipped");
        return false;
    }
    bool match = SiteMatches(kSiteCarDriveCall) && SiteMatches(kSiteCarAfterStepCall) && SiteMatches(kSiteWarmUpFlagRead);
    for (const SiteDef& s : kSiteWorldUpdateCalls) match = match && SiteMatches(s);
    for (const SiteDef& s : kSiteCarTicks) match = match && SiteMatches(s);
    for (const SiteDef& s : kSiteCarRamps) match = match && SiteMatches(s);
    for (const SiteDef& s : kSiteRampConstants) match = match && SiteMatches(s);
    if (!match) return false;
    for (int i = 0; i < 2; ++i) g_ramp[i] = g_gameRamp[i] = *(const double*)(uintptr_t)kSiteRampConstants[i].va;
    // The world update first: it keeps the variables current, which the ramp operands below rely on.
    for (const SiteDef& s : kSiteWorldUpdateCalls)
        if (!PatchCall(s.va, s.bytes, s.len, (const void*)WorldUpdateHook)) {
            Log("physics steps: could not patch the world update call at %08X, skipped", s.va);
            return false;
        }
    if (!PatchCall(kSiteCarDriveCall.va, kSiteCarDriveCall.bytes, kSiteCarDriveCall.len, (const void*)CarDriveHook) ||
        !PatchCall(kSiteCarAfterStepCall.va, kSiteCarAfterStepCall.bytes, kSiteCarAfterStepCall.len, (const void*)CarAfterStepHook)) {
        Log("physics steps: could not patch the car calls, skipped");
        return false;
    }
    g_planReady = true;
    int ticks = 0, gameTicks = 0, ramps = 0;
    if (u8* stub = AllocStub(32)) {
        BuildCarTicksStub(stub, (u32)(uintptr_t)&g_frame.steps, (u32)(uintptr_t)&g_tickDt, kFrameDeltaVa);
        for (const SiteDef& s : kSiteCarTicks)
            if (PatchCall(s.va, s.bytes, 6, stub)) ++ticks;  // the fld alone; the call of _ftol2 behind it stays
        // the rest of the game's (int)dt counters: each one on its own (one that another mod changed is just left out)
        for (const SiteDef& s : kSiteGameTicks)
            if (PatchCall(s.va, s.bytes, 6, stub)) ++gameTicks;
    }
    if (PointOperand(kSiteCarRamps[0], &g_ramp[0])) ++ramps;
    if (PointOperand(kSiteCarRamps[1], &g_ramp[0])) ++ramps;
    if (PointOperand(kSiteCarRamps[2], &g_ramp[1])) ++ramps;
    Log("physics steps: frames shorter than one step of 1/60 s share whole steps (HighFpsPhysics=%d): world update, car control and "
        "car movement hooked, %d of %d car tick counters, %d of %d other tick counters of the game, %d of 3 car sound ramps",
        g_cfg.highFpsPhysics, ticks, (int)(sizeof(kSiteCarTicks) / sizeof(kSiteCarTicks[0])), gameTicks,
        (int)(sizeof(kSiteGameTicks) / sizeof(kSiteGameTicks[0])), ramps);
    return true;
}

}  // namespace ufx
