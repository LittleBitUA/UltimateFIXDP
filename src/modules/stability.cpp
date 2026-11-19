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
volatile bool g_memLow = false;  // the address space is nearly used up (MemoryLine)

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

bool MemoryIsLow() { return g_memLow; }

}  // namespace ufx

// ---- the game's worker threads ----------------------------------------------------------------------------------------
// SerialSetThread: at 00401C9D the main thread wakes SetThread and then waits for it at once, so world objects are never
// built while the next frame updates the world. The game's own wait at 00401C7D then finds nothing to wait for.
extern "C" __attribute__((naked)) void SetThreadSerialStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov eax, 0x0040CC90\n"
        "call eax\n"
        "mov eax, 0x0040CC60\n"
        "jmp eax\n"
        ".att_syntax\n");
}

// RouteFlushWait: the queue flush of a level change first lets RouteThread finish the request it is working on, so it never
// writes into a resident that is being cleared, and no new request is lost behind the flag. At most 250 ms.
extern "C" void __cdecl RouteFlushWorker() {
    using namespace ufx;
    HANDLE mutex = *(volatile HANDLE*)(uintptr_t)kRouteMutexVa;
    u32 pending = 0;
    if (mutex && WaitForSingleObject(mutex, 1000) == WAIT_OBJECT_0) {
        pending = (*(volatile u32*)(uintptr_t)kRouteQueueEnd - *(volatile u32*)(uintptr_t)kRouteQueueBegin) >> 3;
        ReleaseMutex(mutex);
    }
    *(volatile u32*)(uintptr_t)kRouteFlushFlag = 1;  // what the game's function does
    if (!pending) return;                            // the thread works on nothing: it removes a request only after it
    ((void(__cdecl*)())(uintptr_t)kRouteWakeFn)();
    const DWORD start = GetTickCount();
    while (*(volatile u32*)(uintptr_t)kRouteFlushFlag) {
        if (GetTickCount() - start > 250) {
            Log("stability: the route thread did not finish its request within 250 ms of a level change (the queue is cleared anyway)");
            return;
        }
        Sleep(1);
    }
    static int said = 0;
    if (++said <= 5) Log("stability: a level change waited for the route thread (%u requests queued, %u ms)", pending, GetTickCount() - start);
}

extern "C" __attribute__((naked)) void RouteFlushStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "pushfd\n"
        "pushad\n"
        "call _RouteFlushWorker\n"
        "popad\n"
        "popfd\n"
        "ret\n"
        ".att_syntax\n");
}

namespace ufx {

bool InstallStability() {
    bool any = false;
    if (g_cfg.fasterLoading) {
        const SiteDef& s = kSiteLoaderSleep;
        static const u8 one = 0x01;
        if (BytesMatch(s.va, s.bytes, s.len) && WriteCode(s.va + 5, &one, 1)) {
            Log("stability: the loader thread waits 1 ms instead of 16 between requests");
            any = true;
        } else {
            Log("stability: the loader wait at %08X differs from DP.exe 1.01b, left as it is", s.va);
        }
    }
    if (g_cfg.serialSetThread) {
        const SiteDef& s = kSiteSetThreadWake;
        if (PatchCall(s.va, s.bytes, s.len, (const void*)SetThreadSerialStub)) {
            Log("stability: the main thread waits for SetThread right after waking it (no world objects built during the update)");
            any = true;
        } else {
            Log("stability: the SetThread wake at %08X differs from DP.exe 1.01b, left as it is", s.va);
        }
    }
    if (g_cfg.routeFlushWait && BytesMatch(kSiteRouteFlushFn.va, kSiteRouteFlushFn.bytes, kSiteRouteFlushFn.len)) {
        const bool a = PatchCall(kSiteRouteFlush1.va, kSiteRouteFlush1.bytes, kSiteRouteFlush1.len, (const void*)RouteFlushStub);
        const bool b = PatchCall(kSiteRouteFlush2.va, kSiteRouteFlush2.bytes, kSiteRouteFlush2.len, (const void*)RouteFlushStub);
        Log("stability: a level change waits for the route thread (%d of 2 places)", (int)a + (int)b);
        any = any || a || b;
    }
    // the stick-waggle QTE counts each swap as at 60 FPS (above 60 the frame delta made every swap count less)
    if (g_cfg.qteFix) {
        const SiteDef& s = kSiteQteGauge;
        u8* stub = BytesMatch(s.va, s.bytes, s.len) ? AllocStub(32) : nullptr;
        if (stub) {
            BuildQteGaugeStub(stub, 0x14AFFE0);
            if (PatchCall(s.va, s.bytes, s.len, stub)) {
                Log("stability: the stick-waggle QTE gauge no longer shrinks above 60 FPS");
                any = true;
            }
        } else {
            Log("stability: the waggle QTE site at %08X differs from DP.exe 1.01b, left as it is", s.va);
        }
    }
    // The window is not swapped for a white "not responding" ghost while a long load keeps the game thread busy.
    typedef void(WINAPI * GhostFn)();
    if (GhostFn ghost = (GhostFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "DisableProcessWindowsGhosting")) ghost();
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

// Once a minute: free address space and its largest block. A 32-bit game runs out of address space long before RAM; the
// line shows whether a crash late in a session was memory (docs/crashes/05).
void MemoryLine() {
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (last && now - last < 60000) return;
    last = now ? now : 1;
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return;
    SIZE_T largest = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (uintptr_t a = 0x10000; a < (uintptr_t)UserAddressEnd() && VirtualQuery((const void*)a, &mbi, sizeof(mbi)) == sizeof(mbi);) {
        if (mbi.State == MEM_FREE && mbi.RegionSize > largest) largest = mbi.RegionSize;
        const uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= a) break;
        a = next;
    }
    Log("memory: %u MB of %u MB address space free, largest block %u MB", (unsigned)(ms.ullAvailVirtual >> 20),
        (unsigned)(ms.ullTotalVirtual >> 20), (unsigned)(largest >> 20));
    // below these the next big allocation (a texture, a level) can fail: the game then crashes or loops without a word
    const bool low = (ms.ullAvailVirtual >> 20) < 300 || (largest >> 20) < 64;
    if (low && !g_memLow) Log("memory: running low; the player is asked to save and restart");
    g_memLow = low;
}

void StabilityFrame() {
    MemoryLine();
    LargeAddressFrame();
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
