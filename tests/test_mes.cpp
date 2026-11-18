// Host test of src/mes.h: the tagged text encoder and decoder, the line parsers and the merged MES file.
//   test_mes                              the built-in cases (small files made here)
//   test_mes glyphmap.txt                 also: the example glyph map of docs/ says what the built-in map says
//   test_mes glyphmap.txt MES_ALL.MES     also: every record of a real file as text and back, and the file merged from its own text
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

static MesOriginal Orig(uint16_t w0, uint16_t w2, uint16_t w3) {
    MesOriginal o = {};
    o.w0 = w0;
    o.w2 = w2;
    o.w3 = w3;
    return o;
}

struct Encoded {
    MesError error;
    MesErrorInfo info;
    std::vector<uint16_t> w;
};

static Encoded Enc(const char* text, const MesOriginal& o, uint32_t cap = kMesMaxWords) {
    Encoded e;
    std::vector<uint8_t> buf(cap * 2 + 2, 0xEE);
    uint32_t words = 0;
    e.error = MesEncode(text, text + strlen(text), g_map, o, g_icons, buf.data(), cap, &words, &e.info);
    CHECK(buf[cap * 2] == 0xEE && buf[cap * 2 + 1] == 0xEE);  // nothing beyond the limit
    if (e.error == kMesOk)
        for (uint32_t i = 0; i < words; ++i) e.w.push_back((uint16_t)MesRd16(buf.data() + i * 2));
    return e;
}

static bool Same(const std::vector<uint16_t>& a, std::initializer_list<uint16_t> b) {
    return a == std::vector<uint16_t>(b);
}

static std::vector<uint8_t> Bytes(const std::vector<uint16_t>& w) {
    std::vector<uint8_t> b(w.size() * 2);
    for (size_t i = 0; i < w.size(); ++i) MesWr16(b.data() + i * 2, w[i]);
    return b;
}

static std::string Dec(const std::vector<uint16_t>& w) {
    std::vector<uint8_t> b = Bytes(w);
    std::vector<char> out(8192);
    uint32_t n = MesDecode(b.data(), (uint32_t)w.size(), g_map, out.data(), (uint32_t)out.size());
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
    CHECK(g_map.cell['A'] == 0x21 && g_map.cell['a'] == 0x41 && g_map.cell[' '] == 0 && g_map.cell['~'] == 0x5E);
    CHECK(g_map.cell[0x7F] == kMesNoCell && g_map.cell[0xA0] == kMesNoCell && g_map.cell[0xAD] == kMesNoCell &&
          g_map.cell[0xAF] == kMesNoCell);
    CHECK(g_map.cell[0xA1] == 0x80 && g_map.cell[0xA8] == 0x87 && g_map.cell[0xA9] == 0x89 && g_map.cell[0xAE] == 0x8D &&
          g_map.cell[0xB0] == 0x8E && g_map.cell[0xBF] == 0x9D);
    CHECK(g_map.cell[0xC0] == 0x9E && g_map.cell[0xDF] == 0xBD && g_map.cell[0xE0] == 0xC0 && g_map.cell[0xFF] == 0xDF);
    CHECK(g_map.cell[0x152] == 0xE0 && g_map.cell[0x153] == 0xE1 && g_map.cell[0x221E] == 0xE2 && g_map.cell[0x266A] == 0xE3);
    CHECK(g_map.cell[0x0416] == kMesNoCell);
    CHECK(g_map.chr[0x21] == 'A' && g_map.chr[0xBE] == 0 && g_map.chr[0x88] == 0);
}

