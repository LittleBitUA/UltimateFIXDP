// Controller vibration. The game computes rumble (hits, cars, menu test) and keeps the two motor values in a
// singleton, but the PC port never sends them anywhere. We read those values once per frame and send them to the
// pad through XInput (Xbox pads, and DualSense/DualShock when Steam Input or DS4Windows present them as XInput).
//
//   motors:  CInput_Actuator (pointer at 0148CA34h): +28h strong (0..65535), +2Ch weak (0..1023, the game shifts the
//            16-bit value right by 6 before storing it)
//   gate:    CRdInput (pointer at 00BD7660h): +4 the user's "Vibration" option, +0Ch the remaining time (frames);
//            the console treated a timer of exactly 0 as "motors off", so do we
#include "../base.h"
#include "../sites.h"

namespace ufx {
void DsTriggerAxes(unsigned char* l2, unsigned char* r2);
void DsStopAll(unsigned waitMs);
bool PlayerStateNow(u32* state);
int WeaponClassNow();
namespace {

constexpr u32 kActuatorPtrVa = 0x0148CA34;
constexpr u32 kRdInputPtrVa = 0x00BD7660;

struct XVibration {
    WORD left, right;
};
typedef DWORD(WINAPI* SetStateFn)(DWORD, XVibration*);
typedef DWORD(WINAPI* GetStateFn)(DWORD, void*);

SetStateFn g_set = nullptr;
GetStateFn g_get = nullptr;
bool g_loaded = false;
int g_pad = -1;
DWORD g_lastScan = 0, g_lastSend = 0;
WORD g_sentL = 0, g_sentR = 0;

u32 U(u32 base, u32 off) { return *(volatile const u32*)(uintptr_t)(base + off); }

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    const wchar_t* names[3] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (const wchar_t* n : names) {
        HMODULE m = LoadLibraryW(n);
        if (!m) continue;
        g_set = (SetStateFn)(void*)GetProcAddress(m, "XInputSetState");
        g_get = (GetStateFn)(void*)GetProcAddress(m, "XInputGetState");
        if (g_set && g_get) {
            Log("vibration: XInput loaded (%S)", n);
            return;
        }
    }
    g_set = nullptr;
    Log("vibration: XInput is not available");
}

void Send(WORD l, WORD r, bool force) {
    if (!g_set || g_pad < 0) return;
    const DWORD now = GetTickCount();
    if (!force && l == g_sentL && r == g_sentR && now - g_lastSend < 1000) return;
    XVibration v = {l, r};
    if (g_set((DWORD)g_pad, &v) != 0) {
        g_pad = -1;  // disconnected: look again
        return;
    }
    g_sentL = l;
    g_sentR = r;
    g_lastSend = now;
}

// DualSense: rumble made from the game state, added to what the game itself asks for. A car hums with the throttle; every shot of a
// ranged weapon kicks the strong motor (heavier weapons harder and longer), automatic weapons buzz while the fire input is held.
void SynthHaptics(u32& strong, u32& weak) {
    static bool prevFire = false;
    static DWORD kickUntil = 0;
    static u32 kickPower = 0;
    u8 l2 = 0, r2 = 0;
    DsTriggerAxes(&l2, &r2);  // the physical triggers: with SwapTriggers the gas and the fire trigger are the left one
    if (g_cfg.swapTriggers) {
        u8 t = l2;
        l2 = r2;
        r2 = t;
    }
    u32 state = 0;
    if (!PlayerStateNow(&state)) { prevFire = false; return; }
    const DWORD now = GetTickCount();
    const u32 pct = (u32)(g_cfg.vibrationStrength < 0 ? 0 : g_cfg.vibrationStrength > 100 ? 100 : g_cfg.vibrationStrength);
    if (state == 0x87) {  // driving
        if (r2 > 20) {
            u32 hum = (12 + (u32)r2 * 48 / 255) * pct / 100;
            if (hum > weak) weak = hum;
        }
        prevFire = false;
        return;
    }
    bool aiming = state >= 0x14 && state <= 0x2B;
    // Fire is whatever the attack binding says: button 11 (R2) with the trigger layout, a shoulder button with the game's own
    // layout. Bindings 29h..38h are axes; any other value tests that pad button (FUN_006B1780). PadButtons() is what the game
    // was handed, swap and conversions included.
    const u32 bind = *(volatile const u32*)(uintptr_t)kBindAttack;
    const bool fireInput = (bind >= 0x29 && bind <= 0x38) ? r2 > 64 : ((PadButtons() >> (bind & 0x1F)) & 1) != 0;
    bool fire = aiming && fireInput;
    if (fire && !prevFire) {  // the trigger went down: one shot
        int cls = WeaponClassNow();
        u32 power = 0, ms = 0;
        switch (cls) {
            case 4: power = 120; ms = 70; break;    // pistols
            case 6: power = 255; ms = 140; break;   // shotgun
            case 9: power = 255; ms = 220; break;   // RPG
            case 5: power = 90; ms = 50; break;     // SMG (the first shot)
            default: break;
        }
        if (state == 0x1C) { power = 200; ms = 100; }  // .357 Magnum
        if (power) { kickPower = power * pct / 100; kickUntil = now + ms; }
    }
    prevFire = fire;
    if (fire) {  // automatic weapons buzz while held
        int cls = WeaponClassNow();
        u32 hold = cls == 5 ? 70 : cls == 10 ? 50 : cls == 11 ? 60 : 0;
        hold = hold * pct / 100;
        if (hold > strong) strong = hold;
    }
    if ((int)(kickUntil - now) > 0 && kickPower > strong) strong = kickPower;
}

}  // namespace

