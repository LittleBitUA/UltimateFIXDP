// Combat strafe, restored. On the console a shoulder button edge during lock-on made York sidestep; the PC code lost it.
// A bridge at the tail of the player update runs a worker with the player object. The worker watches LB and RB of the
// pad data the game is given, and on a new press, when the lock-on flag and the capability allow it, asks the player
// state machine for the strafe state (9 left, 0Ah right). The idea and the addresses come from the ZachFix analysis.
// Known conflict: the edges are not filtered against the bindings, and both shoulder buttons are bound in both layouts (the
// game's: LB aim, RB fire; TriggerFire: L1 run, R1 hold breath and lock-on). So a press of LB/L1 (left) or RB/R1 (right) while
// locked on also sidesteps. Filtering the bound buttons would leave no strafe at all, so the conflict stays (the menu text says so).
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {
bool PadIsRawSony(unsigned id);  // sony_layout.cpp
namespace {

typedef unsigned(__stdcall* CapabilityFn)(unsigned capability);
typedef void(__attribute__((thiscall)) * StateChangeFn)(void* player, int state);

unsigned g_prevShoulders = 0;
volatile int g_poseRequest = 0;  // 0 none, n = state n-1
typedef void(__attribute__((thiscall)) * StateChangeFn2)(void* player, int state);
typedef void(__cdecl* DefaultMotionFn)();
typedef void(__attribute__((fastcall)) * ApplyMotionFn)(void* player);

// Puts the player into `state` the way the game itself does it: blend length, state change, default motion, apply.
void PoseTick(void* player) {
    int r = g_poseRequest;
    if (!r) return;
    g_poseRequest = 0;
    u8* mgr = *(u8**)(uintptr_t)kGameManagerPtrVa;
    if (!mgr || (mgr[0x8CA12] != 0 && mgr[0x8CA12] != 4) || mgr[0x8CA12] != mgr[0x8CA13]) { Log("pose: this playable character has no pose change, skipped"); return; }
    int state = r - 1;
    if (r >= 1000) {  // the state of the weapon York holds: the item table is a pointer at CGame+835000h, 0DCh per entry, class at +6Dh
        int item = *(volatile int*)(mgr + 0x98E48);
        u8* items = *(u8**)(mgr + 0x835000);
        if (item < 0 || item > 0x400 || !items || (uintptr_t)items < 0x10000) { Log("pose: no weapon in hand, skipped"); return; }
        int cls = *(volatile u8*)(items + item * 0xDC + 0x6D);
        // weapon class -> player state (FUN_0044D520)
        static const struct { int cls; int state; } map[] = {{1, 0x14}, {2, 0x16}, {3, 0x18}, {4, 0x1A}, {5, 0x1E}, {6, 0x20},
                                                               {7, 0x24}, {8, 0x28}, {9, 0x2A}, {10, 0x2B}, {11, 0x26}};
        state = -1;
        for (const auto& m : map)
            if (m.cls == cls) state = m.state;
        Log("pose: weapon item %d, class %d -> state %d", item, cls, state);
        if (state < 0) return;
    }
    if (state < 0 || state > 0x2B) return;
    if (*(volatile u32*)(uintptr_t)kGameplayModeVa != 3) return;              // open-world gameplay only
    if (*(volatile u32*)(uintptr_t)kActionGateVa & 0x268) return;             // the state handlers must be running
    if (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u) return;        // not in a cutscene
    *(volatile float*)((u8*)player + 0x644) = state == 0 ? 0.0f : 8.0f;       // blend length of the new motion
    ((StateChangeFn2)(uintptr_t)kStateChangeFn)(player, state);
    ((DefaultMotionFn)(uintptr_t)kDefaultMotionFn)();
    ((ApplyMotionFn)(uintptr_t)kApplyMotionFn)(player);
    Log("pose: player state %d requested", state);
}

// Logs every change of the player state (Debug.TraceStates=1): the labelled table that static analysis cannot give.
void __cdecl StateTrace(void* player, int newState) {
    if (!g_cfg.traceStates || !player) return;
    static int budget = 600;
    if (budget <= 0) return;
    --budget;
    u8* mgr = *(u8**)(uintptr_t)kGameManagerPtrVa;
    int weapon = mgr ? *(volatile int*)(mgr + 0x98E48) : -1;
    Log("player state: %d -> %d (motion %08X, weapon item %d)", *(volatile int*)((u8*)player + 0x654), newState,
        *(volatile u32*)((u8*)player + 0x628), weapon);
}
volatile int g_suitRequest = 0;
bool g_suitAvailable = false;

typedef void(__attribute__((thiscall)) * SuitChangeFn)(void* manager, int suit, int variant);

// Applies a pending costume request from the game thread, from the player update (esi = the player object).
void SuitTick(void* player) {
    int v = g_suitRequest;
    if (!v || !g_suitAvailable) return;
    g_suitRequest = 0;
    if (v < 1 || v > 10) return;
    if (v == 2) {  // the add-on pack slot: only when an add-on suit is installed, otherwise the loader waits for a file that is not there
        WIN32_FIND_DATAW fd;
        wchar_t pattern[MAX_PATH];
        lstrcpyW(pattern, g_paths.gameDir);
        lstrcatW(pattern, L"ADDON\\SUIT*.DLC");
        HANDLE h = FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            Log("suit: suit 2 is the add-on slot and no add-on suit is installed, skipped");
            return;
        }
        FindClose(h);
    }
    u8* mgr = *(u8**)(uintptr_t)kGameManagerPtrVa;
    if (!mgr) { Log("suit: no game manager yet"); return; }
    int kind = mgr[0x8CA12];
    Log("suit: request %d, playable kind %d/%d, wearing suit %d variant %d, set %d", v, kind, mgr[0x8CA13], mgr[0x8C5E4], mgr[0x8C5E5],
        g_cfg.photoSuitSet);
    if (kind != mgr[0x8CA13] || (kind != 0 && kind != 4)) { Log("suit: this playable character has no costume change, skipped"); return; }
    // the suit digit: York has the sets 1 and 3, Zach (kind 4) the sets 1, 2 and 3 (the third has only its first variant)
    int set = g_cfg.photoSuitSet < 1 ? 1 : g_cfg.photoSuitSet;
    int digit = 1;
    if (kind == 0) digit = set >= 2 ? 3 : 1;
    else digit = set > 3 ? 3 : set;
    if (kind == 4 && digit == 3) v = 1;
    if (mgr[0x8C5E4] == digit && mgr[0x8C5E5] == v) { Log("suit: already wearing it"); return; }
    u8* p = (u8*)player;
    if (*(volatile u32*)(p + 0x1C) & 8) {                         // busy: let the engine apply it when it can
        mgr[0x8C5E6] = (u8)digit;
        mgr[0x8C5E7] = (u8)v;
        *(volatile u32*)(p + 0x1C) |= 0x2000000;
        Log("suit: request %d handed to the engine (the player is busy)", v);
        return;
    }
    ((SuitChangeFn)(uintptr_t)kSuitChangeFn)(mgr, digit, v);
    Log("suit: %s now wears suit %d variant %d", kind == 4 ? "Zach" : "York", digit, v);
}

volatile bool g_triggerLayout = false;  // aim and fire are on the trigger buttons: the trigger axis must not reach the game as well

// The pad layout with aim and fire on the triggers: the four binding globals are rewritten, and put back behind the wheel.
void LayoutTick(void* player) {
    static bool saved = false;
    static u32 orig[4];
    static bool applied = false;
    static const u32 va[4] = {kBindAim, kBindAttack, kBindRun, kBindBreath};
    static const u32 triggers[4] = {10, 11, 4, 5};  // aim L2, fire R2, run L1, hold breath R1
    if (!saved) {
        for (int i = 0; i < 4; ++i) orig[i] = *(volatile u32*)(uintptr_t)va[i];
        saved = true;
    }
    bool driving = *(volatile u32*)((u8*)player + 0x654) == 0x87;
    bool padMode = *(volatile u8*)(uintptr_t)kUseJoyVa != 0;  // on the keyboard the bindings are the game's own: the keys map to the same codes
    // An unconverted Sony pad (SonyLayout=0) has L3/R3 on the buttons 10 and 11 the layout binds aim and fire to: the game's layout stays.
    bool want = g_cfg.triggerFire && !driving && padMode && !PadIsRawSony(0);
    if (want == applied) return;
    for (int i = 0; i < 4; ++i) *(volatile u32*)(uintptr_t)va[i] = want ? triggers[i] : orig[i];
    applied = want;
    g_triggerLayout = want;
    Log("input: pad layout %s", want ? "aim L2, fire R2, run L1, hold breath R1" : "the game's");
}

volatile DWORD g_playerTickMs = 0;  // when the player object was last updated: the main menu and the title screen have none

volatile u32 g_playerObject = 0;

void __cdecl StrafeWorker(void* player) {
    if (player) { g_playerTickMs = GetTickCount(); g_playerObject = (u32)(uintptr_t)player; }
    if (player) { SuitTick(player); PoseTick(player); CheatsTick(player); LayoutTick(player); }
    if (!player || !g_cfg.combatStrafe) return;
    unsigned now = PadButtons() & 0x30;  // buttons 4 (LB) and 5 (RB)
    unsigned rising = now & ~g_prevShoulders;
    g_prevShoulders = now;
    if (!rising) return;
    if (!(*(volatile u32*)((u8*)player + 0x638) & 0x10)) return;          // lock-on flag
    if (!((CapabilityFn)(uintptr_t)kCapabilityFn)(0x2000)) return;        // the player may strafe now
    int state = (rising & 0x10) ? 9 : 0x0A;
    ((StateChangeFn)(uintptr_t)kStateChangeFn)(player, state);
}

}  // namespace