static void TestLines() {
    uint32_t id = 0, cp = 0, cell = 0;
    const char *t = nullptr, *te = nullptr;
    auto line = [&](const char* s) { return MesParseLine(s, s + strlen(s), &id, &t, &te); };
    CHECK(line("") == kLineEmpty && line("   \r") == kLineEmpty && line("; note = 1") == kLineEmpty);
    CHECK(line("0x1A2b = Hello there \r") == kLineEntry && id == 0x1A2B && std::string(t, te) == "Hello there");
    CHECK(line("  0X10=x=y") == kLineEntry && id == 0x10 && std::string(t, te) == "x=y");
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
    CHECK(Same(Enc("A{n}B", talk).w, {1, 2, 0x8000, 0x51, 1, 0x21, 0xFFFE, 1, 0x22, 0xFFFF}));
    CHECK(Same(Enc("A{N}", talk).w, {1, 2, 0x8000, 0x51, 1, 0x21, 0xFFFE, 0, 0xFFFF}));
    // a single line in front of {page} gets the empty second line; two lines do not
    CHECK(Same(Enc("A{page 90}B", talk).w, {1, 3, 0x8000, 0x51, 1, 0x21, 0xFFFE, 0, 0xFFF2, 90, 1, 0x22, 0xFFFF}));
    CHECK(Same(Enc("A{n}{page 90}B", talk).w, {1, 3, 0x8000, 0x51, 1, 0x21, 0xFFFE, 0, 0xFFF2, 90, 1, 0x22, 0xFFFF}));
    CHECK(Same(Enc("A{n}C{page 0x5A}B", talk).w, {1, 3, 0x8000, 0x51, 1, 0x21, 0xFFFE, 1, 0x23, 0xFFF2, 90, 1, 0x22, 0xFFFF}));
    CHECK(Enc("A{n}B{n}C{n}D{page 9}E", talk).error == kMesOk);  // four lines: two whole pages
    Encoded e = Enc("A{n}B{n}C{page 9}D", talk);
    CHECK(e.error == kMesPageLines && e.info.value == 3 && e.info.column == 9);
    // the styles with three-line windows: FFF2 exactly where it is written
    CHECK(Same(Enc("A{page 7}B", Orig(0x3E, 0, 0)).w, {0x3E, 2, 0, 0, 1, 0x21, 0xFFF2, 7, 1, 0x22, 0xFFFF}));
    CHECK(Enc("A{n}B{n}C{page 7}D", Orig(0x38, 0, 0)).error == kMesOk);

    // {page} without a number: the durations of the original record, in order
    MesOriginal timed = Orig(0xA, 0x8000, 0x104);
    timed.pages = 2;
    timed.duration[0] = 300;
    timed.duration[1] = 110;
    CHECK(Same(Enc("A{n}B{page}C{n}D{page}E", timed).w,
               {0xA, 5, 0x8000, 0x104, 1, 0x21, 0xFFFE, 1, 0x22, 0xFFF2, 300, 1, 0x23, 0xFFFE, 1, 0x24, 0xFFF2, 110, 1, 0x25, 0xFFFF}));
    e = Enc("A{page}B{page}C{page}D", timed);
    CHECK(e.error == kMesNoDuration && e.info.value == 3);
    CHECK(Enc("A{page}B{page}C{page 60}D", timed).error == kMesOk);
    // a record without the timer flag: 0, as every such FFF2 of the stock files
    CHECK(Same(Enc("A{n}B{page}C", Orig(0xA, 0, 0)).w, {0xA, 3, 0, 0, 1, 0x21, 0xFFFE, 1, 0x22, 0xFFF2, 0, 1, 0x23, 0xFFFF}));
    CHECK(Enc("A{page 0xFFF2}B", talk).error == kMesBadValue && Enc("A{page 65518}B", talk).error == kMesOk);
    CHECK(Enc("A{page x}B", talk).error == kMesBadValue && Enc("A{page 1 2}B", talk).error == kMesBadValue);

    // widths: a narrow cell 1, a wide one 2, an icon 2; colour and size add nothing
    CHECK(Same(Enc("a\xE2\x88\x9E", talk).w, {1, 1, 0x8000, 0x51, 3, 0x41, 0xE2, 0xFFFF}));
    CHECK(Same(Enc("{icon 9}a", talk).w, {1, 1, 0x8000, 0x51, 3, 0xFFF4, 9, 0x41, 0xFFFF}));
    CHECK(Same(Enc("{color 135,240,120}a{/color}", talk).w, {1, 1, 0x8000, 0x51, 1, 0xFFFA, 135, 240, 120, 0x41, 0xFFF9, 0xFFFF}));
    CHECK(Same(Enc("{Colour 1 2 3}a{/COLOUR}", talk).w, {1, 1, 0x8000, 0x51, 1, 0xFFFA, 1, 2, 3, 0x41, 0xFFF9, 0xFFFF}));
    CHECK(Same(Enc("{size 48}a{/size}", talk).w, {1, 1, 0x8000, 0x51, 1, 0xFFF1, 48, 0x41, 0xFFF0, 0xFFFF}));
    CHECK(Same(Enc("{FFF5}{n}", talk).w, {1, 2, 0x8000, 0x51, 0, 0xFFF5, 0xFFFE, 0, 0xFFFF}));
    CHECK(Same(Enc("{sp}a {sp}", talk).w, {1, 1, 0x8000, 0x51, 4, 0, 0x41, 0, 0, 0xFFFF}));
    CHECK(Same(Enc("{{x}}", talk).w, {1, 1, 0x8000, 0x51, 3, 0x5B, 0x58, 0x5D, 0xFFFF}));
    CHECK(Same(Enc("{#127}{#e2}{#0}", talk).w, {1, 1, 0x8000, 0x51, 5, 0x127, 0xE2, 0, 0xFFFF}));
    CHECK(Same(Enc("**", talk).w, {1, 1, 0x8000, 0x51, 2, 0x0A, 0x0A, 0xFFFF}));  // the number / item placeholder
    CHECK(Same(Enc("\xC3\x80\xC3\xA9", talk).w, {1, 1, 0x8000, 0x51, 2, 0x9E, 0xC9, 0xFFFF}));

    // refusals
    e = Enc("ab\xD0\x96", talk);
    CHECK(e.error == kMesNoGlyph && e.info.value == 0x416 && e.info.column == 2);
    CHECK(Enc("a\xD0", talk).error == kMesBadUtf8 && Enc("a\xC0\x80", talk).error == kMesBadUtf8 && Enc("\xFF", talk).error == kMesBadUtf8);
    CHECK(Enc("a\xF0\x9F\x98\x80", talk).error == kMesNoGlyph && Enc("a\tb", talk).error == kMesNoGlyph);
    CHECK(Enc("a{br}b", talk).error == kMesBadTag && Enc("a{n", talk).error == kMesBadTag && Enc("a{n{n}}", talk).error == kMesBadTag);
    CHECK(Enc("a{}b", talk).error == kMesBadTag && Enc("a{nn}b", talk).error == kMesBadTag && Enc("{#}", talk).error == kMesBadTag);
    CHECK(Enc("{#12G}", talk).error == kMesBadTag && Enc("{#12345}", talk).error == kMesBadTag && Enc("{n 1}", talk).error == kMesBadTag);
    CHECK(Enc("a}b", talk).error == kMesStrayBrace && Enc("}", talk).error == kMesStrayBrace);
    CHECK(Enc("{#980}", talk).error == kMesBadValue && Enc("{#97F}", talk).error == kMesOk);
    CHECK(Enc("{color 1 2}", talk).error == kMesBadValue && Enc("{color 1 2 256}", talk).error == kMesBadValue &&
          Enc("{color 1 2 3 4}", talk).error == kMesBadValue && Enc("{color}", talk).error == kMesBadValue);
    CHECK(Enc("{size 7}", talk).error == kMesBadValue && Enc("{size 129}", talk).error == kMesBadValue && Enc("{size}", talk).error == kMesBadValue);
    e = Enc("{icon 11}", talk);
    CHECK(e.error == kMesBadIcon && e.info.value == 11);
    CHECK(Enc("{icon 300}", talk).error == kMesBadIcon && Enc("{icon}", talk).error == kMesBadValue);

    std::string longLine(kMesMaxLineGlyphs, 'a');
    CHECK(Enc(longLine.c_str(), talk).error == kMesOk);
    e = Enc((longLine + "a").c_str(), talk);
    CHECK(e.error == kMesLineTooLong && e.info.value == kMesMaxLineGlyphs + 1);
    CHECK(Enc((longLine + "{n}" + longLine).c_str(), talk).error == kMesOk);
    // the word limit: 4 header words, N, 3 glyphs, FFFF = 9
    CHECK(Enc("abc", talk, 9).error == kMesOk && Enc("abc", talk, 8).error == kMesTooLong && Enc("abc", talk, 1).error == kMesTooLong);
}

