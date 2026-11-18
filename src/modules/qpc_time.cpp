// QueryPerformanceCounter relative to the start of the game.
//
// DP.exe converts the counter to seconds with x87 arithmetic that Direct3D leaves in 24-bit
// precision (CreateDevice is called without D3DCREATE_FPU_PRESERVE). The raw counter counts from
// Windows boot, so after days of uptime a 24-bit mantissa cannot resolve a frame any more: the game
// stutters until the PC is rebooted. Every consumer in DP.exe uses the counter only as a difference
// (or as entropy for the stack cookie), so handing out a small, process-relative value is safe.
#include "../base.h"
#include "../sites.h"

namespace ufx {

static BOOL(WINAPI* g_realQpc)(LARGE_INTEGER*) = nullptr;
static LONGLONG g_base = 0;
static LONGLONG g_offset = 0;

static BOOL WINAPI HookQpc(LARGE_INTEGER* out) __attribute__((noinline));
static BOOL WINAPI HookQpc(LARGE_INTEGER* out) {
    BOOL ok = g_realQpc(out);
    if (!ok) return ok;
    // The C runtime of DP.exe seeds its stack cookie from the counter: give it the raw value.
    u32 caller = (u32)(uintptr_t)__builtin_return_address(0);
    if (caller >= kCookieFnBegin && caller < kCookieFnEnd) return ok;
    out->QuadPart = out->QuadPart - g_base + g_offset;
    return ok;
}

bool InstallQpcTime() {
    HMODULE exe = GetModuleHandleW(nullptr);
    void** slot = FindImportSlot(exe, "KERNEL32.dll", "QueryPerformanceCounter");
    if (!slot) {
        Log("qpc: import slot not found, skipped");
        return false;
    }
    LARGE_INTEGER freq, now;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&now)) {
        Log("qpc: no performance counter, skipped");
        return false;
    }
    g_base = now.QuadPart;
    g_offset = freq.QuadPart;  // start at one second so no consumer ever sees zero
    g_realQpc = (BOOL(WINAPI*)(LARGE_INTEGER*))*slot;
    if (!SwapImport(slot, (void*)HookQpc, nullptr)) {
        Log("qpc: could not patch the import table, skipped");
        return false;
    }
    Log("qpc: relative counter installed (slot %08X, frequency %d Hz)", (u32)(uintptr_t)slot, (int)freq.QuadPart);
    return true;
}

}  // namespace ufx
