// Host test of src/mes.h: the text of a record in the markers of DPMsgTool's JSON (encoder and decoder), the readers of
// mes_all.json and glyphmap.json, the check of a whole JSON against a MES, and the merged MES file.
//   test_mes                 the built-in cases (small files made here)
//   test_mes MES_ALL.MES     also a real file: every record written as an element of mes_all.json, the JSON read back and
//                            encoded again (each record must come out as it is), and translations merged into the file
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "mes.h"

using namespace ufx;

static int g_failed = 0, g_checked = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        ++g_checked;                                                   \
        if (!(cond)) {                                                 \
            ++g_failed;                                                \
            printf("FAILED line %d: %s\n", __LINE__, #cond);          \
        }                                                              \
    } while (0)

static GlyphMap g_map;
static uint8_t g_icons[32];

// A record to replace of which only the header words matter.
static MesOriginal Orig(uint16_t w0, uint16_t w2, uint16_t w3) {
    MesOriginal o = {};
    o.w0 = w0;
    o.w2 = w2;
    o.w3 = w3;
    return o;
}

static bool Same(const std::vector<uint16_t>& a, std::initializer_list<uint16_t> b) {
    return a == std::vector<uint16_t>(b);
}

static std::vector<uint8_t> Bytes(const std::vector<uint16_t>& w) {
    std::vector<uint8_t> b(w.size() * 2);
    for (size_t i = 0; i < w.size(); ++i) MesWr16(b.data() + i * 2, w[i]);
    return b;
}

// A record to replace given by its words.
struct Record {
    std::vector<uint8_t> bytes;
    MesOriginal o;
    explicit Record(const std::vector<uint16_t>& w) : bytes(Bytes(w)) { CHECK(MesReadOriginal(bytes.data(), (uint32_t)w.size(), &o, nullptr)); }
    std::vector<uint16_t> Normal() const {
        std::vector<uint8_t> n(o.words * 2);
        MesNormalise(bytes.data(), o.words, n.data());
        std::vector<uint16_t> w;
        for (uint32_t i = 0; i < o.words; ++i) w.push_back((uint16_t)MesRd16(n.data() + i * 2));
        return w;
    }
};

struct Encoded {
    MesError error;
    MesErrorInfo info;
    std::vector<uint16_t> w;
};

static Encoded Enc(const std::string& text, const MesOriginal& o, uint32_t cap = kMesMaxWords) {
    Encoded e;
    std::vector<uint8_t> buf(cap * 2 + 2, 0xEE);
    uint32_t words = 0;
    e.error = MesEncode(text.data(), text.data() + text.size(), g_map, o, g_icons, buf.data(), cap, &words, &e.info);
    CHECK(buf[cap * 2] == 0xEE && buf[cap * 2 + 1] == 0xEE);  // nothing beyond the limit
    if (e.error == kMesOk)
        for (uint32_t i = 0; i < words; ++i) e.w.push_back((uint16_t)MesRd16(buf.data() + i * 2));
    return e;
}

static std::string Dec(const std::vector<uint16_t>& w) {
    std::vector<uint8_t> b = Bytes(w);
    std::vector<char> out(8192);
    uint32_t n = MesDecode(b.data(), (uint32_t)w.size(), out.data(), (uint32_t)out.size());
    CHECK(n <= out.size());
    return std::string(out.data(), n <= out.size() ? n : 0);
}

// FUN_0045A700 as the game does it: from the record of `id` over n FFFF words, then the first id whose offset is that
// address. The game's search has no end; here it stops at the table's end and returns -1.
static int IdAfter(const std::vector<uint8_t>& file, uint32_t id, int n) {
    const uint32_t count = MesRd32(file.data() + 8);
    uint32_t at = MesRd32(file.data() + 12 + id * 4);
    for (int i = 0; i < n; ++i) {
        for (;;) {
            if (at + 2 > file.size()) return -1;
            const uint32_t w = MesRd16(file.data() + at);
            at += 2;
            if (w == 0xFFFF) break;
        }
    }
    for (uint32_t k = 0; k < count; ++k)
        if (MesRd32(file.data() + 12 + k * 4) == at) return (int)k;
    return -1;
}

static void TestGlyphMap() {
    // the tools' plain table: the character is the cell + 20h, for ASCII and for Latin-1
    CHECK(g_map.cell['A'] == 0x21 && g_map.cell['a'] == 0x41 && g_map.cell[' '] == 0 && g_map.cell['~'] == 0x5E);
    CHECK(g_map.cell[0x7F] == kMesNoCell && g_map.cell[0x9F] == kMesNoCell && g_map.cell[0x100] == kMesNoCell && g_map.cell[0x416] == kMesNoCell);
    CHECK(g_map.cell[0xA0] == 0x80 && g_map.cell[0xBF] == 0x9F && g_map.cell[0xC7] == 0xA7 && g_map.cell[0xFF] == 0xDF);
    CHECK(MesCellChar(0x21) == 'A' && MesCellChar(0x5F) == 0 && MesCellChar(0x7F) == 0 && MesCellChar(0x80) == 0xA0 && MesCellChar(0xE0) == 0 &&
          MesCellChar(0x127) == 0);
    for (uint32_t cell = 0; cell < kMesAtlasCells; ++cell)
        if (MesCellChar(cell)) CHECK(g_map.cell[MesCellChar(cell)] == cell);

    // the tool's default glyph map: Cyrillic through carriers
    const MesOriginal talk = Orig(1, 0, 0);
    CHECK(Enc("\xD0\x93", talk).error == kMesNoGlyph);  // no glyph map yet
    GlyphMap saved = g_map;
    GlyphMapToolDefaults(g_map);
    // "Гібрид—ї": carriers BFh, i, E0h, p, E8h, E4h, DAh, EFh
    CHECK(Same(Enc("\xD0\x93\xD1\x96\xD0\xB1\xD1\x80\xD0\xB8\xD0\xB4\xE2\x80\x94\xD1\x97", talk).w,
               {1, 1, 0, 0, 8, 0x9F, 0x49, 0xC0, 0x50, 0xC8, 0xC4, 0xBA, 0xCF, 0xFFFF}));
    int mapped = 0;
    for (uint32_t cp = 0; cp < 0x10000; ++cp)
        if (g_map.subst[cp][0]) {
            ++mapped;
            CHECK(g_map.cell[g_map.subst[cp][0]] != kMesNoCell && g_map.cell[g_map.subst[cp][0]] < kMesWideFrom && !g_map.subst[cp][1]);
        }
    CHECK(mapped == 67);
    for (uint32_t cp : {0x404u, 0x406u, 0x407u, 0x490u, 0x454u, 0x456u, 0x457u, 0x491u}) CHECK(g_map.subst[cp][0]);  // the Ukrainian ones
    for (uint32_t cp = 0x410; cp <= 0x44F; ++cp)  // every letter of the Ukrainian alphabet in the basic Cyrillic block
        if (cp != 0x42A && cp != 0x42B && cp != 0x42D && cp != 0x44A && cp != 0x44B && cp != 0x44D) CHECK(g_map.subst[cp][0]);
    // a carrier pair, a carrier without a cell, a character with a cell of its own
    GlyphMapSubst(g_map, 0x429, 'S', 'h');
    CHECK(Same(Enc("\xD0\xA9", talk).w, {1, 1, 0, 0, 2, 0x33, 0x48, 0xFFFF}));
    GlyphMapSubst(g_map, 0x429, 0x2026, 0);
    Encoded e = Enc("a\xD0\xA9", talk);
    CHECK(e.error == kMesNoGlyph && e.info.value == 0x429 && e.info.value2 == 0x2026 && e.info.column == 1);
    GlyphMapSet(g_map, 0x429, 0x5F);
    CHECK(Same(Enc("\xD0\xA9", talk).w, {1, 1, 0, 0, 1, 0x5F, 0xFFFF}) && !g_map.subst[0x429][0]);
    GlyphMapSet(g_map, 0x2026, 0x103);  // a wide cell
    CHECK(Same(Enc("a\xE2\x80\xA6", talk).w, {1, 1, 0, 0, 3, 0x41, 0x103, 0xFFFF}));
    g_map = saved;
}

