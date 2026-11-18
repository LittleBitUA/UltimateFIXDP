// Stability fixes from docs/crashes (00_REPORT.md, 02, 03, 11):
//
//   NanTrapGuard  The angle normalisation FUN_00409E90 hangs the game for good when an angle is not a number (`jmp $` after
//                 __isnan, 323 calls). It now returns 0.0 and the log names the caller, so the bad value is found instead of
//                 a silent freeze. A number goes through unchanged.
//   PinAudio      XAudio2_7.dll and xactengine3_7.dll are pinned in memory once they are loaded. The game creates its XACT
//                 engine twice and closes COM after every video once more than it opened it; Windows may then unload XAudio2
//                 under a sound thread that still runs (the "XAudio2_7.dll unloaded" crash). A pinned library is never
//                 unloaded. Done from the first frames, not from DllMain (the loader lock is held there).
//   PinFpu        The x87 precision of the game's main thread is kept at 24 bits, as a plain Direct3D 9 device sets it (the game
//                 creates its device without FPU_PRESERVE). Code of the port compares float32 values with x87 registers for
//                 equality (the aim limit), which holds only at 24 bits; another Direct3D (DXVK) or a compatibility mode can
//                 leave 53. The mod's clock does its own 53-bit arithmetic, so it is not affected.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {
namespace {

volatile long g_nanCount = 0;
bool g_audioPinned = false;
int g_pinTries = 0;
bool g_fpuSaid = false;

u16 ControlWord() {
    u16 cw = 0;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    return cw;
}

void SetControlWord(u16 cw) { __asm__ __volatile__("fldcw %0" : : "m"(cw)); }

bool Pin(const wchar_t* name) {
    HMODULE h = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, name, &h) && h;
}

}  // namespace

}  // namespace ufx

// Called from the NaN trap guard (stubs.h, BuildNanTrapGuard) with the return address of the normalisation's caller.
extern "C" void __cdecl NanTrapWorker(ufx::u32 caller) {
    using namespace ufx;
    const long n = InterlockedIncrement(&g_nanCount);
    if (n <= 20 || n % 1000 == 0)
        Log("stability: an angle was not a number (NaN) in the call from %08X: returned 0 instead of hanging the game (%ld so far)", caller, n);
}

namespace ufx {

bool InstallStability() {
    bool any = false;
    if (g_cfg.nanTrapGuard) {
        const SiteDef& s = kSiteNanTrap;
        if (!BytesMatch(s.va, s.bytes, s.len)) {
            Log("stability: the NaN trap at %08X differs from DP.exe 1.01b (already patched by another mod?), not guarded", s.va);
        } else if (u8* stub = AllocStub(32)) {
            BuildNanTrapGuard(stub, (u32)(uintptr_t)stub, (u32)(uintptr_t)NanTrapWorker, kNanTrapNumber);
            if (PatchJump(s.va, s.bytes, s.len, stub)) {
                Log("stability: NaN trap guarded (an angle that is not a number gives 0 and a log line instead of a freeze)");
                any = true;
            }
        }
    }
    if (g_cfg.pinAudio || g_cfg.pinFpu) any = true;  // done per frame (StabilityFrame)
    return any;
}

void StabilityFrame() {
    if (g_cfg.pinAudio && !g_audioPinned && g_pinTries < 600) {  // the sound engine exists by the first frame; ten seconds of tries at most
        ++g_pinTries;
        const bool x2 = Pin(L"XAudio2_7.dll"), xact = Pin(L"xactengine3_7.dll");
        if (x2 || g_pinTries == 600) {
            g_audioPinned = true;
            Log("stability: sound libraries pinned in memory: XAudio2_7.dll %s, xactengine3_7.dll %s", x2 ? "yes" : "not loaded",
                xact ? "yes" : "not loaded");
        }
    }
    if (g_cfg.pinFpu) {
        const u16 cw = ControlWord();
        if (cw & 0x0300) {  // precision control other than 24 bits
            if (!g_fpuSaid) {
                g_fpuSaid = true;
                Log("stability: the main thread's x87 precision was %s (control word %04X): set to 24 bits as Direct3D 9 does",
                    (cw & 0x0300) == 0x0300 ? "64 bits" : "53 bits", cw);
            }
            SetControlWord((u16)(cw & ~0x0300));
        }
    }
}

}  // namespace ufx