static void TestDecode() {
    const MesOriginal talk = Orig(1, 0x8000, 0x51);
    const char* texts[] = {
        "Hi", "", "A{n}B", "A{n}{page 90}B", "{color 135 240 120}Since{/color}{n}**{page 0}x",
        "{sp}lead and trail{sp}", "{sp}", "{sp} {sp}", "a  b", "{{x}}", "{#127}a{#232}", "{icon 9}a{size 48}b{/size}",
        "{FFF5}{n}", "\xC3\x80\xC3\xA9\xE2\x88\x9E", "a{n}{n}{n}b", "x{page 5}{page 6}y{n}",
    };
    for (const char* text : texts) {
        Encoded a = Enc(text, talk);
        CHECK(a.error == kMesOk);
        std::string back = Dec(a.w);
        Encoded b = Enc(back.c_str(), talk);
        if (b.error != kMesOk || a.w != b.w) printf("  round trip of \"%s\" gave \"%s\"\n", text, back.c_str());
        CHECK(b.error == kMesOk && a.w == b.w);
    }
    CHECK(Dec(Enc("A{n}B", talk).w) == "A{n}B");
    CHECK(Dec(Enc("A{page 90}B", talk).w) == "A{n}{page 90}B");
    CHECK(Dec(Enc(" a ", talk).w) == "{sp}a{sp}");  // blanks at the ends come back as tags: the reader of a line trims
    CHECK(Dec({1, 1, 0, 0, 3, 0, 0x41, 0, 0xFFFF}) == "{sp}a{sp}");
    CHECK(Dec({1, 1, 0, 0, 2, 0x5F, 0x103, 0xFFFF}) == "{#5F}{#103}");  // cells the map has no character for

    // a character that was given another cell is not used for its old cell
    GlyphMap saved = g_map;
    GlyphMapSet(g_map, 'A', 0x30);
    CHECK(Dec({1, 1, 0, 0, 2, 0x21, 0x30, 0xFFFF}) == "{#21}A");
    g_map = saved;

    // a buffer that is too small: the length is still reported, nothing is written beyond it
    std::vector<uint8_t> rec = Bytes(Enc("Hello{n}there", talk).w);
    char small[5] = {1, 1, 1, 1, 1};
    CHECK(MesDecode(rec.data(), (uint32_t)rec.size() / 2, g_map, small, 4) == 13 && small[4] == 1);
}

