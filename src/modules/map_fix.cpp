// The full map. Ideas and addresses come from MapFix by Behelit09 (MIT licence); the code is our own.
//
// What the pause-menu map does in the game: it draws a 3x3 window of the 64 overworld tiles, never zooms out beyond
// 1.0, keeps the view locked to the tiles around the cursor, and rotates with the player. We
//   - draw all 64 tiles and let the zoom go down to a configurable minimum,
//   - keep the tiles (resource ids 937h..976h) from being released and load a missing one on demand,
//   - fix the orientation (north or south up) with a counter-rotated player arrow,
//   - keep the pan inside Greenvale.
// Everything is checked byte by byte first; nothing is changed if any site differs.
#include "../base.h"
#include "../sites.h"

extern "C" {
volatile int g_mapOrient = 1;
volatile int g_mapFixInstalled = 0;  // 1 once the patches are in: only then does g_mapOrient say how the map is drawn
extern const float g_mapPi = 3.14159265f;
extern const float g_mapLimit = 20000.0f;
volatile float g_mapMinZoom = 0.2f;
volatile float g_mapDefaultZoom = 0.2f;
}

namespace ufx {
namespace {

typedef int(__cdecl* LoadResourceFn)(int id, int flags, int mode);
typedef void*(__fastcall* FindResourceFn)(void* mgr, void* edx, const char* name, int zero);

// 1 = queued for the loading thread now, -1 = asked for before (loaded, or still on its way): FUN_0040B4C0 with mode 1 only
// queues a resource whose state byte (+2Fh of its record) is still 0, and for any other counts one more user.
int LoadTile(int index) { return ((LoadResourceFn)(uintptr_t)kLoadResourceFn)((int)kMapTileFirstId + index, 0x18, 1); }

// ---- Tile timing (log only) ---------------------------------------------------------------------------------------
// Why the pause map can be slow to fill in: the tiles are queued at the open and made by the game's loading thread, and the
// map draws each one only once FindResource finds it. From the open we watch the draw's lookups (main thread, every frame,
// all 64 tiles in order) until each tile has been found once, then write how long that took and what the texture loads
// cost meanwhile (texture_override.cpp; with GenerateMips every tile is a texture whose mipmaps D3DX makes on load).
constexpr int kMaxOpenLines = 60;     // opens logged per run
constexpr u32 kGiveUpUs = 60000000;   // a watch that has not seen all tiles after a minute says so and stops
bool g_watching = false;
u64 g_openTick = 0;
u64 g_lastSweep = 0;                  // when the draw last looked up tile 63: the map was on screen until then
u64 g_seen = 0;                       // bit n: tile n was found since the open
int g_firstFrame = -1;                // tiles found in the first frame's sweep, -1 = that sweep has not ended yet
int g_askedLater = 0;                 // tiles the draw had to queue again (not even asked for since the open)
int g_opens = 0;
u32 g_loadsAtOpen[kLoadKinds], g_usAtOpen[kLoadKinds];
bool g_haveLoads = false;             // the texture hook is in, the numbers above mean something

int CountTiles(u64 bits) {
    int n = 0;
    for (; bits; bits &= bits - 1) ++n;
    return n;
}

// `how` begins the line ("all 64 tiles drawable", "closed with ..."), then the time from the open to `end` and the loads.
void EndWatch(const char* how, u64 end) {
    g_watching = false;
    const u32 us = TickMicros(g_openTick, end);
    char loads[256] = "the texture hook is off, no load times";
    u32 n[kLoadKinds], t[kLoadKinds];
    if (g_haveLoads && TextureLoadTotals(n, t)) {
        for (int k = 0; k < kLoadKinds; ++k) {
            n[k] -= g_loadsAtOpen[k];
            t[k] -= g_usAtOpen[k];
        }
        wsprintfA(loads, "textures made meanwhile: %u with mipmaps made %u.%u ms, %u as given %u.%u ms, %u from packs %u.%u ms", n[0],
                  MsWhole(t[0]), MsTenth(t[0]), n[1], MsWhole(t[1]), MsTenth(t[1]), n[2], MsWhole(t[2]), MsTenth(t[2]));
    }
    Log("map: %s %u.%u ms after the open (%d of 64 in the first frame, %d queued anew by the draw); %s", how, MsWhole(us),
        MsTenth(us), g_firstFrame < 0 ? CountTiles(g_seen) : g_firstFrame, g_askedLater, loads);
}

void StartWatch(u64 opened, int queued, u32 askUs) {
    if (g_watching) {  // the map was closed before every tile came: the time is up to its last frame
        char how[48];
        wsprintfA(how, "closed with %d of 64 tiles drawable", CountTiles(g_seen));
        EndWatch(how, g_lastSweep ? g_lastSweep : g_openTick);
    }
    if (g_opens >= kMaxOpenLines) return;
    ++g_opens;
    g_watching = true;
    g_openTick = opened;
    g_seen = 0;
    g_lastSweep = 0;
    g_firstFrame = -1;
    g_askedLater = 0;
    g_haveLoads = TextureLoadTotals(g_loadsAtOpen, g_usAtOpen);
    Log("map: opened, %d of the 64 tiles queued for loading now, %d asked for before (asked in %u.%u ms)%s", queued, 64 - queued,
        MsWhole(askUs), MsTenth(askUs), g_opens == kMaxOpenLines ? "; the last open timed this run" : "");
}

// The draw looked tile `index` up: `found` = it is there. Main thread.
void WatchTile(int index, bool found) {
    if (found) g_seen |= (u64)1 << index;
    if (g_seen == ~(u64)0) {
        if (g_firstFrame < 0) g_firstFrame = 64;
        EndWatch("all 64 tiles drawable", TickNow());
        return;
    }
    if (index != 63) return;  // the clock is read once per frame, at the end of the sweep
    if (g_firstFrame < 0) g_firstFrame = CountTiles(g_seen);
    g_lastSweep = TickNow();
    if (TickMicros(g_openTick, g_lastSweep) >= kGiveUpUs) {
        char how[64];
        wsprintfA(how, "still only %d of 64 tiles drawable", CountTiles(g_seen));
        EndWatch(how, g_lastSweep);
    }
}

// "SMAP00_00_0" + up to three digits: the tile index, -1 when the name is not a tile's or the index is out of 0..63.
int TileIndex(const char* name) {
    if (!name) return -1;
    const char* p = "SMAP00_00_0";
    int i = 0;
    while (p[i] && name[i] == p[i]) ++i;
    if (p[i] != 0) return -1;
    int n = 0, digits = 0;
    for (const char* d = name + i; *d >= '0' && *d <= '9' && digits < 3; ++d, ++digits) n = n * 10 + (*d - '0');
    return digits > 0 && n >= 0 && n < 64 ? n : -1;
}

}  // namespace
}  // namespace ufx