static void TestLines() {
    uint32_t id = 0, cp = 0, cell = 0;
    const char *t = nullptr, *te = nullptr;
    auto line = [&](const char* s) { return MesParseLine(s, s + strlen(s), &id, &t, &te); };
    CHECK(line("") == kLineEmpty && line("   \r") == kLineEmpty && line("; note = 1") == kLineEmpty);
    CHECK(line("0x1A2b = Hello there \r") == kLineEntry && id == 0x1A2B && std::string(t, te) == "Hello there ");
    CHECK(line("  0X10=x=y") == kLineEntry && id == 0x10 && std::string(t, te) == "x=y");
    CHECK(line("0x10 =  two blanks") == kLineEntry && std::string(t, te) == " two blanks");
    CHECK(line("0x7 =") == kLineEntry && id == 7 && t == te);
    CHECK(line("1234 = decimal id") == kLineBad && line("0x = no digits") == kLineBad && line("0x12 text") == kLineBad);
    CHECK(line("0x12345678 = too big") == kLineBad && line("hello") == kLineBad);

    auto map = [&](const char* s) { return GlyphMapParseLine(s, s + strlen(s), &cp, &cell); };
    CHECK(map("") == kLineEmpty && map(" ; comment") == kLineEmpty);
    CHECK(map("\xD0\x96 = 0xA5") == kLineEntry && cp == 0x416 && cell == 0xA5);
    CHECK(map("U+0416=165 ; Zhe") == kLineEntry && cp == 0x416 && cell == 165);
    CHECK(map("U = 0x35") == kLineEntry && cp == 'U' && cell == 0x35);
    CHECK(map("= = 0x1D") == kLineEntry && cp == '=' && cell == 0x1D);
    CHECK(map("U+003B = 27") == kLineEntry && cp == ';' && cell == 27);
    CHECK(map("A = 0x980") == kLineBad);                 // beyond the atlas
    CHECK(map("A 0x21") == kLineBad && map("A = ") == kLineBad && map("A = 5 6") == kLineBad);
    CHECK(map("\xF0\x9F\x98\x80 = 5") == kLineBad);       // outside the basic plane
    CHECK(map("\xD0 = 5") == kLineBad && map("U+0009 = 5") == kLineBad && map("U+D800 = 5") == kLineBad);
}

static void TestEncode() {
    const MesOriginal talk = Orig(1, 0x8000, 0x51);
    CHECK(Same(Enc("Hi", talk).w, {1, 1, 0x8000, 0x51, 2, 0x28, 0x49, 0xFFFF}));
    CHECK(Same(Enc("", talk).w, {1, 1, 0x8000, 0x51, 0, 0xFFFF}));
    CHECK(Same(Enc(" a ", talk).w, {1, 1, 0x8000, 0x51, 3, 0, 0x41, 0, 0xFFFF}));
    CHECK(Same(Enc("A{NEXT_SEGMENT}B", talk).w, {1, 2, 0x8000, 0x51, 1, 0x21, 0xFFFE, 1, 0x22, 0xFFFF}));
    CHECK(Same(Enc("A{NEXT_SEGMENT}", talk).w, {1, 2, 0x8000, 0x51, 1, 0x21, 0xFFFE, 0, 0xFFFF}));
    // {NEXT_FRAME} is FFF2 with its number; two lines in front of it stay as they are
    CHECK(Same(Enc("A{NEXT_SEGMENT}C{NEXT_FRAME id=90}B", talk).w, {1, 3, 0x8000, 0x51, 1, 0x21, 0xFFFE, 1, 0x23, 0xFFF2, 90, 1, 0x22, 0xFFFF}));
    CHECK(Enc("A{NEXT_SEGMENT}C{NEXT_FRAME id=90}B", talk).info.padded == 0);
    CHECK(Same(Enc("A{NEXT_SEGMENT}{NEXT_FRAME id=0x5A}B", talk).w, {1, 3, 0x8000, 0x51, 1, 0x21, 0xFFFE, 0, 0xFFF2, 90, 1, 0x22, 0xFFFF}));
    // the form with a second number, and the labels are free
    CHECK(Same(Enc("A{NEXT_SEGMENT}C{NEXT_FRAME id=90, letters=7}B", talk).w, {1, 3, 0x8000, 0x51, 1, 0x21, 0xFFFE, 1, 0x23, 0xFFF2, 90, 1, 0x22, 0xFFFF}));
    CHECK(Same(Enc("A{NEXT_SEGMENT}C{NEXT_FRAME 90}B", talk).w, {1, 3, 0x8000, 0x51, 1, 0x21, 0xFFFE, 1, 0x23, 0xFFF2, 90, 1, 0x22, 0xFFFF}));
    // an odd number of lines in front of {NEXT_FRAME} in a window of two-line pages: an empty line is added
    Encoded e = Enc("A{NEXT_FRAME id=90}B", talk);
    CHECK(Same(e.w, {1, 3, 0x8000, 0x51, 1, 0x21, 0xFFFE, 0, 0xFFF2, 90, 1, 0x22, 0xFFFF}) && e.info.padded == 1);
    e = Enc("A{NEXT_SEGMENT}B{NEXT_SEGMENT}C{NEXT_FRAME id=9}D{NEXT_FRAME id=8}E", talk);
    CHECK(Same(e.w, {1, 7, 0x8000, 0x51, 1, 0x21, 0xFFFE, 1, 0x22, 0xFFFE, 1, 0x23, 0xFFFE, 0, 0xFFF2, 9, 1, 0x24, 0xFFFE, 0, 0xFFF2, 8, 1, 0x25, 0xFFFF}) &&
          e.info.padded == 2);
    // the styles with three-line windows: FFF2 exactly where it is written
    e = Enc("A{NEXT_FRAME id=7}B", Orig(0x3E, 0, 0));
    CHECK(Same(e.w, {0x3E, 2, 0, 0, 1, 0x21, 0xFFF2, 7, 1, 0x22, 0xFFFF}) && e.info.padded == 0);
    CHECK(Enc("A{NEXT_SEGMENT}B{NEXT_SEGMENT}C{NEXT_FRAME id=7}D", Orig(0x38, 0, 0)).info.padded == 0);
    CHECK(Enc("A{NEXT_FRAME id=0xFFF2}B", talk).error == kMesBadValue && Enc("A{NEXT_SEGMENT}{NEXT_FRAME id=65518}B", talk).error == kMesOk);
    CHECK(Enc("A{NEXT_FRAME id=x}B", talk).error == kMesBadTag && Enc("A{NEXT_FRAME id=1, 2, 3}B", talk).error == kMesBadTag &&
          Enc("A{NEXT_FRAME}B", talk).error == kMesBadTag && Enc("A{NEXT_SEGMENT id=1}B", talk).error == kMesBadTag);

    // widths: a narrow cell 1, an icon 2; colour and size add nothing
    CHECK(Same(Enc("{ICON id=9}a", talk).w, {1, 1, 0x8000, 0x51, 3, 0xFFF4, 9, 0x41, 0xFFFF}));
    CHECK(Same(Enc("{SET_COLOR color=135, 240, 120}a{RESET_COLOR}", talk).w, {1, 1, 0x8000, 0x51, 1, 0xFFFA, 135, 240, 120, 0x41, 0xFFF9, 0xFFFF}));
    CHECK(Same(Enc("{SET_COLOR r=1, g=2, b=3}a", talk).w, {1, 1, 0x8000, 0x51, 1, 0xFFFA, 1, 2, 3, 0x41, 0xFFFF}));
    CHECK(Same(Enc("{SET_SPACING value=48}a", talk).w, {1, 1, 0x8000, 0x51, 1, 0xFFF1, 48, 0x41, 0xFFFF}));
    CHECK(Same(Enc("**", talk).w, {1, 1, 0x8000, 0x51, 2, 0x0A, 0x0A, 0xFFFF}));  // the number / item placeholder
    CHECK(Same(Enc("\xC3\x87\xC3\xA9", talk).w, {1, 1, 0x8000, 0x51, 2, 0xA7, 0xC9, 0xFFFF}));
    // braces that are not markers are characters
    CHECK(Same(Enc("{x} {} }{", talk).w, {1, 1, 0x8000, 0x51, 9, 0x5B, 0x58, 0x5D, 0, 0x5B, 0x5D, 0, 0x5D, 0x5B, 0xFFFF}));
    CHECK(Same(Enc("{A}", talk).w, {1, 1, 0x8000, 0x51, 3, 0x5B, 0x21, 0x5D, 0xFFFF}));  // one capital is not a name

    // the word markers: only what the record being replaced has
    const Record marked({2, 2, 0x8000, 0x1D5, 5, 0xFFF5, 0x127, 0xFFF0, 0x41, 0xFFF4, 0x30, 0xFFFE, 0, 0xFFFF});
    CHECK(Same(Enc("{NO_OP:0xFFF5}a{UNK:0x0127}{UNK:0xFFF0}", marked.o).w, {2, 1, 0x8000, 0x1D5, 3, 0xFFF5, 0x41, 0x127, 0xFFF0, 0xFFFF}));
    CHECK(Same(Enc("{UNK:0xFFF5}{NO_OP:0xFFF0}{NO_OP:295}", marked.o).w, {2, 1, 0x8000, 0x1D5, 2, 0xFFF5, 0xFFF0, 0x127, 0xFFFF}));
    e = Enc("a{UNK:0x0128}", marked.o);
    CHECK(e.error == kMesUnknownWord && e.info.value == 0x128 && e.info.column == 1);
    CHECK(Enc("{NO_OP:0xFFF5}", talk).error == kMesUnknownWord);                 // the record has no FFF5
    CHECK(Enc("{UNK:0x0030}", marked.o).error == kMesUnknownWord);               // 30h is there only as the icon's number
    CHECK(Enc("{UNK:0xFFF4}", marked.o).error == kMesUnknownWord && Enc("{UNK:0xFFFE}", marked.o).error == kMesUnknownWord &&
          Enc("{NO_OP:0xFFF3}", marked.o).error == kMesUnknownWord && Enc("{UNK:0x1FFF0}", marked.o).error == kMesUnknownWord);
    CHECK(Enc("{NO_OP}", marked.o).error == kMesBadTag && Enc("{NO_OP:}", marked.o).error == kMesBadTag && Enc("{UNK:0xFFF5 }", marked.o).error == kMesBadTag &&
          Enc("{UNK 0xFFF5}", marked.o).error == kMesBadTag);

    // {BR}: refused until the caller says what it is
    CHECK(Enc("a{BR}b", talk).error == kMesUnknownBr && Enc("a{BR}b", talk).info.column == 1);
    g_map.brCode = kMesSizeBack;
    CHECK(Same(Enc("a{BR}b", talk).w, {1, 1, 0x8000, 0x51, 2, 0x41, 0xFFF0, 0x42, 0xFFFF}));
    g_map.brCode = 0;

    // refusals
    e = Enc("ab\xD0\x96", talk);
    CHECK(e.error == kMesNoGlyph && e.info.value == 0x416 && e.info.value2 == 0 && e.info.column == 2);
    CHECK(Enc("a\xD0", talk).error == kMesBadUtf8 && Enc("a\xC0\x80", talk).error == kMesBadUtf8 && Enc("\xFF", talk).error == kMesBadUtf8);
    CHECK(Enc("a\xF0\x9F\x98\x80", talk).error == kMesNoGlyph && Enc("a\tb", talk).error == kMesNoGlyph && Enc("a\nb", talk).error == kMesNoGlyph);
    CHECK(Enc("\xE2\x88\x9E", talk).error == kMesNoGlyph && Enc("\xC5\x92", talk).error == kMesNoGlyph);  // not characters of the tools' table
    CHECK(Enc("a{NEXT_SEGMNT}b", talk).error == kMesBadTag && Enc("a{NEXT_SEGMENT", talk).error == kMesBadTag &&
          Enc("a{NEXT_SEGMENT{BR}}", talk).error == kMesUnknownBr && Enc("a{PAGE 5}", talk).error == kMesBadTag);
    CHECK(Enc("{SET_COLOR color=1, 2}", talk).error == kMesBadTag && Enc("{SET_COLOR color=1, 2, 256}", talk).error == kMesBadValue &&
          Enc("{SET_COLOR color=1, 2, 3, 4}", talk).error == kMesBadTag && Enc("{SET_COLOR}", talk).error == kMesBadTag &&
          Enc("{RESET_COLOR 1}", talk).error == kMesBadTag);
    CHECK(Enc("{SET_SPACING value=7}", talk).error == kMesBadValue && Enc("{SET_SPACING value=129}", talk).error == kMesBadValue &&
          Enc("{SET_SPACING}", talk).error == kMesBadTag && Enc("{SET_SPACING value=-48}", talk).error == kMesBadTag);
    e = Enc("{ICON id=11}", talk);
    CHECK(e.error == kMesBadIcon && e.info.value == 11);
    CHECK(Enc("{ICON id=300}", talk).error == kMesBadIcon && Enc("{ICON}", talk).error == kMesBadTag);

    std::string longLine(kMesMaxLineGlyphs, 'a');
    CHECK(Enc(longLine, talk).error == kMesOk);
    e = Enc(longLine + "a", talk);
    CHECK(e.error == kMesLineTooLong && e.info.value == kMesMaxLineGlyphs + 1);
    CHECK(Enc(longLine + "{NEXT_SEGMENT}" + longLine, talk).error == kMesOk);
    // the word limit: 4 header words, N, 3 glyphs, FFFF = 9
    CHECK(Enc("abc", talk, 9).error == kMesOk && Enc("abc", talk, 8).error == kMesTooLong && Enc("abc", talk, 1).error == kMesTooLong);

    uint32_t counts[kKindCount];
    const std::string all = "a{NEXT_SEGMENT}b{NEXT_FRAME id=3}{ICON id=1}{ICON id=2}{SET_COLOR color=1, 2, 3}{RESET_COLOR}{SET_SPACING value=48}{BR}{BR}{BR}"
                            "{NO_OP:0xFFF5}{x}{NEXT_SEGMENT}";
    MesCountMarkers(all.data(), all.data() + all.size(), counts);
    CHECK(counts[kKindSegment] == 2 && counts[kKindFrame] == 1 && counts[kKindIcon] == 2 && counts[kKindColour] == 1 && counts[kKindColourBack] == 1 &&
          counts[kKindSpacing] == 1 && counts[kKindBr] == 3 && counts[kKindSizeBack] == 0);
}

