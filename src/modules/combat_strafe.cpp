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
#include "../lab_dsb.h"

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

volatile u32 g_playerObject = 0;  // the player object of the last update

// ---- Photo mode: any body motion with a held frame, and the face on its own (EXPERIMENTAL, from static analysis) ----
// The actor keeps its animators inside itself: the main one at +178h (motion +18Ch, time in frames +194h), the speed of all of
// them at +1F8h. FUN_006C0480 (thiscall, ret 14h) starts a motion given by pointer; FUN_006B2BE0 gives the pointer of a
// resident motion by its resource id (0 when it is not loaded: only sets the game keeps in memory are offered).
// The face is bones driven by 50 weights a frame; the engine hands them to the actor's virtual slot +68h (CPlayer: the pointer
// at 776864h, FUN_00410210, thiscall(float* w, int n), ret 8) before it adds the blink (scaled by w[0], -1 = none) and the eye
// darts (scaled by w[1]). The slot is wrapped: the chosen channels are written there, which leaves the body's motion alone.
constexpr u32 kPlayerVtable = 0x7767FC, kPlayerFaceSlot = 0x776864, kFaceWeightsFn = 0x410210;
constexpr u32 kResourceMgrFn = 0x4051F0, kMotionByIdFn = 0x6B2BE0, kPlayMotionFn = 0x6C0480;
typedef void*(__cdecl* ResourceMgrFn)();
typedef void*(__attribute__((thiscall)) * MotionByIdFn)(void* mgr, int id, int one);
typedef void(__attribute__((thiscall)) * PlayMotionFn)(void* actor, void* motion, float startFrame, float blendFrames, int keepIfSame, int mode);
typedef void(__attribute__((thiscall)) * FaceWeightsFn)(void* actor, float* weights, int count);
const struct { u16 first, last; } kBodySets[4] = {{0x2413, 0x2554}, {0x2557, 0x25DC}, {0x2CD6, 0x2DC9}, {0x2861, 0x2898}};
FaceWeightsFn r_faceWeights = nullptr;
void* g_photoMotion = nullptr;  // the motion the photo mode put on York
int g_photoMotionKey = -1;      // set * 1000 + number it was chosen by
int g_photoReplays = 0;

void __attribute__((thiscall)) H_FaceWeights(void* actor, float* w, int n) {
    r_faceWeights(actor, w, n);
    static bool told = false;
    if (!told && (u32)(uintptr_t)actor == g_playerObject) {
        told = true;
        Log("photo face: the face weights of York pass the hook (%d channels)", n);
    }
    if ((u32)(uintptr_t)actor != g_playerObject || !w || n < 4 || n > 64) return;
    if (!g_cfg.photoFaceOn && !g_cfg.photoNoBlink) return;
    if (g_cfg.photoFaceOn) {
        if (!g_cfg.photoFaceAdd)  // the page's face stands alone: the game's own face of this frame is cleared
            for (int i = 2; i < n; ++i) w[i] = 0.f;
        for (int i = 0; i < 58; ++i)  // the buffer has 64 floats; channels past n reach poses the game's data never use
            if (g_cfg.photoFaceW[i]) w[i + 2] += (float)g_cfg.photoFaceW[i] / 100.f;
    }
    if (g_cfg.photoNoBlink) {
        w[0] = -1.f;
        w[1] = 0.f;
    }
}