extern "C" {

// Keeps the zoom (float at map + 8EE0h) inside [minimum, 3.5].
void __cdecl MapZoomClampWorker(void* map) {
    if (!map) return;
    volatile float* zoom = (volatile float*)((char*)map + 0x8EE0);
    float lo = g_mapMinZoom;
    if (*zoom < lo) *zoom = lo;
    else if (*zoom > 3.5f) *zoom = 3.5f;
}

// Opens the map at the chosen zoom and asks for all 64 tiles.
void __cdecl MapZoomInitWorker(void* map) {
    if (!map) return;
    *(volatile float*)((char*)map + 0x8EE0) = g_mapDefaultZoom;
    const ufx::u64 opened = ufx::TickNow();
    int queued = 0;
    for (int i = 0; i < 64; ++i)
        if (ufx::LoadTile(i) == 1) ++queued;
    ufx::StartWatch(opened, queued, ufx::TickMicros(opened, ufx::TickNow()));
}

// FindResourceByName for the map tiles: a tile that is not there yet is requested and looked up again.
void* __cdecl MapFindTileWorker(void* mgr, const char* name) {
    void* res = ((ufx::FindResourceFn)(uintptr_t)ufx::kFindResourceFn)(mgr, nullptr, name, 0);
    if (res && !ufx::g_watching) return res;  // the common frame: no name to read
    const int n = ufx::TileIndex(name);
    if (!res && n >= 0) {
        if (ufx::LoadTile(n) == 1) ++ufx::g_askedLater;  // it was not even queued: released since the open?
        res = ((ufx::FindResourceFn)(uintptr_t)ufx::kFindResourceFn)(mgr, nullptr, name, 0);
    }
    if (ufx::g_watching && n >= 0) ufx::WatchTile(n, res != nullptr);
    return res;
}

// ---- machine code glue (naked: no prologue, the caller's registers and stack stay as the game expects) ----------

// Replaces `call 00700620` after the zoom update: clamp first, then do what the call did.
__attribute__((naked)) void MapZoomClampStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "pushad\n"
        "push esi\n"
        "call _MapZoomClampWorker\n"
        "add esp, 4\n"
        "popad\n"
        "mov eax, 0x00700620\n"
        "jmp eax\n"
        ".att_syntax\n");
}

