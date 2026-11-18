// Analog gas and brake in vehicles. The PC port reduces the two shoulder inputs of the controller to on/off in three
// car functions; the console used analog values. We hand those functions the trigger values (0..1) instead.
//
// The pad reports both triggers on one axis (Z: LT above the centre, RT below). The value comes from there, after
// the DualSense conversion, so it is the same data the game sees. Pressing both at once cancels out (rare in a car).
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"
#include <mmsystem.h>

namespace ufx {
namespace {

volatile float g_lt = 0.f, g_rt = 0.f;
volatile u8 g_valid = 0;

}  // namespace

// Called from PadCapture with what the game is about to receive for pad 0 (null = none).
void TriggerCapture(const void* raw, bool menuOpen) {
    const JOYINFOEX* j = (const JOYINFOEX*)raw;
    if (!j || j->dwSize != sizeof(JOYINFOEX) || !g_cfg.analogTriggers || menuOpen) {
        g_valid = 0;
        g_lt = g_rt = 0.f;
        return;
    }
    const int z = (int)j->dwZpos - 32767;
    int lt = z > 0 ? z * 255 / 32768 : 0;
    int rt = z < 0 ? -z * 255 / 32768 : 0;
    const int dead = g_cfg.triggerDeadzone;  // the console's: raw <= 30 counts as released
    g_lt = lt > dead ? (float)lt * (1.0f / 255.0f) : 0.f;
    g_rt = rt > dead ? (float)rt * (1.0f / 255.0f) : 0.f;
    g_valid = 1;
    // Diagnostic: what the triggers read, logged when the (rounded) values change; at most 60 lines.
    static int lastL = -1, lastR = -1, lines = 0;
    int l = (int)(g_lt * 10.f + 0.5f), r = (int)(g_rt * 10.f + 0.5f);
    if ((l != lastL || r != lastR) && lines < 60) {
        lastL = l;
        lastR = r;
        ++lines;
        Log("triggers: Z axis %u -> LT %d%% RT %d%%", (unsigned)j->dwZpos, l * 10, r * 10);
    }
}

bool InstallVehicleTriggers() {
    const SiteDef* sites[3] = {&kSiteTrig1, &kSiteTrig2, &kSiteTrig3};
    for (const SiteDef* s : sites)
        if (!BytesMatch(s->va, s->bytes, s->len)) {
            Log("triggers: bytes at %08X differ from DP.exe 1.01b, skipped", s->va);
            return false;
        }
    u8* st1 = AllocStub(64);
    u8* st2 = AllocStub(80);
    u8* st3 = AllocStub(64);
    if (!st1 || !st2 || !st3) return false;
    const u32 use = kUseJoyVa, valid = (u32)(uintptr_t)&g_valid, lt = (u32)(uintptr_t)&g_lt, rt = (u32)(uintptr_t)&g_rt;
    BuildTrigStub1(st1, (u32)(uintptr_t)st1, use, valid, lt, rt, kTrig1Keyboard, kTrig1Pad, kTrig1Merge);
    BuildTrigStub2(st2, (u32)(uintptr_t)st2, use, valid, lt, rt, kTrig2Resume, kTrig2Merge);
    BuildTrigStub3(st3, (u32)(uintptr_t)st3, use, valid, lt, rt, kTrig3Keyboard, kTrig3Pad, kTrig3Merge);
    if (!PatchJump(kSiteTrig1.va, kSiteTrig1.bytes, kSiteTrig1.len, st1) ||
        !PatchJump(kSiteTrig2.va, kSiteTrig2.bytes, kSiteTrig2.len, st2) ||
        !PatchJump(kSiteTrig3.va, kSiteTrig3.bytes, kSiteTrig3.len, st3)) {
        Log("triggers: could not patch the car functions");
        return false;
    }
    Log("triggers: analog gas and brake installed in 3 car functions (%s)", g_cfg.analogTriggers ? "on" : "off");
    return true;
}

}  // namespace ufx
