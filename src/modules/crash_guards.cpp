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
    return ok1 || ok2;
}

}  // namespace ufx