// Replaces `fstp dword [esi+8EE0h]` (the initial zoom): pop the value the game computed and use ours.
__attribute__((naked)) void MapZoomInitStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "fstp st(0)\n"
        "pushad\n"
        "push esi\n"
        "call _MapZoomInitWorker\n"
        "add esp, 4\n"
        "popad\n"
        "ret\n"
        ".att_syntax\n");
}

// Replaces `call FindResource(name, 0)` (thiscall, ret 8).
__attribute__((naked)) void MapFindStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "push dword ptr [esp + 4]\n"
        "push ecx\n"
        "call _MapFindTileWorker\n"
        "add esp, 8\n"
        "ret 8\n"
        ".att_syntax\n");
}

// Entry of the resource delete function (ret 4): tiles are never deleted.
__attribute__((naked)) void MapDeleteStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov eax, dword ptr [esp + 4]\n"
        "cmp eax, 0x937\n"
        "jl 1f\n"
        "cmp eax, 0x976\n"
        "jg 1f\n"
        "xor eax, eax\n"
        "ret 4\n"
        "1:\n"
        "push ebp\n"
        "mov ebp, esp\n"
        "sub esp, 0x0C\n"
        "mov eax, 0x006B3AA6\n"
        "jmp eax\n"
        ".att_syntax\n");
}

// Entry of the resource release function (thiscall, ret 0Ch): tiles are never released.
__attribute__((naked)) void MapReleaseStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov eax, dword ptr [esp + 4]\n"
        "cmp eax, 0x937\n"
        "jl 1f\n"
        "cmp eax, 0x976\n"
        "jg 1f\n"
        "ret 12\n"
        "1:\n"
        "push ebp\n"
        "mov ebp, esp\n"
        "sub esp, 0x08\n"
        "mov eax, 0x006B2836\n"
        "jmp eax\n"
        ".att_syntax\n");
}

// Pan limits: `fld dword [esp+3Ch / 38h]` / `fld st0` become a clamp to 20000 (inside Greenvale).
__attribute__((naked)) void MapPanXStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "fld dword ptr [esp + 0x40]\n"
        "fld dword ptr [_g_mapLimit]\n"
        "fcomip st(0), st(1)\n"
        "jb 1f\n"
        "jmp 3f\n"
        "1:\n"
        "fstp st(0)\n"
        "fld dword ptr [_g_mapLimit]\n"
        "3:\n"
        "fld st(0)\n"
        "ret\n"
        ".att_syntax\n");
}

