// Host-side check against a real DP.exe on disk: every site in kAllSites, the longer byte runs in kAllTails,
// the shadow literals and every import slot in kImports (sites.h). A site a module checks with bytes of its
// own that are not in sites.h is not covered. Also the decoder of the menu's Story page (story.h), run on made-up
// game memory with the destination table of the real DP.exe.
// Build:  make test DPEXE=/path/to/DP.exe
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "sites.h"
#include "story.h"

using namespace ufx;

static std::vector<uint8_t> g_file;

static uint32_t rd32(size_t off) {
    uint32_t v;
    memcpy(&v, &g_file[off], 4);
    return v;
}
static uint16_t rd16(size_t off) {
    uint16_t v;
    memcpy(&v, &g_file[off], 2);
    return v;
}

struct Section {
    uint32_t va, vsize, raw, rawSize;
};
static std::vector<Section> g_secs;
static uint32_t g_imageBase;

static bool vaToOff(uint32_t va, size_t* off) {
    uint32_t rva = va - g_imageBase;
    for (const Section& s : g_secs)
        if (rva >= s.va && rva < s.va + (s.vsize > s.rawSize ? s.vsize : s.rawSize)) {
            if (rva - s.va >= s.rawSize) return false;  // uninitialised part, no bytes in the file
            *off = s.raw + (rva - s.va);
            return true;
        }
    return false;
}

static bool rvaToOff(uint32_t rva, size_t* off) { return vaToOff(rva + g_imageBase, off); }

static bool g_storyOk = true;
static void storyCheck(bool condition, const char* what) {
    printf("%s  story decoder: %s\n", condition ? "ok  " : "FAIL", what);
    g_storyOk &= condition;
}