// Records given by their words: as text, and the text encoded for the same record gives the record back (with its width
// words and line count put right).
static void TestDecode() {
    const std::vector<std::vector<uint16_t>> records = {
        {1, 1, 0x8000, 0x51, 2, 0x28, 0x49, 0xFFFF},
        {1, 2, 0x8000, 0x51, 1, 0x21, 0xFFFE, 0, 0xFFFF},
        {0xA, 4, 0x8000, 0x104, 1, 0x21, 0xFFFE, 2, 0xFFF4, 9, 0xFFF2, 300, 3, 0xFFFA, 1, 2, 3, 0x41, 0xFFF1, 48, 0x42, 0xFFF0, 0xFFF9, 0x43, 0xFFFE, 0,
         0xFFF5, 0xFFFF},
        {2, 2, 0x8000, 0x1D5, 0, 0xFFF5, 0xFFFE, 0, 0xFFFF},
        {0xB, 1, 0, 0, 9, 0, 0x5B, 0x5D, 0x103, 0x127, 0xE0, 0x5F, 0, 0xFFFF},
        {0x3E, 3, 0, 0, 1, 0x21, 0xFFF2, 0, 1, 0x22, 0xFFF2, 5, 1, 0xA7, 0xFFFF},
        {1, 9, 0x8000, 7, 99, 0x21, 0xFFFE, 99, 0x22, 0xFFFF},  // a wrong line count and wrong widths
    };
    for (const auto& words : records) {
        const Record rec(words);
        const std::string text = Dec(words);
        const Encoded e = Enc(text, rec.o);
        if (e.error != kMesOk || e.w != rec.Normal()) printf("  \"%s\" does not give its record back (error %d)\n", text.c_str(), (int)e.error);
        CHECK(e.error == kMesOk && e.w == rec.Normal() && e.info.padded == 0);
    }
    CHECK(Dec(records[0]) == "Hi" && Dec(records[1]) == "A{NEXT_SEGMENT}");
    CHECK(Dec(records[2]) == "A{NEXT_SEGMENT}{ICON id=9}{NEXT_FRAME id=300}{SET_COLOR color=1, 2, 3}a{SET_SPACING value=48}b{UNK:0xFFF0}{RESET_COLOR}c"
                             "{NEXT_SEGMENT}{NO_OP:0xFFF5}");
    CHECK(Dec(records[3]) == "{NO_OP:0xFFF5}{NEXT_SEGMENT}");
    CHECK(Dec(records[4]) == " {UNK:0x005B}}{UNK:0x0103}{UNK:0x0127}{UNK:0x00E0}{UNK:0x005F} ");
    CHECK(Dec(records[5]) == "A{NEXT_FRAME id=0}B{NEXT_FRAME id=5}\xC3\x87");

    // a buffer that is too small: the length is still reported, nothing is written beyond it
    char small[5] = {1, 1, 1, 1, 1};
    CHECK(MesDecode(Bytes(records[1]).data(), 9, small, 4) == 15 && small[4] == 1 && small[0] == 'A');
}