__attribute__((naked)) void MapPanZStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "fld dword ptr [esp + 0x3C]\n"
        "fld dword ptr [_g_mapLimit]\n"
        "fcomip st(0), st(1)\n"
        "jb 1f\n"
        "jmp 3f\n"
        "1:\n"
        "fstp st(0)\n"
        "fld dword ptr [_g_mapLimit]\n"
        "3:\n"
        "fld st(0)\n"
        "ret\n"
        ".att_syntax\n");
}

// Replaces `call 006C5FD0` + `fld dword [eax+7Ch]` (the player's heading): st0 = pi / 0 when the orientation is fixed.
__attribute__((naked)) void MapHeadingStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov eax, dword ptr [_g_mapOrient]\n"
        "cmp eax, 1\n"
        "je 1f\n"
        "cmp eax, 2\n"
        "je 2f\n"
        "push dword ptr [esp + 4]\n"
        "mov eax, 0x006C5FD0\n"
        "call eax\n"
        "test eax, eax\n"
        "jz 1f\n"
        "fld dword ptr [eax + 0x7c]\n"
        "ret 4\n"
        "1:\n"
        "fld dword ptr [_g_mapPi]\n"
        "ret 4\n"
        "2:\n"
        "fldz\n"
        "ret 4\n"
        ".att_syntax\n");
}

// Replaces `fldz / mov ebx,eax / fstp dword [ebx+48h]` (the player arrow's angle) so that it turns against the map.
__attribute__((naked)) void MapArrowStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov ebx, eax\n"
        "mov eax, dword ptr [_g_mapOrient]\n"
        "cmp eax, 1\n"
        "je 1f\n"
        "cmp eax, 2\n"
        "je 2f\n"
        "fldz\n"
        "fstp dword ptr [ebx + 0x48]\n"
        "ret\n"
        "1:\n"
        "push ecx\n"
        "push edx\n"
        "push dword ptr [0x008A9BA4]\n"
        "mov eax, 0x006C5FD0\n"
        "call eax\n"
        "pop edx\n"
        "pop ecx\n"
        "test eax, eax\n"
        "jz 3f\n"
        "fld dword ptr [_g_mapPi]\n"
        "fsub dword ptr [eax + 0x7c]\n"
        "fstp dword ptr [ebx + 0x48]\n"
        "ret\n"
        "2:\n"
        "push ecx\n"
        "push edx\n"
        "push dword ptr [0x008A9BA4]\n"
        "mov eax, 0x006C5FD0\n"
        "call eax\n"
        "pop edx\n"
        "pop ecx\n"
        "test eax, eax\n"
        "jz 3f\n"
        "fld dword ptr [eax + 0x7c]\n"
        "fchs\n"
        "fstp dword ptr [ebx + 0x48]\n"
        "ret\n"
        "3:\n"
        "fldz\n"
        "fstp dword ptr [ebx + 0x48]\n"
        "ret\n"
        ".att_syntax\n");
}

}  // extern "C"

