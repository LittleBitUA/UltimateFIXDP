// The residents' timetable lookup (src/schedule.h) against tables built here in the game's record format. The expected answers
// are what the game's own functions give for the same data, as read in the disassembly of DP.exe 1.01b: FUN_004DB440 (block),
// FUN_004DB480 (block in use), FUN_004C0010 (block number), FUN_004C0930 / FUN_004C75A0 (table), FUN_004DBB20 and FUN_00470450
// (entry for a time), FUN_004DB640 (area of a place). Block 3 is Emily's day from TB_0102F.PRM.
#include <stdio.h>
#include <string.h>
#include <vector>
#include "schedule.h"
using namespace ufx;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); ++g_fail; } } while (0)

static std::vector<signed char> g_buf;

static bool InBuffer(const void* p, unsigned size) {
    const signed char* b = (const signed char*)p;
    return !g_buf.empty() && b >= g_buf.data() && b + size <= g_buf.data() + g_buf.size();
}

static void Record(int kind, int hour, int minute, const char* place, const char* action) {
    signed char r[kSchedRecord];
    memset(r, 0, sizeof(r));
    r[0] = (signed char)kind;
    r[2] = (signed char)hour;
    r[3] = (signed char)minute;
    strncpy((char*)r + kSchedPlace, place, kSchedText - 1);
    strncpy((char*)r + kSchedAction, action, kSchedText - 1);
    g_buf.insert(g_buf.end(), r, r + kSchedRecord);
}
static void Entry(int hour, int minute, const char* place, const char* action) { Record(1, hour, minute, place, action); }
static void EndBlock() { Record(0, 0, 0, "", ""); }
static void EndTable() { Record(-1, 0, 0, "", ""); }

static int Sec(int h, int m, int s = 0) { return (h * 60 + m) * 60 + s; }

// Index of the entry for a time within a block, -1 = no answer; *nextIndex -1 = no next entry.
static int At(const signed char* block, int now, int* nextIndex) {
    const signed char *cur = nullptr, *next = nullptr;
    *nextIndex = -2;
    if (!SchedEntryAt(block, now, &cur, &next)) return -1;
    *nextIndex = next ? (int)((next - block) / kSchedRecord) : -1;
    return (int)((cur - block) / kSchedRecord);
}

static void BuildGood() {
    g_buf.clear();
    // block 0
    Entry(0, 0, "03_00entrance", "win.close");
    Entry(6, 0, "03_00entrance", "idling");
    Entry(12, 0, "03_00entrance", "dummy");
    Entry(18, 0, "03_00entrance", "dummy");
    EndBlock();
    // block 1: the resident is not on this table
    Entry(0, 0, "none", "");
    EndBlock();
    // block 2: empty
    EndBlock();
    // block 3: the day begins at 07:30 and its last entry starts after midnight
    Entry(7, 30, "23_01living_05bedroom", "mijitaku");
    Entry(8, 0, "23_01living_02mirror1", "next.table");
    Entry(9, 0, "06_02office_01emily.chair", "deskwork.gf1");
    Entry(17, 0, "06_02office_01emily.chair", "next.table");
    Entry(18, 0, "14_00inside_12chair.e.pos", "drinking");
    Entry(19, 25, "23_02bathroom", "kigaeru.fudangi");
    Entry(19, 35, "23_01living_03boxing", "training");
    Entry(22, 0, "23_02bathroom", "idling");
    Entry(22, 30, "23_01living_06tv.mae", "tvwatching");
    Entry(1, 30, "23_01living_05bedroom", "idling");
    EndBlock();
    // block 4: one entry without a time
    Entry(-1, -1, "28_00property_02gi.pos", "miseban");
    EndBlock();
    EndTable();
}