// While the photo mode holds a motion on York, the game's own requests to play another are left out. The log of 0.7.10-test5
// showed the idle state putting its motion back every frame (100 times in 0.6 s) and nothing of ours on the screen. The two
// "play" slots of the player's virtual table (+50h by resource id, +54h by pointer; both thiscall with 5 stack words, ret 14h)
// are wrapped; ours goes straight to the engine (FUN_006C0480) and is not affected.
constexpr u32 kPlayerPlayByIdSlot = 0x77684C, kPlayerPlayByPtrSlot = 0x776850, kPlayByIdFn = 0x417650, kPlayByPtrFn = 0x4174C0;
typedef u32(__attribute__((thiscall)) * PlaySlotFn)(void* actor, u32 a, u32 b, u32 c, u32 d, u32 e);
PlaySlotFn r_playById = nullptr, r_playByPtr = nullptr;
int g_photoBlocked = 0;
bool PhotoHoldsMotion(void* actor) { return g_photoMotion && (u32)(uintptr_t)actor == g_playerObject; }
u32 __attribute__((thiscall)) H_PlayById(void* actor, u32 a, u32 b, u32 c, u32 d, u32 e) {
    if (PhotoHoldsMotion(actor)) {
        ++g_photoBlocked;
        return 0;
    }
    return r_playById(actor, a, b, c, d, e);
}
u32 __attribute__((thiscall)) H_PlayByPtr(void* actor, u32 a, u32 b, u32 c, u32 d, u32 e) {
    if (PhotoHoldsMotion(actor)) {
        ++g_photoBlocked;
        return 0;
    }
    return r_playByPtr(actor, a, b, c, d, e);
}

void PhotoAnimTick(void* player) {
    u8* p = (u8*)player;
    if (*(volatile u32*)p != kPlayerVtable) return;  // York, Zach and the others of the player class only
    const int no = g_cfg.photoBodyNo;
    if (!no) {
        if (g_photoMotion) {  // back to the game's own motions
            g_photoMotion = nullptr;
            g_photoMotionKey = -1;
            *(volatile float*)(p + 0x1F8) = 1.f;
            g_poseRequest = 1;  // state 0, calm: PoseTick runs right after this in the next update
            Log("photo motion: off, York's own motions again (%d of the game's own motions left out, %d put back over one)", g_photoBlocked, g_photoReplays);
            g_photoReplays = 0;
            g_photoBlocked = 0;
        }
        return;
    }
    if (*(volatile u32*)(uintptr_t)kGameplayModeVa != 3 || (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u)) return;
    const u32 state = *(volatile u32*)(p + 0x654);
    if (state == 0x87 || state == 0x88) return;  // at the wheel
    const int set = g_cfg.photoBodySet & 3, key = set * 1000 + no;
    if (key != g_photoMotionKey) {
        g_photoMotionKey = key;
        g_photoMotion = nullptr;
        int id = kBodySets[set].first + no - 1;
        if (id > kBodySets[set].last) id = kBodySets[set].last;
        void* mgr = ((ResourceMgrFn)(uintptr_t)kResourceMgrFn)();
        u8* m = mgr ? (u8*)((MotionByIdFn)(uintptr_t)kMotionByIdFn)(mgr, id, 1) : nullptr;
        const u8* cur = *(u8* volatile*)(p + 0x18C);
        if (!m || (uintptr_t)m < 0x10000) {
            Log("photo motion: resource %04X is not in memory here, skipped", id);
            return;
        }
        const int bones = *(const u16*)(m + 8), frames = *(const u16*)(m + 0xA);
        if (cur && (uintptr_t)cur > 0x10000 && *(const u16*)(cur + 8) != bones) {
            Log("photo motion: resource %04X has %d bones, York's motion has %d, skipped", id, bones, *(const u16*)(cur + 8));
            return;
        }
        ((PlayMotionFn)(uintptr_t)kPlayMotionFn)(player, m, 0.f, 0.f, 0, 0);
        g_photoMotion = m;
        Log("photo motion: resource %04X on York (%d frames, %s; player state %u)", id, frames,
            (*(const u16*)(m + 0xC) & 0x8000) ? "loops" : "plays once", state);
    }
    if (!g_photoMotion) return;
    if (*(void* volatile*)(p + 0x18C) != g_photoMotion) {  // a state handler put its own motion back
        ((PlayMotionFn)(uintptr_t)kPlayMotionFn)(player, g_photoMotion, 0.f, 0.f, 0, 0);
        if (++g_photoReplays <= 3 || g_photoReplays == 100) Log("photo motion: the game replaced the motion, put back (%d so far, state %u)", g_photoReplays, state);
    }
    if (g_cfg.photoPlay) {
        *(volatile float*)(p + 0x1F8) = 1.f;
    } else {
        const int frames = *(const u16*)((const u8*)g_photoMotion + 0xA);
        *(volatile float*)(p + 0x194) = frames > 1 ? (float)(frames - 1) * (float)g_cfg.photoFrame / 100.f : 0.f;
        *(volatile float*)(p + 0x1F8) = 0.f;  // nothing advances: the frame of the slider stays
    }
}

