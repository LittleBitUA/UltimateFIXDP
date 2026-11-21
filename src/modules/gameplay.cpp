// Gameplay fixes: difficulty (the PC port always plays on Easy), the wet reflection gate, and a tolerance for
// the camera angle limits (exact x87 float compares that fail by one ulp).
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {
namespace {

u8 g_difficultySel = 1;          // the chosen difficulty: the start cursor of the selection screen (written into the title stubs),
                                 // or read by the store stub when there is no selection screen
bool g_difficultyReady = false;  // sites verified and patched
bool g_wetReady = false;
volatile u32* g_dialogCursor[2] = {nullptr, nullptr};  // the cursor value in the two selection screen stubs (our own memory)

// The title stubs put the selection screen's cursor on Normal (`mov dword [cursor], 1`); the cursor is what the new-game
// store writes as the difficulty, so it is set to the chosen one instead and the screen opens on it.
void SetDialogCursor() {
    for (volatile u32* c : g_dialogCursor)
        if (c) *c = g_difficultySel;
}

// The immediate of `mov dword [kTitleCursorVa], imm32` in a stub built by BuildTitleDialogStub, after checking the bytes.
volatile u32* CursorImmediate(u8* stub) {
    const u8 want[6] = {0xC7, 0x05, (u8)kTitleCursorVa, (u8)(kTitleCursorVa >> 8), (u8)(kTitleCursorVa >> 16),
                        (u8)(kTitleCursorVa >> 24)};
    for (int i = 0; i < 6; ++i)
        if (stub[10 + i] != want[i]) return nullptr;
    return (volatile u32*)(stub + 16);
}
alignas(8) const double kAimEpsilon = 1e-5;  // radians, about 0.0006 degrees; one float32 ulp is ~1e-7 below 1.0

u8* MakeStub(size_t size) {
    u8* p = AllocStub(size);
    if (!p) Log("gameplay: no room for a stub");
    return p;
}

}  // namespace

// Writes the chosen difficulty into the running game (keeps the "finished" flag in bit 7).
static void WriteDifficultyNow() {
    u32 manager = *(volatile const u32*)(uintptr_t)kGameManagerPtrVa;
    if (!manager) return;
    volatile u8* b = (volatile u8*)(uintptr_t)(manager + kDifficultyOffset);
    *b = (u8)((*b & 0x80) | g_difficultySel);
}

// Wet reflection gate. (The difficulty has its own function: this one is called from unrelated menu items and must
// never overwrite the difficulty the player chose in the game.)
void ApplyGameplay() {
    if (g_wetReady) {
        u8 v = g_cfg.wetReflections ? 1 : 0;
        WriteCode(kSiteWetGate.va + 6, &v, 1);
    }
}

// The menu changed the difficulty: write it into the running game, and the selection screen of a new game opens on it.
void ApplyDifficulty() {
    if (g_cfg.difficulty < 0) g_cfg.difficulty = 0;
    if (g_cfg.difficulty > 2) g_cfg.difficulty = 2;
    if (!g_difficultyReady) return;
    g_difficultySel = (u8)g_cfg.difficulty;
    SetDialogCursor();
    WriteDifficultyNow();
}

// While the menu is open it shows the difficulty the game really has (chosen on the selection screen or loaded).
void SyncDifficultyFromGame() {
    if (!g_difficultyReady) return;
    u32 manager = *(volatile const u32*)(uintptr_t)kGameManagerPtrVa;
    if (!manager) return;
    u8 v = *(volatile const u8*)(uintptr_t)(manager + kDifficultyOffset) & 0x7F;
    if (v <= 2) g_cfg.difficulty = v;
}

bool InstallDifficulty() {
    const SiteDef& store = kSiteDifficultyNewGame;
    if (!BytesMatch(store.va, store.bytes, store.len)) {
        Log("difficulty: bytes at %08X differ from DP.exe 1.01b, skipped", store.va);
        return false;
    }
    g_difficultySel = (u8)(g_cfg.difficulty < 0 ? 0 : g_cfg.difficulty > 2 ? 2 : g_cfg.difficulty);

    // The selection screen (optional): both title places that skip it jump to state 3; the cursor starts on the chosen difficulty (SetDialogCursor).
    bool dialog = false;
    if (g_cfg.difficultyDialog) {
        const SiteDef& d1 = kSiteDifficultyDialog1;
        const SiteDef& d2 = kSiteDifficultyDialog2;
        if (BytesMatch(d1.va, d1.bytes, d1.len) && BytesMatch(d2.va, d2.bytes, d2.len)) {
            u8* s1 = MakeStub(32);
            u8* s2 = MakeStub(32);
            if (s1 && s2) {
                BuildTitleDialogStub(s1, (u32)(uintptr_t)s1, kTitleStateVa, kTitleCursorVa, kDifficultyDialog1Return);
                BuildTitleDialogStub(s2, (u32)(uintptr_t)s2, kTitleStateVa, kTitleCursorVa, kDifficultyDialog2Return);
                g_dialogCursor[0] = CursorImmediate(s1);
                g_dialogCursor[1] = CursorImmediate(s2);
                SetDialogCursor();  // before the jumps go in: the game never sees the stubs with the old value
                dialog = PatchJump(d1.va, d1.bytes, d1.len, s1) && PatchJump(d2.va, d2.bytes, d2.len, s2);
            }
        }
        if (!dialog) Log("difficulty: the selection screen could not be enabled, the setting is used instead");
    }

    u8* st = MakeStub(32);
    if (!st) return false;
    u32 selVa = dialog ? kTitleCursorVa : (u32)(uintptr_t)&g_difficultySel;
    BuildDifficultyStore(st, selVa, kDifficultyOffset);
    if (!PatchCall(store.va, store.bytes, store.len, st)) return false;

    if (g_cfg.keepSavedDifficulty) {
        const SiteDef& c = kSiteDifficultyLoadClear;
        if (BytesMatch(c.va, c.bytes, c.len)) {
            u8 nops[7] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
            WriteCode(c.va, nops, c.len);
        } else {
            Log("difficulty: the clear after loading a slot differs from DP.exe 1.01b, left as is");
        }
    }
    g_difficultyReady = true;
    const char* name = g_difficultySel == 0 ? "easy" : g_difficultySel == 1 ? "normal" : "hard";
    if (dialog)
        Log("difficulty: a new game asks on the selection screen (cursor on %s%s), saved difficulty %s", name,
            g_dialogCursor[0] && g_dialogCursor[1] ? "" : ", or on normal where the stub differs",
            g_cfg.keepSavedDifficulty ? "kept" : "reset to easy on load");
    else
        Log("difficulty: new games start on %d (%s), saved difficulty %s", (int)g_difficultySel, name,
            g_cfg.keepSavedDifficulty ? "kept" : "reset to easy on load");
    return true;
}

bool InstallWetGate() {
    const SiteDef& s = kSiteWetGate;
    bool match = BytesMatch(s.va, s.bytes, 6) && BytesMatch(s.va + 7, s.bytes + 7, 3);
    u8 imm = *(const u8*)(uintptr_t)(s.va + 6);
    if (!match || imm > 1) {
        Log("wet reflections: bytes at %08X differ from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    g_wetReady = true;
    return true;
}

bool InstallAimTolerance() {
    const SiteDef& v = kSiteAimV;
    if (!BytesMatch(kSiteAimH.va, kAimHFull, sizeof(kAimHFull)) || !BytesMatch(v.va, v.bytes, v.len)) {
        Log("aim: bytes at %08X / %08X differ from DP.exe 1.01b, skipped", kSiteAimH.va, v.va);
        return false;
    }
    u8* h = MakeStub(80);
    u8* w = MakeStub(48);
    if (!h || !w) return false;
    u32 eps = (u32)(uintptr_t)&kAimEpsilon;
    BuildAimHorizontal(h, (u32)(uintptr_t)h, eps, kAimHNotEqual, kAimHEqual);
    BuildAimVertical(w, (u32)(uintptr_t)w, eps, kAimVExit);
    if (!PatchJump(kSiteAimH.va, kAimHFull, sizeof(kAimHFull), h)) return false;
    if (!PatchJump(v.va, v.bytes, v.len, w)) {
        Log("aim: the vertical limit could not be patched");
        return true;  // the horizontal one is in place
    }
    Log("aim: camera angle limits compare with a tolerance of %d e-6 rad", 10);
    return true;
}

// kSiteLockOnDrop / kSiteLockOnHeight: the lock-on point no longer drops with distance and sits LockOnHeight above the waist.
const double kLockOnNoDrop = 0.0;
float g_lockOnHeight = 0.f;

bool InstallLockOnAim() {
    const SiteDef& d = kSiteLockOnDrop;
    const SiteDef& h = kSiteLockOnHeight;
    if (!BytesMatch(d.va, d.bytes, d.len) || !BytesMatch(h.va, h.bytes, h.len)) {
        Log("aim: the lock-on code at %08X / %08X differs from DP.exe 1.01b, left as it is", d.va, h.va);
        return false;
    }
    u8* stub = MakeStub(32);
    if (!stub) return false;
    g_lockOnHeight = (float)g_cfg.lockOnHeight / 10.f;
    BuildLockOnHeightStub(stub, (u32)(uintptr_t)stub, (u32)(uintptr_t)&g_lockOnHeight, kLockOnHeightBack);
    u8 drop[6] = {0xDD, 0x05, 0, 0, 0, 0};  // fld qword [our 0.0]
    const u32 zero = (u32)(uintptr_t)&kLockOnNoDrop;
    for (int i = 0; i < 4; ++i) drop[2 + i] = (u8)(zero >> (8 * i));
    if (!PatchJump(h.va, h.bytes, h.len, stub)) return false;
    if (!WriteCode(d.va, drop, sizeof(drop))) Log("aim: the lock-on drop could not be patched (the height is in place)");
    Log("aim: the lock-on aims %d.%d units above the waist, without the drop of a tenth of the distance", g_cfg.lockOnHeight / 10,
        (g_cfg.lockOnHeight < 0 ? -g_cfg.lockOnHeight : g_cfg.lockOnHeight) % 10);
    return true;
}

bool InstallRiverReflections() {
    const SiteDef& g = kSiteRiverWeightGate;
    const SiteDef& f = kSiteRiverWeightFloor;
    if (!BytesMatch(g.va, g.bytes, g.len) || !BytesMatch(f.va, f.bytes, f.len)) {
        Log("water: the river reflection weight at %08X / %08X differs from DP.exe 1.01b, left as it is", g.va, f.va);
        return false;
    }
    const u8 jmp[2] = {0xEB, 0x0E};
    const u8 floor100[5] = {0xBA, 0x64, 0x00, 0x00, 0x00};
    if (!WriteCode(g.va, jmp, 2) || !WriteCode(f.va, floor100, 5)) return false;
    Log("water: rivers keep their reflection outside cutscenes (weight 1.0)");
    return true;
}

bool InstallGameplay() {
    int n = 0;
    if (g_cfg.difficultyEnabled && InstallDifficulty()) ++n;
    if (InstallWetGate()) ++n;
    if (g_cfg.aimTolerance && InstallAimTolerance()) ++n;
    if (g_cfg.lockOnAim && InstallLockOnAim()) ++n;
    if (g_cfg.riverReflections && InstallRiverReflections()) ++n;
    ApplyGameplay();
    return n > 0;
}

}  // namespace ufx
