// Freezes the physics of the game while the F10 menu is open (and in the photo mode).
//
// The game steps its PhysX scene from the main thread, from FUN_006EB3C0 called by the object update phase. Freezing the game
// clock alone leaves the physics running (a car keeps rolling), so the call itself is skipped while a flag is set.
#include "../base.h"
#include "../sites.h"

namespace ufx {
namespace {
volatile bool g_frozen = false;  // the call is replaced only when the bytes matched, so the flag alone does nothing otherwise
double g_physAccumMs = 0.0;  // time spent in the PhysX step since the overlay last took it

typedef void(__attribute__((thiscall)) * StepFn)(void* self, float dt);  // FUN_006EB3C0 is a thiscall: ECX is kept in a local and used later

// Replaces the call of the physics step (thiscall: ECX = the physics object, one float argument = the frame step): skipped while
// frozen, otherwise timed. Same convention as the original, so the callee pops its argument.
void __attribute__((thiscall)) PhysicsStepHook(void* self, float dt) {
    if (g_frozen) return;
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
    Log("physics freeze: the PhysX step stands still while the menu or the photo mode freezes time");
    return true;
}

}  // namespace ufx
