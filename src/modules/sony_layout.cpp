// DualSense and DualShock 4 shown to the game as an Xbox 360 controller.
//
// DP.exe reads controllers only through winmm's joyGetPosEx and expects the Xbox 360 layout: X/Y left stick,
// U/R right stick, Z both triggers on one axis (centre at rest), buttons in Xbox order. A Sony pad reports the
// right stick on Z/R and its triggers as two separate axes U and V that rest at zero, so the game reads R2 as a
// right stick pushed to the end: the camera turns on its own. The layout (measured on a DualSense) is:
//   X,Y left stick; Z right stick X; R right stick Y; U = R2; V = L2;
//   buttons: 0 square, 1 cross, 2 circle, 3 triangle, 4 L1, 5 R1, 6 L2, 7 R2, 8 create, 9 options, 10 L3, 11 R3.
#include "../base.h"
#include <mmsystem.h>

namespace ufx {
void MarkTriggerButtons();  // stick_profile.cpp: this source set buttons 10/11 itself
namespace {

typedef MMRESULT(WINAPI* GetDevCapsFn)(UINT_PTR, LPJOYCAPSA, UINT);

// What is in each slot, asked once per connection: ForgetPad clears it when a read of the slot fails, so a pad swapped while
// the game runs (DualSense out, Xbox pad in) is looked at again.
enum { kUnknown = 0, kSony = 1, kOther = 2 };
u8 g_kind[16];
bool g_capsOk[16];
JOYCAPSA g_caps[16];

bool IsSony(unsigned id) {
    if (id >= 16) return false;
    if (g_kind[id] == kUnknown) {
        g_kind[id] = kOther;
        HMODULE winmm = GetModuleHandleW(L"winmm.dll");
        GetDevCapsFn getCaps = winmm ? (GetDevCapsFn)(void*)GetProcAddress(winmm, "joyGetDevCapsA") : nullptr;
        g_capsOk[id] = getCaps && getCaps(id, &g_caps[id], sizeof(JOYCAPSA)) == JOYERR_NOERROR;
        if (g_capsOk[id] && g_caps[id].wMid == 0x054C) {
            g_kind[id] = kSony;
            Log("input: pad %u is a Sony controller (product %04X), %s", id, (unsigned)g_caps[id].wPid,
                g_cfg.sonyLayout ? "converting to the Xbox layout" : "left as it is (SonyLayout=0)");
        }
    }
    return g_kind[id] == kSony;
}

// Rescales an axis to the 0..65535 range the game expects.
u32 Norm(u32 v, u32 lo, u32 hi) {
    if (hi <= lo) return v;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (u32)((unsigned long long)(v - lo) * 65535ull / (hi - lo));
}

}  // namespace

// (An unconverted Sony pad, SonyLayout=0, keeps the icons it had before: its buttons are not in the Xbox order the icons follow.)
bool SonyPadActive() { return (g_kind[0] == kSony && g_cfg.sonyLayout) || DualSenseConnected(); }

// The read of this slot failed (pad unplugged): its kind and ranges are asked again on the next successful read.
void ForgetPad(unsigned id) {
    if (id < 16) g_kind[id] = kUnknown;
}

// Whether the Z axis of this WinMM pad is the shared trigger axis the game expects (Xbox layout). Not on an unconverted Sony pad
// (there Z is the right stick X), not on a pad that reports no Z at all, and not on a 4-axis DirectInput pad (X Y Z R: Logitech
// in D mode, 8BitDo D-input and many cheap pads put the right stick on Z/R). An Xbox pad on WinMM reports X Y Z R U.
bool PadZCarriesTriggers(unsigned id) {
    if (id >= 16) return false;
    if (IsSony(id)) return false;
    return !g_capsOk[id] || ((g_caps[id].wCaps & JOYCAPS_HASZ) && (g_caps[id].wCaps & JOYCAPS_HASU));
}

// A Sony pad read through WinMM and left in its own layout (SonyLayout=0): its buttons 10 and 11 are L3 and R3, not the triggers.
bool PadIsRawSony(unsigned id) { return id < 16 && g_kind[id] == kSony && !g_cfg.sonyLayout && !DualSenseConnected(); }

// A Microsoft pad (an Xbox controller seen through WinMM): only then is XInput asked for its separate trigger values.
bool PadIsMicrosoft(unsigned id) { return id < 16 && g_capsOk[id] && g_caps[id].wMid == 0x045E; }

void ConvertSonyLayout(unsigned id, void* raw) {
    if (!g_cfg.sonyLayout || !raw || !IsSony(id)) return;
    JOYINFOEX* j = (JOYINFOEX*)raw;
    if (j->dwSize != sizeof(JOYINFOEX)) return;
    const JOYCAPSA& c = g_caps[id];

    u32 lx = Norm(j->dwXpos, c.wXmin, c.wXmax);
    u32 ly = Norm(j->dwYpos, c.wYmin, c.wYmax);
    u32 rx = Norm(j->dwZpos, c.wZmin, c.wZmax);
    u32 ry = Norm(j->dwRpos, c.wRmin, c.wRmax);
    u32 r2 = Norm(j->dwUpos, c.wUmin, c.wUmax);
    u32 l2 = Norm(j->dwVpos, c.wVmin, c.wVmax);
    u32 l2Click = (j->dwButtons >> 6) & 1, r2Click = (j->dwButtons >> 7) & 1;
    if (g_cfg.swapTriggers) {  // the analog values and the click bits together, or one trigger would set both buttons 10 and 11
        u32 t = l2;
        l2 = r2;
        r2 = t;
        t = l2Click;
        l2Click = r2Click;
        r2Click = t;
    }
    int trig = 32767 + ((int)l2 - (int)r2) / 2;  // both triggers on one axis, centre at rest

    j->dwXpos = lx;
    j->dwYpos = ly;
    j->dwZpos = (DWORD)trig;
    j->dwRpos = ry;
    j->dwUpos = rx;
    j->dwVpos = 0;

    // Sony button bit for each Xbox button 0..9: A, B, X, Y, LB, RB, Back, Start, LS, RS
    static const u8 from[10] = {1, 2, 0, 3, 4, 5, 8, 9, 10, 11};
    DWORD out = 0;
    int count = 0;
    for (int i = 0; i < 10; ++i)
        if ((j->dwButtons >> from[i]) & 1) {
            out |= 1u << i;
            ++count;
        }
    // L2 and R2 as buttons 10 and 11 (pressed past about a quarter, or the digital click): both can be held at once
    if (l2Click || l2 > 16000) { out |= 1u << 10; ++count; }
    if (r2Click || r2 > 16000) { out |= 1u << 11; ++count; }
    j->dwButtons = out;
    j->dwButtonNumber = (DWORD)count;
    if (id == 0) MarkTriggerButtons();
}

}  // namespace ufx