bool TriggerLayoutActive() { return g_triggerLayout; }

// True while a game world is being played (the player is updated): the main menu and the title screen draw through the same final
// composite, but their depth buffer holds no scene.
bool WorldActive() { return g_playerTickMs && (GetTickCount() - g_playerTickMs) < 300; }
u32 PlayerObjectNow() { return WorldActive() ? g_playerObject : 0; }

// The class (1..11) of the weapon York holds, or -1: the item table is a pointer at CGame+835000h, 0DCh per entry, class at +6Dh.
int WeaponClassNow() {
    u8* mgr = *(u8**)(uintptr_t)kGameManagerPtrVa;
    if (!mgr || (uintptr_t)mgr < 0x10000) return -1;
    int item = *(volatile int*)(mgr + 0x98E48);
    u8* items = *(u8**)(mgr + 0x835000);
    if (item < 0 || item > 0x400 || !items || (uintptr_t)items < 0x10000) return -1;
    int cls = *(volatile u8*)(items + item * 0xDC + 0x6D);
    return (cls >= 1 && cls <= 11) ? cls : -1;
}

void RequestSuit(int variant) {
    g_suitRequest = variant;
    Log("suit: request %d queued (waiting for the next player update)", variant);
}
void RequestPose(int stateAndOne) { g_poseRequest = stateAndOne; }