static void TestReadOriginal() {
    MesOriginal o;
    uint8_t icons[32] = {};
    std::vector<uint8_t> r = Bytes({0xA, 4, 0x8000, 0x104, 1, 0x21, 0xFFFE, 2, 0xFFF4, 9, 0xFFF2, 300, 3, 0xFFFA, 1, 2, 3, 0x41, 0xFFF1, 48, 0x42,
                                    0xFFF0, 0xFFF9, 0x43, 0xFFFE, 0, 0xFFF5, 0xFFFF, 0x1234});
    CHECK(MesReadOriginal(r.data(), (uint32_t)r.size() / 2, &o, icons));
    CHECK(o.rec == r.data() && o.w0 == 0xA && o.w1 == 4 && o.w2 == 0x8000 && o.w3 == 0x104 && o.words == 28 && o.lines == 4);
    CHECK(o.codes[kKindSegment] == 2 && o.codes[kKindFrame] == 1 && o.codes[kKindIcon] == 1 && o.codes[kKindColour] == 1 &&
          o.codes[kKindColourBack] == 1 && o.codes[kKindSpacing] == 1 && o.codes[kKindSizeBack] == 1 && o.codes[kKindBr] == 0);
    CHECK(icons[1] == 2);
    CHECK(MesRecordBytes(r.data(), (uint32_t)r.size()) == 56);
    CHECK(MesRecordHasWord(r.data(), 28, 0x21) && MesRecordHasWord(r.data(), 28, 0xFFF0) && MesRecordHasWord(r.data(), 28, 0xFFF5) &&
          MesRecordHasWord(r.data(), 28, 0xFFF9));
    // not as a width word, a parameter, a line end or a code with parameters
    CHECK(!MesRecordHasWord(r.data(), 28, 9) && !MesRecordHasWord(r.data(), 28, 300) && !MesRecordHasWord(r.data(), 28, 48) &&
          !MesRecordHasWord(r.data(), 28, 2) && !MesRecordHasWord(r.data(), 28, 0xFFFE) && !MesRecordHasWord(r.data(), 28, 0xFFF4) &&
          !MesRecordHasWord(r.data(), 28, 0x1234) && !MesRecordHasWord(r.data(), 28, 0xA));
    CHECK(!MesReadOriginal(r.data(), 27, &o, nullptr));  // the FFFF is outside the limit
    CHECK(!MesReadOriginal(Bytes({1, 1, 0, 0, 1, 0xFFF6, 0, 0, 0xFFFF}).data(), 9, &o, nullptr));   // ruby text
    CHECK(!MesReadOriginal(Bytes({1, 1, 0, 0, 1, 0xFFF3, 0xFFFF}).data(), 7, &o, nullptr));        // a code the files never use
    CHECK(!MesReadOriginal(Bytes({1, 1, 0xFFFE, 0, 1, 0x21, 0xFFFF}).data(), 7, &o, nullptr));     // a line end in the header
    CHECK(!MesReadOriginal(Bytes({1, 1, 0, 0, 1, 0xFFFA, 1, 0xFFFF, 3, 0xFFFF}).data(), 10, &o, nullptr));  // a line end as a parameter
    CHECK(!MesReadOriginal(Bytes({1, 1, 0, 0, 1, 0xFFFA, 1, 2}).data(), 8, &o, nullptr));          // cut inside the parameters
    CHECK(MesRecordBytes(Bytes({1, 2, 3}).data(), 6) == 0);
}

struct Element {
    MesJsonRecord r;
    std::string text;
};

// A whole mes_all.json; false when it is not read to its end.
static bool ReadJson(const std::string& json, std::vector<Element>* out, uint32_t textCap = 1 << 16) {
    MesJsonReader reader = {{json.data(), json.data() + json.size()}, 0};
    std::vector<char> text(textCap + 1, '#');
    Element e;
    int more;
    out->clear();
    while ((more = MesJsonNext(reader, &e.r, text.data(), textCap)) == 1) {
        e.text.assign(text.data(), e.r.textLen <= textCap ? e.r.textLen : 0);
        out->push_back(e);
    }
    CHECK(text[textCap] == '#');
    return more == 0;
}

static void TestJson() {
    std::vector<Element> el;
    // as the tools write it: two spaces, CR LF
    const std::string file =
        "[\r\n  {\r\n    \"Id1\": 1,\r\n    \"Id2\": 2,\r\n    \"Flag1\": 32768,\r\n    \"Flag2\": 229,\r\n    \"FirstSegmentLenght\": 29,\r\n"
        "    \"Text\": \"Oh, that? \\\"We\\\" took it{NEXT_SEGMENT}\\u0413\\u00e9 \xD0\x93 \\\\ \\/\",\r\n    \"EmptyRecord\": false\r\n  },\r\n"
        "  {\r\n    \"Id1\": 0,\r\n    \"Id2\": 0,\r\n    \"Flag1\": 0,\r\n    \"Flag2\": 0,\r\n    \"FirstSegmentLenght\": 0,\r\n    \"Text\": \"\",\r\n"
        "    \"EmptyRecord\": true\r\n  },\r\n"
        "  {\"EmptyRecord\":false,\"Text\":\"x\\ud83d\\ude00y\\ud83dz\\n\",\"Flag1\":-32768,\"Extra\":{\"a\":[1,{\"b\":\"]}\\\"\"}],\"c\":null},\"Id1\":-1,\"Half\":1.5},\r\n"
        "  7,\r\n  {}\r\n]\r\n";
    CHECK(ReadJson(file, &el) && el.size() == 5);
    if (el.size() == 5) {
        CHECK(el[0].r.id1 == 1 && el[0].r.id2 == 2 && el[0].r.flag1 == 32768 && el[0].r.flag2 == 229 && el[0].r.firstSegment == 29 && !el[0].r.empty &&
              el[0].r.have == (kJsonId1 | kJsonId2 | kJsonFlag1 | kJsonFlag2 | kJsonFirstSegment | kJsonText | kJsonEmpty));
        CHECK(el[0].text == "Oh, that? \"We\" took it{NEXT_SEGMENT}\xD0\x93\xC3\xA9 \xD0\x93 \\ /");
        CHECK(el[1].r.empty && el[1].text.empty() && (el[1].r.have & kJsonText));
        CHECK(el[2].text == "x\xF0\x9F\x98\x80y\xEF\xBF\xBDz\n" && el[2].r.flag1 == -32768 && el[2].r.id1 == -1 &&
              el[2].r.have == (kJsonId1 | kJsonFlag1 | kJsonText | kJsonEmpty));
        CHECK(el[3].r.have == 0 && el[4].r.have == 0);
    }
    // a text longer than the buffer: its length is reported, the reader goes on
    CHECK(ReadJson("[{\"Text\":\"0123456789\"},{\"Text\":\"ab\"}]", &el, 4) && el.size() == 2 && el[0].r.textLen == 10 && el[1].text == "ab");
    CHECK(ReadJson("[]", &el) && el.empty() && ReadJson(" [ ] ", &el));
    for (const char* bad : {"", "{}", "[{\"Text\":\"a}]", "[{\"Text\":\"a\"}", "[{\"Text\":\"a\"} {\"Text\":\"b\"}]", "[{\"Text\":\"\\x\"}]", "[{\"Text\" \"a\"}]",
                            "[{\"Text\":\"\\u12G4\"}]", "[{Text:1}]", "[{\"Text\":\"a\x01\"}]", "[{\"Id1\":1,}]"})
        CHECK(!ReadJson(bad, &el));
    // a field of another type is stepped over, not taken
    CHECK(ReadJson("[{\"Id1\":\"1\",\"Text\":5,\"EmptyRecord\":\"true\",\"Id2\":3}]", &el) && el.size() == 1 && el[0].r.have == kJsonId2 && el[0].r.id2 == 3);

    // written and read back
    std::vector<char> buf(4096);
    MesText t = {buf.data(), (uint32_t)buf.size(), 0};
    const std::string odd = "a\"b\\c\td{NEXT_SEGMENT}\xD0\x93";
    t.Str("[\r\n");
    MesJsonRecordOut(t, 10, 2, 0x8000, 0x104, 26, odd.data(), (uint32_t)odd.size(), false);
    t.Str(",\r\n");
    MesJsonRecordOut(t, 0, 0, 0, 0, 0, "", 0, true);
    t.Str("\r\n]");
    const std::string written(buf.data(), t.n);
    CHECK(written.find("  {\r\n    \"Id1\": 10,\r\n    \"Id2\": 2,\r\n    \"Flag1\": 32768,\r\n    \"Flag2\": 260,\r\n    \"FirstSegmentLenght\": 26,\r\n    \"Text\": \"a\\\"b\\\\c\\u0009d") == 3);
    CHECK(ReadJson(written, &el) && el.size() == 2 && el[0].text == odd && el[0].r.id1 == 10 && el[0].r.flag1 == 0x8000 && el[0].r.firstSegment == 26 &&
          !el[0].r.empty && el[1].r.empty);

    // glyphmap.json
    auto readMap = [](const std::string& json, std::vector<std::vector<uint32_t>>* out) {
        GlyphJsonReader reader = {{json.data(), json.data() + json.size()}, 0};
        uint32_t key, value[2], count;
        int more;
        out->clear();
        while ((more = GlyphJsonNext(reader, &key, value, &count)) == 1) out->push_back({key, value[0], value[1], count});
        return more;
    };
    std::vector<std::vector<uint32_t>> m;
    CHECK(readMap("{\r\n  \"version\": 1,\r\n  \"map\": {\r\n    \"\xD0\x91\": \"\xC3\x94\",\r\n    \"\xD0\xA9\": \"Sh\",\r\n    \"\\u0413\": \"\\u00bf\",\r\n"
                  "    \"ab\": \"c\",\r\n    \"\xD0\x96\": \"\",\r\n    \"\xD0\x97\": \"abc\",\r\n    \"\xD0\x98\": 5\r\n  },\r\n  \"after\": {\"x\": \"y\"}\r\n}", &m) == 0);
    CHECK(m.size() == 7);
    if (m.size() == 7) {
        CHECK(m[0] == std::vector<uint32_t>({0x411, 0xD4, 0, 1}) && m[1] == std::vector<uint32_t>({0x429, 'S', 'h', 2}) &&
              m[2] == std::vector<uint32_t>({0x413, 0xBF, 0, 1}));
        CHECK(m[3][0] == 0 && m[4][3] == 0 && m[5][3] == 3 && m[6][3] == 0xFF);  // none of these four is usable
    }
    CHECK(readMap("{\"version\":1}", &m) == 0 && m.empty() && readMap("{\"map\":{}}", &m) == 0 && m.empty() && readMap("{\"map\":5}", &m) == 0 && m.empty());
    CHECK(readMap("[1]", &m) == -1 && readMap("", &m) == -1 && readMap("{\"map\":{\"a\":\"b\"", &m) == -1 && readMap("{\"map\":{\"a\" \"b\"}}", &m) == -1);
}

