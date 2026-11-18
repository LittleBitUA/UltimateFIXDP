// Frame delta of exactly zero must not become NaN/INF.
//
// FUN_0058C6F0 computes a movement speed as  distance / frameDelta  and stores it in [obj+0x4E4].
// With frameDelta == 0 the result is NaN or INF; later movement code multiplies it into angles and
// FUN_00409E90 answers a NaN angle with an endless `for(;;)`. We replace the three instructions with
// a stub that divides by 1.0 (that is, keeps the distance) when the delta is +/-0.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {

bool InstallZeroDelta() {
    const SiteDef& s = kSiteZeroDelta;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("zero-delta: bytes at %08X differ from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    u8* stub = AllocStub(64);
    if (!stub) {
        Log("zero-delta: no memory for the stub, skipped");
        return false;
    }
    BuildZeroDeltaStub(stub, (u32)(uintptr_t)stub, kFrameDeltaVa, kZeroDeltaReturn);
    if (!PatchJump(s.va, s.bytes, s.len, stub)) {
        Log("zero-delta: could not patch %08X", s.va);
        return false;
    }
    Log("zero-delta: guard installed at %08X (stub %08X)", s.va, (u32)(uintptr_t)stub);
    return true;
}

}  // namespace ufx