static void TestReadOriginal() {
    MesOriginal o;
    uint8_t icons[32] = {};
    std::vector<uint8_t> r = Bytes({0xA, 4, 0x8000, 0x104, 1, 0x21, 0xFFFE, 2, 0xFFF4, 9, 0xFFF2, 300, 3, 0xFFFA, 1, 2, 3, 0x41, 0xFFF1, 48, 0x42,
                                    0xFFF0, 0xFFF9, 0x43, 0xFFFE, 0, 0xFFF5, 0xFFFF, 0x1234});
    CHECK(MesReadOriginal(r.data(), (uint32_t)r.size() / 2, &o, icons));
    CHECK(o.w0 == 0xA && o.w1 == 4 && o.w2 == 0x8000 && o.w3 == 0x104 && o.words == 28 && o.lines == 4 && o.pages == 1 && o.duration[0] == 300);
    CHECK(icons[1] == 2);
    CHECK(MesRecordBytes(r.data(), (uint32_t)r.size()) == 56);
    CHECK(!MesReadOriginal(r.data(), 27, &o, nullptr));  // the FFFF is outside the limit
    CHECK(!MesReadOriginal(Bytes({1, 1, 0, 0, 1, 0xFFF6, 0, 0, 0xFFFF}).data(), 9, &o, nullptr));   // ruby text
    CHECK(!MesReadOriginal(Bytes({1, 1, 0, 0, 1, 0xFFF3, 0xFFFF}).data(), 7, &o, nullptr));        // an unused code
    CHECK(!MesReadOriginal(Bytes({1, 1, 0xFFFE, 0, 1, 0x21, 0xFFFF}).data(), 7, &o, nullptr));     // a line end in the header
    CHECK(!MesReadOriginal(Bytes({1, 1, 0, 0, 1, 0xFFFA, 1, 0xFFFF, 3, 0xFFFF}).data(), 10, &o, nullptr));  // a line end as a parameter
    CHECK(!MesReadOriginal(Bytes({1, 1, 0, 0, 1, 0xFFFA, 1, 2}).data(), 8, &o, nullptr));          // cut inside the parameters
    CHECK(MesRecordBytes(Bytes({1, 2, 3}).data(), 6) == 0);
}