void StopVibration() {
    if (g_set && g_pad >= 0 && (g_sentL || g_sentR)) {
        XVibration v = {0, 0};
        g_set((DWORD)g_pad, &v);
        g_sentL = g_sentR = 0;
    }
    DsStopAll(100);  // a DualSense over HID: motors and trigger effects off before the window closes or loses focus
}

// Once per frame, on the game thread.
void VibrationFrame() {
    if (DsOutputReady()) {  // a DualSense written over HID: no XInput pad needed
        HWND hw = GameWindowHandle();
        // The game in front, in pad mode, no menu: otherwise R2 pressed in another program would hum or kick the pad.
        const bool front = hw && GetForegroundWindow() == hw && *(volatile u8*)(uintptr_t)kUseJoyVa && !ui::IsOpen();
        u32 actuator = U(kActuatorPtrVa, 0), rd = U(kRdInputPtrVa, 0);
        bool active = g_cfg.vibration && front && actuator && rd && U(rd, 4) != 0 && U(rd, 0xC) != 0;
        u32 strong = 0, weak = 0;
        if (active) {
            u32 a = U(actuator, 0x28), b = U(actuator, 0x2C);
            if (a > 65535) a = 65535;
            strong = a >> 8;
            weak = (b >= 1023 ? 65535u : (b * 65535 + 511) / 1023) >> 8;
            const u32 pct = (u32)(g_cfg.vibrationStrength < 0 ? 0 : g_cfg.vibrationStrength > 100 ? 100 : g_cfg.vibrationStrength);
            strong = strong * pct / 100;
            weak = weak * pct / 100;
        }
        // The car and shot haptics are an option of their own: they work with the game's vibration off as well.
        if (g_cfg.synthHaptics && front) SynthHaptics(strong, weak);
        DsSetRumble((unsigned char)strong, (unsigned char)weak);
        return;
    }
    if (!g_cfg.vibration) {
        StopVibration();
        return;
    }
    Load();
    if (!g_set) return;
    const DWORD now = GetTickCount();
    if (g_pad < 0) {
        if (now - g_lastScan < 1000) return;
        g_lastScan = now;
        char state[32];
        for (int i = 0; i < 4; ++i)
            if (g_get((DWORD)i, state) == 0) {
                g_pad = i;
                Log("vibration: XInput pad %d found", i);
                break;
            }
        if (g_pad < 0) return;
    }
    u32 actuator = U(kActuatorPtrVa, 0), rd = U(kRdInputPtrVa, 0);
    bool active = actuator && rd && U(rd, 4) != 0 && U(rd, 0xC) != 0 && *(volatile u8*)(uintptr_t)kUseJoyVa &&
                  !ui::IsOpen();
    HWND w = GameWindowHandle();
    if (!w || GetForegroundWindow() != w) active = false;
    if (!active) {
        Send(0, 0, false);
        return;
    }
    u32 a = U(actuator, 0x28), b = U(actuator, 0x2C);
    if (a > 65535) a = 65535;
    u32 strong = a;
    u32 weak = b >= 1023 ? 65535 : (b * 65535 + 511) / 1023;
    const u32 pct = (u32)(g_cfg.vibrationStrength < 0 ? 0 : g_cfg.vibrationStrength > 100 ? 100 : g_cfg.vibrationStrength);
    Send((WORD)(strong * pct / 100), (WORD)(weak * pct / 100), false);
}

}  // namespace ufx
