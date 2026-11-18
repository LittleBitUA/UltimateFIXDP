// Residents of the town: named marks on the pause map, and the data of the schedule viewer in the F10 menu.
//
// Read-only. Nothing in the game's memory is written and none of its functions is called: its getters create their singletons on
// first use (0040A320, 00418460, 00449480, 004B76D0), its registry walk calls a virtual function of every object and its table
// lookup reads the clock through those getters, so none of them is provably free of side effects from here. The same lookups are
// done again over the same data instead. Every pointer is checked with VirtualQuery before it is followed (title screen, loading,
// an unloaded stage), and a timetable is used only when all of it looks like one.
//
// The facts, from docs/engine/10_npc_time_weather.md, each read again in the disassembly of DP.exe 1.01b (kNpcFacts in sites.h
// holds the bytes that prove them; "make test" compares those with the real DP.exe and InstallNpcMap with the running one):
//   CNpcMain = [0138A760h], 29B44h bytes (004B76D9): +4 bit 800h "timetables in effect" (004B1ADE), +114h[128] the live NPC objects
//     (004AC042), +D1Ch[12] the loaded timetables (004AB560), +D4Ch NPC_LIST.PRM: 50 records of A8h bytes, index = resident id
//     (004AB570), +00h char[16] name, +10h char[16] second name, +33h s8 number of the resident's block in a timetable (004C004C).
//   An NPC object: [+0] vtable, whose function at +A4h returns the kind (a resident: `mov al,1 / ret` at 004D4830), +1Ch flags and
//     +38h byte (the pause map leaves the object out with bit 200h set or a byte that is not 0: 0065B852, 0065B76E), +58h/+5Ch/+60h
//     position (0065B845), +622h s8 schedule mode, +D34h / +D38h current and next timetable entry, +D54h s8 resident id.
//   The timetables and how the game picks a table, a block and an entry: ../schedule.h (tested on the host).
#include "../base.h"
#include "../schedule.h"
#include "../sites.h"

extern "C" {
}

namespace ufx {
namespace {

constexpr u32 kNpcMainPtrVa = 0x0138A760, kNpcMainSize = 0x29B44;
constexpr u32 kTimerPtrVa = 0x00BE1EA0;    // CTimer: +0Ch hour, +10h minute, +14h second (00470300)
constexpr u32 kWeatherPtrVa = 0x00BDA0F4;  // CWeather: +1270h is not 0 while it rains (00470E70)
constexpr u32 kTextStart = 0x00401000, kRdataStart = 0x0076E000, kRdataEnd = 0x00885000;  // .text and .rdata of DP.exe 1.01b
constexpr u32 kRegistryOff = 0x114, kTablesOff = 0xD1C, kListOff = 0xD4C;
constexpr int kRegistrySize = 128, kResidentCount = 50;
constexpr u32 kListRecord = 0xA8;
constexpr u32 kNpcSpan = 0xD58;  // an NPC object up to the resident id
// CGame (pointer at 00BDA004h)
constexpr u32 kAreaOff = 0x834FE0;     // the area the player is in, 0 = the town
constexpr u32 kModeOff = 0x834FFC;     // the game mode: 7 = the field task FUN_005D26B0 (set by FUN_00452780), a game world is played
constexpr u32 kChapterOff = 0x8C609;   // s8 chapter (0044F7D0), then s8 timetable variant of the day at +8C60Ah (0044F7F0)
constexpr u32 kSavedOff = 0x99078, kSavedSize = 0x78;  // per resident: +3 s8 where a waiting resident is, +6 s8 forced timetable
constexpr u32 kStory5Off = 0xCED66;    // two story bytes (residents 5 and 6) and one at +8CA11h (9, 10, 22h): another block
constexpr u32 kStory9Off = 0x8CA11;

bool g_factsOk = false;

// The last stretch VirtualQuery reported as readable: most reads of a pass land in a few blocks. Each pass starts with an empty one
// (memory may be freed between frames), and the map pass and the menu keep their own (they need not be on one thread).
struct Probe {
    u32 base = 0, end = 0;
};

bool Readable(Probe& probe, u32 p, u32 size) {
    const u32 end = p + size;
    if (p < 0x10000 || size == 0 || end < p || end > UserAddressEnd()) return false;
    while (p < end) {
        if (p >= probe.base && p < probe.end) {
            p = probe.end;
            continue;
        }
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((const void*)(uintptr_t)p, &mbi, sizeof(mbi))) return false;
        const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State != MEM_COMMIT || !(mbi.Protect & readable) || (mbi.Protect & PAGE_GUARD)) return false;
        const u32 base = (u32)(uintptr_t)mbi.BaseAddress, top = base + (u32)mbi.RegionSize;
        if (top <= p) return false;
        probe.base = base;
        probe.end = top;
        p = top;
    }
    return true;
}

template <typename T>
T Rd(u32 address) { return *(volatile T*)(uintptr_t)address; }

int S8(u32 address) { return Rd<signed char>(address); }