// A small file: 8 ids, the records out of id order (3, 0, 5, 1, 6), ids 2 and 7 without a record, id 4 sharing the record
// of id 1, two words nobody points at behind the record of id 5 and three bytes behind the last record.
static std::vector<uint8_t> MakeFile(std::vector<std::vector<uint16_t>>* records) {
    const MesOriginal talk = Orig(1, 0x8000, 0x51);
    std::vector<std::vector<uint16_t>> rec(8);
    rec[3] = Enc("Three{n}lines{page 60}here", talk).w;
    rec[0] = Enc("Zero", Orig(0xB, 0, 0)).w;
    rec[5] = Enc("{icon 9}Five", talk).w;
    rec[1] = Enc("One{n}", talk).w;
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
    std::vector<uint8_t> new3 = Bytes(Enc("Three is much longer now{n}than it was{page 60}and has{n}one more line", talk).w);
    std::vector<uint8_t> new5 = Bytes(Enc("5", talk).w);
    std::vector<uint8_t> new4 = Bytes(Enc("Shared{n}record", talk).w);
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

// The checks that need the file: the id, its record, the two template records, the icons the file itself uses.
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
    const std::vector<uint16_t> tmpl = Enc("{color 135 240 120}Since{n}**{page 0}moved.{/color}", Orig(0x38, 0, 0)).w;
    add(0, Enc("Zero {icon 9}", talk).w);
    add(1, {1, 1, 0, 0, 1, 0xFFF6, 1, 0x21, 1, 0x22, 0xFFFF});      // ruby text
    add(3, {1, 1, 0, 0, 1, 0xFFF4, 0xFFFF, 0x21, 0xFFFF});          // a parameter the game's walk takes for the end
    add(0x3B70, tmpl);
    add(4, Enc("Last", talk).w);
    MesFile f;
    CHECK(MesOpen(file.data(), (uint32_t)file.size(), &f));
    std::vector<uint32_t> order(count), spanEnd(count);
    const uint32_t n = MesPhysicalOrder(f, order.data());
    CHECK(n == 5);
    uint8_t icons[32] = {};
    MesIndex(f, order.data(), n, spanEnd.data(), icons);
    CHECK(icons[1] == 2 && icons[0] == 0 && spanEnd[4] == file.size() && spanEnd[0] == MesOffset(f, 1));

    std::vector<uint8_t> out(kMesMaxWords * 2);
    uint32_t words = 0;
    MesErrorInfo info;
    auto enc = [&](uint32_t id, const std::string& text) {
        return MesEncodeFor(f, spanEnd.data(), id, text.data(), text.data() + text.size(), g_map, icons, out.data(), &words, &info);
    };
    CHECK(enc(0, "New {icon 9}text") == kMesOk && words == 16 && MesRd16(out.data()) == 1 && MesRd16(out.data() + 4) == 0x8000);
    CHECK(enc(0, "New {icon 3}") == kMesBadIcon && info.value == 3);  // the file never uses icon 3
    CHECK(enc(count, "x") == kMesNoSuchId && info.value == count - 1);
    CHECK(enc(2, "x") == kMesNoRecord);
    CHECK(enc(1, "x") == kMesNotHandled && enc(3, "x") == kMesNotHandled);
    CHECK(enc(4, "Fine") == kMesOk);
    CHECK(enc(4, std::string(kMesMaxLineGlyphs, 'a')) == kMesOk);
    std::string big;
    for (int i = 0; i < 12; ++i) big += std::string(kMesMaxLineGlyphs, 'a') + "{n}";
    CHECK(enc(4, big) == kMesTooLong && info.value == kMesMaxWords);
    // the template: as long as it was is fine, one word more is not
    CHECK(enc(0x3B70, "{color 135 240 120}Since{n}**{page 0}moved.{/color}") == kMesOk && words == tmpl.size());
    CHECK(enc(0x3B70, "{color 135 240 120}Now{n}**{page 0}it moved{/color}") == kMesOk);
    CHECK(enc(0x3B70, "{color 135 240 120}Since{n}**{page 0}moved..{/color}") == kMesTemplateGrew && info.value == tmpl.size());
    CHECK(MesIsTemplate(0x3B73) && !MesIsTemplate(0x3B71));
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

// docs/glyphmap.example.txt lists the built-in cells: every line of it must parse and agree with GlyphMapDefault.
static void TestExampleMap(const char* path) {
    const std::vector<uint8_t> data = ReadAll(path);
    const char* p = (const char*)data.data();
    const char* end = p + data.size();
    int entries = 0, bad = 0, differ = 0;
    while (p < end) {
        const char* line = p;
        while (p < end && *p != '\n') ++p;
        uint32_t cp = 0, cell = 0;
        const MesLine kind = GlyphMapParseLine(line, p, &cp, &cell);
        if (p < end) ++p;
        if (kind == kLineBad) ++bad;
        if (kind != kLineEntry) continue;
        ++entries;
        differ += g_map.cell[cp] != cell;
    }
    printf("  %s: %d entries, %d lines not understood, %d differ from the built-in map\n", path, entries, bad, differ);
    CHECK(entries == 95 + 8 + 21 + 32 + 32 + 4 && bad == 0 && differ == 0);
}

// A record with its N words and its line count made what they should be (a few stock records have them wrong).
static std::vector<uint8_t> Normalised(const uint8_t* rec, uint32_t words) {
    std::vector<uint8_t> r(rec, rec + words * 2);
    uint32_t p = 4, lines = 0;
    for (;;) {
        const uint32_t nAt = p++;
        uint32_t width = 0, c;
        ++lines;
        for (;;) {
            c = MesRd16(rec + p * 2);
            ++p;
            if (c < kMesFirstControl) width += c < kMesWideFrom ? 1 : 2;
            else if (c == kMesIcon) { width += 2; ++p; }
            else if (c == kMesSize) ++p;
            else if (c == kMesColour) p += 3;
            else if (c == kMesNewLine || c == kMesPage || c == kMesEnd) break;
        }
        MesWr16(r.data() + nAt * 2, width);
        if (c == kMesPage) ++p;
        if (c == kMesEnd) break;
    }
    MesWr16(r.data() + 2, lines);
    return r;
}

static void TestRealFile(const char* path) {
    const std::vector<uint8_t> file = ReadAll(path);
    MesFile f;
    CHECK(MesOpen(file.data(), (uint32_t)file.size(), &f));
    if (g_failed) return;
    std::vector<uint32_t> order(f.count);
    const uint32_t n = MesPhysicalOrder(f, order.data());
    CHECK(n != 0xFFFFFFFF && n > 0);
    if (g_failed) return;

    uint8_t icons[32] = {};
    uint32_t unread = 0, wrongWidth = 0, moreInSpan = 0;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t off = MesOffset(f, order[k]), span = MesSpanEnd(f, order.data(), n, k) - off;
        MesOriginal o;
        if (!MesReadOriginal(file.data() + off, span / 2, &o, icons)) { ++unread; continue; }
        if (o.words * 2 != span || MesRecordBytes(file.data() + off, span) != span) ++moreInSpan;
    }
    memcpy(g_icons, icons, 32);

    // every record: text, back to words, and compared with the record itself
    std::vector<std::vector<uint8_t>> again(f.count);
    std::vector<MesOverride> ov(f.count);
    uint32_t refused = 0, differ = 0, pageRule = 0;
    std::vector<char> text(1 << 16);
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t id = order[k], off = MesOffset(f, id), span = MesSpanEnd(f, order.data(), n, k) - off;
        MesOriginal o;
        if (!MesReadOriginal(file.data() + off, span / 2, &o, nullptr)) continue;
        const uint32_t len = MesDecode(file.data() + off, o.words, g_map, text.data(), (uint32_t)text.size());
        CHECK(len <= text.size());
        std::vector<uint8_t> buf(kMesMaxWords * 2);
        uint32_t words = 0;
        MesErrorInfo info;
        const MesError e = MesEncode(text.data(), text.data() + len, g_map, o, g_icons, buf.data(), kMesMaxWords, &words, &info);
        if (e != kMesOk) {
            if (e == kMesPageLines) ++pageRule;
            if (++refused <= 5) printf("  id 0x%X: refused with error %d (value %u) at column %u\n", id, (int)e, info.value, info.column);
            continue;
        }
        buf.resize(words * 2);
        const std::vector<uint8_t> normal = Normalised(file.data() + off, o.words);
        if (buf != normal) {
            if (++differ <= 5) printf("  id 0x%X: the words differ after text and back\n", id);
            continue;
        }
        if (memcmp(buf.data(), file.data() + off, buf.size()) != 0) ++wrongWidth;
        again[id] = buf;
        ov[id] = {again[id].data(), (uint32_t)again[id].size()};
    }
    printf("  %s: %u ids, %u records; not understood %u; a span with more than its record %u; refused %u (page rule %u); "
           "differ %u; stock width or line count wrong %u\n",
           path, f.count, n, unread, moreInSpan, refused, pageRule, differ, wrongWidth);
    CHECK(differ == 0);
    CHECK(refused == 0);

    // the whole file built from its own text: the same size and, but for the corrected width words, the same bytes
    const uint32_t size = MesMerge(f, order.data(), n, ov.data(), nullptr, 0);
    CHECK(size == file.size());
    std::vector<uint8_t> merged(size);
    CHECK(MesMerge(f, order.data(), n, ov.data(), merged.data(), size) == size);
    uint32_t changed = 0;
    for (uint32_t i = 0; i < size && i < file.size(); ++i) changed += merged[i] != file[i];
    CHECK(changed <= wrongWidth * 4);
    MesFile m;
    std::vector<uint32_t> order2(f.count);
    CHECK(MesOpen(merged.data(), size, &m) && MesPhysicalOrder(m, order2.data()) == n && order == order2);

    // every 7th record replaced by a longer text: the neighbours stay whole and the walk to "the next record" gives the same ids
    std::vector<MesOverride> some(f.count);
    std::vector<std::vector<uint8_t>> longer(f.count);
    const char* extra = "This line is here to make the record longer.{n}";
    for (uint32_t k = 0; k < n; k += 7) {
        const uint32_t id = order[k], off = MesOffset(f, id), span = MesSpanEnd(f, order.data(), n, k) - off;
        MesOriginal o;
        if (!MesReadOriginal(file.data() + off, span / 2, &o, nullptr) || MesThreeLineStyle(o.w0)) continue;
        std::string t = std::string(extra) + extra;
        longer[id].resize(kMesMaxWords * 2);
        uint32_t words = 0;
        MesErrorInfo info;
        if (MesEncode(t.data(), t.data() + t.size(), g_map, o, g_icons, longer[id].data(), kMesMaxWords, &words, &info) != kMesOk) continue;
        longer[id].resize(words * 2);
        some[id] = {longer[id].data(), words * 2};
    }
    const uint32_t size2 = MesMerge(f, order.data(), n, some.data(), nullptr, 0);
    std::vector<uint8_t> merged2(size2);
    CHECK(size2 > file.size() && MesMerge(f, order.data(), n, some.data(), merged2.data(), size2) == size2);
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
}

int main(int argc, char** argv) {
    GlyphMapDefault(g_map);
    for (int icon : {0, 1, 2, 3, 9, 16}) g_icons[icon >> 3] |= (uint8_t)(1 << (icon & 7));
    TestGlyphMap();
    TestLines();
    TestEncode();
    TestDecode();
    TestReadOriginal();
    TestMerge();
    TestEncodeFor();
    if (argc > 1 && argv[1][0]) TestExampleMap(argv[1]);
    if (argc > 2 && argv[2][0]) TestRealFile(argv[2]);
    printf("%s (%d checks)\n", g_failed ? "MES TEST FAILED" : "MES TEST PASSED", g_checked);
    return g_failed ? 1 : 0;
}
