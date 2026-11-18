// XInput as a fallback source for the controller. The game reads pads only through WinMM (joyGetPosEx). Some pads are
// visible to XInput but not to WinMM (Steam Input and DS4Windows present a virtual Xbox pad, which WinMM may not list).
// When WinMM has no pad 0 and XInput does, we fill the JOYINFOEX the game asked for from the XInput state, in the layout
// of an Xbox 360 pad on WinMM: X/Y left stick, U/R right stick, Z both triggers (LT above the centre), buttons A B X Y LB
// RB Back Start LS RS = 0..9, hat in hundredths of a degree.
#include "../base.h"
#include <mmsystem.h>

namespace ufx {
void MarkTriggerButtons();  // stick_profile.cpp: this source set buttons 10/11 itself
namespace {

struct XState {
    DWORD packet;
    WORD buttons;
    BYTE lt, rt;
    SHORT lx, ly, rx, ry;
};
typedef DWORD(WINAPI* GetStateFn)(DWORD, XState*);

GetStateFn g_get = nullptr;
bool g_loaded = false;
int g_pad = -1;
DWORD g_lastScan = 0;
bool g_said = false;

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    const wchar_t* names[3] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (const wchar_t* n : names) {
        HMODULE m = LoadLibraryW(n);
        if (m && (g_get = (GetStateFn)(void*)GetProcAddress(m, "XInputGetState"))) return;
    }
}

u32 Axis(int v) { return (u32)(v + 32768); }

// The state of the XInput pad; the slots are searched again at most once a second (an empty slot is slow to ask).
bool Poll(XState* s) {
    Load();
    if (!g_get) return false;
    if (g_pad < 0 || g_get((DWORD)g_pad, s) != 0) {
        g_pad = -1;
        const DWORD now = GetTickCount();
        if (now - g_lastScan < 1000) return false;
        g_lastScan = now;
        for (int i = 0; i < 4; ++i)
            if (g_get((DWORD)i, s) == 0) {
                g_pad = i;
                break;
            }
        if (g_pad < 0) return false;
    }
    return true;
}

}  // namespace

// The two trigger values (0..255) of the XInput pad, physical (no swap). For an Xbox pad read through WinMM, which has both
// triggers on one axis: there L2 + R2 together look like no trigger at all (stick_profile.cpp).
bool XInputTriggers(unsigned char* lt, unsigned char* rt) {
    XState s;
    if (!Poll(&s)) return false;
    *lt = s.lt;
    *rt = s.rt;
    return true;
}

// Returns true and fills `j` when an XInput pad is connected.
bool XInputToJoy(void* raw) {
    JOYINFOEX* j = (JOYINFOEX*)raw;
    if (!j || j->dwSize != sizeof(JOYINFOEX)) return false;
    XState s;
    if (!Poll(&s)) return false;
    if (g_cfg.swapTriggers) {  // as for the DualSense (dualsense.cpp, sony_layout.cpp)
        BYTE t = s.lt;
        s.lt = s.rt;
        s.rt = t;
    }
    if (!g_said) {
        g_said = true;
        Log("input: no WinMM pad, using XInput pad %d", g_pad);
    }
    j->dwXpos = Axis(s.lx);
    j->dwYpos = 65535 - Axis(s.ly);   // XInput up is positive, WinMM up is small
    j->dwUpos = Axis(s.rx);
    j->dwRpos = 65535 - Axis(s.ry);
    j->dwZpos = (u32)(32767 + ((int)s.lt - (int)s.rt) * 128);  // LT above the centre, RT below
    j->dwVpos = 32767;
    static const u16 mask[10] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080};
    DWORD b = 0;
    int count = 0;
    for (int i = 0; i < 10; ++i)
        if (s.buttons & mask[i]) {
            b |= 1u << i;
            ++count;
        }
    if (s.lt > 64) { b |= 1u << 10; ++count; }  // LT / RT as buttons of their own (the shared axis cannot carry both)
    if (s.rt > 64) { b |= 1u << 11; ++count; }
    j->dwButtons = b;
    j->dwButtonNumber = (DWORD)count;
    const bool up = s.buttons & 1, down = s.buttons & 2, left = s.buttons & 4, right = s.buttons & 8;
    DWORD pov = 0xFFFF;
    if (up && right) pov = 4500;
    else if (right && down) pov = 13500;
    else if (down && left) pov = 22500;
    else if (left && up) pov = 31500;
    else if (up) pov = 0;
    else if (right) pov = 9000;
    else if (down) pov = 18000;
    else if (left) pov = 27000;
    j->dwPOV = pov;
    MarkTriggerButtons();
    return true;
}

}  // namespace ufx
