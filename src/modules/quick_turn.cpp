// Quick turn, restored. On the Xbox 360 York turned around on the spot when the stick was pulled back and X (the run
// button) was pressed: player state 0Bh. The PC code still has the state and the call that enters it from the idle
// state, but the idle branch waits for a flag the PC idle code never sets and for L3 instead of X.
//
// The console moved York like a tank (the stick turned him, "back" walked backwards), so "the stick is pulled back"
// meant "the stick points away from where York faces". The PC moves him relative to the camera and turns him towards
// the stick by itself, about 690 degrees a second (00523D6A: at most 0.2 rad per 1/60 s), so the console rule read
// literally would spin him every time the player starts to run towards the camera. The rule is therefore kept in York's
// own terms: the stick is pulled back (the console's 150 degrees), the direction it asks for (camera yaw + stick angle,
// what the game's own steering turns him to) lies more than 150 degrees away from his heading, and the logical X is
// pressed. Because he starts turning the moment the stick moves, a press counts for a short while after that
// moment, and the turn goes to the opposite of the heading he had then: the console's "heading + pi". In practice the
// press has to come together with the stick or within about 0.2 s: at 0.2 rad per 1/60 s York has less than 45 degrees
// left after 12 ticks, and then the game's own steering is left to finish.
//
// The gate sits where the game's own test is (00503FA7), after the weapon wheel and before the run and breath buttons,
// so a turn leaves FUN_00503C60 the way the game's own path does. Only the idle state gets it; the weapon states of the
// console's rule (14h, 16h, 18h, 24h, 26h: a weapon of class 1, 2, 3, 7 or 11 held ready, with the aiming camera) keep
// the PC code untouched.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {
namespace {

typedef unsigned(__stdcall* CapabilityFn)(unsigned capability);                                // FUN_004FEE60: ret 4, ecx not read
typedef void(__attribute__((thiscall)) * StateChangeFn)(void* player, int state);             // FUN_00528F40: ret 4
typedef int(__attribute__((thiscall)) * LogicalPressFn)(void* manager, unsigned logicalMask);  // FUN_004547B0: ret 4 (004547EA)
typedef unsigned(__attribute__((thiscall)) * InputMaskFn)(void* input, unsigned pad, unsigned kind, unsigned mask);  // FUN_00708910: ret 0Ch
typedef float(__attribute__((thiscall)) * InputStickFn)(void* input, unsigned pad, unsigned stick, unsigned axis);   // FUN_00708A30: ret 0Ch

const float kPi = 3.14159265f;
const float kBackAngle = 2.6179938f;  // 150 degrees: the game's constant at 00775F78h, the console's DAT_82004E5C
const float kLeastTurn = 0.7853982f;  // 45 degrees: with less left to turn, the game's own steering has already done the job
// How long after the stick was pulled back the press still counts. While the game's steering runs, kLeastTurn ends the
// window earlier (about 0.2 s); this bound is for the ticks when it does not run (00522FADh: the counter at 008A971Ch below 10).
const DWORD kChordMs = 300;

const unsigned kStickBackBit = 0x8000;  // left stick Y beyond +50/128 (FUN_00709130), the bit the PC weapon branch asks for at 005042E4
// The console's X in the PC numbering: the idle branch asks for it held to run (005041D8) and the weapon branch asks for its
// press to turn (005043EF). The table at manager+82EB1Ch (FUN_004546F0) sends it to the run button: 400000h by default
// (00455970), which is Left Shift on the keyboard (0070B8AB) and the left trigger, or L1 with TriggerFire, on a pad.
const unsigned kLogicalX = 0x2000;

float Atan2(float y, float x) { float r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
float Abs(float x) { return x < 0 ? -x : x; }

// Brings an angle into [-pi, pi]. False for what is not an angle (NaN, a huge number): the callers then do nothing.
bool WrapAngle(float a, float* out) {
    if (!(a > -64.0f && a < 64.0f)) return false;
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    *out = a;
    return true;
}

// The state 0Bh handler turns by row[+8] degrees per 1/60 s from the movement table at player+694h (14h bytes a row; row
// 1Bh or 39h by bit 1 of manager+8C5ECh, plus the fatigue class at player+650h) and ends only when the heading has reached
// the target (00524C4A, 00524D5A). A row without a real speed would keep York in that state for good, so it is looked at first.
bool TurnSpeedUsable(u8* player, u8* mgr) {
    u8* table = *(u8* volatile*)(player + 0x694);
    int fatigue = *(volatile int*)(player + 0x650);
    if ((uintptr_t)table < 0x10000 || fatigue < 0 || fatigue > 2) return false;
    int row = fatigue + ((*(volatile u32*)(mgr + 0x8C5EC) >> 1 & 1) ? 0x1B : 0x39);
    float speed = *(volatile float*)(table + row * 0x14 + 8);
    return speed >= 1.0f && speed < 100000.0f;  // 1 degree per 1/60 s: half a turn in 3 seconds at the slowest
}

// Game thread only (the player update), so plain variables.
bool g_armed = false;        // York faced away from the pulled stick a moment ago
DWORD g_armedAt = 0;         // when that was first seen
float g_armedHeading = 0;    // his heading then

// Called from the idle branch of FUN_00503C60 in place of its quick-turn flag test. Returns 1 after it has put the player
// into state 0Bh (the gate then leaves the function), 0 to let the game's own test run.
int __cdecl QuickTurnWorker(void* playerPtr) {
    u8* player = (u8*)playerPtr;
    u8* mgr = *(u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    u8* input = *(u8* volatile*)(uintptr_t)kInputPtrVa;
    u8* camera = *(u8* volatile*)(uintptr_t)kCameraPtrVa;
    if (!g_cfg.quickTurn || ui::IsOpen() || !player || !mgr || !input || !camera ||
        // York and Zach only, like the pose and costume code: kind 2 moves by other rules (00522DA5), 1 and 3 were not studied.
        (mgr[0x8CA12] != 0 && mgr[0x8CA12] != 4) || mgr[0x8CA12] != mgr[0x8CA13] ||
        (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u) ||                  // a cutscene
        // The idle handler leaves at once, the stick does not move York at all: a script has taken the control away (bit 31
        // of +638h clear, 00522DABh; event 19h / 2711h switches it) or +65Ch is not zero (00522EADh).
        !(*(volatile u32*)(player + 0x638) & 0x80000000u) || *(volatile u32*)(player + 0x65C) != 0 ||
        // Flag 4 belongs to the game's own test (and walks him backwards); with 8 (stance) or 80h, or with the aiming camera,
        // the idle handler does not steer by the camera (00523CC8, 00523CDD, 00523D5D) and the rule above has no meaning.
        (*(volatile u32*)(mgr + 0x8C5C8) & (4 | 8 | 0x80)) || *(volatile u32*)(camera + 0x154) == 2) {
        g_armed = false;
        return 0;
    }
    unsigned pad = *(volatile u8*)(uintptr_t)kPadIndexVa;
    InputStickFn stick = (InputStickFn)(uintptr_t)kStickGetterFn;
    float lx = stick(input, pad, 0, 0), ly = stick(input, pad, 0, 1);
    float stickAngle = Atan2(-lx, ly);  // 0 = up, +-pi = back, as the game takes it (00523D7A, 0050434C)
    if (!((InputMaskFn)(uintptr_t)kInputMaskFn)(input, pad, 0, kStickBackBit) || !(Abs(stickAngle) > kBackAngle)) {
        g_armed = false;  // the stick is not pulled back
        return 0;
    }
    float heading = *(volatile float*)(player + 0x7C);
    float away;
    DWORD now = GetTickCount();
    if (WrapAngle(*(volatile float*)(camera + 0x70) + stickAngle - heading, &away) && Abs(away) > kBackAngle) {
        // Kept from the first moment, before the game's steering turns him. Taken anew when he still faces away later
        // (nothing is turning him, so the old heading may be out of date).
        if (!g_armed || now - g_armedAt > kChordMs) {
            g_armed = true;
            g_armedAt = now;
            g_armedHeading = heading;
        }
    } else if (g_armed && now - g_armedAt > kChordMs) {
        g_armed = false;
    }
    if (!g_armed) return 0;

    float target, left;
    if (!WrapAngle(g_armedHeading + kPi, &target) || !WrapAngle(target - heading, &left) || Abs(left) < kLeastTurn) return 0;
    if (!((CapabilityFn)(uintptr_t)kCapabilityFn)(0x100)) return 0;                      // the script lets him move (the game's own check, 00503FC5)
    if (!((LogicalPressFn)(uintptr_t)kLogicalPressFn)(mgr, kLogicalX)) return 0;         // a new press this tick
    if (!TurnSpeedUsable(player, mgr)) {
        static bool logged = false;
        if (!logged) Log("quickturn: the movement table has no usable turn speed for state 0Bh, the turn is not started");
        logged = true;
        return 0;
    }
    *(volatile float*)(player + 0x6C) = target;                         // where state 0Bh turns to (the game writes it at 0050403B)
    ((StateChangeFn)(uintptr_t)kStateChangeFn)(player, 0x0B);
    g_armed = false;
    static int budget = 40;
    if (budget > 0) {
        --budget;
        Log("quickturn: turn started, heading %d -> %d degrees", (int)(heading * 57.29578f), (int)(target * 57.29578f));
    }
    return 1;
}

}  // namespace

bool InstallQuickTurn() {
    for (const SiteDef& c : kQuickTurnChecks)
        if (!BytesMatch(c.va, c.bytes, c.len)) {
            Log("quickturn: %s at %08X differs from DP.exe 1.01b, quick turn off", c.name, c.va);
            return false;
        }
    const SiteDef& s = kSiteQuickTurnGate;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("quickturn: the idle quick-turn test at %08X differs from DP.exe 1.01b (another mod may have patched it), quick turn off", s.va);
        return false;
    }
    u8* stub = AllocStub(48);
    if (!stub) {
        Log("quickturn: no memory for the gate, quick turn off");
        return false;
    }
    BuildQuickTurnGate(stub, (u32)(uintptr_t)stub, (u32)(uintptr_t)QuickTurnWorker, kFlagTestFn, kQuickTurnResume, kQuickTurnExit);
    if (!PatchJump(s.va, s.bytes, s.len, stub)) {
        Log("quickturn: could not patch the idle quick-turn test");
        return false;
    }
    Log("quickturn: gate ready (%s): stick back + the run button with York's back to the camera, on the keyboard S + the run key",
        g_cfg.quickTurn ? "on" : "off, switch on in the menu");
    return true;
}

}  // namespace ufx