int main() {
    SchedTable t;
    BuildGood();
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 5, "blocks %d", t.blocks);
    const signed char* base = g_buf.data();
    CHECK(t.block[0] == base && t.block[1] == base + 5 * kSchedRecord && t.block[2] == base + 7 * kSchedRecord &&
          t.block[3] == base + 8 * kSchedRecord && t.block[4] == base + 19 * kSchedRecord, "block starts");
    CHECK(SchedBlock(t, 0) == t.block[0], "block 0 in use");
    CHECK(SchedBlock(t, 1) == t.block[1], "a \"none\" block counts as a block, as in the game");
    CHECK(SchedBlock(t, 2) == nullptr, "an empty block is no block");
    CHECK(SchedBlock(t, 5) == nullptr && SchedBlock(t, -1) == nullptr && SchedBlock(t, 127) == nullptr, "block numbers outside the table");

    int next;
    CHECK(At(t.block[0], Sec(0, 0), &next) == 0 && next == 1, "00:00");
    CHECK(At(t.block[0], Sec(5, 59, 59), &next) == 0 && next == 1, "05:59:59");
    CHECK(At(t.block[0], Sec(6, 0), &next) == 1 && next == 2, "06:00");
    CHECK(At(t.block[0], Sec(17, 59, 59), &next) == 2 && next == 3, "17:59:59");
    CHECK(At(t.block[0], Sec(23, 59, 59), &next) == 3 && next == 0, "the last entry runs to the first one's start");
    CHECK(At(t.block[1], Sec(13, 0), &next) == 0 && next == -1, "a block of one entry");
    CHECK(At(t.block[2], Sec(13, 0), &next) == -1, "an empty block has no entry");
    CHECK(At(nullptr, Sec(13, 0), &next) == -1, "no block");
    CHECK(At(t.block[3], Sec(7, 30), &next) == 0 && next == 1, "07:30");
    CHECK(At(t.block[3], Sec(19, 30), &next) == 5 && next == 6, "19:30");
    CHECK(At(t.block[3], Sec(23, 0), &next) == 8 && next == 9, "23:00");
    CHECK(At(t.block[3], Sec(0, 30), &next) == 8 && next == 9, "00:30 is still the 22:30 entry");
    CHECK(At(t.block[3], Sec(1, 30), &next) == 9 && next == 0, "01:30");
    CHECK(At(t.block[3], Sec(7, 29, 59), &next) == 9 && next == 0, "07:29:59 is the entry from 01:30");
    CHECK(At(t.block[4], Sec(3, 0), &next) == 0 && next == -1 && t.block[4][2] == -1, "an entry without a time");

    // FUN_00470450
    CHECK(SchedInStretch(5, 5, 10) && SchedInStretch(9, 5, 10) && !SchedInStretch(10, 5, 10) && !SchedInStretch(4, 5, 10), "a plain stretch");
    CHECK(SchedInStretch(0, 10, 5) && SchedInStretch(4, 10, 5) && !SchedInStretch(5, 10, 5) && !SchedInStretch(7, 10, 5) && SchedInStretch(10, 10, 5), "over midnight");
    CHECK(!SchedInStretch(5, 5, 5), "an empty stretch");

    // FUN_004C75A0 / FUN_004C0930
    const signed char one = 1;
    const signed char* none[kSchedSlots] = {};
    const signed char* all[kSchedSlots];
    for (int i = 0; i < kSchedSlots; ++i) all[i] = &one;
    const signed char* baseRain[kSchedSlots] = {&one, &one};
    CHECK(SchedChooseSlot(0, 0, false, all) == 2 && SchedChooseSlot(0, 0, false, none) == 0, "dry weather");
    CHECK(SchedChooseSlot(0, 0, true, all) == 3 && SchedChooseSlot(0, 0, true, baseRain) == 1 && SchedChooseSlot(0, 0, true, none) == 0, "rain");
    CHECK(SchedChooseSlot(0, 1, true, all) == 4 && SchedChooseSlot(0, 2, false, all) == 6 && SchedChooseSlot(0, 3, false, all) == 8 &&
          SchedChooseSlot(0, 4, false, all) == 10 && SchedChooseSlot(0, 5, false, all) == 0, "the variant of the day");
    CHECK(SchedChooseSlot(-1, -1, false, all) == 2, "a negative variant is none");
    CHECK(SchedChooseSlot(3, 0, false, all) == 2 && SchedChooseSlot(4, 0, false, all) == 4 && SchedChooseSlot(11, 2, true, all) == 11, "a forced table");

    // FUN_004C0010
    CHECK(SchedBlockNumber(5, 0, 3, 5, 1, 1, 1) == 3 && SchedBlockNumber(5, 4, 3, 10, 1, 1, 1) == 3, "only the chapter's tables switch blocks");
    CHECK(SchedBlockNumber(5, 2, 3, 5, 1, 0, 0) == 0x1C && SchedBlockNumber(5, 3, 3, 6, 2, 0, 0) == 0x1D && SchedBlockNumber(5, 3, 3, 12, 1, 0, 0) == 0x1C, "resident 5, chapters 5, 6, 12");
    CHECK(SchedBlockNumber(6, 2, 4, 5, 0, 2, 0) == 0x1F && SchedBlockNumber(6, 2, 4, 5, 1, 0, 0) == 4 && SchedBlockNumber(5, 2, 3, 5, 0, 1, 0) == 3, "resident 6, and a story byte of 0");
    CHECK(SchedBlockNumber(5, 2, 3, 7, 1, 0, 0) == 0x1C && SchedBlockNumber(5, 3, 3, 7, 1, 0, 0) == 3 && SchedBlockNumber(6, 2, 4, 11, 0, 1, 0) == 0x1E &&
          SchedBlockNumber(6, 3, 4, 8, 0, 1, 0) == 4, "chapters 7, 8, 11: the dry table only");
    CHECK(SchedBlockNumber(5, 2, 3, 9, 1, 1, 1) == 3 && SchedBlockNumber(9, 3, 7, 9, 1, 1, 1) == 7 && SchedBlockNumber(5, 2, 3, 4, 1, 1, 1) == 3 &&
          SchedBlockNumber(5, 2, 3, 13, 1, 1, 1) == 3, "chapters without a switch");
    CHECK(SchedBlockNumber(5, 2, 3, 10, 1, 0, 0) == 0x25 && SchedBlockNumber(5, 3, 3, 10, 1, 0, 1) == 0x25 && SchedBlockNumber(6, 2, 4, 10, 1, 1, 1) == 4, "chapter 10, resident 5");
    CHECK(SchedBlockNumber(9, 2, 7, 10, 0, 0, 1) == 0x1C && SchedBlockNumber(10, 3, 8, 10, 0, 0, 2) == 0x20 && SchedBlockNumber(0x22, 2, 27, 10, 0, 0, 1) == 0x22 &&
          SchedBlockNumber(9, 2, 7, 10, 0, 0, 0) == 7, "chapter 10, residents 9, 10, 22h");
    CHECK(SchedBlockNumber(1, 2, -1, 10, 1, 1, 1) == -1, "no block");

    // FUN_004DB640
    CHECK(SchedPlaceArea((const signed char*)"18_01k.l.bedroom_01k.chair") == 18 && SchedPlaceArea((const signed char*)"03_00entrance") == 3, "areas");
    CHECK(SchedPlaceArea((const signed char*)"none") == 0 && SchedPlaceArea((const signed char*)"") == -1 && SchedPlaceArea((const signed char*)"7") == -1 &&
          SchedPlaceArea((const signed char*)"6_00annastree_04jim.pos") == 6, "places that are not ZZ_...");

    // what is not a timetable gives no blocks at all
    BuildGood();
    g_buf[2 * kSchedRecord] = 2;
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 0, "a record kind above 1");
    BuildGood();
    g_buf[kSchedRecord + 2] = 24;
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 0, "an hour of 24");
    BuildGood();
    g_buf[kSchedRecord + 3] = -1;
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 0, "a minute of -1 with an hour");
    BuildGood();
    g_buf[kSchedRecord + kSchedPlace] = 7;
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 0, "text that is not plain");
    BuildGood();
    memset(&g_buf[kSchedRecord + kSchedAction], 'a', kSchedText);
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 0, "text without an end");
    BuildGood();
    g_buf.resize(g_buf.size() - kSchedRecord);  // the end of the table is missing: the walk leaves the readable memory
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 0, "no end of the table");
    SchedIndexTable(nullptr, InBuffer, t);
    CHECK(t.blocks == 0, "no table");
    g_buf.clear();
    for (int i = 0; i < kSchedMaxBlocks + 1; ++i) {
        Entry(0, 0, "none", "");
        EndBlock();
    }
    EndTable();
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 0, "more blocks than a timetable has");

    // a table that ends inside a block: the index is kept, the unfinished block gives no entry
    g_buf.clear();
    Entry(0, 0, "03_00entrance", "idling");
    EndBlock();
    Entry(8, 0, "03_00entrance", "idling");
    Entry(9, 0, "03_00entrance", "idling");
    EndTable();
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 2, "blocks %d", t.blocks);
    CHECK(At(t.block[0], Sec(1, 0), &next) == 0 && At(t.block[1], Sec(8, 30), &next) == -1, "the unfinished block");

    // TB_1920F.PRM has a block that is only an end record carrying the place "none": in use for the game, but it holds no entry
    g_buf.clear();
    Entry(0, 0, "none", "");
    EndBlock();
    Record(0, 0, 0, "none", "");
    EndBlock();
    EndTable();
    SchedIndexTable(g_buf.data(), InBuffer, t);
    CHECK(t.blocks == 3, "blocks %d", t.blocks);
    CHECK(SchedBlock(t, 1) == t.block[1] && At(t.block[1], Sec(12, 0), &next) == -1 && SchedBlock(t, 2) == nullptr, "an end record with a place");

    if (g_fail) {
        printf("%d schedule checks FAILED\n", g_fail);
        return 1;
    }
    printf("all schedule checks passed\n");
    return 0;
}
