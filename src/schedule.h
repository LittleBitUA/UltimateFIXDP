// The residents' timetables of the game, read the way the game reads them, without any Windows dependency so the host can test it
// (tests/test_schedule.cpp). Used by modules/npc_map.cpp over the game's own memory; nothing here writes.
//
// A timetable is a run of records of 54h bytes (TB_*.PRM, loaded into CNpcMain+D1Ch[12]):
//   +0 s8: 1 an entry, 0 the end of a resident's block, below 0 the end of the table
//   +2 s8 hour, +3 s8 minute of the entry's start (both -1 in a block of one entry that lasts all day)
//   +4 char[32] place "ZZ_RRroom_NNnode" (ZZ = the area, the number in CGame+834FE0h) or "none"
//   +24h char[32] action ("idling", "sleep", "next.table"...)
// Every function names the game function whose behaviour it repeats (DP.exe 1.01b; kNpcFacts in sites.h holds the bytes).
#pragma once

namespace ufx {

constexpr int kSchedRecord = 0x54, kSchedPlace = 4, kSchedAction = 0x24, kSchedText = 32;
constexpr int kSchedSlots = 12;
// The game's tables have up to 398 records, 39 blocks and 22 entries in a block: anything far beyond that is not a timetable.
constexpr int kSchedMaxRecords = 2048, kSchedMaxBlocks = 64, kSchedMaxEntries = 64;

// Where every block of one loaded timetable begins.
struct SchedTable {
    const signed char* block[kSchedMaxBlocks];
    int blocks;  // 0 = not loaded, or not a timetable
};

// Tells whether [p, p + size) may be read (the module answers with VirtualQuery, the test with the bounds of its buffer).
typedef bool (*SchedReadable)(const void* p, unsigned size);

inline bool SchedPlainText(const signed char* text) {  // a string of at most 31 printable characters
    for (int i = 0; i < kSchedText; ++i) {
        if (text[i] == 0) return true;
        if (text[i] < 0x20 || text[i] > 0x7E) return false;
    }
    return false;
}

// Walks a table once, the way FUN_004DB440 finds a block: block 0 begins at the first record, the next one after every
// end-of-block record, until the end-of-table record. Unlike the game it gives up (no blocks at all) on anything that does not
// look like a timetable: memory that cannot be read, a record kind above 1, a time out of range, text that is not plain.
inline void SchedIndexTable(const signed char* record, SchedReadable readable, SchedTable& t) {
    t.blocks = 0;
    if (!record) return;
    bool begins = true;
    for (int i = 0; i < kSchedMaxRecords; ++i, record += kSchedRecord) {
        if (!readable(record, kSchedRecord)) break;
        const int kind = record[0];
        if (kind < 0) return;  // the end of the table: the index is complete
        if (kind > 1) break;
        if (kind == 1) {
            const int hour = record[2], minute = record[3];
            const bool allDay = hour == -1 && minute == -1;
            if (!allDay && (hour < 0 || hour > 23 || minute < 0 || minute > 59)) break;
            if (!SchedPlainText(record + kSchedPlace) || !SchedPlainText(record + kSchedAction)) break;
        }
        if (begins) {
            if (t.blocks >= kSchedMaxBlocks) break;
            t.block[t.blocks++] = record;
            begins = false;
        }
        if (kind == 0) begins = true;
    }
    t.blocks = 0;
}

// The resident's block in a table, null = none (what FUN_004C0010 stores at npc+D64h+slot*0Ch+4): the block must exist and its
// first record must have a place (FUN_004DB480 takes the length of that string; "none" is a place for this test, as in the game).
inline const signed char* SchedBlock(const SchedTable& t, int number) {
    if (number < 0 || number >= t.blocks) return nullptr;
    return t.block[number][kSchedPlace] != 0 ? t.block[number] : nullptr;
}

// The number of a resident's block in the timetable of a slot (FUN_004C0010): listBlock (NPC_LIST+33h), except that in the
// chapter's two tables (slots 2 and 3) some chapters send residents 5, 6, 9, 10 and 22h to story blocks (the jump table at
// 004C0214, indexed by the chapter minus 5; story5 = CGame+CED66h, story6 = +CED67h, story9 = +8CA11h). The game does the sums in
// a signed byte, and a negative number means "no block".
inline int SchedBlockNumber(int id, int slot, int listBlock, int chapter, int story5, int story6, int story9) {
    signed char block = (signed char)listBlock;
    if (slot != 2 && slot != 3) return block;
    if (chapter == 5 || chapter == 6 || chapter == 12 || ((chapter == 7 || chapter == 8 || chapter == 11) && slot == 2)) {
        if (id == 5) {
            if (story5) block = (signed char)(story5 + 0x1B);
        } else if (id == 6 && story6) {
            block = (signed char)(story6 + 0x1D);
        }
    } else if (chapter == 10) {
        if (id == 5 && story5) block = (signed char)(story5 + 0x24);
        if (story9) {
            if (id == 9) block = (signed char)(story9 + 0x1B);
            else if (id == 10) block = (signed char)(story9 + 0x1E);
            else if (id == 0x22) block = (signed char)(story9 + 0x21);
        }
    }
    return block;
}

// Which timetable the game uses for a resident now (FUN_004C75A0, FUN_004C0930): a forced one when the saved byte
// (CGame+9907Eh+id*78h) is 4 or more; otherwise the variant of the day (CGame+8C60Ah: 1..4 = slots 4, 6, 8, 10, more = 0);
// otherwise by the weather: in the rain the chapter's rain table (3) if the resident has a block in it, else the base rain table
// (1), else the base table (0); in dry weather the chapter's table (2), else the base table. blocks = the resident's block per slot.
inline int SchedChooseSlot(int forced, int variant, bool rain, const signed char* const* blocks) {
    if (forced >= 4) return forced;
    if (variant > 0) return variant <= 4 ? 2 + variant * 2 : 0;
    if (!rain) return blocks[2] ? 2 : 0;
    return blocks[3] ? 3 : (blocks[1] ? 1 : 0);
}

inline int SchedStartSecond(const signed char* entry) { return (entry[2] * 60 + entry[3]) * 60; }

// FUN_00470450: is `now` in [from, to), where the stretch may run over midnight; an empty stretch holds nothing.
inline bool SchedInStretch(int now, int from, int to) {
    if (from < to) return from <= now && now < to;
    if (from > to) return now < to || now >= from;
    return false;
}

// The entry of a block for a time of day (seconds since midnight), and the one after it (FUN_004DBB20): a block of one entry is
// that entry; otherwise the first entry whose stretch up to the next one's start holds the time (the last entry runs on to the
// first), or the last entry when none does. *next is null for a block of one entry. The block must come from SchedIndexTable.
// One difference from the game: a block that begins with its own end record has no entry here. TB_1920F.PRM has such a block (its
// end record carries the place "none", so the block counts as in use), and the game's lookup then returns the record in front of
// the block, the end record of the block before, whose place is empty: no place either way.
inline bool SchedEntryAt(const signed char* block, int now, const signed char** current, const signed char** next) {
    if (!block) return false;
    int count = 0;
    for (const signed char* p = block; *p != 0; p += kSchedRecord, ++count)
        if (*p < 0 || count >= kSchedMaxEntries) return false;  // the table ends inside the block
    if (count == 0) return false;
    *current = block + (count - 1) * kSchedRecord;
    *next = count > 1 ? block : nullptr;
    for (int i = 0; i + 1 < count; ++i) {
        const signed char* entry = block + i * kSchedRecord;
        if (SchedInStretch(now, SchedStartSecond(entry), SchedStartSecond(entry + kSchedRecord))) {
            *current = entry;
            *next = entry + kSchedRecord;
            break;
        }
    }
    return true;
}

// The area of a place, as FUN_004DB640 reads it: atoi of the first two characters, -1 when the place is shorter than two
// characters ("none" gives 0, as in the game).
inline int SchedPlaceArea(const signed char* place) {
    const char a = (char)place[0], b = a ? (char)place[1] : 0;
    if (!a || !b) return -1;
    if (a < '0' || a > '9') return 0;
    return (b >= '0' && b <= '9') ? (a - '0') * 10 + (b - '0') : a - '0';
}

}  // namespace ufx
