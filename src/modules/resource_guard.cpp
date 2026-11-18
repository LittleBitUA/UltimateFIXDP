// Guards for the two readers of packed game resources (sites.h, "Resource read guard"): a record that does not unpack is
// read again a few times instead of forever, and unpacking cannot write past the end of its buffer.
//
// Why a read that works is not changed:
//  - The retry guard stands in for a jump the reader takes only after the unpack function returned 0. A record that
//    unpacks never reaches it.
//  - The room guard gives inflate() min(20000h, bytes left in the buffer) of output room where the game gives 20000h.
//    For a stream that unpacks to no more than the buffer (every record of the stock archives: the unpacked size in the
//    header is exact) that is never less than the stream still has to write, and what inflate() writes does not depend
//    on how much room beyond that it is offered. With no room left it still finishes such a stream: after the last
//    byte only the end-of-block code and the Adler-32 trailer remain, which need input, not output (zlib 1.2.1
//    inflate(): the LEN, TYPE and CHECK states; Z_BUF_ERROR only when a call consumed and produced nothing, 007323DDh).
//    So the buffer, the result and the file position are what the game alone would have produced. The host test
//    tests/test_resguard.cpp runs the game's loop both ways against zlib and compares.
//
// Both readers run on the game's loading thread only (FUN_006B4DE0 -> FUN_006B4F20 -> FUN_006B2780 -> FUN_006B33F0, the
// only function that calls them), so the state below needs no lock; the counters are read by the main thread for the
// report. tools/emu_resguard.py runs the game's readers and this file's compiled code in an emulator on the host.
#include "../base.h"
#include "../resguard.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {
namespace {

constexpr u32 kMaxAttempts = 5;    // reads of one record before it is reported missing
constexpr u32 kRetryPauseMs = 15;  // between two attempts: a read error that passes by itself gets a little time
constexpr u32 kMaxLogLines = 40;

RetryState g_archive, g_loose;
volatile LONG g_retries = 0, g_givenUp = 0, g_overflows = 0;
u32 g_logged = 0;
// What the room worker answered last: no room left in a buffer of g_roomSize bytes. A failure right after that means the
// stream had more to write than its header declares.
bool g_roomSpent = false;
u32 g_roomSize = 0;
// The first resource given up on that the player has not been told about yet (taken by the main thread).
volatile LONG g_failurePending = 0;
char g_failureName[64];

// Copies a printable ASCII text of the game (at most cap-1 characters; anything else ends it).
void CopyGameText(char* out, int cap, const char* text) {
    int n = 0;
    if (text)
        for (; n < cap - 1 && text[n] >= 0x20 && text[n] < 0x7F; ++n) out[n] = text[n];
    out[n] = 0;
}

const char* BaseName(const char* path) {
    const char* base = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    return base;
}

// One failed unpack of `what` (file name `name`). Non-zero = read it again (the game's own jump), 0 = report it missing.
int Failed(RetryState& state, u32 key, const char* what, const char* name, u32 packed, u32 unpacked) {
    const bool overflow = g_roomSpent;
    g_roomSpent = false;
    if (overflow) InterlockedIncrement(&g_overflows);
    char why[112];
    why[0] = 0;
    if (overflow) wsprintfA(why, "; its buffer of %u bytes was full and the stream had not ended", g_roomSize);
    const bool firstOfRun = state.key != key;
    const RetryStep step = NextRetryStep(state, key, kMaxAttempts);
    if (step == kReadAgain) {
        InterlockedIncrement(&g_retries);
        if (firstOfRun && g_logged < kMaxLogLines) {
            ++g_logged;
            Log("resource guard: %s did not unpack (packed %u bytes, %u unpacked by its header%s), reading it again", what, packed,
                unpacked, why);
        }
        Sleep(kRetryPauseMs);
        return 1;
    }
    InterlockedIncrement(&g_givenUp);
    if (step == kGiveUpKnown) return 0;  // said once already
    if (g_logged < kMaxLogLines) {
        ++g_logged;
        // The unpack function also fails when zlib gets no memory for its own state, and the reader cannot tell that from a
        // bad file. The free address space is logged so that the two can be told apart afterwards.
        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(mem);
        char memory[64];
        if (GlobalMemoryStatusEx(&mem)) wsprintfA(memory, "%u MB of its address space are free", (u32)(mem.ullAvailVirtual >> 20));
        else lstrcpyA(memory, "its free address space is not known");
        Log("resource guard: %s did not unpack in %u attempts%s. It is reported to the game as missing (the game alone would retry "
            "forever). The file is damaged or cannot be read (verify the game files, or reinstall the mod that replaced it), or the "
            "game ran out of memory: %s%s",
            what, kMaxAttempts, why, memory, g_logged == kMaxLogLines ? " (further messages are not logged)" : "");
    }
    if (!g_failurePending) {
        CopyGameText(g_failureName, sizeof(g_failureName), name);
        InterlockedExchange(&g_failurePending, 1);
    }
    return 0;
}

// After the failed unpack in FUN_006B0000. Its frame: [ebp+8] = id, [ebp-8] = the 20h-byte index record of that id
// (+0Ch archive number, +1Ch path in the exe image, FUN_006AFC90), [ebp-118h] = packed size, [ebp-114h] = unpacked size.
int __cdecl ArchiveRetryWorker(const u32* frame) {
    const u32 id = frame[2];
    const u32* entry = (const u32*)(uintptr_t)frame[-2];
    const u32 pathVa = entry[7];
    char path[64], what[128];
    // the path is a string of the exe image; anything else is not read
    const bool inImage = pathVa >= 0x400000 + 0x1000 && pathVa < 0x400000 + kExeSizeOfImage - sizeof(path);
    CopyGameText(path, sizeof(path), inImage ? (const char*)(uintptr_t)pathVa : nullptr);
    const char* name = path[0] ? BaseName(path) : "(no name)";
    wsprintfA(what, "resource %04Xh of DPSERIAL.%03u (%s)", id, entry[3] + 1, name);
    const u8* base = (const u8*)frame;
    return Failed(g_archive, id + 1, what, name, *(const u32*)(base + kArchiveUnpackedSizeOffset - 4),
                  *(const u32*)(base + kArchiveUnpackedSizeOffset));
}

// After the failed unpack in FUN_006AF370. Its frame: [ebp+8] = path (the game has just opened the file by it),
// [ebp-134h] = packed size, [ebp-130h] = unpacked size.
int __cdecl LooseRetryWorker(const u32* frame) {
    char path[120], what[160];
    CopyGameText(path, sizeof(path), (const char*)(uintptr_t)frame[2]);
    u32 key = 2166136261u;  // FNV-1a of the path
    for (const char* p = path; *p; ++p) key = (key ^ (u8)*p) * 16777619u;
    wsprintfA(what, "loose file %s", path[0] ? path : "(no name)");
    const u8* base = (const u8*)frame;
    return Failed(g_loose, key | 1, what, path[0] ? BaseName(path) : "(no name)", *(const u32*)(base + kLooseUnpackedSizeOffset - 4),
                  *(const u32*)(base + kLooseUnpackedSizeOffset));
}

// The output room for inflate() in FUN_006AF5B0 (frame: [ebp] = the caller's ebp, [ebp+4] = return address,
// [ebp-28h] = z_stream.total_out). `written` = what the stream has written into the buffer so far.
u32 RoomFor(const u32* frame, u32 written) {
    const u32 caller = frame[1];
    int sizeOffset;
    if (caller == kSiteArchiveInflateCall.va + kSiteArchiveInflateCall.len) sizeOffset = kArchiveUnpackedSizeOffset;
    else if (caller == kSiteLooseInflateCall.va + kSiteLooseInflateCall.len) sizeOffset = kLooseUnpackedSizeOffset;
    else return kInflateRoomStep;  // FUN_006AFE40 and FUN_006B0510, which nothing calls: as the game
    const u32 size = *(const u32*)((const u8*)(uintptr_t)frame[0] + sizeOffset);
    const u32 room = InflateRoom(size, written, kInflateRoomStep);
    g_roomSpent = room == 0;
    g_roomSize = size;
    return room;
}

u32 __cdecl InflateRoomFirstWorker(const u32* frame) { return RoomFor(frame, 0); }  // before the loop: nothing is written yet
u32 __cdecl InflateRoomRefillWorker(const u32* frame) { return RoomFor(frame, *(const u32*)((const u8*)frame - 0x28)); }

bool InstallRetry(const SiteDef& jump, const SiteDef& missing, u32 retryVa, int(__cdecl* worker)(const u32*), const char* reader) {
    if (!BytesMatch(jump.va, jump.bytes, jump.len) || !BytesMatch(missing.va, missing.bytes, missing.len)) {
        Log("resource guard: the %s reader differs from DP.exe 1.01b, its retry limit is off", reader);
        return false;
    }
    u8* stub = AllocStub(32);
    if (!stub) {
        Log("resource guard: no room left for the stub of the %s reader, its retry limit is off", reader);
        return false;
    }
    BuildRetryGuard(stub, (u32)(uintptr_t)stub, (u32)(uintptr_t)worker, retryVa, missing.va);
    if (!PatchJump(jump.va, jump.bytes, jump.len, stub)) {
        Log("resource guard: the jump at %08X of the %s reader could not be rewritten, its retry limit is off", jump.va, reader);
        return false;
    }
    return true;
}

bool InstallRoom() {
    const SiteDef* const need[6] = {&kSiteInflateRoomFirst, &kSiteInflateRoomRefill, &kSiteArchiveInflateCall,
                                    &kSiteLooseInflateCall, &kSiteArchiveBufferSize, &kSiteLooseBufferSize};
    for (const SiteDef* s : need)
        if (!BytesMatch(s->va, s->bytes, s->len)) {
            Log("resource guard: the unpack code differs from DP.exe 1.01b at %08X, the buffer limit is off", s->va);
            return false;
        }
    u8* first = AllocStub(32);
    u8* refill = AllocStub(32);
    if (!first || !refill) {
        Log("resource guard: no room left for the stubs of the unpack code, the buffer limit is off");
        return false;
    }
    BuildInflateRoomStub(first, (u32)(uintptr_t)first, (u32)(uintptr_t)InflateRoomFirstWorker, kInflateRoomFirstReturn);
    BuildInflateRoomStub(refill, (u32)(uintptr_t)refill, (u32)(uintptr_t)InflateRoomRefillWorker, kInflateRoomRefillReturn);
    // The refill first: with only the first one in place a small buffer would still be handed 20000h more once it is full.
    // Either of the two alone only ever gives less room than the game does, so a half-done install is harmless.
    if (!PatchJump(kSiteInflateRoomRefill.va, kSiteInflateRoomRefill.bytes, kSiteInflateRoomRefill.len, refill) ||
        !PatchJump(kSiteInflateRoomFirst.va, kSiteInflateRoomFirst.bytes, kSiteInflateRoomFirst.len, first)) {
        Log("resource guard: the unpack code could not be rewritten, the buffer limit is off or only partly on");
        return false;
    }
    return true;
}

}  // namespace

bool ResourceGuardTakeFailure(char* name, int cap) {
    if (!g_failurePending) return false;
    lstrcpynA(name, g_failureName, cap);
    InterlockedExchange(&g_failurePending, 0);
    return true;
}

void ResourceGuardCounts(u32* retries, u32* givenUp, u32* overflows) {
    *retries = (u32)g_retries;
    *givenUp = (u32)g_givenUp;
    *overflows = (u32)g_overflows;
}

bool InstallResourceGuard() {
    const bool archive = InstallRetry(kSiteArchiveRetry, kSiteArchiveMissing, kArchiveRetryTarget, ArchiveRetryWorker, "archive");
    const bool loose = InstallRetry(kSiteLooseRetry, kSiteLooseMissing, kLooseRetryTarget, LooseRetryWorker, "loose file");
    const bool room = InstallRoom();
    Log("resource guard: at most %u reads of a record that does not unpack: archives %s, loose files %s; unpack buffer limit %s",
        kMaxAttempts, archive ? "on" : "OFF", loose ? "on" : "OFF", room ? "on" : "OFF");
    return archive || loose || room;
}

}  // namespace ufx