bool InstallCombatStrafe() {
    g_suitAvailable = BytesMatch(kSiteSuitChange.va, kSiteSuitChange.bytes, kSiteSuitChange.len);
    if (!g_suitAvailable) Log("suit: the costume function differs from DP.exe 1.01b, costume change off");
    const SiteDef& s = kSiteStrafeTail;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("strafe: the player update tail at %08X differs from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    u8* stub = AllocStub(48);
    if (!stub) return false;
    BuildStrafeBridge(stub, (u32)(uintptr_t)stub, (u32)(uintptr_t)StrafeWorker, kStrafeReturn);
    if (!PatchJump(s.va, s.bytes, s.len, stub)) {
        Log("strafe: could not patch the player update tail");
        return false;
    }
    if (BytesMatch(kSiteStateEntry.va, kSiteStateEntry.bytes, kSiteStateEntry.len)) {
        u8* hook = AllocStub(64);
        if (hook) {
            BuildEntryHook(hook, (u32)(uintptr_t)hook, (u32)(uintptr_t)StateTrace, kSiteStateEntry.bytes, kSiteStateEntry.len,
                           kStateEntryReturn);
            if (PatchJump(kSiteStateEntry.va, kSiteStateEntry.bytes, kSiteStateEntry.len, hook))
                Log("pose: state change hook ready (tracing %s)", g_cfg.traceStates ? "on" : "off");
        }
    }
    Log("strafe: combat strafe bridge ready (%s)", g_cfg.combatStrafe ? "on" : "off, switch on in the menu");
    return true;
}

}  // namespace ufx
