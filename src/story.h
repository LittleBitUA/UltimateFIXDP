// The story state of DP.exe 1.01b, decoded from the game's own memory for the read-only Story page of the F10 menu. Nothing here
// writes, and nothing of the game is called. No Windows headers: tests/verify_sites.cpp runs the decoder on the host.
//
// Where things live (read in the disassembly; the byte runs that fix each offset are kStorySites in sites.h):
//   CGame (pointer at 00BDA004h)
//     +8C608h  episode, +8C609h chapter (a signed byte; FUN_004526D0 stores both, the episode comes from the chapter through
//              FUN_0044F830), +8C60Ah sub-step (FUN_0044F7E0)
//     +8C60Bh  bit 7 = "the game was finished" (FUN_0043D270; the low 7 bits are the difficulty)
//     +CD368h  the time windows of the town's destinations, 4 bytes each: hour, minute, hour, minute (FUN_00452270)
//   CEvCore (pointer at 00BD9FE8h)
//     +3Ch     pointer to the story flags, one bit each: flag n is bit n & 7 of byte n >> 3 (FUN_0070FE90); +40h their size in bytes
//     +12ACh   the PROJECT.DPB image (the number of flag groups at +18h); +12B4h its group table: 20h bytes per group, the first
//              flag of the group at +0 and the number of its flags at +4 (FUN_0070DFE0 adds the local number to the first flag)
//   The game's code names the groups by number:
//     28h  side quests: flag i = quest i taken, flag 50 + i = quest i done, i < 50 (FUN_0043D270)
//     44h  flag 3Ch = the game is finished (the same function sets the bit of CGame+8C60Bh from it)
//     48h  the town's destinations 0..7Fh. FUN_00452270 (what the minimap's time line shows) takes the first raised one whose
//          byte in the table at 008BF560h is 1 (FUN_005E6E10)
#pragma once
#include <stdint.h>

namespace ufx {

constexpr uint32_t kStoryEpisodeOff = 0x8C608, kStoryChapterOff = 0x8C609, kStoryStepOff = 0x8C60A, kStoryFinishedOff = 0x8C60B;
constexpr uint32_t kStoryWindowOff = 0xCD368;
constexpr int kStorySideQuests = 50, kStoryDestinations = 0x80;
constexpr uint32_t kStoryGameSpan = kStoryWindowOff + kStoryDestinations * 4;  // the part of CGame the decoder reads lies below this
constexpr uint32_t kStoryDestTableVa = 0x008BF560;
constexpr uint32_t kStoryGroupSide = 0x28, kStoryGroupCommon = 0x44, kStoryGroupDestination = 0x48, kStoryFlagFinished = 0x3C;
constexpr uint32_t kStoryGroupSize = 0x20;

// What the decoder reads. The caller makes sure every pointer is readable for the length given.
struct StoryView {
    const uint8_t* game;         // CGame, kStoryGameSpan bytes
    const uint8_t* flags;        // the flag bits
    uint32_t flagBytes;
    const uint8_t* groups;       // the group table, groupCount * 20h bytes
    uint32_t groupCount;
    const uint8_t* destAllowed;  // the table at 008BF560h, kStoryDestinations bytes
};

struct StoryState {
    int episode, chapter, step;
    bool finished;
    bool sideKnown;                        // the side quest group is in the table
    int sideTaken, sideDone, sideOpenCount;
    uint8_t sideOpen[kStorySideQuests];    // the numbers (from 0, as the game counts them) of the quests taken and not done
    bool destKnown;                        // the destination group is in the table
    int destIndex;                         // the destination FUN_00452270 would return, -1 = none
    int destActive;                        // how many destinations count as active
    uint8_t destWindow[4];                 // hour, minute, hour, minute; all 0 = any time
    int flagsRaised, flagsTotal;
};

inline uint32_t StoryLe32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The number of flag `local` of `group`, or -1 when the table has no such group or flag (the game does not check, its data fits).
inline int StoryFlagId(const StoryView& v, uint32_t group, uint32_t local) {
    if (group >= v.groupCount) return -1;
    const uint8_t* g = v.groups + group * kStoryGroupSize;
    uint32_t first = StoryLe32(g), count = StoryLe32(g + 4);
    if (local >= count || first > v.flagBytes * 8 || local >= v.flagBytes * 8 - first) return -1;
    return (int)(first + local);
}

inline bool StoryFlag(const StoryView& v, uint32_t group, uint32_t local) {
    int id = StoryFlagId(v, group, local);
    return id >= 0 && ((v.flags[id >> 3] >> (id & 7)) & 1) != 0;
}

inline void StoryDecode(const StoryView& v, StoryState* s) {
    s->episode = v.game[kStoryEpisodeOff];
    s->chapter = (int8_t)v.game[kStoryChapterOff];
    s->step = v.game[kStoryStepOff];
    // the flag is in the saved flag array; the bit of the difficulty byte is cleared by the stock game when a slot is loaded (004549C2)
    s->finished = StoryFlag(v, kStoryGroupCommon, kStoryFlagFinished) || (v.game[kStoryFinishedOff] & 0x80) != 0;

    s->sideKnown = StoryFlagId(v, kStoryGroupSide, 2 * kStorySideQuests - 1) >= 0;
    s->sideTaken = s->sideDone = s->sideOpenCount = 0;
    for (int i = 0; i < kStorySideQuests; ++i) {
        bool taken = StoryFlag(v, kStoryGroupSide, (uint32_t)i), done = StoryFlag(v, kStoryGroupSide, (uint32_t)(kStorySideQuests + i));
        if (taken) ++s->sideTaken;
        if (done) ++s->sideDone;
        if (taken && !done) s->sideOpen[s->sideOpenCount++] = (uint8_t)i;
    }

    s->destKnown = StoryFlagId(v, kStoryGroupDestination, kStoryDestinations - 1) >= 0;
    s->destIndex = -1;
    s->destActive = 0;
    for (int k = 0; k < 4; ++k) s->destWindow[k] = 0;
    for (int i = 0; i < kStoryDestinations; ++i) {
        if (!StoryFlag(v, kStoryGroupDestination, (uint32_t)i) || v.destAllowed[i] != 1) continue;
        ++s->destActive;
        if (s->destIndex >= 0) continue;
        s->destIndex = i;
        for (int k = 0; k < 4; ++k) s->destWindow[k] = v.game[kStoryWindowOff + (uint32_t)i * 4 + (uint32_t)k];
    }

    s->flagsRaised = 0;
    for (uint32_t i = 0; i < v.flagBytes; ++i)
        for (uint8_t b = v.flags[i]; b; b &= (uint8_t)(b - 1)) ++s->flagsRaised;
    s->flagsTotal = 0;
    for (uint32_t g = 0; g < v.groupCount; ++g) {
        uint32_t count = StoryLe32(v.groups + g * kStoryGroupSize + 4);
        if (count <= v.flagBytes * 8) s->flagsTotal += (int)count;
    }
}

}  // namespace ufx
