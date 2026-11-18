// Culling distances of the camera.
//
// The camera function builds six far planes, one per visibility class, by loading six constants. Objects that
// carry no class bit fall into the last class (500 units), so huts and vending machines disappear at that
// distance, which the Xbox 360 version does not do. Each load is redirected to a float slot of ours; the slots
// hold the original values except for the classes the user changes. The function runs every frame, so changes
// take effect at once.
#include "../base.h"
#include "../sites.h"

namespace ufx {
namespace {

float* g_slots = nullptr;  // six floats, in the order of the classes

u32 BitsOf(float f) {
    u32 bits;
    const u8* s = (const u8*)&f;
    u8* d = (u8*)&bits;
    for (int i = 0; i < 4; ++i) d[i] = s[i];
    return bits;
}

void StoreSlot(int index, int value) {
    float f = (float)value;
    u32 bits = BitsOf(f);
    ((volatile u32*)g_slots)[index] = bits;
}

}  // namespace

void ApplyWorldDistance() {
    if (!g_slots) return;
    int v3 = g_cfg.farClass3, v4 = g_cfg.farClass4, v5 = g_cfg.farClass5;
    if (v3 < 5000) v3 = 5000;  // the game's own values are the floor
    if (v4 < 1000) v4 = 1000;
    if (v5 < 500) v5 = 500;
    StoreSlot(3, v3);
    StoreSlot(4, v4);
    StoreSlot(5, v5);
}

bool InstallWorldDistance() {
    for (const SiteDef& s : kFarPlane) {
        if (!BytesMatch(s.va, s.bytes, s.len)) {
            Log("world: the camera far-plane code at %08X differs from DP.exe 1.01b, skipped", s.va);
            return false;
        }
    }
    u8* arena = AllocStub(32);
    if (!arena) {
        Log("world: no memory for the distance slots, skipped");
        return false;
    }
    g_slots = (float*)arena;
    StoreSlot(0, 200000);
    StoreSlot(1, 80000);
    StoreSlot(2, 20000);
    ApplyWorldDistance();
    for (int i = 0; i < 6; ++i) {
        u32 slotVa = (u32)(uintptr_t)(g_slots + i);
        u8 imm[4] = {(u8)slotVa, (u8)(slotVa >> 8), (u8)(slotVa >> 16), (u8)(slotVa >> 24)};
        WriteCode(kFarPlane[i].va + 2, imm, 4);  // the address operand of `fld dword [abs]`
    }
    Log("world: culling distances 5000/1000/500 -> %d/%d/%d", g_cfg.farClass3, g_cfg.farClass4, g_cfg.farClass5);
    return true;
}

}  // namespace ufx