// The check of a whole JSON against the MES, on made-up numbers.
static void TestJudge() {
    const Record rec({1, 2, 0x8000, 0x51, 1, 0x21, 0xFFFE, 2, 0xFFF4, 9, 0xFFF0, 0xFFFF});
    MesJsonRecord r = {};
    r.have = kJsonId1 | kJsonId2 | kJsonFlag1 | kJsonFlag2 | kJsonText;
    r.id1 = 1;
    r.id2 = 2;
    r.flag1 = -32768;  // 8000h written as a signed number
    r.flag2 = 0x51;
    CHECK(MesJsonHeaderMatches(r, rec.o));
    uint32_t kind;
    uint16_t br;
    auto add = [&](MesJsonCheck& c, const MesJsonRecord& rr, const std::string& text, int times) {
        for (int i = 0; i < times; ++i) MesJsonCheckAdd(c, c.elements, rr, text.data(), (uint32_t)text.size(), &rec.o);
    };
    MesJsonCheck c = {};
    add(c, r, "\xD0\x90{NEXT_SEGMENT}{ICON id=9}{BR}", 30);
    MesJsonRecord empty = {};
    empty.empty = true;
    add(c, empty, "", 5);
    MesJsonCheckAdd(c, c.elements, r, "x", 1, nullptr);  // no record of that number
    CHECK(c.elements == 36 && c.compared == 30 && c.headerDiffers == 0 && c.linesDiffer == 0 && c.either[kKindSegment] == 30 && c.agree[kKindSegment] == 30 &&
          c.either[kKindIcon] == 30 && c.agree[kKindIcon] == 30 && c.either[kKindFrame] == 0 && c.either[kKindBr] == 30 && c.agree[kKindBr] == 30 && c.brTexts == 30);
    CHECK(MesJsonJudge(c, &kind, &br) == kJsonGood && br == kMesSizeBack);
    // one record where {BR} and FFF0 do not go together: {BR} stays unknown, the file is still good
    add(c, r, "a{NEXT_SEGMENT}{ICON id=9}", 1);
    CHECK(c.either[kKindBr] == 31 && c.agree[kKindBr] == 30 && MesJsonJudge(c, &kind, &br) == kJsonGood && br == 0);
    // a translator's few records with other line breaks do not matter
    add(c, r, "a{NEXT_SEGMENT}b{NEXT_SEGMENT}{ICON id=9}", 10);
    CHECK(MesJsonJudge(c, &kind, &br) == kJsonGood);
    // a marker that is not its code: most records disagree
    add(c, r, "a{ICON id=9}{ICON id=9}{NEXT_SEGMENT}", 45);
    CHECK(c.either[kKindIcon] == 86 && c.agree[kKindIcon] == 41 && MesJsonJudge(c, &kind, &br) == kJsonMarkersDiffer && kind == kKindIcon);
    // header words: a few records off are tolerated (and refused one by one), many are not
    c = {};
    add(c, r, "a{NEXT_SEGMENT}{ICON id=9}", 95);
    MesJsonRecord other = r;
    other.flag2 = 0x52;
    CHECK(!MesJsonHeaderMatches(other, rec.o));
    add(c, other, "a{NEXT_SEGMENT}{ICON id=9}", 5);
    CHECK(c.headerDiffers == 5 && c.firstHeaderDiffers == 95 && MesJsonJudge(c, &kind, &br) == kJsonGood);
    add(c, other, "a{NEXT_SEGMENT}{ICON id=9}", 1);
    CHECK(MesJsonJudge(c, &kind, &br) == kJsonNotThisMes);
    other = r;
    other.id2 = 5;  // the line count alone is not held against the file
    c = {};
    add(c, other, "a{NEXT_SEGMENT}{ICON id=9}", 40);
    CHECK(c.linesDiffer == 40 && c.headerDiffers == 0 && MesJsonJudge(c, &kind, &br) == kJsonGood);
    other = r;
    other.have = kJsonText;  // an element without the header fields is not held against it either
    CHECK(MesJsonHeaderMatches(other, rec.o));
}

// A small file: 8 ids, the records out of id order (3, 0, 5, 1, 6), ids 2 and 7 without a record, id 4 sharing the record
// of id 1, two words nobody points at behind the record of id 5 and three bytes behind the last record.
static std::vector<uint8_t> MakeFile(std::vector<std::vector<uint16_t>>* records) {
    const MesOriginal talk = Orig(1, 0x8000, 0x51);
    std::vector<std::vector<uint16_t>> rec(8);
    rec[3] = Enc("Three{NEXT_SEGMENT}lines{NEXT_FRAME id=60}here", talk).w;
    rec[0] = Enc("Zero", Orig(0xB, 0, 0)).w;
    rec[5] = Enc("{ICON id=9}Five", talk).w;
    rec[1] = Enc("One{NEXT_SEGMENT}", talk).w;
    rec[6] = Enc("Six", talk).w;
    const int physical[5] = {3, 0, 5, 1, 6};
    std::vector<uint8_t> file(12 + 8 * 4, 0);
    memcpy(file.data(), ".MES", 4);
    MesWr32(file.data() + 4, 1);
    MesWr32(file.data() + 8, 8);
    for (int id : physical) {
        MesWr32(file.data() + 12 + id * 4, (uint32_t)file.size());
        std::vector<uint8_t> b = Bytes(rec[id]);
        file.insert(file.end(), b.begin(), b.end());
        if (id == 5) {
            std::vector<uint8_t> extra = Bytes({0x1111, 0x2222});
            file.insert(file.end(), extra.begin(), extra.end());
        }
    }
    MesWr32(file.data() + 12 + 4 * 4, MesRd32(file.data() + 12 + 1 * 4));
    file.push_back(0xAA);
    file.push_back(0xBB);
    file.push_back(0xCC);
    *records = rec;
    return file;
}