namespace ufx {

void ApplyMapFix() {
    g_mapOrient = g_cfg.mapOrientation;
    g_mapMinZoom = (float)g_cfg.mapMinZoom / 100.0f;
    g_mapDefaultZoom = (float)g_cfg.mapDefaultZoom / 100.0f;
}

bool InstallMapFix() {
    // 1. every site must be exactly what we expect
    const SiteDef* sites[] = {&kMapZoomFloor, &kMapZoomClampCall, &kMapZoomInit, &kMapPanX, &kMapPanZ, &kMapTileLookup,
                              &kMapTileLimit, &kMapDeleteResource, &kMapReleaseResource, &kMapFindResource,
                              &kMapHeading[0], &kMapHeading[1], &kMapHeading[2], &kMapHeading[3], &kMapHeading[4],
                              &kMapHeading[5], &kMapHeading[6], &kMapArrow};
    for (const SiteDef* s : sites)
        if (!BytesMatch(s->va, s->bytes, s->len)) {
            Log("map: bytes at %08X differ from DP.exe 1.01b, the map fix is skipped", s->va);
            return false;
        }
    if (!BytesMatch(kMapTileLookup.va + 16, kMapTileLookupTail, 4)) {
        Log("map: the tile lookup at %08X differs from DP.exe 1.01b, the map fix is skipped", kMapTileLookup.va);
        return false;
    }
    ApplyMapFix();

    // 2. apply; a failure in the middle of the list reverts what was written
    struct Undo {
        u32 va;
        u8 bytes[24];
        u8 len;
    } undo[32];
    int nUndo = 0;
    auto write = [&](u32 va, const u8* bytes, size_t len) {
        if (nUndo >= 32 || len > 24) return false;
        undo[nUndo].va = va;
        undo[nUndo].len = (u8)len;
        for (size_t i = 0; i < len; ++i) undo[nUndo].bytes[i] = ((const u8*)(uintptr_t)va)[i];
        ++nUndo;
        return WriteCode(va, bytes, len);
    };
    auto rel = [](u32 from, const void* target) { return (u32)((u32)(uintptr_t)target - (from + 5)); };
    bool ok = true;
    auto callTo = [&](const SiteDef& s, const void* target) {
        u8 code[8];
        for (int i = 0; i < 8; ++i) code[i] = 0x90;
        code[0] = 0xE8;
        u32 r = rel(s.va, target);
        code[1] = (u8)r; code[2] = (u8)(r >> 8); code[3] = (u8)(r >> 16); code[4] = (u8)(r >> 24);
        return write(s.va, code, s.len);
    };
    auto jumpTo = [&](const SiteDef& s, const void* target) {
        u8 code[8];
        for (int i = 0; i < 8; ++i) code[i] = 0x90;
        code[0] = 0xE9;
        u32 r = rel(s.va, target);
        code[1] = (u8)r; code[2] = (u8)(r >> 8); code[3] = (u8)(r >> 16); code[4] = (u8)(r >> 24);
        return write(s.va, code, s.len);
    };
    const u8 skipFloor[2] = {0xEB, 0x0D};
    ok = ok && write(kMapZoomFloor.va, skipFloor, 2);
    ok = ok && callTo(kMapZoomClampCall, (const void*)MapZoomClampStub);
    ok = ok && callTo(kMapZoomInit, (const void*)MapZoomInitStub);
    ok = ok && callTo(kMapPanX, (const void*)MapPanXStub);
    ok = ok && callTo(kMapPanZ, (const void*)MapPanZStub);
    u8 lookup[20];
    lookup[0] = 0x89; lookup[1] = 0xD3;  // mov ebx, edx  (the loop counter is the tile index)
    for (int i = 2; i < 20; ++i) lookup[i] = 0x90;
    ok = ok && write(kMapTileLookup.va, lookup, 20);
    const u8 limit[3] = {0x83, 0xF8, 0x40};  // cmp eax, 64
    ok = ok && write(kMapTileLimit.va, limit, 3);
    ok = ok && jumpTo(kMapDeleteResource, (const void*)MapDeleteStub);
    ok = ok && jumpTo(kMapReleaseResource, (const void*)MapReleaseStub);
    ok = ok && callTo(kMapFindResource, (const void*)MapFindStub);
    for (int i = 0; i < 7 && ok; ++i) ok = callTo(kMapHeading[i], (const void*)MapHeadingStub);
    ok = ok && callTo(kMapArrow, (const void*)MapArrowStub);

    if (!ok) {
        for (int i = nUndo - 1; i >= 0; --i) WriteCode(undo[i].va, undo[i].bytes, undo[i].len);
        Log("map: a patch could not be written, everything was reverted");
        return false;
    }
    g_mapFixInstalled = 1;
    Log("map: all 64 tiles, zoom down to %d%%, orientation %d, pan inside Greenvale; how long the tiles take is logged at each open",
        g_cfg.mapMinZoom, g_cfg.mapOrientation);
    return true;
}

}  // namespace ufx