// ---- Mission lab ([Debug] MissionLab=1): the first experiment for a mission creator (from static analysis, untested) ----
// The game's event scripts are DSB buffers in 384 slots (0BE06A0h, 10h each; +4 = the buffer). An event is found by the number
// inside the buffer, not by a file name, so a buffer of the mod's own with a free number (900) can be registered with the
// game's loader FUN_0070DD40 (thiscall on CEvCore [0BD9FE8h]: buffer, tag; ret 8; it relocates the name pointers in place, so
// it gets a fresh writable copy that is never freed) and started with CEvent::RunEvent FUN_0043C140 (thiscall on CEvent
// [0BD9E58h]: group 0, script, event, 0, 0, flags; ret 18h). Numpad 7 runs event 3 of that script: fade out, wait, fade in.
constexpr u32 kEvCorePtrVa = 0xBD9FE8, kEventPtrVa = 0xBD9E58, kEvSlotsVa = 0xBE06A0, kEvProcessesVa = 0xBDBEE0;
constexpr u32 kEvLoadFn = 0x70DD40, kRunEventFn = 0x43C140, kMovieFlagVa = 0x8A721C, kEventBlockVa = 0x1470F24;
typedef u32(__attribute__((thiscall)) * EvLoadFn)(void* core, void* buffer, u32 tag);
typedef int(__attribute__((thiscall)) * RunEventFn)(void* event, u32 group, u32 script, u32 id, int a, u32 b, int flags);
u8* g_labBuf = nullptr;
int g_labPid = -1;
u32 g_labFrames = 0;