static void TestMerge() {
    std::vector<std::vector<uint16_t>> rec;
    const std::vector<uint8_t> file = MakeFile(&rec);
    MesFile f;
    CHECK(MesOpen(file.data(), (uint32_t)file.size(), &f) && f.count == 8 && f.tableEnd == 44);
    uint32_t order[8];
    const uint32_t n = MesPhysicalOrder(f, order);
    CHECK(n == 6 && order[0] == 3 && order[1] == 0 && order[2] == 5 && order[3] == 1 && order[4] == 4 && order[5] == 6);
    CHECK(MesSpanEnd(f, order, n, 3) == MesOffset(f, 6) && MesSpanEnd(f, order, n, 5) == file.size());

    // nothing replaced: the same bytes
    MesOverride none[8] = {};
    CHECK(MesMerge(f, order, n, none, nullptr, 0) == file.size());
    std::vector<uint8_t> out(file.size() + 64, 0xEE);
    CHECK(MesMerge(f, order, n, none, out.data(), (uint32_t)file.size()) == file.size());
    CHECK(memcmp(out.data(), file.data(), file.size()) == 0 && out[file.size()] == 0xEE);

    // a longer record 3, a shorter record 5, and the shared record through its second id
    const MesOriginal talk = Orig(1, 0x8000, 0x51);
    std::vector<uint8_t> new3 = Bytes(Enc("Three is much longer now{NEXT_SEGMENT}than it was{NEXT_FRAME id=60}and has{NEXT_SEGMENT}one more line", talk).w);
    std::vector<uint8_t> new5 = Bytes(Enc("5", talk).w);
    std::vector<uint8_t> new4 = Bytes(Enc("Shared{NEXT_SEGMENT}record", talk).w);
    MesOverride ov[8] = {};
    ov[3] = {new3.data(), (uint32_t)new3.size()};
    ov[5] = {new5.data(), (uint32_t)new5.size()};
    ov[4] = {new4.data(), (uint32_t)new4.size()};
    const uint32_t size = MesMerge(f, order, n, ov, nullptr, 0);
    CHECK(size == file.size() + new3.size() + new5.size() + new4.size() - (rec[3].size() + rec[5].size() + rec[1].size()) * 2);
    std::vector<uint8_t> merged(size + 2, 0xEE);
    CHECK(MesMerge(f, order, n, ov, merged.data(), size - 1) == 0);  // too small: refused
    CHECK(MesMerge(f, order, n, ov, merged.data(), size) == size && merged[size] == 0xEE);
    merged.resize(size);

    MesFile m;
    CHECK(MesOpen(merged.data(), size, &m) && m.count == 8);
    uint32_t order2[8];
    CHECK(MesPhysicalOrder(m, order2) == n && memcmp(order, order2, n * sizeof(uint32_t)) == 0);  // the same physical order
    CHECK(MesOffset(m, 2) == 0 && MesOffset(m, 7) == 0 && MesOffset(m, 1) == MesOffset(m, 4) && MesOffset(m, 3) == 44);
    auto at = [&](uint32_t id, const std::vector<uint8_t>& bytes) {
        return MesOffset(m, id) + bytes.size() <= size && memcmp(merged.data() + MesOffset(m, id), bytes.data(), bytes.size()) == 0;
    };
    CHECK(at(3, new3) && at(5, new5) && at(1, new4) && at(0, Bytes(rec[0])) && at(6, Bytes(rec[6])));
    // what followed the record of id 5 and the last record is still there
    CHECK(MesRd16(merged.data() + MesOffset(m, 5) + new5.size()) == 0x1111 && MesRd16(merged.data() + MesOffset(m, 5) + new5.size() + 2) == 0x2222);
    CHECK(MesOffset(m, 1) == MesOffset(m, 5) + new5.size() + 4);
    CHECK(merged[size - 3] == 0xAA && merged[size - 2] == 0xBB && merged[size - 1] == 0xCC);
    // the game's "n-th record after this one" gives the same ids as before
    for (uint32_t id : {3u, 0u, 5u, 1u, 4u, 6u})
        for (int k = 0; k <= 3; ++k) CHECK(IdAfter(merged, id, k) == IdAfter(file, id, k));
    CHECK(IdAfter(file, 3, 1) == 0 && IdAfter(file, 3, 2) == 5 && IdAfter(file, 0, 0) == 0);

    // files that are not touched
    std::vector<uint8_t> bad = file;
    bad[0] = 'X';
    CHECK(!MesOpen(bad.data(), (uint32_t)bad.size(), &f));
    bad = file;
    MesWr32(bad.data() + 8, 0x20000);
    CHECK(!MesOpen(bad.data(), (uint32_t)bad.size(), &f));
    CHECK(!MesOpen(file.data(), 40, &f) && !MesOpen(nullptr, 100, &f));
    for (uint32_t offset : {20u, (uint32_t)file.size(), 45u}) {  // into the table, beyond the file, odd
        bad = file;
        MesWr32(bad.data() + 12 + 2 * 4, offset);
        CHECK(MesOpen(bad.data(), (uint32_t)bad.size(), &f) && MesPhysicalOrder(f, order2) == 0xFFFFFFFF);
    }
}

