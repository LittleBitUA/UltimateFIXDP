// Xbox 360 stick profile.
//
// The PC port reads a stick in two stages: an axis counts only beyond ~40% of its travel and then jumps to ~0.32
// (FUN_006B1780), and the result is slewed by 0.5 per poll (FUN_00709400), which depends on the frame rate. The
// console used a smooth 24% dead zone without slew, and its camera code added a 0.25 dead zone with a 4/3 rescale
// on the right stick. With the profile on:
//   1. after FUN_00709400 we overwrite the four final axis floats from the raw pad values the game was given;
//   2. the four right-stick reads of the aiming camera get the 0.25 / 4/3 shaping (small machine-code thunks).
// Only in controller mode (USEJOY = 1); in keyboard mode the same floats carry mouse movement.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"
#include <mmsystem.h>

namespace ufx {
namespace {

struct Pad {
    u32 x, y, u, r;  // 0..65535, centre 32768
    bool valid;
};
Pad g_pad;

// Read by the machine-code thunks.
volatile u8 g_profileFlag = 0;
const float kQuarter = 0.25f;
const float kFourThirds = 1.3333334f;
u32 g_retSave[4];

typedef void(__fastcall* EvalFn)(void* self, void* edx, int slot, void* rec);
EvalFn g_orig = nullptr;

// Console normalisation: dead zone 7864 of 32768, full travel maps to 1.0 (divisor 24903).
float Curve(u32 raw) {
    const int dead = 7864;
    int s = (int)raw - 32768;
    float v = 0.f;
    if (s > dead)
        v = (float)(s - dead) * (1.0f / 24903.0f);
    else if (s < -dead)
        v = (float)(s + dead + 1) * (1.0f / 24903.0f);
    if (v > 1.f) v = 1.f;
    if (v < -1.f) v = -1.f;
    return v;
}

// Mouse look speed (keyboard mode, where the right stick values carry the mouse): the game clamps them to +-2 and slews them by
// 0.5 a poll, so a faster mouse must be applied after that. The evaluator slews from its last result, so it gets back the
// unscaled value before each run and the scaled one is left only for the reads that follow (the camera).
float g_mouseRaw[2];
bool g_mouseScaled = false;

void __fastcall StickHook(void* self, void* edx, int slot, void* rec) {
    float* acc = (float*)((u8*)self + kInputStickOffset);  // slot 0
    if (slot == 0 && g_mouseScaled) {
        acc[2] = g_mouseRaw[0];
        acc[3] = g_mouseRaw[1];
        g_mouseScaled = false;
    }
    g_orig(self, edx, slot, rec);
    if (slot == 0 && !*(volatile u8*)(uintptr_t)kUseJoyVa) {
        const float speed = MouseLookSpeed();
        if (speed != 1.0f) {
            g_mouseRaw[0] = acc[2];
            g_mouseRaw[1] = acc[3];
            acc[2] *= speed;
            acc[3] *= speed;
            g_mouseScaled = true;
        }
    }
    g_profileFlag = g_cfg.stickProfile ? 1 : 0;
    if (!g_cfg.stickProfile || slot != 0 || !g_pad.valid) return;
    if (!*(volatile u8*)(uintptr_t)kUseJoyVa) return;
    // The internal Y axes are "down positive", like the raw WinMM values (up = small), so no sign change.
    float* dst = (float*)((u8*)self + kInputStickOffset + (u32)slot * 0x4C);
    dst[0] = Curve(g_pad.x);  // left stick X
    dst[1] = Curve(g_pad.y);  // left stick Y
    dst[2] = Curve(g_pad.u);  // right stick X
    dst[3] = Curve(g_pad.r);  // right stick Y
}

}  // namespace

// Called by the joyGetPosEx hook with what the game is about to receive for pad 0 (null = no pad).
static unsigned g_buttons = 0;
unsigned PadButtons() { return g_pad.valid ? g_buttons : 0; }

bool TriggerLayoutActive();
bool XInputTriggers(unsigned char* lt, unsigned char* rt);
bool PadZCarriesTriggers(unsigned id);
bool PadIsMicrosoft(unsigned id);
void ForgetPad(unsigned id);

// Set by the sources that put L2/R2 on buttons 10/11 themselves (DualSenseToJoy, XInputToJoy, ConvertSonyLayout) for the pad-0
// data the next PadCapture receives.
static bool g_sourceTriggerButtons = false;
void MarkTriggerButtons() { g_sourceTriggerButtons = true; }

namespace {

// A WinMM pad in the Xbox layout (an Xbox pad, Steam Input or DS4Windows as a virtual Xbox pad, most other pads) reports L2 and
// R2 only on the shared Z axis (LT above the centre, RT below), never as buttons 10 and 11, and aim and fire of the trigger layout
// are bound to those buttons (combat_strafe.cpp). They are made here. Z alone cannot carry both triggers: L2 + R2 together read
// as the centre, which would drop the aim the moment York fires. So the two separate values of the XInput pad are used when they
// agree with Z (the same pad, or an idle one); otherwise Z. XInput is asked only for a Microsoft pad (an Xbox pad, or the virtual
// Xbox pad of Steam Input / DS4Windows): other pads have no XInput values, and asking empty XInput slots is slow. The buttons are
// added only while the trigger layout uses them; on a pad with real buttons 10 and 11 they would otherwise press those too.
void TriggerButtonsFromAxis(JOYINFOEX* j) {
    if (!PadZCarriesTriggers(0)) return;
    const int z = (int)j->dwZpos - 32767;
    int lt = z > 0 ? z / 128 : 0, rt = z < 0 ? -z / 128 : 0;  // 0..255
    unsigned char xl = 0, xr = 0;
    if (PadIsMicrosoft(0) && XInputTriggers(&xl, &xr)) {
        int d = ((int)xl - (int)xr) * 128 - z;
        if (d < 0) d = -d;
        if (d < 12000) {
            lt = xl;
            rt = xr;
        }
    }
    if (g_cfg.swapTriggers) {  // as on the other sources: the buttons and the axis the game reads as LT/RT
        int t = lt;
        lt = rt;
        rt = t;
        j->dwZpos = 65535 - j->dwZpos;
    }
    if (!TriggerLayoutActive()) return;
    if (lt > 64) j->dwButtons |= 1u << 10;
    if (rt > 64) j->dwButtons |= 1u << 11;
    int count = 0;
    for (int i = 0; i < 32; ++i) count += (j->dwButtons >> i) & 1;
    j->dwButtonNumber = (DWORD)count;
}

}  // namespace

void PadCapture(const void* raw) {
    JOYINFOEX* j = (JOYINFOEX*)raw;
    const bool fromSource = g_sourceTriggerButtons;
    g_sourceTriggerButtons = false;
    if (!j) ForgetPad(0);  // the read of pad 0 failed: the next pad there is identified again
    const bool valid = j && j->dwSize == sizeof(JOYINFOEX);
    // Before Z is pinned below: the trigger buttons come from it. Not while the menu or the free camera hand the game a neutral pad.
    if (valid && !fromSource && !ui::IsOpen() && !FreeCamBlocksInput()) TriggerButtonsFromAxis(j);
    TriggerCapture(raw, ui::IsOpen());
    // With aim and fire on the trigger BUTTONS (10 and 11), the shared trigger axis must stay at rest: the game also reads it as its
    // own LT (aim without a weapon, running) and RT (first-person view), and R2 would then look through York's eyes while firing.
    if (valid && TriggerLayoutActive() && !ui::IsOpen()) j->dwZpos = 32767;
    if (!valid) {
        g_pad.valid = false;
        return;
    }
    g_buttons = j->dwButtons;
    g_pad.x = j->dwXpos;
    g_pad.y = j->dwYpos;
    g_pad.u = j->dwUpos;
    g_pad.r = j->dwRpos;
    g_pad.valid = true;
}

bool InstallStickProfile() {
    const SiteDef& e = kSiteStickEval;
    if (!BytesMatch(e.va, e.bytes, e.len)) {
        Log("stick profile: bytes at %08X differ from DP.exe 1.01b, skipped", e.va);
        return false;
    }
    for (const SiteDef& s : kSiteAimStick)
        if (!BytesMatch(s.va, s.bytes, s.len)) {
            Log("stick profile: bytes at %08X differ from DP.exe 1.01b, skipped", s.va);
            return false;
        }
    u8* orig = AllocStub(32);
    if (!orig) return false;
    BuildStickOriginal(orig, (u32)(uintptr_t)orig, kStickEvalReturn);
    g_orig = (EvalFn)(void*)orig;
    int shaped = 0;
    for (int i = 0; i < 4; ++i) {
        u8* thunk = AllocStub(96);
        if (!thunk) break;
        BuildAimShapeThunk(thunk, (u32)(uintptr_t)thunk, kStickGetterFn, (u32)(uintptr_t)&g_retSave[i], kUseJoyVa,
                           (u32)(uintptr_t)&g_profileFlag, (u32)(uintptr_t)&kQuarter, (u32)(uintptr_t)&kFourThirds);
        if (PatchCall(kSiteAimStick[i].va, kSiteAimStick[i].bytes, kSiteAimStick[i].len, thunk)) ++shaped;
    }
    g_profileFlag = g_cfg.stickProfile ? 1 : 0;
    if (!PatchJump(e.va, e.bytes, e.len, (const void*)StickHook)) {
        Log("stick profile: could not hook the stick evaluator");
        return false;
    }
    Log("stick profile: Xbox 360 stick evaluator hooked (%s), aim shaping on %d of 4 reads",
        g_cfg.stickProfile ? "on" : "off, switch it in the menu", shaped);
    return true;
}

}  // namespace ufx
