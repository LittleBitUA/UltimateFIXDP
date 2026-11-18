#include "base.h"

namespace ufx {

bool BytesMatch(u32 va, const u8* expected, size_t len) {
    const u8* p = (const u8*)(uintptr_t)va;
    for (size_t i = 0; i < len; ++i)
        if (p[i] != expected[i]) return false;
    return true;
}

// Writes bytes into the image, handling page protection and the instruction cache.
bool WriteCode(u32 va, const u8* bytes, size_t len) {
    void* addr = (void*)(uintptr_t)va;
    DWORD old;
    if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    u8* dst = (u8*)addr;
    for (size_t i = 0; i < len; ++i) dst[i] = bytes[i];
    DWORD ignored;
    VirtualProtect(addr, len, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), addr, len);
    return true;
}

bool SwapImport(void** slot, void* replacement, void** previousOut) {
    if (!slot) return false;
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    if (previousOut) *previousOut = *slot;
    *slot = replacement;
    DWORD ignored;
    VirtualProtect(slot, sizeof(void*), old, &ignored);
    return true;
}

// One executable page is enough for every trampoline we emit.
static u8* g_arena = nullptr;
static size_t g_arenaUsed = 0;
static const size_t kArenaSize = 4096;

u8* AllocStub(size_t size) {
    if (!g_arena) {
        g_arena = (u8*)VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_arena) return nullptr;
    }
    size = (size + 15) & ~(size_t)15;
    if (g_arenaUsed + size > kArenaSize) return nullptr;
    u8* p = g_arena + g_arenaUsed;
    g_arenaUsed += size;
    return p;
}

void PutRel32(u8* at, u32 nextInstrVa, u32 targetVa) {
    u32 rel = targetVa - nextInstrVa;
    at[0] = (u8)rel;
    at[1] = (u8)(rel >> 8);
    at[2] = (u8)(rel >> 16);
    at[3] = (u8)(rel >> 24);
}

// Replaces `len` bytes at `va` with `jmp target` padded with NOPs, after checking they match.
bool PatchJump(u32 va, const u8* expected, size_t len, const void* target) {
    if (len < 5 || len > 32) return false;
    if (!BytesMatch(va, expected, len)) return false;
    u8 code[32];
    code[0] = 0xE9;
    PutRel32(code + 1, va + 5, (u32)(uintptr_t)target);
    for (size_t i = 5; i < len; ++i) code[i] = 0x90;
    return WriteCode(va, code, len);
}

// Replaces `len` bytes at `va` with `call target` padded with NOPs, after checking they match.
bool PatchCall(u32 va, const u8* expected, size_t len, const void* target) {
    if (len < 5 || len > 32) return false;
    if (!BytesMatch(va, expected, len)) return false;
    u8 code[32];
    code[0] = 0xE8;
    PutRel32(code + 1, va + 5, (u32)(uintptr_t)target);
    for (size_t i = 5; i < len; ++i) code[i] = 0x90;
    return WriteCode(va, code, len);
}

// The end of the user address space, rounded down to 64 KB: 7FFE0000h for the stock exe, FFFE0000h under WOW64 once
// ufx_laa.exe has set the large address flag, BFFE0000h on a 32-bit Windows started with /3GB. Read once from the system.
u32 UserAddressEnd() {
    static u32 end = 0;
    if (!end) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        end = (u32)(uintptr_t)si.lpMaximumApplicationAddress & ~0xFFFFu;
        if (end < 0x10000u) end = 0x7FFE0000u;
    }
    return end;
}

}  // namespace ufx
