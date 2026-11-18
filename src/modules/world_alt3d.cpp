// Full models of far objects (the pop-in in front of the player).
//
// The streaming manager shows objects outside a 4x4 block of cells around the view (about 5000 units wide) by a low detail
// "alternate" package and swaps in the full model only inside that block. A hook after each run of the residency state
// machine looks at the objects in a wider ring of cells around the same block and asks the engine, through its own
// SetTarget function, to give them their full model. It follows the engine's own exceptions (some resources, some object
// types, the player's car, the other grid mode) and never touches anything it cannot validate. The idea comes from the
// Alternate3D feature of ZachFix; the algorithm and its checks are our own.
#include "../base.h"
#include "../sites.h"

namespace ufx {
typedef int i32;
namespace {

typedef int(__attribute__((thiscall)) * ResidencyFn)(void* mgr, void* a0, void* a1);
typedef void(__attribute__((thiscall)) * SetTargetFn)(void* mgr, int index, int target);
typedef int(__attribute__((thiscall)) * SlotResourceFn)(void* base, int kind, int index);

int g_idleTicks = 0;
unsigned g_calls = 0, g_promoted = 0, g_reverted = 0;
unsigned g_statScanned = 0, g_statAlt = 0, g_statPending = 0;
bool g_firstLogged = false;
// the top of the user address space as the system reports it (UserAddressEnd): with the large address flag (ufx_laa.exe)
// objects streamed in late can lie above 0x80000000; set at install
u32 g_ptrEnd = 0x7FFE0000u;

bool Finite(float f) {
    u32 bits;
    const u8* s = (const u8*)&f;
    u8* d = (u8*)&bits;
    for (int i = 0; i < 4; ++i) d[i] = s[i];
    return ((bits >> 23) & 0xFF) != 0xFF;
}

bool InMain(u32 va) { return va >= 0x76E000 && va < 0x885000; }

void Promote(u8* mgr) {
    ++g_calls;
    if (*(volatile i32*)(mgr + 0x283CC) != 0) return;  // the other grid: left to the engine
    int ring = g_cfg.alt3dScale - 1;
    if (ring <= 0) return;
    i32 world = *(volatile i32*)(mgr + 0x28004);
    if (world < 0 || world > 8) return;
    u32 base = 0x1437A20u + (u32)world * 0x232Cu;
    if (base < 0x885000u || base + 0x20u > 0x14B5000u) return;
    if (*(volatile u8*)(uintptr_t)(base + 4) == 0) return;
    i32 first = *(volatile i32*)(uintptr_t)(base + 0x10), end = *(volatile i32*)(uintptr_t)(base + 0x14);
    if (first < 0 || end < first || end > 0xA000) return;

    // the window: the cells of the engine's own near block, widened by the ring
    int minX = 64, maxX = -1, minZ = 64, maxZ = -1, valid = 0;
    for (int i = 0; i < 16; ++i) {
        i32 c = *(volatile i32*)(mgr + 0x2801C + i * 4);
        if (c < 0 || c >= 4096) continue;
        int cx = c & 63, cz = c >> 6;
        if (cx < minX) minX = cx;
        if (cx > maxX) maxX = cx;
        if (cz < minZ) minZ = cz;
        if (cz > maxZ) maxZ = cz;
        ++valid;
    }
    if (valid < 4) return;
    int loX = minX - ring, hiX = maxX + ring, loZ = minZ - ring, hiZ = maxZ + ring;
    int ccx = (minX + maxX) / 2, ccz = (minZ + maxZ) / 2;

    int pending = 0, nPicks = 0;
    int pickIdx[4], pickDist[4];
    unsigned scanned = 0, alts = 0;
    SlotResourceFn slotResource = (SlotResourceFn)(uintptr_t)kSlotResourceFn;
    SetTargetFn setTarget = (SetTargetFn)(uintptr_t)kSetTargetFn;

    for (int idx = first; idx < end; ++idx) {
        u32 o = *(volatile u32*)(mgr + idx * 4);
        if (!o) continue;
        if ((o & 3) || o < 0x10000u || o >= g_ptrEnd) continue;
        if (!InMain(*(volatile u32*)(uintptr_t)o)) continue;          // not an object with a vtable of the game
        if (*(volatile u8*)(uintptr_t)(o + 0x29) & 0x80) continue;    // dead or failed
        ++scanned;
        int cur = *(volatile signed char*)(uintptr_t)(o + 0x12);
        int alt = *(volatile signed char*)(uintptr_t)(o + 0x416);
        i32 target = *(volatile i32*)(uintptr_t)(o + 0x444);
        if (alt < 0 || alt >= 75 || (cur != 0 && cur != 1) || (target != 0 && target != 1)) continue;
        if (cur == 1) ++alts;
        if (cur == 1 && target == 0) {  // already on its way to the full model
            ++pending;
            continue;
        }
        if (target == 0) continue;
        u32 type = *(volatile u16*)(uintptr_t)(o + 0x30);
        if (type == 0x33 && *(volatile u8*)(uintptr_t)(o + 0x206B)) continue;  // the engine's own exceptions
        if (type == 0xAA && *(volatile u8*)(uintptr_t)(o + 0x45D)) continue;
        if (type == 0x53) continue;
        int res = slotResource((void*)(uintptr_t)base, 0, idx);
        if (res < 0 || res == 0x3863 || res == 0x3865 || res == 0x3867 || res == 0x3869 || res == 0x3716) continue;
        float x = *(volatile float*)(uintptr_t)(o + 0x58), z = *(volatile float*)(uintptr_t)(o + 0x60);
        if (!Finite(x) || !Finite(z)) continue;
        if (x < -40000.f || x > 40000.f || z < -40000.f || z > 40000.f) continue;
        int cx = (int)((x + 40000.0f) * 0.0008f), cz = (int)((z + 40000.0f) * 0.0008f);
        if (cx < loX || cx > hiX || cz < loZ || cz > hiZ) continue;
        if (cur == 0) {  // the sweep marked a full model for demotion: take the mark back (no loading involved)
            setTarget(mgr, idx, 0);
            ++g_reverted;
            continue;
        }
        int dx = cx - ccx, dz = cz - ccz;
        int dist = (dx < 0 ? -dx : dx) > (dz < 0 ? -dz : dz) ? (dx < 0 ? -dx : dx) : (dz < 0 ? -dz : dz);
        if (nPicks < 4) {
            pickIdx[nPicks] = idx;
            pickDist[nPicks++] = dist;
        } else {
            int worst = 0;
            for (int k = 1; k < 4; ++k)
                if (pickDist[k] > pickDist[worst]) worst = k;
            if (dist < pickDist[worst]) {
                pickIdx[worst] = idx;
                pickDist[worst] = dist;
            }
        }
    }
    g_statScanned = scanned;
    g_statAlt = alts;
    g_statPending = (unsigned)pending;
    if (pending < 8)  // a few at a time: every promotion queues its own loads
        for (int k = 0; k < nPicks; ++k) {
            setTarget(mgr, pickIdx[k], 0);
            ++g_promoted;
        }
    if (!g_firstLogged || g_calls % 2000 == 0) {
        g_firstLogged = true;
        Log("alt3d: ring %d, window cells %d..%d x %d..%d; scanned %u, alternate %u, loading %d; promoted %u, reverted %u", ring,
            loX, hiX, loZ, hiZ, scanned, alts, pending, g_promoted, g_reverted);
    }
}

int __attribute__((fastcall)) PhaseHook(void* mgr, void* edx, void* a0, void* a1) {
    (void)edx;
    int p0 = *(volatile i32*)((u8*)mgr + 0x28410);
    int r = ((ResidencyFn)(uintptr_t)kResidencyFn)(mgr, a0, a1);
    int p1 = *(volatile i32*)((u8*)mgr + 0x28410);
    if (g_cfg.alt3dScale > 1) {
        bool swept = p1 == 7 && (p0 == 0 || p0 == 3);
        bool idle = false;
        if (p1 == 0 && ++g_idleTicks >= 20) {
            g_idleTicks = 0;
            idle = true;  // objects the worker spawned since the last sweep
        }
        if (swept || idle) Promote((u8*)mgr);
    }
    return r;
}

}  // namespace

bool InstallAlternate3D() {
    for (const SiteDef& s : kAltPhaseCalls)
        if (!BytesMatch(s.va, s.bytes, s.len)) {
            Log("alt3d: the call at %08X differs from DP.exe 1.01b, skipped", s.va);
            return false;
        }
    if (!BytesMatch(kSiteResidencyEntry.va, kSiteResidencyEntry.bytes, kSiteResidencyEntry.len) ||
        !BytesMatch(kSiteSetTargetEntry.va, kSiteSetTargetEntry.bytes, kSiteSetTargetEntry.len) ||
        !BytesMatch(kSiteResourceOfSlot.va, kSiteResourceOfSlot.bytes, kSiteResourceOfSlot.len)) {
        Log("alt3d: the residency functions differ from DP.exe 1.01b, skipped");
        return false;
    }
    g_ptrEnd = UserAddressEnd();
    for (const SiteDef& s : kAltPhaseCalls)
        if (!PatchCall(s.va, s.bytes, s.len, (const void*)PhaseHook)) {
            Log("alt3d: could not patch the call at %08X", s.va);
            return false;
        }
    Log("alt3d: far objects get their full model from farther away (ring %d)", g_cfg.alt3dScale - 1);
    return true;
}

}  // namespace ufx