// The resident's name as NPC_LIST has it, first letter in upper case: the second name where there is one ("carol", "ushah",
// "kaysen", "lilly": the names the player knows), otherwise the first ("carroll", "usher", "forest", "lily"). Only letters, digits
// and spaces get through: the text goes to the game's print function as its format string.
void ResidentName(u32 list, int id, char* out, int capacity) {
    int n = 0;
    if (list) {
        u32 text = list + (u32)id * kListRecord + 0x10;
        if (Rd<char>(text) == 0) text -= 0x10;
        for (int i = 0; i < 15 && n < capacity - 1; ++i) {
            char c = Rd<char>(text + (u32)i);
            if (c == 0) break;
            if (c == '_') c = ' ';
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (!letter && !(c >= '0' && c <= '9') && c != ' ') continue;
            if (n == 0 && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[n++] = c;
        }
    }
    out[n] = 0;
    if (n == 0) wsprintfA(out, "No %d", id);
}

// The schedule viewer's state (the Present hook only): the index of the twelve tables and the probe its table walk reads through.
SchedTable g_tables[kSchedSlots];
Probe g_tableProbe;

bool TableReadable(const void* p, unsigned size) { return Readable(g_tableProbe, (u32)(uintptr_t)p, size); }

void CopyText(const signed char* from, char* to) {  // 32 bytes, checked by SchedIndexTable
    int n = 0;
    for (; n < kSchedText - 1 && from[n]; ++n) to[n] = (char)from[n];
    to[n] = 0;
}

u32 Pointer(u32 va) {
    const u32 p = Rd<u32>(va);
    return p >= 0x10000 && p < UserAddressEnd() ? p : 0;
}

}  // namespace

int ReadResidentSchedules(ResidentRow* rows, int max, int* hour, int* minute, int* rainy) {
    if (!g_factsOk) return kScheduleOff;
    Probe& probe = g_tableProbe;
    probe = Probe();
    const u32 game = Pointer(kGameManagerPtrVa), main = Pointer(kNpcMainPtrVa);
    const u32 timer = Pointer(kTimerPtrVa), weather = Pointer(kWeatherPtrVa);
    if (!game || !main || !timer || !weather) return kScheduleNoGame;
    if (!Readable(probe, main, kNpcMainSize) || !Readable(probe, timer, 0x18) || !Readable(probe, weather + 0x1270, 1)) return kScheduleNoGame;
    if (!Readable(probe, game + kModeOff, 4) || !Readable(probe, game + kChapterOff, 2) || !Readable(probe, game + kStory9Off, 1) ||
        !Readable(probe, game + kStory5Off, 2) || !Readable(probe, game + kSavedOff, kResidentCount * kSavedSize))
        return kScheduleNoGame;
    // Outside a game world the table pointers are leftovers of the last one (only the next refresh, FUN_004B19F0, rewrites them).
    // The game sets mode 7 when a game starts (00642640) and another one (0, 2, 9) when an event, the pause menu or the field
    // task itself leaves the field.
    if (Rd<int>(game + kModeOff) != 7) return kScheduleNoGame;
    const int h = Rd<int>(timer + 0xC), m = Rd<int>(timer + 0x10), s = Rd<int>(timer + 0x14);
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return kScheduleNoGame;
    const u32 list = Rd<u32>(main + kListOff);
    if (!Readable(probe, list, kResidentCount * kListRecord)) return kScheduleNoGame;
    if (!(Rd<u32>(main + 4) & 0x800u)) return kScheduleNotInUse;
    for (int slot = 0; slot < kSchedSlots; ++slot)
        SchedIndexTable((const signed char*)(uintptr_t)Rd<u32>(main + kTablesOff + (u32)slot * 4), TableReadable, g_tables[slot]);
    const int chapter = S8(game + kChapterOff), variant = S8(game + kChapterOff + 1);
    const int story5 = S8(game + kStory5Off), story6 = S8(game + kStory5Off + 1), story9 = S8(game + kStory9Off);
    const bool rain = Rd<u8>(weather + 0x1270) != 0;
    const int now = (h * 60 + m) * 60 + s;
    *hour = h;
    *minute = m;
    *rainy = rain ? 1 : 0;
    int count = 0;
    for (int id = 1; id < kResidentCount && count < max; ++id) {
        const int listBlock = S8(list + (u32)id * kListRecord + 0x33);
        const signed char* blocks[kSchedSlots];
        bool any = false;
        for (int slot = 0; slot < kSchedSlots; ++slot) {
            blocks[slot] = SchedBlock(g_tables[slot], SchedBlockNumber(id, slot, listBlock, chapter, story5, story6, story9));
            any = any || blocks[slot];
        }
        if (!any) continue;  // no block in any loaded timetable: the game places this resident without one
        ResidentRow& row = rows[count++];
        ResidentName(list, id, row.name, (int)sizeof(row.name));
        row.nowHour = row.nowMinute = row.nextHour = row.nextMinute = -1;
        lstrcpyA(row.nowPlace, "none");
        row.nowAction[0] = row.nextPlace[0] = row.nextAction[0] = 0;
        const int slot = SchedChooseSlot(S8(game + kSavedOff + (u32)id * kSavedSize + 6), variant, rain, blocks);
        const signed char *current = nullptr, *next = nullptr;
        if (slot < 0 || slot >= kSchedSlots || !SchedEntryAt(blocks[slot], now, &current, &next)) continue;
        row.nowHour = current[2];
        row.nowMinute = current[3];
        CopyText(current + kSchedPlace, row.nowPlace);
        CopyText(current + kSchedAction, row.nowAction);
        if (next) {
            row.nextHour = next[2];
            row.nextMinute = next[3];
            CopyText(next + kSchedPlace, row.nextPlace);
            CopyText(next + kSchedAction, row.nextAction);
        }
    }
    return count;
}

bool InstallNpcMap() {
    for (const SiteDef& f : kNpcFacts) {
        if (BytesMatch(f.va, f.bytes, f.len)) continue;
        Log("npc map: \"%s\" at %08X differs from DP.exe 1.01b: the schedule viewer stays off", f.name, f.va);
        return false;
    }
    g_factsOk = true;
    Log("npc map: ready, read-only (%d facts checked, nothing patched): the schedule viewer on the F10 page", kNpcFactCount);
    return true;
}

}  // namespace ufx
