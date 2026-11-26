// Guards for the NPC navigation table crash (NPC after a cutscene is sent to a node that does not exist).
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {

// The head of a weed list is used only while it is still a weed object in mapped memory; a stale one is dropped from the list.
static bool WeedAlive(const void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    if ((uintptr_t)p < 0x10000 || !VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (mbi.State != MEM_COMMIT || !(mbi.Protect & readable) || (mbi.Protect & PAGE_GUARD)) return false;
    if ((uintptr_t)mbi.BaseAddress + mbi.RegionSize < (uintptr_t)p + 4) return false;
    return *(const u32*)p == kWeedVtable;
}

extern "C" void __stdcall WeedHeadCall(void** slot) {
    void* head = *slot;
    if (WeedAlive(head)) {
        typedef void (__attribute__((thiscall)) *Fn)(void*);
        ((Fn)(*(u32**)head)[0x30 / 4])(head);
        return;
    }
    static int logged = 0;
    if (logged < 10) {
        ++logged;
        Log("crash guards: weed pool list head %08X is not a live object any more, dropped (the game would have crashed here)", (u32)(uintptr_t)head);
    }
    *slot = nullptr;
}

extern "C" __attribute__((naked)) void WeedStopStub() {
    asm volatile(".intel_syntax noprefix\n" "push esi\n" "call _WeedHeadCall@4\n" "ret\n" ".att_syntax\n");
}
extern "C" __attribute__((naked)) void WeedDtorStub() {
    asm volatile(".intel_syntax noprefix\n" "push edi\n" "call _WeedHeadCall@4\n" "ret\n" ".att_syntax\n");
}

static bool InstallWeedGuard() {
    const bool a = PatchCall(kSiteWeedStop.va, kSiteWeedStop.bytes, kSiteWeedStop.len, (const void*)WeedStopStub);
    const bool b = PatchCall(kSiteWeedDtor.va, kSiteWeedDtor.bytes, kSiteWeedDtor.len, (const void*)WeedDtorStub);
    Log("crash guards: weed pool lists checked before use (%d of 2 places)", (int)a + (int)b);
    return a || b;
}

bool InstallCrashGuards() {
    const bool weed = InstallWeedGuard();
    const SiteDef& lookup = kSiteNavLookup;
    const SiteDef& nul = kSiteNavNullCheck;
    if (!BytesMatch(lookup.va, lookup.bytes, lookup.len) || !BytesMatch(nul.va, nul.bytes, nul.len)) {
        Log("crash guards: bytes differ from DP.exe 1.01b, skipped");
        return weed;
    }
    u8* a = AllocStub(128);
    u8* b = AllocStub(32);
    if (!a || !b) return weed;
    BuildNavLookupGuard(a, (u32)(uintptr_t)a, kNavLookupContinue);
    BuildNavNullGuard(b, (u32)(uintptr_t)b, kNavNullContinue);
    bool ok1 = PatchJump(lookup.va, lookup.bytes, lookup.len, a);
    bool ok2 = PatchJump(nul.va, nul.bytes, nul.len, b);
    Log("crash guards: navigation lookup %s, NULL check %s", ok1 ? "guarded" : "FAILED", ok2 ? "guarded" : "FAILED");
    {  // the lookup without a result: a node number outside the table gives zeros (issue #11)
        const SiteDef& pos = kSiteNavPosLookup;
        u8* c = AllocStub(128);
        bool ok3 = false;
        if (c && BytesMatch(pos.va, pos.bytes, pos.len)) {
            BuildNavPosGuard(c, (u32)(uintptr_t)c, kNavPosContinue, kNavPosReturn);
            ok3 = PatchJump(pos.va, pos.bytes, pos.len, c);
        }
        Log("crash guards: navigation position lookup %s", ok3 ? "guarded" : "FAILED");
    }
    // a failed route lookup leaves the resident where it stood instead of moving it to (0, 0, 0)
    int kept = 0;
    const SiteDef* calls[2] = {&kSiteRouteCall1, &kSiteRouteCall2};
    for (int i = 0; i < 2; ++i) {
        u8* s = AllocStub(96);
        if (!s || !BytesMatch(calls[i]->va, calls[i]->bytes, calls[i]->len)) continue;
        BuildRouteKeepStub(s, (u32)(uintptr_t)s, kRouteLookupFn, i == 0);
        if (PatchCall(calls[i]->va, calls[i]->bytes, calls[i]->len, s)) ++kept;
    }
    Log("crash guards: %d of 2 route lookups keep the resident in place when the route is missing", kept);
    return ok1 || ok2 || kept || weed;
}

}  // namespace ufx