// The checks that need the file: the id, its record, the two template records, the icons the file itself uses, and a text
// that says what the record says.
static void TestEncodeFor() {
    const uint32_t count = 0x3B74;
    const MesOriginal talk = Orig(1, 0x8000, 0x51);
    std::vector<uint8_t> file(12 + count * 4, 0);
    memcpy(file.data(), ".MES", 4);
    MesWr32(file.data() + 8, count);
    auto add = [&](uint32_t id, const std::vector<uint16_t>& words) {
        MesWr32(file.data() + 12 + id * 4, (uint32_t)file.size());
        std::vector<uint8_t> b = Bytes(words);
        file.insert(file.end(), b.begin(), b.end());
    };
    const std::string tmplText = "{SET_COLOR color=135, 240, 120}Since{NEXT_SEGMENT}**{NEXT_FRAME id=0}moved.{RESET_COLOR}";
    const std::vector<uint16_t> tmpl = Enc(tmplText, Orig(0x38, 0, 0)).w;
    add(0, Enc("Zero {ICON id=9}", talk).w);
    add(1, {1, 1, 0, 0, 1, 0xFFF6, 1, 0x21, 1, 0x22, 0xFFFF});      // ruby text
    add(3, {1, 1, 0, 0, 1, 0xFFF4, 0xFFFF, 0x21, 0xFFFF});          // a parameter the game's walk takes for the end
    add(0x3B70, tmpl);
    add(5, {1, 7, 0x8000, 9, 77, 0x21, 0x127, 0xFFFE, 0, 0xFFFF});  // a wrong line count and width, and a cell without a character
    add(4, Enc("Last", talk).w);
    MesFile f;
    CHECK(MesOpen(file.data(), (uint32_t)file.size(), &f));
    std::vector<uint32_t> order(count), spanEnd(count);
    const uint32_t n = MesPhysicalOrder(f, order.data());
    CHECK(n == 6);
    uint8_t icons[32] = {};
    MesIndex(f, order.data(), n, spanEnd.data(), icons);
    CHECK(icons[1] == 2 && icons[0] == 0 && spanEnd[4] == file.size() && spanEnd[0] == MesOffset(f, 1));

    std::vector<uint8_t> out(kMesMaxWords * 2), scratch(kMesMaxWords * 2);
    uint32_t words = 0;
    bool same = false;
    MesErrorInfo info;
    auto enc = [&](uint32_t id, const std::string& text) {
        return MesEncodeFor(f, spanEnd.data(), id, text.data(), text.data() + text.size(), g_map, icons, out.data(), scratch.data(), &words, &same, &info);
    };
    CHECK(enc(0, "New {ICON id=9}text") == kMesOk && !same && words == 16 && MesRd16(out.data()) == 1 && MesRd16(out.data() + 4) == 0x8000);
    CHECK(enc(0, "Zero {ICON id=9}") == kMesOk && same);
    CHECK(enc(0, "Zero {ICON id=9} ") == kMesOk && !same && enc(0, "Zero{ICON id=9}") == kMesOk && !same);
    CHECK(enc(0, "New {ICON id=3}") == kMesBadIcon && info.value == 3);  // the file never uses icon 3
    CHECK(enc(count, "x") == kMesNoSuchId && info.value == count - 1);
    CHECK(enc(2, "x") == kMesNoRecord);
    CHECK(enc(1, "x") == kMesNotHandled && enc(3, "x") == kMesNotHandled);
    CHECK(enc(4, "Fine") == kMesOk && !same && enc(4, "Last") == kMesOk && same && enc(4, "Lasu") == kMesOk && !same);
    // the record with the wrong width word: its own text is "the same", and what is encoded has the width put right
    CHECK(enc(5, "A{UNK:0x0127}{NEXT_SEGMENT}") == kMesOk && same && MesRd16(out.data() + 2) == 2 && MesRd16(out.data() + 8) == 3);
    CHECK(enc(4, std::string(kMesMaxLineGlyphs, 'a')) == kMesOk);
    std::string big;
    for (int i = 0; i < 12; ++i) big += std::string(kMesMaxLineGlyphs, 'a') + "{NEXT_SEGMENT}";
    CHECK(enc(4, big) == kMesTooLong && info.value == kMesMaxWords);
    // the template: as long as it was is fine, one word more is not
    CHECK(enc(0x3B70, tmplText) == kMesOk && same && words == tmpl.size());
    CHECK(enc(0x3B70, "{SET_COLOR color=135, 240, 120}Now{NEXT_SEGMENT}**{NEXT_FRAME id=0}it moved{RESET_COLOR}") == kMesOk && !same);
    CHECK(enc(0x3B70, "{SET_COLOR color=135, 240, 120}Since{NEXT_SEGMENT}**{NEXT_FRAME id=0}moved..{RESET_COLOR}") == kMesTemplateGrew && info.value == tmpl.size());
    CHECK(MesIsTemplate(0x3B73) && !MesIsTemplate(0x3B71));
    // the template's "**" places: no more, no fewer, and not a line ten wide that starts with '*'
    CHECK(MesTemplateSlots(Bytes(tmpl).data(), (uint32_t)tmpl.size()) == 1);
    CHECK(enc(0x3B70, "{SET_COLOR color=135, 240, 120}Si{NEXT_SEGMENT}*{NEXT_FRAME id=0}moved.{RESET_COLOR}") == kMesTemplateSlots && info.value == 1);
    CHECK(enc(0x3B70, "{SET_COLOR color=1, 2, 3}**{NEXT_SEGMENT}**{NEXT_FRAME id=0}x{RESET_COLOR}") == kMesTemplateSlots);
    CHECK(enc(0x3B70, "*123456789{NEXT_SEGMENT}**{NEXT_FRAME id=0}x") == kMesTemplateSlots);  // width word 10, then '*'
    CHECK(enc(0x3B70, "*12345678{NEXT_SEGMENT}**{NEXT_FRAME id=0}x") == kMesOk);
    CHECK(enc(4, "*123456789") == kMesOk);  // any other record may
}

static std::vector<uint8_t> ReadAll(const char* path) {
    std::vector<uint8_t> data;
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        printf("cannot open %s\n", path);
        ++g_failed;
        return data;
    }
    uint8_t chunk[65536];
    for (size_t got; (got = fread(chunk, 1, sizeof(chunk), fp)) > 0;) data.insert(data.end(), chunk, chunk + got);
    fclose(fp);
    return data;
}

// The loaded file as a mes_all.json, the way the mod's TextDump writes it.
static std::string FileAsJson(const MesFile& f, const std::vector<uint32_t>& spanEnd, uint32_t* understood) {
    std::string json = "[\r\n";
    std::vector<char> text(1 << 16), element(1 << 18);
    *understood = 0;
    for (uint32_t id = 0; id < f.count; ++id) {
        const uint32_t off = MesOffset(f, id);
        MesText t = {element.data(), (uint32_t)element.size(), 0};
        MesOriginal o;
        if (!off) {
            MesJsonRecordOut(t, 0, 0, 0, 0, 0, "", 0, true);
        } else if (!MesReadOriginal(f.data + off, (spanEnd[id] - off) / 2, &o, nullptr)) {
            MesJsonRecordOut(t, MesRd16(f.data + off), MesRd16(f.data + off + 2), MesRd16(f.data + off + 4), MesRd16(f.data + off + 6), 0, "", 0, false);
        } else {
            const uint32_t len = MesDecode(f.data + off, o.words, text.data(), (uint32_t)text.size());
            CHECK(len <= text.size());
            MesJsonRecordOut(t, o.w0, o.w1, o.w2, o.w3, MesRd16(f.data + off + 8), text.data(), len, false);
            ++*understood;
        }
        CHECK(t.n <= element.size());
        json.append(element.data(), t.n);
        json += id + 1 < f.count ? ",\r\n" : "\r\n";
    }
    return json + "]";
}

static void Replace(std::string& s, const std::string& what, const std::string& with) {
    for (size_t at = 0; (at = s.find(what, at)) != std::string::npos; at += with.size()) s.replace(at, what.size(), with);
}

static MesJsonVerdict JudgeJson(const MesFile& f, const std::vector<uint32_t>& spanEnd, const std::vector<Element>& el, MesJsonCheck* check, uint16_t* br) {
    *check = {};
    for (uint32_t i = 0; i < el.size(); ++i) {
        MesOriginal o;
        MesErrorInfo info;
        const bool known = MesOriginalOf(f, spanEnd.data(), i, &o, &info) == kMesOk;
        MesJsonCheckAdd(*check, i, el[i].r, el[i].text.data(), (uint32_t)el[i].text.size(), known ? &o : nullptr);
    }
    uint32_t kind;
    return MesJsonJudge(*check, &kind, br);
}

