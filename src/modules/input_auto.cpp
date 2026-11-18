// Automatic keyboard <-> gamepad switching.
//
// DP.exe uses the controller only when a flag (USEJOY, byte 014810F0h) is set, which happens only if the ninth value
// in configJ.cnf is non-zero. Without that file a connected pad is dead for the game. We set the flag when the
// pad is used and clear it when the keyboard is used. Runs inside the joyGetPosEx hook, i.e. on the game's own
// thread once per input poll.
#include "../base.h"
#include "../sites.h"
#include <mmsystem.h>

namespace ufx {
namespace {

constexpr unsigned kPadReleaseMs = 500;  // keyboard may take over only after this long without pad activity

DWORD g_lastPadActive = 0;
unsigned g_lastResult = 0xFFFFFFFF;
bool g_identified = false;
u8 g_keysPrev[256];
bool g_keysKnown = false;
DWORD g_rest[5];          // each axis as first seen after the pad appeared, and again after it has been still for a while
bool g_restKnown = false;
DWORD g_stillAxes[5];     // the axes when the pad last stopped moving
DWORD g_stillSince = 0;   // GetTickCount of that moment, 0 = moving (or not known)
constexpr int kStillJitter = 1500;
constexpr DWORD kStillMs = 2000;

// A stick or trigger held at the first poll would be taken as its rest, and once released it would look like constant pad use.
// So the rest is taken again whenever the pad has lain still (no button, no POV, every axis within a small jitter) for 2 seconds.
void RefreshRestWhenStill(const JOYINFOEX* j, const DWORD* axes) {
    const bool pov = j->dwPOV != 0xFFFF && j->dwPOV != 0xFFFFFFFF && (j->dwFlags & JOY_RETURNPOV);
    bool still = !j->dwButtons && !pov && g_stillSince != 0;
    for (int i = 0; still && i < 5; ++i) {
        int d = (int)axes[i] - (int)g_stillAxes[i];
        if (d < -kStillJitter || d > kStillJitter) still = false;
    }
    const DWORD now = GetTickCount();
    if (!still) {
        for (int i = 0; i < 5; ++i) g_stillAxes[i] = axes[i];
        g_stillSince = (j->dwButtons || pov) ? 0 : (now | 1);
        return;
    }
    if (now - g_stillSince >= kStillMs)
        for (int i = 0; i < 5; ++i) g_rest[i] = axes[i];
}

// An axis that rests at the centre counts when it leaves the centre. One that does not rest there (the separate trigger axes of an
// unconverted Sony pad, a pedal, an axis the pad does not have) counts only when it moves away from where it rested: otherwise it
// looks like constant pad use and the keyboard could never take over.
bool PadActive(const JOYINFOEX* j) {
    const int center = 32767, limit = 9000;
    const DWORD axes[5] = {j->dwXpos, j->dwYpos, j->dwZpos, j->dwRpos, j->dwUpos};
    if (!g_restKnown) {
        for (int i = 0; i < 5; ++i) g_rest[i] = axes[i];
        g_restKnown = true;
        g_stillSince = 0;
    }
    RefreshRestWhenStill(j, axes);
    if (j->dwButtons) return true;
    if (j->dwPOV != 0xFFFF && j->dwPOV != 0xFFFFFFFF && (j->dwFlags & JOY_RETURNPOV)) return true;
    for (int i = 0; i < 5; ++i) {
        int r = (int)g_rest[i] - center;
        if (r < 0) r = -r;
        int d = (int)axes[i] - (r <= limit ? center : (int)g_rest[i]);
        if (d < 0) d = -d;
        if (d > limit) return true;
    }
    return false;
}

// Keys that do not mean "the player switched to the keyboard": modifiers (also the left/right variants, which report the same
// keys), the Windows keys, screenshots (Steam F12, Print Screen) and the mod's own hotkeys, which are pressed while playing on
// the pad.
bool IgnoredKey(int vk) {
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2) return true;
    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || (vk >= VK_LSHIFT && vk <= VK_RMENU)) return true;
    if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_SNAPSHOT || vk == VK_F12) return true;
    if (vk == VK_F9 || vk == VK_F10) return true;  // F9: the sampling profiler, F10: the menu
    return vk == g_cfg.waypointKey || vk == g_cfg.screenshotKey || vk == g_cfg.freeCamKey || vk == g_cfg.freeCamHudKey;
}

// True when a key went down since the last call. Mouse buttons are not keys here.
bool KeyboardEdge(SHORT(WINAPI* getKey)(int)) {
    bool edge = false;
    for (int vk = 8; vk < 255; ++vk) {
        if (IgnoredKey(vk)) continue;
        bool down = (getKey(vk) & 0x8000) != 0;
        if (down && g_keysKnown && !g_keysPrev[vk]) edge = true;
        g_keysPrev[vk] = down ? 1 : 0;
    }
    g_keysKnown = true;
    return edge;
}

void Identify(unsigned id) {
    typedef MMRESULT(WINAPI * GetDevCapsFn)(UINT_PTR, LPJOYCAPSA, UINT);
    HMODULE winmm = GetModuleHandleW(L"winmm.dll");
    GetDevCapsFn getCaps = winmm ? (GetDevCapsFn)(void*)GetProcAddress(winmm, "joyGetDevCapsA") : nullptr;
    JOYCAPSA caps;
    if (getCaps && getCaps(id, &caps, sizeof(caps)) == JOYERR_NOERROR)
        Log("input: pad %u found: vendor %04X product %04X \"%s\", %u axes, %u buttons", id, (unsigned)caps.wMid,
            (unsigned)caps.wPid, caps.szPname, (unsigned)caps.wNumAxes, (unsigned)caps.wNumButtons);
}

}  // namespace

void AutoInputTick(unsigned id, const void* raw, unsigned result, SHORT(WINAPI* getKey)(int)) {
    if (id != 0) return;
    if (result != 0) g_restKnown = false;  // the next pad (or the same one again) sets its own rest values
    if (result != g_lastResult) {
        Log("input: joyGetPosEx(0) -> %u%s", result, result == 0 ? " (pad connected)" : " (no pad)");
        g_lastResult = result;
        if (result == 0 && !g_identified) {
            g_identified = true;
            Identify(0);
        }
    }
    if (!g_cfg.autoSwitchInput) return;
    volatile u8* useJoy = (volatile u8*)(uintptr_t)kUseJoyVa;
    HWND w = GameWindowHandle();
    if (!w || GetForegroundWindow() != w) {
        g_keysKnown = false;  // do not count keys pressed elsewhere
        return;
    }
    const DWORD now = GetTickCount();
    bool padActive = result == 0 && raw && ((const JOYINFOEX*)raw)->dwSize == sizeof(JOYINFOEX) && PadActive((const JOYINFOEX*)raw);
    if (padActive) {
        g_lastPadActive = now;
        if (!*useJoy) {
            *useJoy = 1;
            Log("input: gamepad used, switched to gamepad mode");
        }
        return;
    }
    if (*useJoy && now - g_lastPadActive > kPadReleaseMs && KeyboardEdge(getKey)) {
        *useJoy = 0;
        Log("input: keyboard used, switched to keyboard mode");
    }
}

}  // namespace ufx
