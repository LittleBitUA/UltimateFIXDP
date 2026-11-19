// Guards for the NPC navigation table crash (NPC after a cutscene is sent to a node that does not exist).
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {

bool InstallCrashGuards() {
    const SiteDef& lookup = kSiteNavLookup;
    const SiteDef& nul = kSiteNavNullCheck;
    if (!BytesMatch(lookup.va, lookup.bytes, lookup.len) || !BytesMatch(nul.va, nul.bytes, nul.len)) {
        Log("crash guards: bytes differ from DP.exe 1.01b, skipped");
        return false;
    }
    u8* a = AllocStub(128);
    u8* b = AllocStub(32);
    if (!a || !b) return false;
    BuildNavLookupGuard(a, (u32)(uintptr_t)a, kNavLookupContinue);
    BuildNavNullGuard(b, (u32)(uintptr_t)b, kNavNullContinue);
    bool ok1 = PatchJump(lookup.va, lookup.bytes, lookup.len, a);
    bool ok2 = PatchJump(nul.va, nul.bytes, nul.len, b);
    Log("crash guards: navigation lookup %s, NULL check %s", ok1 ? "guarded" : "FAILED", ok2 ? "guarded" : "FAILED");
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
    return ok1 || ok2 || kept;
}

}  // namespace ufx
