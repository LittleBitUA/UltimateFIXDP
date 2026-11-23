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

void PhotoFaceApply(float* w, int n);
void __attribute__((thiscall)) H_FaceWeights(void* actor, float* w, int n) {
    r_faceWeights(actor, w, n);
    static bool told = false;
    if (!told && (u32)(uintptr_t)actor == g_playerObject) {
        told = true;
        Log("photo face: the face weights of York pass the hook (%d channels)", n);
    }
    if ((u32)(uintptr_t)actor != g_playerObject || !w || n < 4 || n > 64) return;
    if (g_cfg.photoFaceTarget != 0) return;  // the page's face goes to the doll
    PhotoFaceApply(w, n);
}
void PhotoFaceApply(float* w, int n) {
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
int g_labGiveItem = 100, g_labGiveCount = 1;  // what the first answer gives (Cherry Pie for the lab's own talk)
bool g_labGiftSeen = false;                   // the running event reached its "give" branch
bool g_talkPartnerSet = false;   // CGame+8C57Ch holds the doll for a talk; g_talkPartnerOld is the game's own value
u32 g_talkPartnerOld = 0;
bool g_labCallArmed = false;      // this talk has made a call of its own (the core's last target is no longer the talk before's)
bool g_labGiftWatch = false;                  // the running event is a talk whose first answer gives the item
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
    if (!player) return;
    if (g_labPid != -1) {  // one of ours was started: say when it is over
        ++g_labFrames;
        // The item of the first answer: the script's own give command (84h) gave nothing in the mod's events, so the mod
        // watches the game's event threads (00BDC960h, 80 x C4h, +18h in use, +14h -> u16 script, u16 event) for the
        // branch of the first answer and gives the item itself, with the game's give call, when the event is over.
        for (int i = 0; i < 80 && !g_labGiftSeen; ++i) {
            const u8* t = (const u8*)(uintptr_t)(0xBDC960 + i * 0xC4);
            if (!*(volatile u32*)(t + 0x18)) continue;
            const u16* rec = *(const u16* volatile*)(t + 0x14);
            if (rec && (uintptr_t)rec > 0x10000 && !IsBadReadPtr(rec, 4) && rec[0] == kLabDsbNo && (rec[1] == kMsEvYesGift || rec[1] == kLabEvTakeGift))
                g_labGiftSeen = true;
        }
        if (LabProcess() < 0 && g_labGiftWatch) {
            // second sign: the last event a script called (opcode 0Bh keeps its target in the event core, +0Ch..+13h)
            Log("lab: talk over: the first answer's branch %s", g_labGiftSeen ? "was seen" : "was not seen");
            const bool yes = g_labGiftSeen;
            g_labGiftSeen = false;
            g_labGiftWatch = false;
            if (MissionActive()) MissionTalkOver(yes);  // the quest runner gives its own rewards and moves to the next step
            else if (yes && g_labGiveItem > 0) {
                const int r = GiveItemNow(g_labGiveItem, g_labGiveCount);
                Log("lab: the first answer was picked: item %d x %d given by the mod (the game answered %d)", g_labGiveItem, g_labGiveCount, r);
                ShowNotice(r == -3 ? L"Предмет пішов у сховище" : L"Отримано предмет", r == -3 ? L"The item went to the storage" : L"Item obtained",
                           L"Дивіться інвентар.", L"See the inventory.", 4);
            }
        }
        if (LabProcess() < 0 && g_talkPartnerSet) {
            u8* game = *(u8* volatile*)(uintptr_t)kGameManagerPtrVa;
            if (game) *(volatile u32*)(game + 0x8C57C) = g_talkPartnerOld;
            g_talkPartnerSet = false;
        }
        if (LabProcess() < 0) {
            Log("lab: the event is over after %u player updates", g_labFrames);
            if (!MissionActive()) ShowNotice(L"Лабораторія місій: подія завершилась", L"Mission lab: the event is over", L"Власний скрипт мода відпрацював у грі.",
                       L"The mod's own script ran in the game.", 4);
            g_labPid = -1;
        } else if (g_labFrames == 3600) {
            Log("lab: the event is still running after 3600 player updates");
        }
    }
    if (!g_cfg.missionLab) return;  // the Numpad keys are the lab's; the mission editor runs its events itself
    static bool held = false;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    // Numpad 7 = the smallest event (a fade), Numpad 8 = a line in the game's dialogue window with a yes / no choice,
    // Numpad 9 = a mission banner with its sound. 8 and 9 need the lab texts in the game's messages.
    static const int kKeys[3] = {VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9};
    // The dialogue is the event without the talk wrappers of COMMON 0_0003 (events 6 and 74): the wrapper looks up the talk
    // partner (FUN_004AC110 on the NPC manager, result to CEvent+3060h) and reads it without a check, so with no resident
    // to talk to it crashed at 004BA360 (0.7.12-test1). The wrapped event (kLabEvTalk) waits for a spawned partner.
    static const u32 kEvents[3] = {kLabEvMin, kLabEvChoice, kLabEvBanner};
    const bool ours = pid == GetCurrentProcessId() && !ui::IsOpen();
    int pick = -1;
    bool down = false;
    for (int k = 0; k < 3; ++k)
        if (ours && (GetAsyncKeyState(kKeys[k]) & 0x8000)) {
            down = true;
            if (pick < 0) pick = k;
        }
    const bool hit = down && !held;
    held = down;
    if (!hit) return;
    // Ctrl + Numpad 8: the same lines through the game's own talk wrappers (COMMON 0_0003 events 6 and 74: event mode 1, the
    // framed dialogue). The wrapper takes the talk partner from the process's actor (+30h, RunEvent's fifth argument) and
    // crashed on a missing one (004BA360); the actor 7F64h is the game's own "no partner" case (00439A00), so that is given.
    const bool framed = false;  // (0.7.12-test12 tried it on Ctrl: 7F64h without an object in CGame+8C57Ch crashed at 00417F8B)
    const u32 event = framed ? kLabEvTalk : kEvents[pick];
    const u32 actor = framed ? 0x7F64u : 0u;
    if (event != kLabEvMin && !LabTextsIn()) {
        Log("lab: event %u needs the lab texts, and they are not in the game's messages (see the lab: lines at the start)", event);
        ShowNotice(L"Лабораторія місій: немає текстів", L"Mission lab: no texts", L"Тексти лабораторії не додано до повідомлень гри. Деталі в ufx.log.",
                   L"The lab texts were not added to the game's messages. See ufx.log.", 5);
        return;
    }
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
    const int r = ((RunEventFn)(uintptr_t)kRunEventFn)((void*)ev, 0, kLabDsbNo, event, 0, actor, 0);
    Log("lab: RunEvent(script %u, event %u, actor %04X) returned %d; our process slot %d", kLabDsbNo, event, actor, r, LabProcess());
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

// The pad layout with aim and fire on the triggers: the four binding globals are rewritten. Behind the wheel the car reads the
// same two actions: "aim" (held bit 40h) is its brake and reverse, "attack" (bit 80h) its gas (FUN_0054BFC0 and its two
// sisters; the action functions FUN_0070AAF0 and FUN_0070ABA0 ask for the bindings BE5904h and BE58FCh). With TriggerDrive
// those two stay on L2 and R2 in the car as on the console, and run / hold breath go back to the game's buttons there, so the
// car's other buttons are where the game has them; without it the game's whole layout is used behind the wheel.
void LayoutTick(void* player) {
    static bool saved = false;
    static u32 orig[4];
    static int applied = 0;  // 0 the game's, 1 on foot, 2 behind the wheel
    static const u32 va[4] = {kBindAim, kBindAttack, kBindRun, kBindBreath};
    static const u32 triggers[4] = {10, 11, 4, 5};  // aim L2, fire R2, run L1, hold breath R1
    if (!saved) {
        for (int i = 0; i < 4; ++i) orig[i] = *(volatile u32*)(uintptr_t)va[i];
        saved = true;
    }
    bool driving = *(volatile u32*)((u8*)player + 0x654) == 0x87;
    bool padMode = *(volatile u8*)(uintptr_t)kUseJoyVa != 0;  // on the keyboard the bindings are the game's own: the keys map to the same codes
    // An unconverted Sony pad (SonyLayout=0) has L3/R3 on the buttons 10 and 11 the layout binds aim and fire to: the game's layout stays.
    const bool can = padMode && !PadIsRawSony(0);
    const int want = !can ? 0 : driving ? (g_cfg.triggerDrive ? 2 : 0) : (g_cfg.triggerFire ? 1 : 0);
    if (want == applied) return;
    for (int i = 0; i < 4; ++i) *(volatile u32*)(uintptr_t)va[i] = want == 1 || (want == 2 && i < 2) ? triggers[i] : orig[i];
    applied = want;
    g_triggerLayout = want != 0;
    Log("input: pad layout %s", want == 1 ? "aim L2, fire R2, run L1, hold breath R1" : want == 2 ? "car: brake L2, gas R2, the rest the game's" : "the game's");
}

volatile DWORD g_playerTickMs = 0;  // when the player object was last updated: the main menu and the title screen have none


void __cdecl StrafeWorker(void* player) {
    if (player) { g_playerTickMs = GetTickCount(); g_playerObject = (u32)(uintptr_t)player; }
    if (player) { SuitTick(player); PhotoAnimTick(player); MissionTick(player); DollTick(player); LabTick(player); PoseTick(player); CheatsTick(player); LayoutTick(player); }
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


// ---- Photo mode: a resident as a doll ---------------------------------------------------------------------------------
// The game itself makes plain characters (CCharacter, object type 13h: no AI, no schedule, nothing in the save) and gives them a
// model: 004B04BC makes one with the object factory FUN_005E7620 (thiscall on 0143C078h: type, 0; ret 8), sets its model with
// FUN_006BE6E0 (model, textures, nodes, 0; ret 10h), ors +DCh with 100400h and plays a motion through its virtual slot +54h.
// The photo page does the same with the model of a resident from the game's NPC list (models at *(CNpcMain+D84h), a record
// of C0h per resident id: 12 outfits of 10h, each u8 set, u8 variant, 2 pad, then the XMD, XPC and XNV resource ids).
constexpr u32 kFactoryThisVa = 0x143C078, kFactoryFn = 0x5E7620, kResLoadFn = 0x40B330, kResObjectFn = 0x6B2C70, kSetModelFn = 0x6BE6E0,
              kFaceLinkFn = 0x40E780, kResEntryFn = 0x40B2F0, kObjDeleteFn = 0x6C5DA0, kObjLookupFn = 0x6C5FD0, kObjMgrPtrVa = 0xBD7670,
              kNpcMainPtrVa = 0x138A760, kDollVtable = 0x770224, kDollFaceSlot = 0x77028C, kNpcIdleMotion = 0x25AA;
typedef void*(__attribute__((thiscall)) * FactoryFn)(void* self, u32 type, u32 spawn);
typedef void(__attribute__((thiscall)) * ResLoadFn)(void* game, u32 id, u32 kind, u32 wait);
typedef void*(__attribute__((thiscall)) * ResObjectFn)(void* res, u32 id, u32 flag);
typedef void(__attribute__((thiscall)) * SetModelFn)(void* obj, void* model, void* textures, void* nodes, u32 zero);
typedef void*(__attribute__((thiscall)) * ResEntryFn)(void* game, u32 id);
typedef void(__attribute__((cdecl)) * FaceLinkFn)(void* entry);
typedef void(__attribute__((thiscall)) * ObjDeleteFn)(void* mgr, void* obj);
typedef void*(__attribute__((thiscall)) * ObjLookupFn)(void* mgr, u32 handle);
typedef void*(__attribute__((cdecl)) * ResMgrFn)();
extern void* g_dollMotion;
extern int g_dollMotionKey;
volatile int g_dollRequest = 0;
int g_dollPendingSet = 0, g_dollPendingMotion = 0;
bool g_dollAtOn = false;   // the next doll goes to g_dollAt (x, y, z, heading) instead of in front of York
float g_dollAt[4];
u32 g_dollTalkEvent = kLabEvGift;  // what the E key near the doll runs
u8* g_doll = nullptr;
u32 g_dollHandle = 0;
float g_dollBase[3], g_dollYaw = 0.f;
FaceWeightsFn r_dollFaceWeights = nullptr;

inline float Sinf(float x) { float r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
inline float Cosf(float x) { float r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
inline u32 Swap32(u32 v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24); }
// The model ids of a resident's first outfit; how the list is laid out is checked on Emily (id 6: XMD BDAh, XPC BDCh, XNV BDBh)
bool DollModel(int id, int* xmd, int* xpc, int* xnv) {
    const u8* main = *(u8* volatile*)(uintptr_t)kNpcMainPtrVa;
    if (!main || IsBadReadPtr(main + 0xD84, 4)) return false;
    const u8* list = *(u8* volatile*)(main + 0xD84);
    if (!list || IsBadReadPtr(list, 50 * 0xC0)) return false;
    static int layout = -1;  // bit 0: ids start at 1 without a record 0; bit 1: big-endian
    if (layout < 0) {
        for (int k = 0; k < 4 && layout < 0; ++k) {
            u32 v = *(const u32*)(list + (6 - (k & 1)) * 0xC0 + 4);
            if (k & 2) v = Swap32(v);
            if (v == 0xBDA) layout = k;
        }
        if (layout < 0) {
            const u32* w = (const u32*)(list + 6 * 0xC0);
            Log("doll: the game's NPC model list is not laid out as expected (record 6: %08X %08X %08X %08X), no doll", w[0], w[1], w[2], w[3]);
            return false;
        }
        Log("doll: NPC model list at %08X, layout %d", (u32)(uintptr_t)list, layout);
    }
    const u32* e = (const u32*)(list + (id - (layout & 1)) * 0xC0);
    u32 v[3] = {e[1], e[2], e[3]};
    if (layout & 2)
        for (u32& x : v) x = Swap32(x);
    *xmd = (int)v[0];
    *xpc = (int)v[1];
    *xnv = (int)v[2];
    return *xmd > 0 && *xmd < 0x8000 && *xpc > 0 && *xpc < 0x8000;
}

u8* DollAlive() {
    if (!g_doll || !g_dollHandle) return nullptr;
    void* mgr = *(void* volatile*)(uintptr_t)kObjMgrPtrVa;
    if (!mgr || ((ObjLookupFn)(uintptr_t)kObjLookupFn)(mgr, g_dollHandle) != g_doll || *(volatile u32*)g_doll != kDollVtable) {
        Log("doll: the doll is gone (the game removed it)");
        g_doll = nullptr;
        g_dollHandle = 0;
    }
    return g_doll;
}

void DollRemove(const char* why) {
    u8* doll = DollAlive();
    if (!doll) return;
    void* mgr = *(void* volatile*)(uintptr_t)kObjMgrPtrVa;
    ((ObjDeleteFn)(uintptr_t)kObjDeleteFn)(mgr, doll);
    g_doll = nullptr;
    g_dollHandle = 0;
    g_cfg.dollMoveX = g_cfg.dollMoveY = g_cfg.dollMoveZ = g_cfg.dollTurn = 0;
    Log("doll: removed (%s)", why);
}

void DollSpawn(u8* player) {
    static const u8 kFactory[8] = {0x6A, 0xFF, 0x68, 0x64, 0x78, 0x76, 0x00, 0x64}, kLoad[8] = {0x8B, 0x44, 0x24, 0x08, 0x8B, 0x4C, 0x24, 0x04},
                    kSet[8] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x5B, 0x87}, kDel[8] = {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B},
                    kLink[8] = {0x81, 0xEC, 0x84, 0x00, 0x00, 0x00, 0xA1, 0xCC}, kEntry[8] = {0x8B, 0x44, 0x24, 0x04, 0x50, 0xE8, 0xA6, 0x5E};
    if (!BytesMatch(kFactoryFn, kFactory, 8) || !BytesMatch(kResLoadFn, kLoad, 8) || !BytesMatch(kSetModelFn, kSet, 8) ||
        !BytesMatch(kObjDeleteFn, kDel, 8) || !BytesMatch(kFaceLinkFn, kLink, 8) || !BytesMatch(kResEntryFn, kEntry, 8)) {
        // (0.7.12-test3 also compared the doll's face slot with the game's function here, and the mod's own hook sits in it)
        Log("doll: the game's functions differ from DP.exe 1.01b, nothing done (%d%d%d%d%d%d)", BytesMatch(kFactoryFn, kFactory, 8),
            BytesMatch(kResLoadFn, kLoad, 8), BytesMatch(kSetModelFn, kSet, 8), BytesMatch(kObjDeleteFn, kDel, 8), BytesMatch(kFaceLinkFn, kLink, 8),
            BytesMatch(kResEntryFn, kEntry, 8));
        return;
    }
    const int id = g_cfg.photoDollId;
    int xmd = 0, xpc = 0, xnv = -1;
    if (id < 1 || id > 49 || !DollModel(id, &xmd, &xpc, &xnv)) {
        Log("doll: resident %d has no model in the game's list", id);
        ShowNotice(L"Персонаж: немає моделі", L"Character: no model", L"Для цього номера у грі немає моделі. Виберіть інший.", L"The game has no model for this number. Pick another.", 4);
        return;
    }
    DollRemove("another one is put");
    void* game = *(void* volatile*)(uintptr_t)kGameManagerPtrVa;
    void* res = ((ResMgrFn)(uintptr_t)kResourceMgrFn)();
    if (!game || !res) return;
    Log("doll: resident %d, model %04X, textures %04X, nodes %04X: loading", id, xmd, xpc, xnv & 0xFFFF);
    ((ResLoadFn)(uintptr_t)kResLoadFn)(game, (u32)xmd, 0xB, 1);
    ((ResLoadFn)(uintptr_t)kResLoadFn)(game, (u32)xpc, 0xB, 1);
    if (xnv > 0) ((ResLoadFn)(uintptr_t)kResLoadFn)(game, (u32)xnv, 0xB, 1);
    void* model = ((ResObjectFn)(uintptr_t)kResObjectFn)(res, (u32)xmd, 0);
    void* tex = ((ResObjectFn)(uintptr_t)kResObjectFn)(res, (u32)xpc, 0);
    void* nodes = xnv > 0 ? ((ResObjectFn)(uintptr_t)kMotionByIdFn)(res, (u32)xnv, 1) : nullptr;
    if (!model || !tex) {
        Log("doll: the model did not load (model %08X, textures %08X)", (u32)(uintptr_t)model, (u32)(uintptr_t)tex);
        ShowNotice(L"Персонаж: модель не завантажилась", L"Character: the model did not load", L"Деталі в ufx.log.", L"See ufx.log.", 4);
        return;
    }
    u8* obj = (u8*)((FactoryFn)(uintptr_t)kFactoryFn)((void*)(uintptr_t)kFactoryThisVa, 0x13, 0);
    if (!obj || *(volatile u32*)obj != kDollVtable) {
        Log("doll: the factory gave %08X (not a plain character), nothing done", (u32)(uintptr_t)obj);
        return;
    }
    ((SetModelFn)(uintptr_t)kSetModelFn)(obj, model, tex, nodes, 0);
    if (nodes) *(volatile u32*)(obj + 0xDC) |= 0x100400u;  // as the game does at 004B04E9
    void* entry = ((ResEntryFn)(uintptr_t)kResEntryFn)(game, (u32)xmd);
    if (entry) ((FaceLinkFn)(uintptr_t)kFaceLinkFn)(entry);  // the model's face set (<name>.XFE)
    // two units in front of York, facing him; or where a mission wants it
    const float yaw = *(volatile float*)(player + 0x7C);
    g_dollBase[0] = *(volatile float*)(player + 0x58) + Sinf(yaw) * 8.f;
    g_dollBase[1] = *(volatile float*)(player + 0x5C);
    g_dollBase[2] = *(volatile float*)(player + 0x60) + Cosf(yaw) * 8.f;
    g_dollYaw = yaw + 3.14159265f;
    if (g_dollAtOn) {
        for (int i = 0; i < 3; ++i) g_dollBase[i] = g_dollAt[i];
        g_dollYaw = g_dollAt[3];
        g_dollAtOn = false;
    }
    for (int i = 0; i < 3; ++i) *(volatile float*)(obj + 0x58 + 4 * i) = g_dollBase[i];
    *(volatile float*)(obj + 0x64) = 1.f;
    *(volatile float*)(obj + 0x7C) = *(volatile float*)(obj + 0x6C) = g_dollYaw;
    void* idle = ((ResObjectFn)(uintptr_t)kMotionByIdFn)(res, kNpcIdleMotion, 1);
    if (idle) {
        const u32 play = (*(u32* volatile*)obj)[0x54 / 4];
        ((PlayMotionFn)(uintptr_t)play)(obj, idle, 0.f, 0.f, 0, 0);
    }
    g_doll = obj;
    g_dollHandle = *(volatile u32*)(obj + 0x3C);
    g_dollMotion = nullptr;
    g_dollMotionKey = -1;
    g_cfg.dollBodyNo = 0;
    if (g_dollPendingMotion > 0) {  // a mission's pose
        g_cfg.dollBodySet = g_dollPendingSet & 3;
        g_cfg.dollBodyNo = g_dollPendingMotion;
        g_cfg.dollFrame = 0;
        g_cfg.dollPlay = 1;
    }
    g_dollPendingMotion = 0;
    g_cfg.dollMoveX = g_cfg.dollMoveY = g_cfg.dollMoveZ = g_cfg.dollTurn = 0;
    Log("doll: resident %d stands at %d %d %d (object %08X, handle %08X, idle motion %s)", id, (int)g_dollBase[0], (int)g_dollBase[1],
        (int)g_dollBase[2], (u32)(uintptr_t)obj, g_dollHandle, idle ? "played" : "not in memory");
}

// Cheats: play as another of the game's playable characters. The game has five player kinds with their own models and motion
// sets and switches between them itself with FUN_0044F360 (thiscall on CGame, kind 0..4, ret 4; script command 8Fh/1Eh):
// CGame+8CA13h = the kind wanted, +8CA12h = the kind loaded; when they differ it reloads the player's model through the
// costume function. The row asks once (playAsOn) and then shows whoever the game plays, so a switch the story makes
// itself is never undone. The kind is part of the save.
constexpr u32 kPlayerKindFn = 0x44F360, kKindLoadedOff = 0x8CA12;
typedef void(__attribute__((thiscall)) * PlayerKindFn)(void* game, int kind);
void PlayAsTick(u8* player, bool free) {
    static const u8 kFn[8] = {0xA1, 0xA4, 0x9B, 0x8A, 0x00, 0x53, 0x56, 0x6A};
    u8* game = *(u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    if (!game) return;
    const int loaded = *(volatile signed char*)(game + kKindLoadedOff), wanted = *(volatile signed char*)(game + kKindLoadedOff + 1);
    if (!g_cfg.playAsOn) {
        g_cfg.photoPlayAs = loaded;  // the row shows who the game plays
        return;
    }
    const int want = g_cfg.photoPlayAs;
    if (want == loaded || want < 0 || want > 4 || !BytesMatch(kPlayerKindFn, kFn, 8)) {
        g_cfg.playAsOn = 0;
        return;
    }
    if (wanted == want) return;  // asked already, the game is loading the model
    if (!free || *(volatile u32*)player != kPlayerVtable) return;  // waits for free play on foot
    Log("play as: player kind %d -> %d", loaded, want);
    ((PlayerKindFn)(uintptr_t)kPlayerKindFn)(game, want);
}

// The doll's body motion: the same motion sets as York's rows, played through the doll's own virtual slot +54h
void* g_dollMotion = nullptr;
int g_dollMotionKey = -1;
void DollMotionTick(u8* doll) {
    const int no = g_cfg.dollBodyNo;
    void* mgr = ((ResourceMgrFn)(uintptr_t)kResourceMgrFn)();
    const u32 play = (*(u32* volatile*)doll)[0x54 / 4];
    if (!no) {
        if (g_dollMotion) {  // back to the residents' idle
            g_dollMotion = nullptr;
            g_dollMotionKey = -1;
            *(volatile float*)(doll + 0x1F8) = 1.f;
            void* idle = mgr ? ((MotionByIdFn)(uintptr_t)kMotionByIdFn)(mgr, kNpcIdleMotion, 1) : nullptr;
            if (idle) ((PlayMotionFn)(uintptr_t)play)(doll, idle, 0.f, 0.f, 0, 0);
        }
        return;
    }
    const int set = g_cfg.dollBodySet & 3, key = set * 1000 + no;
    if (key != g_dollMotionKey) {
        g_dollMotionKey = key;
        g_dollMotion = nullptr;
        int id = kBodySets[set].first + no - 1;
        if (id > kBodySets[set].last) id = kBodySets[set].last;
        u8* m = mgr ? (u8*)((MotionByIdFn)(uintptr_t)kMotionByIdFn)(mgr, id, 1) : nullptr;
        const u8* cur = *(u8* volatile*)(doll + 0x18C);
        if (!m || (uintptr_t)m < 0x10000) {
            Log("doll motion: resource %04X is not in memory here, skipped", id);
            return;
        }
        const int bones = *(const u16*)(m + 8), frames = *(const u16*)(m + 0xA);
        if (cur && (uintptr_t)cur > 0x10000 && *(const u16*)(cur + 8) != bones) {
            Log("doll motion: resource %04X has %d bones, the doll's motion has %d, skipped", id, bones, *(const u16*)(cur + 8));
            return;
        }
        ((PlayMotionFn)(uintptr_t)play)(doll, m, 0.f, 0.f, 0, 0);
        g_dollMotion = m;
        Log("doll motion: resource %04X on the doll (%d frames)", id, frames);
    }
    if (!g_dollMotion) return;
    if (g_cfg.dollPlay) {
        *(volatile float*)(doll + 0x1F8) = 1.f;
    } else {
        const int frames = *(const u16*)((const u8*)g_dollMotion + 0xA);
        *(volatile float*)(doll + 0x194) = frames > 1 ? (float)(frames - 1) * (float)g_cfg.dollFrame / 100.f : 0.f;
        *(volatile float*)(doll + 0x1F8) = 0.f;
    }
}

// Mission lab, step 3: the character that was put gives something. With [Debug] MissionLab=1, York within a few steps of the
// doll and the E key: the lab's event 7 runs (a line, a yes / no choice, on yes the game's own give-item command and a line).
void DollTalkTick(u8* player, u8* doll, bool free) {
    if (!LabTextsIn() || (!g_cfg.missionLab && !MissionActive())) return;
    if (g_labPid != -1) return;  // an event of ours runs (LabTick says when it is over)
    const float dx = *(volatile float*)(doll + 0x58) - *(volatile float*)(player + 0x58), dz = *(volatile float*)(doll + 0x60) - *(volatile float*)(player + 0x60);
    const bool close = dx * dx + dz * dz < 12.f * 12.f;
    static bool wasClose = false, held = false;
    if (close && !wasClose)
        ShowNotice(L"Натисніть E, щоб заговорити", L"Press E to talk", MissionActive() ? L"" : L"Лабораторія місій: персонаж має для вас дещо.",
                   MissionActive() ? L"" : L"Mission lab: the character has something for you.", 4);
    wasClose = close;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    const bool down = pid == GetCurrentProcessId() && !ui::IsOpen() && (GetAsyncKeyState('E') & 0x8000);
    const bool hit = down && !held;
    held = down;
    if (!hit || !close || !free) return;
    const u8* ev = *(const u8* volatile*)(uintptr_t)kEventPtrVa;
    if (!ev || (*(volatile u32*)(ev + 0x44) & 0x40000) || *(volatile u32*)(ev + 0x34) || (*(volatile u32*)(uintptr_t)kMovieFlagVa & 0x80000000u) ||
        *(volatile u32*)(uintptr_t)kEventBlockVa) {
        Log("lab: talk not now (an event or a video runs)");
        return;
    }
    if (!LabRegistered() && !LabRegister()) return;
    // The game's talk mode (the frame, York turned to the one he talks to) takes its partner from the process's actor; the
    // actor 7F64h means "the object in CGame+8C57Ch" (00439A00: it is read for its position, +58h, by FUN_00430F20). The
    // doll is put there for the talk and the game's own value comes back when the event is over (the field is in the save).
    u8* game = *(u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    if (!game) return;
    if (!g_talkPartnerSet) {
        g_talkPartnerOld = *(volatile u32*)(game + 0x8C57C);
        g_talkPartnerSet = true;
    }
    *(volatile u32*)(game + 0x8C57C) = (u32)(uintptr_t)doll;
    const int r = ((RunEventFn)(uintptr_t)kRunEventFn)((void*)ev, 0, kLabDsbNo, g_dollTalkEvent, 0, 0x7F64, 0);
    Log("lab: talk with the doll: RunEvent(script %u, event %u, actor 7F64, partner %08X) returned %d", kLabDsbNo, g_dollTalkEvent, (u32)(uintptr_t)doll, r);
    if (r == -1) {
        *(volatile u32*)(game + 0x8C57C) = g_talkPartnerOld;
        g_talkPartnerSet = false;
    }
    if (r != -1) {
        g_labPid = r;
        g_labFrames = 0;
        g_labGiftSeen = false;
        g_labGiftWatch = g_dollTalkEvent == kLabEvGift || g_dollTalkEvent == kMsEvTalkGift || g_dollTalkEvent == kMsEvSay;
        g_labCallArmed = false;
    }
}

// ---- The partner (a try at local co-op): the doll driven by the gamepad while York keeps the keyboard and mouse ----
// It walks where the left stick points, relative to the camera, at York's height (a doll has no physics and the game finds no
// ground for it). Its standing, walking and running motions are learnt from York: whatever motion York has at rest, at a
// walk and at a run is remembered and played on the partner in the same three cases.
void* g_partnerMotion[3];  // York's motion at rest, walking, running
int g_partnerState = -1;
float g_yorkLast[3];
LARGE_INTEGER g_partnerClock;
inline float Atan2f(float y, float x) { float r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
inline float Sqrtf(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
void PartnerTick(u8* player, u8* doll) {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = g_partnerClock.QuadPart ? (float)((double)(now.QuadPart - g_partnerClock.QuadPart) / (double)freq.QuadPart) : 0.f;
    g_partnerClock = now;
    if (dt <= 0.f || dt > 0.2f) dt = 0.f;
    // learn from York: his speed over the ground this update tells which of the three his motion is
    float yp[3];
    for (int i = 0; i < 3; ++i) yp[i] = *(volatile float*)(player + 0x58 + 4 * i);
    if (dt > 0.f && !g_photoMotion) {
        const float vx = (yp[0] - g_yorkLast[0]) / dt, vz = (yp[2] - g_yorkLast[2]) / dt, v = Sqrtf(vx * vx + vz * vz);
        void* m = *(void* volatile*)(player + 0x18C);
        const int kind = v < 0.5f ? 0 : v < 9.f ? 1 : v < 40.f ? 2 : -1;  // faster than a run is a teleport
        const u32 state = *(volatile u32*)(player + 0x654);
        // Each of the three is learnt once, from a motion York kept for half a second at that speed. (0.7.12 took whatever
        // York had every update: his turns, aiming and door motions at rest became the partner's "standing", so the
        // partner seemed to answer the keyboard and its motions fell apart.)
        static void* cand[3];
        static float held[3];
        if (kind >= 0 && m && state != 0x87 && state != 0x88 && !g_partnerMotion[kind]) {
            if (cand[kind] != m) {
                cand[kind] = m;
                held[kind] = 0.f;
            } else if ((held[kind] += dt) >= 0.5f) {
                g_partnerMotion[kind] = m;
                Log("partner: learnt York's %s motion (%08X, York at %d thousandths of a unit a second)",
                    kind == 0 ? "standing" : kind == 1 ? "walking" : "running", (u32)(uintptr_t)m, (int)(v * 1000.f));
            }
        }
    }
    const float yorkWas[3] = {g_yorkLast[0], g_yorkLast[1], g_yorkLast[2]};
    int want = 0;
    if (NetCoopOn()) {
        // A friend over the network: this York's place goes out, the figure stands where the friend's York is (at the friend's own
        // height: the friend's game found the ground). In another location, or with nothing heard, the figure is kept out of sight.
        static int mine = 0;
        if (dt > 0.f) {
            const float vx = (yp[0] - yorkWas[0]) / dt, vz = (yp[2] - yorkWas[2]) / dt, v = Sqrtf(vx * vx + vz * vz);
            if (v < 60.f) mine = v < 0.5f ? 0 : v < 9.f ? 1 : 2;
        }
        for (int i = 0; i < 3; ++i) g_yorkLast[i] = yp[i];
        const u8* gm = *(const u8* volatile*)(uintptr_t)kGameManagerPtrVa;
        const u32 zoneNow = gm ? *(volatile u32*)(gm + 0x834FE0) : 0;
        const u32 st = *(volatile u32*)(player + 0x654);
        // which of the game's cars York has: CGame+9906Ch, the number the game itself takes for "York's car" (FUN_005F1840)
        const bool driving = st == 0x87 || st == 0x88;
        const int myCar = gm ? (int)*(volatile u16*)(gm + 0x9906C) : 0;
        NetCoopExchange(yp, *(volatile float*)(player + 0x7C), zoneNow, mine, driving, myCar);
        NetPeerState peer;
        static bool shown = false;
        const bool see = NetCoopPeer(&peer) && peer.zone == zoneNow;
        static float carAt[3];
        static bool carShown = false;
        if (see && peer.inCar) {
            // The friend drives: one of the game's cars is held where the friend's York is, and the figure is put out of sight.
            // A car of the same number as the one this York sits in would be pulled from under him, so another one stands in.
            int n = peer.car == 7 || peer.car == 8 || (peer.car >= 77 && peer.car <= 90) ? peer.car : 7;
            if (driving && n == myCar) n = n == 7 ? 8 : 7;
            const float ex = peer.pos[0] - carAt[0], ey = peer.pos[1] - carAt[1], ez = peer.pos[2] - carAt[2];
            const float k = !carShown || ex * ex + ez * ez > 60.f * 60.f || dt <= 0.f ? 1.f : (dt * 14.f > 1.f ? 1.f : dt * 14.f);
            carAt[0] += ex * k;
            carAt[1] += ey * k;
            carAt[2] += ez * k;
            const bool ok = NetCarAt(n, carAt, peer.yaw);
            if (ok != carShown) Log("net partner: the friend's car is %s", ok ? "shown" : "not shown (needs free play outdoors in Greenvale)");
            carShown = ok;
            g_dollBase[0] = yp[0];
            g_dollBase[1] = yp[1] - 400.f;
            g_dollBase[2] = yp[2];
            shown = false;
        } else if (see) {
            carShown = false;
            const float ex = peer.pos[0] - g_dollBase[0], ey = peer.pos[1] - g_dollBase[1], ez = peer.pos[2] - g_dollBase[2];
            const float k = !shown || ex * ex + ez * ez > 40.f * 40.f || dt <= 0.f ? 1.f : (dt * 12.f > 1.f ? 1.f : dt * 12.f);  // eased; a jump is taken at once
            g_dollBase[0] += ex * k;
            g_dollBase[1] += ey * k;
            g_dollBase[2] += ez * k;
            g_dollYaw = peer.yaw;
            want = peer.kind;
        } else {
            g_dollBase[0] = yp[0];
            g_dollBase[1] = yp[1] - 400.f;  // far under the ground
            g_dollBase[2] = yp[2];
        }
        const bool onFoot = see && !peer.inCar;
        if (onFoot != shown) Log("net partner: the friend's figure is %s", onFoot ? "shown" : see ? "hidden (the friend drives)" : "hidden (another location or nothing heard)");
        shown = onFoot;
    } else {
    for (int i = 0; i < 3; ++i) g_yorkLast[i] = yp[i];
    // the pad
    float sx = 0.f, sy = 0.f;
    u32 buttons = 0;
    static bool padSeen = false, padTold = false;
    const bool pad = PartnerPad(&sx, &sy, &buttons);
    if (pad != padSeen || !padTold) {
        padTold = true;
        padSeen = pad;
        Log("partner: the gamepad is %s", pad ? "read" : "not found (the partner stands)");
    }
    float mag = Sqrtf(sx * sx + sy * sy);
    if (mag > 1.f) { sx /= mag; sy /= mag; mag = 1.f; }
    // camera forward on the ground
    volatile float* eye = (volatile float*)(uintptr_t)(kRenderCameraVa + 0x04);
    volatile float* tgt = (volatile float*)(uintptr_t)(kRenderCameraVa + 0x14);
    float fx = tgt[0] - eye[0], fz = tgt[2] - eye[2];
    const float len = Sqrtf(fx * fx + fz * fz);
    if (len > 1e-4f) { fx /= len; fz /= len; } else { fx = 0.f; fz = 1.f; }
    const float rx = -fz, rz = fx;  // screen right, as for the free camera and the fly cheat
    const bool run = mag > 0.85f;
    const float speed = mag > 0.f ? (run ? 15.f : 5.5f) : 0.f;
    const float dx = (fx * sy + rx * sx), dz = (fz * sy + rz * sx);
    if (mag > 0.f && dt > 0.f) {
        const float dl = Sqrtf(dx * dx + dz * dz);
        if (dl > 1e-4f) {
            g_dollBase[0] += dx / dl * speed * dt;
            g_dollBase[2] += dz / dl * speed * dt;
            g_dollYaw = Atan2f(dx, dz);  // forward = (sin, cos) of the heading
        }
    }
    // York's height, eased; Y / Triangle (WinMM button 4) or a long way off: back next to York
    g_dollBase[1] += (yp[1] - g_dollBase[1]) * (dt > 0.f ? (dt * 6.f > 1.f ? 1.f : dt * 6.f) : 0.f);
    const float ox = g_dollBase[0] - yp[0], oz = g_dollBase[2] - yp[2];
    // York went somewhere at once (another location: the zone number at CGame+834FE0h changed; or a jump of more than 30
    // units in one update: a door, a car, a script): the partner comes along
    static u32 lastZone = 0xFFFFFFFFu;
    const u8* game = *(const u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    const u32 zone = game ? *(volatile u32*)(game + 0x834FE0) : 0;
    const float jx = yp[0] - yorkWas[0], jz = yp[2] - yorkWas[2];
    const bool moved = zone != lastZone || jx * jx + jz * jz > 30.f * 30.f;
    if (moved && lastZone != 0xFFFFFFFFu) Log("partner: York went elsewhere (zone %u -> %u), the partner comes along", lastZone, zone);
    lastZone = zone;
    if (moved || (buttons & 8u) || ox * ox + oz * oz > 400.f * 400.f) {
        const float yaw = *(volatile float*)(player + 0x7C);
        g_dollBase[0] = yp[0] + Cosf(yaw) * 6.f;
        g_dollBase[1] = yp[1];
        g_dollBase[2] = yp[2] - Sinf(yaw) * 6.f;
    }
    want = mag <= 0.f ? 0 : run ? 2 : 1;
    }
    for (int i = 0; i < 3; ++i) *(volatile float*)(doll + 0x58 + 4 * i) = g_dollBase[i];
    *(volatile float*)(doll + 0x7C) = *(volatile float*)(doll + 0x6C) = g_dollYaw;
    // the motion of the case
    void* m = g_partnerMotion[want] ? g_partnerMotion[want] : g_partnerMotion[want == 2 ? 1 : 0];
    if (m && (want != g_partnerState || *(void* volatile*)(doll + 0x18C) != m)) {
        const u8* cur = *(u8* volatile*)(doll + 0x18C);
        if (!cur || (uintptr_t)cur < 0x10000 || *(const u16*)(cur + 8) == *(const u16*)((const u8*)m + 8)) {
            const u32 play = (*(u32* volatile*)doll)[0x54 / 4];
            ((PlayMotionFn)(uintptr_t)play)(doll, m, 0.f, 6.f, 1, 0);
            *(volatile float*)(doll + 0x1F8) = 1.f;
        }
        g_partnerState = want;
    }
}

void DollTick(void* playerPtr) {
    u8* player = (u8*)playerPtr;
    const bool scene = (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u) != 0;
    const bool free = *(volatile u32*)(uintptr_t)kGameplayModeVa == 3 && !scene;
    static bool wasPhoto = false;
    const bool photo = g_cfg.photoActive || g_cfg.photoFreeze;
    // (with the mission lab on the doll stays after the photo mode: it is the one York talks to)
    if (g_doll && (scene || (wasPhoto && !photo && !g_cfg.missionLab && !MissionActive() && !g_cfg.coopPartner && !NetCoopOn()))) DollRemove(scene ? "a scene began" : "the photo mode ended");
    wasPhoto = photo;
    PlayAsTick(player, free);
    const int req = g_dollRequest;
    if (req) {
        g_dollRequest = 0;
        if (req == 2) DollRemove("asked");
        else if (!free || *(volatile u32*)player != kPlayerVtable) Log("doll: not now (needs free play on foot outside any scene)");
        else DollSpawn(player);
    }
    {   // the partner: there while its switch is on, gone when it goes off
        static bool wasPartner = false;
        static DWORD asked = 0;
        if ((g_cfg.coopPartner || NetCoopOn()) && !g_doll && free && *(volatile u32*)player == kPlayerVtable && GetTickCount() - asked > 3000) {
            asked = GetTickCount();
            g_partnerState = -1;
            g_dollRequest = 1;
        }
        if (!(g_cfg.coopPartner || NetCoopOn()) && wasPartner && g_doll) DollRemove("the partner was switched off");
        wasPartner = g_cfg.coopPartner != 0 || NetCoopOn();
    }
    u8* doll = DollAlive();
    if (!doll) return;
    if (g_cfg.coopPartner || NetCoopOn()) {
        PartnerTick(player, doll);
        return;
    }
    const float s = Sinf(g_dollYaw), c = Cosf(g_dollYaw);  // forward = where the doll faced when it was put
    const float right = (float)g_cfg.dollMoveX * 0.1f, fwd = (float)g_cfg.dollMoveZ * 0.1f;
    *(volatile float*)(doll + 0x58) = g_dollBase[0] + s * fwd + c * right;
    *(volatile float*)(doll + 0x5C) = g_dollBase[1] + (float)g_cfg.dollMoveY * 0.1f;
    *(volatile float*)(doll + 0x60) = g_dollBase[2] + c * fwd - s * right;
    *(volatile float*)(doll + 0x7C) = *(volatile float*)(doll + 0x6C) = g_dollYaw + (float)g_cfg.dollTurn * (3.14159265f / 180.f);
    DollMotionTick(doll);
    DollTalkTick(player, doll, free);
}

void __attribute__((thiscall)) H_DollFaceWeights(void* actor, float* w, int n) {
    r_dollFaceWeights(actor, w, n);
    if (!actor || actor != g_doll || g_cfg.photoFaceTarget != 1 || !w || n < 4 || n > 64) return;
    PhotoFaceApply(w, n);
}

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
    if (*(volatile u32*)(uintptr_t)kDollFaceSlot == kFaceWeightsFn)
        SwapImport((void**)(uintptr_t)kDollFaceSlot, (void*)H_DollFaceWeights, (void**)&r_dollFaceWeights);
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

void ufx::RequestDoll(int what) { g_dollRequest = what; }
bool ufx::DollName(int id, char* out16) {
    const u8* main = *(u8* volatile*)(uintptr_t)kNpcMainPtrVa;
    if (id < 1 || id > 49 || !main || IsBadReadPtr(main + 0xD4C, 4)) return false;
    const char* rec = *(const char* volatile*)(main + 0xD4C);
    if (!rec || IsBadReadPtr(rec + id * 0xA8, 16)) return false;
    rec += id * 0xA8;
    int n = 0;
    for (; n < 15 && rec[n] >= 0x20 && rec[n] < 0x7F; ++n) out16[n] = rec[n];
    out16[n] = 0;
    return n > 0;
}

// ---- for the mission editor (missions.cpp) ----
bool ufx::PlayerPlace(float out[4]) {
    u8* p = (u8*)(uintptr_t)g_playerObject;
    if (!p || IsBadReadPtr(p, 0x80) || *(volatile u32*)p != kPlayerVtable) return false;
    for (int i = 0; i < 3; ++i) out[i] = *(volatile float*)(p + 0x58 + 4 * i);
    out[3] = *(volatile float*)(p + 0x7C);
    return true;
}
void ufx::DollPutAt(int resident, const float place[4], int motionSet, int motion, u32 talkEvent) {
    g_cfg.photoDollId = resident;
    for (int i = 0; i < 4; ++i) g_dollAt[i] = place[i];
    g_dollAtOn = true;
    g_dollTalkEvent = talkEvent;
    g_dollRequest = 1;
    g_dollPendingSet = motionSet;
    g_dollPendingMotion = motion;
}
void ufx::DollTalkDefault() { g_dollTalkEvent = kLabEvGift; }
void ufx::LabSetGiveItem(int id, int count) {
    g_labGiveItem = id;
    g_labGiveCount = count < 1 ? 1 : count;
}
// Runs an event of the mod's script on the game thread; -1 when the game cannot take it now
int ufx::LabRunEvent(u32 event) {
    const u8* ev = *(const u8* volatile*)(uintptr_t)kEventPtrVa;
    if (g_labPid != -1 || !ev || *(volatile u32*)(uintptr_t)kGameplayModeVa != 3 || (*(volatile u32*)(ev + 0x44) & 0x40000) || *(volatile u32*)(ev + 0x34) ||
        (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u) || (*(volatile u32*)(uintptr_t)kMovieFlagVa & 0x80000000u) ||
        *(volatile u32*)(uintptr_t)kEventBlockVa)
        return -1;
    if (!LabRegistered() && !LabRegister()) return -1;
    const int r = ((RunEventFn)(uintptr_t)kRunEventFn)((void*)ev, 0, kLabDsbNo, event, 0, 0, 0);
    Log("lab: RunEvent(script %u, event %u) returned %d", kLabDsbNo, event, r);
    if (r != -1) {
        g_labPid = r;
        g_labFrames = 0;
        g_labGiftSeen = false;
    }
    return r;
}
bool ufx::DollStands() { return g_doll != nullptr; }

// Once a frame (the player's update hardly runs while an event holds the game): the branch of the first answer shows as an
// event thread of script 900, event 22 or 13 (00BDC960h, 80 x C4h, +18h in use, +14h -> u16 script, u16 event), or as the
// target of the last call a script made (the event core, +0Ch..+13h).
void ufx::LabWatchFrame() {
    if (g_labPid == -1 || !g_labGiftWatch || g_labGiftSeen) return;
    for (int i = 0; i < 80; ++i) {
        const u8* t = (const u8*)(uintptr_t)(0xBDC960 + i * 0xC4);
        if (!*(volatile u32*)(t + 0x18)) continue;
        const u16* rec = *(const u16* volatile*)(t + 0x14);
        if (rec && (uintptr_t)rec > 0x10000 && !IsBadReadPtr(rec, 4) && rec[0] == kLabDsbNo && (rec[1] == kMsEvYesGift || rec[1] == kLabEvTakeGift)) {
            g_labGiftSeen = true;
            Log("lab: the first answer's branch seen as an event thread (event %u)", rec[1]);
            return;
        }
    }
    const u8* core = *(const u8* volatile*)(uintptr_t)kEvCorePtrVa;
    if (core && !IsBadReadPtr(core + 0xC, 8)) {
        const u16* w = (const u16*)(core + 0xC);
        bool match = false;
        for (int i = 0; i < 3; ++i) match |= w[i] == kLabDsbNo && (w[i + 1] == kMsEvYesGift || w[i + 1] == kLabEvTakeGift);
        if (!match) {
            g_labCallArmed = true;  // (the talk's first call is the talk wrapper, COMMON 3 / 6)
        } else if (g_labCallArmed) {
            g_labGiftSeen = true;
            Log("lab: the first answer's branch seen as the last called event (%04X %04X %04X %04X)", w[0], w[1], w[2], w[3]);
        }
    }
}