static void TestRealFile(const char* path) {
    const std::vector<uint8_t> file = ReadAll(path);
    MesFile f;
    CHECK(MesOpen(file.data(), (uint32_t)file.size(), &f));
    if (g_failed) return;
    std::vector<uint32_t> order(f.count), spanEnd(f.count);
    const uint32_t n = MesPhysicalOrder(f, order.data());
    CHECK(n != 0xFFFFFFFF && n > 0);
    if (g_failed) return;
    uint8_t icons[32] = {};
    MesIndex(f, order.data(), n, spanEnd.data(), icons);

    // the file as JSON, the JSON read back
    uint32_t understood = 0;
    const std::string json = FileAsJson(f, spanEnd, &understood);
    std::vector<Element> el;
    CHECK(ReadJson(json, &el) && el.size() == f.count);
    if (el.size() != f.count) return;

    // held against the file it came from: every header word and every marker count agrees
    MesJsonCheck check;
    uint16_t br = 1;
    CHECK(JudgeJson(f, spanEnd, el, &check, &br) == kJsonGood && br == 0);
    CHECK(check.elements == f.count && check.compared == understood && check.headerDiffers == 0);
    for (uint32_t k = kKindSegment; k <= kKindSpacing; ++k) CHECK(check.agree[k] == check.either[k] && check.either[k] > 0);

    // every element encoded for its record: the same words (the record stays byte for byte), and Id1.. are its header
    std::vector<uint8_t> out(kMesMaxWords * 2), scratch(kMesMaxWords * 2), normal(kMesMaxWords * 2);
    uint32_t refused = 0, differ = 0, stockWrong = 0, linesWrong = 0, empties = 0, unknownCells = 0, sizeBack = 0;
    for (uint32_t id = 0; id < f.count; ++id) {
        const Element& e = el[id];
        const uint32_t off = MesOffset(f, id);
        if (!off) {
            empties += e.r.empty;
            continue;
        }
        MesOriginal o;
        MesErrorInfo info;
        if (MesOriginalOf(f, spanEnd.data(), id, &o, &info) != kMesOk) continue;
        if (e.r.id1 != o.w0 || e.r.id2 != o.w1 || e.r.flag1 != o.w2 || e.r.flag2 != o.w3 || (uint32_t)e.r.firstSegment != MesRd16(f.data + off + 8)) ++differ;
        uint32_t words = 0;
        bool same = false;
        const MesError err = MesEncodeFor(f, spanEnd.data(), id, e.text.data(), e.text.data() + e.text.size(), g_map, icons, out.data(), scratch.data(),
                                          &words, &same, &info);
        if (err != kMesOk) {
            if (++refused <= 5) printf("  record 0x%X: refused with error %d (value %u) at column %u\n", id, (int)err, info.value, info.column);
            continue;
        }
        MesNormalise(f.data + off, o.words, normal.data());
        if (!same || info.padded || words != o.words || memcmp(out.data(), normal.data(), words * 2) != 0) {
            if (++differ <= 5) printf("  record 0x%X: its own text does not give it back\n", id);
            continue;
        }
        if (memcmp(out.data(), f.data + off, words * 2) != 0) ++stockWrong;
        linesWrong += o.lines != o.w1;
        unknownCells += e.text.find("{UNK:0x0") != std::string::npos;
        sizeBack += e.text.find("{UNK:0xFFF0}") != std::string::npos;
    }
    printf("  %s: %u record numbers, %u records (%u understood), %u empty; refused %u; differ %u; stock width or line count wrong in %u "
           "(line count in %u); records with cells that have no character %u, with the code FFF0 %u\n",
           path, f.count, n, understood, empties, refused, differ, stockWrong, linesWrong, unknownCells, sizeBack);
    CHECK(differ == 0 && refused == 0 && understood == n && empties == f.count - n);

    // a JSON that does not belong to the file (one element missing at the front), and one whose markers are another tool's
    std::vector<Element> shifted(el.begin() + 1, el.end());
    CHECK(JudgeJson(f, spanEnd, shifted, &check, &br) == kJsonNotThisMes);
    std::vector<Element> renamed = el;
    for (Element& e : renamed) {
        Replace(e.text, "{NEXT_SEGMENT}", "{TMP}");
        Replace(e.text, "{RESET_COLOR}", "{NEXT_SEGMENT}");
        Replace(e.text, "{TMP}", "{RESET_COLOR}");
    }
    CHECK(JudgeJson(f, spanEnd, renamed, &check, &br) == kJsonMarkersDiffer);
    // {BR} written wherever the file has FFF0: established as that code, and such a text then gives its record back
    std::vector<Element> withBr = el;
    for (Element& e : withBr) Replace(e.text, "{UNK:0xFFF0}", "{BR}");
    CHECK(JudgeJson(f, spanEnd, withBr, &check, &br) == kJsonGood && br == (sizeBack ? kMesSizeBack : 0));
    if (sizeBack) {
        g_map.brCode = br;
        uint32_t brSame = 0, brTexts = 0;
        for (uint32_t id = 0; id < f.count; ++id) {
            if (withBr[id].text.find("{BR}") == std::string::npos) continue;
            uint32_t words = 0;
            bool same = false;
            MesErrorInfo info;
            ++brTexts;
            brSame += MesEncodeFor(f, spanEnd.data(), id, withBr[id].text.data(), withBr[id].text.data() + withBr[id].text.size(), g_map, icons,
                                   out.data(), scratch.data(), &words, &same, &info) == kMesOk && same;
        }
        CHECK(brTexts == sizeBack && brSame == brTexts);
        g_map.brCode = 0;
    }

    // a translation: every 7th record of a two-line style gets Ukrainian text through the tool's default glyph map, with the
    // markers a translator works with; the neighbours stay whole and the walk to "the next record" gives the same ids
    GlyphMap saved = g_map;
    GlyphMapToolDefaults(g_map);
    std::vector<MesOverride> some(f.count);
    std::vector<std::vector<uint8_t>> longer(f.count);
    const std::string ukr = "\xD0\xA6\xD0\xB5\xD0\xB9 \xD1\x80\xD1\x8F\xD0\xB4\xD0\xBE\xD0\xBA \xD0\xB7\xD1\x80\xD0\xBE\xD0\xB1\xD0\xB8\xD0\xB2 "
                            "\xD0\xB7\xD0\xB0\xD0\xBF\xD0\xB8\xD1\x81 \xD0\xB4\xD0\xBE\xD0\xB2\xD1\x88\xD0\xB8\xD0\xBC \xE2\x80\x94 \xD1\x97\xD1\x94\xD2\x91.";
    const std::string translated = ukr + "{NEXT_SEGMENT}" + ukr + "{NEXT_FRAME id=120}" + ukr;
    uint32_t replaced = 0;
    for (uint32_t k = 0; k < n; k += 7) {
        const uint32_t id = order[k];
        MesOriginal o;
        MesErrorInfo info;
        if (MesOriginalOf(f, spanEnd.data(), id, &o, &info) != kMesOk || MesThreeLineStyle(o.w0) || MesIsTemplate(id)) continue;
        uint32_t words = 0;
        bool same = true;
        const MesError err = MesEncodeFor(f, spanEnd.data(), id, translated.data(), translated.data() + translated.size(), g_map, icons, out.data(),
                                          scratch.data(), &words, &same, &info);
        CHECK(err == kMesOk && !same && info.padded == 0);
        if (err != kMesOk) continue;
        // w0, w2, w3 kept; three lines; each line 36 narrow cells wide
        CHECK(MesRd16(out.data()) == o.w0 && MesRd16(out.data() + 2) == 3 && MesRd16(out.data() + 4) == o.w2 && MesRd16(out.data() + 6) == o.w3 &&
              MesRd16(out.data() + 8) == 36 && words == 4 + 3 * 38 + 1);
        longer[id].assign(out.begin(), out.begin() + words * 2);
        some[id] = {longer[id].data(), words * 2};
        ++replaced;
    }
    g_map = saved;
    CHECK(replaced > 0 || n < 8);
    const uint32_t size2 = MesMerge(f, order.data(), n, some.data(), nullptr, 0);
    std::vector<uint8_t> merged2(size2);
    MesFile m;
    std::vector<uint32_t> order2(f.count);
    CHECK(MesMerge(f, order.data(), n, some.data(), merged2.data(), size2) == size2);
    CHECK(MesOpen(merged2.data(), size2, &m) && MesPhysicalOrder(m, order2.data()) == n && order == order2);
    uint32_t wrong = 0;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t id = order[k], off = MesOffset(f, id), span = MesSpanEnd(f, order.data(), n, k) - off;
        const uint32_t off2 = MesOffset(m, id), span2 = MesSpanEnd(m, order2.data(), n, k) - off2;
        if (some[id].data) wrong += span2 != some[id].bytes || memcmp(merged2.data() + off2, some[id].data, span2) != 0;
        else wrong += span2 != span || memcmp(merged2.data() + off2, file.data() + off, span) != 0;
    }
    CHECK(wrong == 0);
    uint32_t walk = 0;
    for (uint32_t k = 0; k + 4 < n; k += 3)
        for (int steps = 1; steps <= 3; ++steps) walk += IdAfter(merged2, order[k], steps) != IdAfter(file, order[k], steps);
    CHECK(walk == 0);

    // nothing replaced: the merged file is the loaded file
    std::vector<MesOverride> none(f.count);
    std::vector<uint8_t> merged(file.size());
    CHECK(MesMerge(f, order.data(), n, none.data(), merged.data(), (uint32_t)merged.size()) == file.size() && merged == file);
}

int main(int argc, char** argv) {
    GlyphMapDefault(g_map);
    for (int icon : {0, 1, 2, 3, 9, 16}) g_icons[icon >> 3] |= (uint8_t)(1 << (icon & 7));
    TestGlyphMap();
    TestLines();
    TestEncode();
    TestDecode();
    TestReadOriginal();
    TestJson();
    TestJudge();
    TestMerge();
    TestEncodeFor();
    if (argc > 1 && argv[1][0]) TestRealFile(argv[1]);
    printf("%s (%d checks)\n", g_failed ? "MES TEST FAILED" : "MES TEST PASSED", g_checked);
    return g_failed ? 1 : 0;
}