// The Story page decoder on made-up memory: a CGame image, the flag bits, a group table with the three groups the game's code
// names by number (their first flags and sizes are those of the game's PROJECT.DPB) and the destination table of DP.exe.
static bool storySelfTest() {
    size_t tableOff, tableEnd;
    if (!vaToOff(kStoryDestTableVa, &tableOff) || !vaToOff(kStoryDestTableVa + kStoryDestinations - 1, &tableEnd)) {
        printf("FAIL  story decoder: the destination table %08X is not backed by file bytes\n", kStoryDestTableVa);
        return false;
    }
    const uint8_t* allowed = &g_file[tableOff];
    storyCheck(allowed[0] != 1 && allowed[1] == 1 && allowed[2] == 1 && allowed[25] != 1 && allowed[33] == 1 && allowed[34] != 1,
               "the table of DP.exe allows destinations 1, 2 and 33 and not 0, 25 and 34");

    std::vector<uint8_t> game(kStoryGameSpan, 0), flags(0x306, 0), groups(0x65 * kStoryGroupSize, 0);
    auto setGroup = [&](uint32_t group, uint32_t first, uint32_t count) {
        memcpy(&groups[group * kStoryGroupSize], &first, 4);
        memcpy(&groups[group * kStoryGroupSize + 4], &count, 4);
    };
    setGroup(kStoryGroupSide, 0xB00, 0x80);
    setGroup(kStoryGroupCommon, 0xC58, 0x400);
    setGroup(kStoryGroupDestination, 0x10FC, 0x100);
    auto raise = [&](uint32_t flag) { flags[flag >> 3] |= (uint8_t)(1u << (flag & 7)); };
    StoryView view = {game.data(), flags.data(), (uint32_t)flags.size(), groups.data(), 0x65, allowed};
    StoryState s;

    StoryDecode(view, &s);
    storyCheck(s.episode == 0 && s.chapter == 0 && s.step == 0 && !s.finished && s.sideKnown && s.sideTaken == 0 && s.sideDone == 0 &&
                   s.sideOpenCount == 0 && s.destKnown && s.destIndex == -1 && s.destActive == 0 && s.flagsRaised == 0 &&
                   s.flagsTotal == 0x80 + 0x400 + 0x100,
               "empty memory: nothing started, no quest, no destination");

    game[kStoryEpisodeOff] = 4;
    game[kStoryChapterOff] = 15;
    game[kStoryStepOff] = 2;
    raise(0xB00 + 3);
    raise(0xB00 + 7);
    raise(0xB00 + 49);
    raise(0xB00 + 50 + 7);   // quest 7 done
    raise(0x10FC + 0);       // raised, but the table does not allow it
    raise(0x10FC + 25);      // the same
    raise(0x10FC + 2);
    raise(0x10FC + 33);
    const uint8_t window[4] = {9, 0, 21, 30};
    memcpy(&game[kStoryWindowOff + 2 * 4], window, 4);
    StoryDecode(view, &s);
    storyCheck(s.episode == 4 && s.chapter == 15 && s.step == 2 && !s.finished, "episode, chapter and sub-step bytes");
    storyCheck(s.sideTaken == 3 && s.sideDone == 1 && s.sideOpenCount == 2 && s.sideOpen[0] == 3 && s.sideOpen[1] == 49,
               "side quests: flag i = taken, flag 50 + i = done");
    storyCheck(s.destIndex == 2 && s.destActive == 2 && memcmp(s.destWindow, window, 4) == 0,
               "destination: the first raised flag the table allows, with its time window");
    storyCheck(s.flagsRaised == 8, "raised flags are counted");

    raise(0xC58 + kStoryFlagFinished);
    StoryDecode(view, &s);
    storyCheck(s.finished, "finished: flag 3Ch of group 44h");
    flags[(0xC58 + kStoryFlagFinished) >> 3] = 0;
    game[kStoryFinishedOff] = 0x81;  // difficulty 1 (normal) with bit 7
    game[kStoryChapterOff] = 0xFF;
    StoryDecode(view, &s);
    storyCheck(s.finished && s.chapter == -1, "finished: bit 7 of the difficulty byte; the chapter is a signed byte");

    view.groupCount = 0x40;  // a table without the groups 44h and 48h
    game[kStoryFinishedOff] = 0;
    raise(0xC58 + kStoryFlagFinished);
    StoryDecode(view, &s);
    storyCheck(s.sideKnown && s.sideTaken == 3 && !s.finished && !s.destKnown && s.destIndex == -1, "a missing group reads as nothing");
    view.groupCount = 0x65;
    setGroup(kStoryGroupDestination, 0x1800, 0x100);  // would end past the 0x306 bytes of flags
    StoryDecode(view, &s);
    storyCheck(!s.destKnown && s.destIndex == -1 && s.destActive == 0, "a group that does not fit the flag array is never read");
    return g_storyOk;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: verify_sites <DP.exe>\n");
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_file.resize((size_t)sz);
    if (fread(g_file.data(), 1, g_file.size(), f) != g_file.size()) return 2;
    fclose(f);

    uint32_t nt = rd32(0x3C);
    bool ok = true;
    uint16_t machine = rd16(nt + 4), nsec = rd16(nt + 6);
    uint32_t stamp = rd32(nt + 8);
    uint16_t optSize = rd16(nt + 20), chars = rd16(nt + 22);
    size_t opt = nt + 24;
    g_imageBase = rd32(opt + 28);
    uint32_t sizeOfImage = rd32(opt + 56);
    printf("machine %04X, timestamp %08X, size of image %08X, image base %08X, characteristics %04X\n", machine,
           stamp, sizeOfImage, g_imageBase, chars);
    if (machine != 0x14C || stamp != kExeTimeDateStamp || sizeOfImage != kExeSizeOfImage || g_imageBase != 0x400000) {
        printf("FAIL: not DP.exe 1.01b Steam\n");
        ok = false;
    }
    size_t sec = opt + optSize;
    for (int i = 0; i < nsec; ++i) {
        size_t o = sec + i * 40;
        g_secs.push_back({rd32(o + 12), rd32(o + 8), rd32(o + 20), rd32(o + 16)});
    }

    for (const SiteDef* s : kAllSites) {
        size_t off;
        if (!vaToOff(s->va, &off)) {
            printf("FAIL  %-28s %08X: not backed by file bytes\n", s->name, s->va);
            ok = false;
            continue;
        }
        bool match = memcmp(&g_file[off], s->bytes, s->len) == 0;
        printf("%s  %-28s %08X (%d bytes)\n", match ? "ok  " : "FAIL", s->name, s->va, s->len);
        ok &= match;
    }

    for (const TailDef& t : kAllTails) {
        size_t off, last;  // the whole run must be backed by file bytes
        bool match = vaToOff(t.va, &off) && vaToOff(t.va + t.len - 1, &last) && memcmp(&g_file[off], t.bytes, t.len) == 0;
        printf("%s  %-28s %08X (%d bytes)\n", match ? "ok  " : "FAIL", t.name, t.va, t.len);
        ok &= match;
    }

    // The composite pixel shader the Xbox 360 look recognises at run time by size and hash: both copies in the file.
    for (uint32_t va : {kCompositeShader.va, kCompositeShaderCopyVa}) {
        size_t off, last;
        bool match = vaToOff(va, &off) && vaToOff(va + kCompositeShaderSize - 1, &last) &&
                     Fnv1a(&g_file[off], kCompositeShaderSize) == kCompositeShaderFnv &&
                     rd32(off + kCompositeShaderSize - 4) == 0x0000FFFF;  // the end token closes the byte code exactly there
        printf("%s  composite shader hash    %08X (%u bytes)\n", match ? "ok  " : "FAIL", va, kCompositeShaderSize);
        ok &= match;
    }

    for (const ShadowSite& site : kShadowSites) {
        size_t off;
        uint8_t expected[5] = {0x68, (uint8_t)site.value, (uint8_t)(site.value >> 8), 0, 0};
        bool match = vaToOff(site.va, &off) && memcmp(&g_file[off], expected, 5) == 0;
        printf("%s  shadow literal %08X = %X\n", match ? "ok  " : "FAIL", site.va, site.value);
        ok &= match;
    }

    // Import slots: find each by name in the import directory and compare with the recorded VA.
    uint32_t impRva = rd32(opt + 96 + 8);
    size_t impOff;
    if (!rvaToOff(impRva, &impOff)) {
        printf("FAIL: no import directory\n");
        return 1;
    }
    for (const ImportDef& imp : kImports) {
        uint32_t found = 0;
        for (size_t d = impOff;; d += 20) {
            uint32_t nameRva = rd32(d + 12);
            if (!nameRva) break;
            size_t nOff;
            rvaToOff(nameRva, &nOff);
            std::string dll((const char*)&g_file[nOff]);
            for (char& c : dll) c = (char)tolower(c);
            std::string want(imp.dll);
            for (char& c : want) c = (char)tolower(c);
            if (dll != want) continue;
            uint32_t lookup = rd32(d) ? rd32(d) : rd32(d + 16);
            uint32_t first = rd32(d + 16);
            size_t lOff;
            rvaToOff(lookup, &lOff);
            for (int i = 0;; ++i) {
                uint32_t thunk = rd32(lOff + i * 4);
                if (!thunk) break;
                if (thunk & 0x80000000u) continue;
                size_t hOff;
                rvaToOff(thunk, &hOff);
                if (strcmp((const char*)&g_file[hOff + 2], imp.func) == 0) found = g_imageBase + first + i * 4;
            }
        }
        bool match = found != 0 && (imp.slotVa == 0 || found == imp.slotVa);
        printf("%s  import %-14s %-26s slot %08X%s\n", match ? "ok  " : "FAIL", imp.dll, imp.func, found,
               imp.slotVa == 0 ? " (not pinned)" : "");
        ok &= match;
    }
    ok &= storySelfTest();
    printf("%s\n", ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return ok ? 0 : 1;
}