bool LabRegistered() {
    if (!g_labBuf) return false;
    for (int i = 0; i < 384; ++i)
        if (*(u8* volatile*)(uintptr_t)(kEvSlotsVa + i * 0x10 + 4) == g_labBuf) return *(const u32*)(g_labBuf + 8) == 11;
    return false;
}
bool LabRegister() {
    static const u8 kLoad[8] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D}, kRun[8] = {0x83, 0xEC, 0x18, 0x66, 0x8B, 0x54, 0x24, 0x20};
    if (!BytesMatch(kEvLoadFn, kLoad, 8) || !BytesMatch(kRunEventFn, kRun, 8)) {
        Log("lab: the event functions differ from DP.exe 1.01b, nothing done");
        return false;
    }
    u8* core = *(u8* volatile*)(uintptr_t)kEvCorePtrVa;
    if (!core || *(volatile u32*)(core + 0xAEC) != kEvSlotsVa) {  // the slot table is bound when the game's events start up
        Log("lab: the event system is not ready yet (core %08X)", (u32)(uintptr_t)core);
        return false;
    }
    u8* buf = (u8*)VirtualAlloc(nullptr, kLabDsbSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);  // never freed: threads point into it
    if (!buf) return false;
    for (u32 i = 0; i < kLabDsbSize; ++i) buf[i] = kLabDsb[i];
    const u32 slot = ((EvLoadFn)(uintptr_t)kEvLoadFn)(core, buf, 4) & 0xFFFFu;
    if (slot == 0xFFFFu) {
        Log("lab: the game has no free script slot");
        return false;
    }
    g_labBuf = buf;
    Log("lab: script %u registered in slot %u (buffer %08X, %u bytes, %u events)", kLabDsbNo, slot, (u32)(uintptr_t)buf, kLabDsbSize, kLabDsbEventCount);
    return true;
}
// The process running one of our events, -1 when none: its event record (+48h) lies inside our buffer
int LabProcess() {
    if (!g_labBuf) return -1;
    for (int i = 0; i < 32; ++i) {
        const u8* p = (const u8*)(uintptr_t)(kEvProcessesVa + i * 0x54);
        const u8* rec = *(const u8* volatile*)(p + 0x48);
        if (*(volatile int*)(p + 8) != -1 && rec >= g_labBuf && rec < g_labBuf + kLabDsbSize) return i;
    }
    return -1;
}
void LabTick(void* player) {
    if (!g_cfg.missionLab || !player) return;
    if (g_labPid != -1) {  // one of ours was started: say when it is over
        ++g_labFrames;
        if (LabProcess() < 0) {
            Log("lab: the event is over after %u player updates", g_labFrames);
            ShowNotice(L"Лабораторія місій: подія завершилась", L"Mission lab: the event is over", L"Власний скрипт мода відпрацював у грі.",
                       L"The mod's own script ran in the game.", 4);
            g_labPid = -1;
        } else if (g_labFrames == 3600) {
            Log("lab: the event is still running after 3600 player updates");
        }
    }
    static bool held = false;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    const bool down = pid == GetCurrentProcessId() && !ui::IsOpen() && (GetAsyncKeyState(VK_NUMPAD7) & 0x8000);
    const bool hit = down && !held;
    held = down;
    if (!hit) return;
    if (g_labPid != -1) {
        Log("lab: Numpad 7 while the event still runs, ignored");
        return;
    }
    const u8* ev = *(const u8* volatile*)(uintptr_t)kEventPtrVa;
    const u32 mode = *(volatile u32*)(uintptr_t)kGameplayModeVa;
    if (!ev || mode != 3 || (*(volatile u32*)(ev + 0x44) & 0x40000) || *(volatile u32*)(ev + 0x34) || (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u) ||
        (*(volatile u32*)(uintptr_t)kMovieFlagVa & 0x80000000u) || *(volatile u32*)(uintptr_t)kEventBlockVa) {
        Log("lab: not now (gameplay mode %u, event %08X: needs free play outside any scene or video)", mode, (u32)(uintptr_t)ev);
        ShowNotice(L"Лабораторія місій: не зараз", L"Mission lab: not now", L"Потрібна вільна гра: не сцена, не відео, не меню.",
                   L"It needs free play: no scene, no video, no menu.", 4);
        return;
    }
    if (!LabRegistered() && !LabRegister()) return;
    const int r = ((RunEventFn)(uintptr_t)kRunEventFn)((void*)ev, 0, kLabDsbNo, kLabEvMin, 0, 0, 0);
    Log("lab: RunEvent(script %u, event %u) returned %d; our process slot %d", kLabDsbNo, kLabEvMin, r, LabProcess());
    if (r == -1) {
        ShowNotice(L"Лабораторія місій: гра відмовила", L"Mission lab: the game refused", L"RunEvent повернув -1. Деталі в ufx.log.", L"RunEvent returned -1. See ufx.log.", 5);
        return;
    }
    g_labPid = r;
    g_labFrames = 0;
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


void __cdecl StrafeWorker(void* player) {
    if (player) { g_playerTickMs = GetTickCount(); g_playerObject = (u32)(uintptr_t)player; }
    if (player) { SuitTick(player); PhotoAnimTick(player); LabTick(player); PoseTick(player); CheatsTick(player); LayoutTick(player); }
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
    // the face: CPlayer's virtual slot +68h, only when it holds the game's own function
    if (*(volatile u32*)(uintptr_t)kPlayerFaceSlot == kFaceWeightsFn &&
        SwapImport((void**)(uintptr_t)kPlayerFaceSlot, (void*)H_FaceWeights, (void**)&r_faceWeights))
        Log("photo face: the player's face weights hooked (expressions in the photo mode)");
    else
        Log("photo face: the player's virtual table differs from DP.exe 1.01b, no expressions");
    if (*(volatile u32*)(uintptr_t)kPlayerPlayByIdSlot == kPlayByIdFn && *(volatile u32*)(uintptr_t)kPlayerPlayByPtrSlot == kPlayByPtrFn &&
        SwapImport((void**)(uintptr_t)kPlayerPlayByIdSlot, (void*)H_PlayById, (void**)&r_playById) &&
        SwapImport((void**)(uintptr_t)kPlayerPlayByPtrSlot, (void*)H_PlayByPtr, (void**)&r_playByPtr))
        Log("photo motion: the player's play slots wrapped (the game's own motions wait while the photo mode holds one)");
    else
        Log("photo motion: the player's play slots differ from DP.exe 1.01b: the game may put its own motion back");
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
