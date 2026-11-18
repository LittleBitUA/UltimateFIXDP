// MES_ALL.MES, the game's one text file: reading it, the tagged text a translator writes for one record, and the merged
// file the text overlay hands to the game. No Windows headers and no C library: the host tests it (tests/test_mes.cpp).
//
// The file (all little endian; every rule below was read in DP.exe 1.01b, the addresses are where):
//     +0 ".MES"   +4 u32 1   +8 u32 count   +0Ch u32 offset[count]   then the records, one after another
//   offset[id] counts from the start of the file, 0 = the id has no record. The game finds a record as
//   base + offset[id] with no check at all (0045A597 and 16 more places).
// A record is a stream of 16-bit words:
//     w0  speaker / window style (0045A590)      w1  number of lines (the line loops of the drawer stop on it, 0045D04F)
//     w2  flags, 8000h = the pages run on a timer    w3  duration of the first page in frames (0 = 180)
//   then for every line:  N  codes...  end,  where N is the width of the line in half cells (a code below E2h is one,
//   E2h and above two, an icon two; the drawer aligns with it, 0045D45A) and the end is
//     FFFE      new line
//     FFF2 t    new line; t is the duration of the page that starts with the next line (0045A9ED)
//     FFFF      end of the record
//   Codes inside a line: below FFEFh a cell of the font atlas (64 columns x 38 rows), FFFA r g b colour, FFF9 colour back,
//   FFF1 s glyph size in pixels, FFF0 size back, FFF4 i button icon, FFF5 no parameter (as the word at +0Ah it marks a
//   record without a window background, 0045A630). FFF6 (ruby text) and the other codes do not occur in the western files
//   and are not handled here: a record with one is left alone.
// Three things the game does that the merged file has to respect:
//   - FUN_0045A700 (NPC talk) walks from a record over n FFFF words to "the n-th record after it" and then looks for the
//     id whose offset is that address, with no end to the search. So the records keep their physical order and every
//     replaced record stays in its own place.
//   - the line counter (0045A6C0), the page timer (0045A9D1) and the skip loops of the drawer (0045D08A) look at every
//     word for FFF2 / FFFE / FFFF without knowing about parameters: no parameter and no header word may have those values.
//   - a window page is two lines (0045F4FE; three for the styles 38h, 3Eh, 3Fh, 0045F4D3): in every stock record of a
//     two-line style each FFF2 comes after an even number of lines, a single line is followed by an empty one.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ufx {

constexpr uint16_t kMesEnd = 0xFFFF, kMesNewLine = 0xFFFE, kMesColour = 0xFFFA, kMesColourBack = 0xFFF9, kMesRuby = 0xFFF6,
                   kMesMarker = 0xFFF5, kMesIcon = 0xFFF4, kMesPage = 0xFFF2, kMesSize = 0xFFF1, kMesSizeBack = 0xFFF0,
                   kMesFirstControl = 0xFFEF;
constexpr uint32_t kMesAtlasCells = 64 * 38;  // cells 000h..97Fh; a code beyond them samples outside the texture
constexpr uint32_t kMesWideFrom = 0xE2;       // the drawer's own threshold (0045E8E2 and five more places)
constexpr uint32_t kMesMaxIds = 0x10000;
constexpr uint32_t kMesMaxPages = 64;         // page durations kept from the original record (the stock files have up to 19)
constexpr uint32_t kMesMaxWords = 4096;       // a replaced record, with its header (the longest stock record has 1114 words)
// One draw call holds 300h quads (0045A766) and a glyph of any colour but black takes two (its shadow), so a line of more
// than 384 glyphs can never be drawn whole.
constexpr uint32_t kMesMaxLineGlyphs = 384;
constexpr uint16_t kMesNoCell = 0xFFFF;

inline uint32_t MesRd16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
inline uint32_t MesRd32(const uint8_t* p) { return MesRd16(p) | (MesRd16(p + 2) << 16); }
inline void MesWr16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
inline void MesWr32(uint8_t* p, uint32_t v) {
    MesWr16(p, v);
    MesWr16(p + 2, v >> 16);
}

// ---- Characters ---------------------------------------------------------------------------------------------------
// The game has no character encoding: a text word is the number of an atlas cell. So the overlay needs a table from
// Unicode to cells, and that table belongs to the atlas in use.
struct GlyphMap {
    uint16_t cell[0x10000];        // by code point (the basic plane); kMesNoCell = no cell
    uint16_t chr[kMesAtlasCells];  // by cell: the character shown for it in a dump; 0 = none
};

inline void GlyphMapSet(GlyphMap& m, uint32_t cp, uint32_t cell) {
    m.cell[cp] = (uint16_t)cell;
    m.chr[cell] = (uint16_t)cp;
}

// The cells of the US / FR atlas (the same file): ASCII 20h..7Eh in the cells 00h..5Eh, the Latin-1 signs A1h..BFh in
// 80h..9Dh (the atlas has no cell for the soft hyphen, the macron and one more place, so the run is not a plain shift),
// the capitals C0h..DFh in 9Eh..BDh, the small letters E0h..FFh in C0h..DFh, then OE, oe, the infinity sign and the note.
inline void GlyphMapDefault(GlyphMap& m) {
    for (uint32_t i = 0; i < 0x10000; ++i) m.cell[i] = kMesNoCell;
    for (uint32_t i = 0; i < kMesAtlasCells; ++i) m.chr[i] = 0;
    for (uint32_t c = 0x20; c <= 0x7E; ++c) GlyphMapSet(m, c, c - 0x20);
    for (uint32_t c = 0xA1; c <= 0xA8; ++c) GlyphMapSet(m, c, c - 0x21);
    static const uint8_t signs[21] = {0xA9, 0xAA, 0xAB, 0xAC, 0xAE, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5,
                                      0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF};
    for (uint32_t i = 0; i < 21; ++i) GlyphMapSet(m, signs[i], 0x89 + i);
    for (uint32_t c = 0xC0; c <= 0xDF; ++c) GlyphMapSet(m, c, c - 0x22);
    for (uint32_t c = 0xE0; c <= 0xFF; ++c) GlyphMapSet(m, c, c - 0x20);
    GlyphMapSet(m, 0x0152, 0xE0);
    GlyphMapSet(m, 0x0153, 0xE1);
    GlyphMapSet(m, 0x221E, 0xE2);
    GlyphMapSet(m, 0x266A, 0xE3);
}

// One UTF-8 character. False at a byte sequence that is not valid UTF-8 (overlong forms and surrogates included).
inline bool Utf8Next(const char*& p, const char* end, uint32_t* cp) {
    const uint32_t b0 = (uint8_t)*p;
    uint32_t need, v, low;
    if (b0 < 0x80) { *cp = b0; ++p; return true; }
    if (b0 >= 0xC2 && b0 <= 0xDF) { need = 1; v = b0 & 0x1F; low = 0x80; }
    else if (b0 >= 0xE0 && b0 <= 0xEF) { need = 2; v = b0 & 0x0F; low = 0x800; }
    else if (b0 >= 0xF0 && b0 <= 0xF4) { need = 3; v = b0 & 0x07; low = 0x10000; }
    else return false;
    if ((uint32_t)(end - p) <= need) return false;
    for (uint32_t i = 1; i <= need; ++i) {
        const uint32_t b = (uint8_t)p[i];
        if ((b & 0xC0) != 0x80) return false;
        v = (v << 6) | (b & 0x3F);
    }
    if (v < low || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return false;
    p += need + 1;
    *cp = v;
    return true;
}

// A number: 0x and hexadecimal digits, or decimal digits. At most FFFFFFh.
inline bool MesParseNumber(const char*& p, const char* end, uint32_t* out) {
    uint32_t v = 0, digits = 0;
    if (end - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        for (p += 2; p < end; ++p, ++digits) {
            const char c = *p;
            uint32_t d;
            if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
            else break;
            v = (v << 4) | d;
            if (v > 0xFFFFFF) return false;
        }
    } else {
        for (; p < end && *p >= '0' && *p <= '9'; ++p, ++digits) {
            v = v * 10 + (uint32_t)(*p - '0');
            if (v > 0xFFFFFF) return false;
        }
    }
    *out = v;
    return digits != 0;
}

inline bool MesBlank(char c) { return c == ' ' || c == '\t' || c == '\r'; }
inline void MesSkipBlank(const char*& p, const char* end) {
    while (p < end && MesBlank(*p)) ++p;
}

enum MesLine { kLineEmpty, kLineEntry, kLineBad };

// A line of the glyph map:  <character> = <cell>   with the character written as itself or as U+XXXX, the cell as 0x..
// or a decimal number. A line that starts with ';' is a comment, and so is what follows a ';' after the cell.
inline MesLine GlyphMapParseLine(const char* p, const char* end, uint32_t* cp, uint32_t* cell) {
    MesSkipBlank(p, end);
    if (p == end || *p == ';') return kLineEmpty;
    const bool hexDigit = end - p >= 3 && ((p[2] >= '0' && p[2] <= '9') || ((p[2] | 0x20) >= 'a' && (p[2] | 0x20) <= 'f'));
    if (p[0] == 'U' && end - p >= 3 && p[1] == '+' && hexDigit) {
        uint32_t v = 0, digits = 0;
        for (p += 2; p < end && digits < 6; ++p, ++digits) {
            const char c = *p;
            if (c >= '0' && c <= '9') v = (v << 4) | (uint32_t)(c - '0');
            else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') v = (v << 4) | (uint32_t)((c | 0x20) - 'a' + 10);
            else break;
        }
        *cp = v;
    } else if (!Utf8Next(p, end, cp)) {
        return kLineBad;
    }
    // the basic plane only, no control characters and no surrogates
    if (*cp < 0x20 || *cp > 0xFFFF || (*cp >= 0xD800 && *cp <= 0xDFFF)) return kLineBad;
    MesSkipBlank(p, end);
    if (p == end || *p != '=') return kLineBad;
    ++p;
    MesSkipBlank(p, end);
    if (!MesParseNumber(p, end, cell) || *cell >= kMesAtlasCells) return kLineBad;
    MesSkipBlank(p, end);
    return (p == end || *p == ';') ? kLineEntry : kLineBad;
}

// A line of a text file:  0x1234 = text   (the id in hexadecimal with 0x). The text is what follows the first '=', without
// the blanks around it. A line that starts with ';' is a comment.
inline MesLine MesParseLine(const char* p, const char* end, uint32_t* id, const char** text, const char** textEnd) {
    MesSkipBlank(p, end);
    if (p == end || *p == ';') return kLineEmpty;
    if (end - p < 3 || p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) return kLineBad;
    if (!MesParseNumber(p, end, id)) return kLineBad;
    MesSkipBlank(p, end);
    if (p == end || *p != '=') return kLineBad;
    ++p;
    MesSkipBlank(p, end);
    while (end > p && MesBlank(end[-1])) --end;
    *text = p;
    *textEnd = end;
    return kLineEntry;
}

// ---- One record of the loaded file --------------------------------------------------------------------------------
struct MesOriginal {
    uint16_t w0, w1, w2, w3;
    uint32_t words;                    // the whole record: the four header words up to and with the FFFF
    uint32_t lines;                    // the lines it really has (w1 says the same in all but a few stock records)
    uint32_t pages;                    // how many FFF2 it has
    uint16_t duration[kMesMaxPages];   // their parameters, in order
};

inline bool MesStopWord(uint32_t w) { return w == kMesEnd || w == kMesNewLine || w == kMesPage; }

// True for the styles whose window turns three lines at a time (0045A5E7..0045A5FE give them -2 and -3).
inline bool MesThreeLineStyle(uint32_t w0) { return w0 == 0x38 || w0 == 0x3E || w0 == 0x3F; }

// Reads the record at `rec` (at most maxWords words belong to it) token by token. False when it is not a record this
// code understands completely: no FFFF inside the limit, a header word or a parameter that the game's plain scanners
// would take for a line end, FFF6 or one of the unused codes. `icons` (32 bytes, may be null) collects the icon numbers
// below 256 as a bit set.
inline bool MesReadOriginal(const uint8_t* rec, uint32_t maxWords, MesOriginal* o, uint8_t* icons) {
    if (maxWords < 6) return false;
    o->w0 = (uint16_t)MesRd16(rec);
    o->w1 = (uint16_t)MesRd16(rec + 2);
    o->w2 = (uint16_t)MesRd16(rec + 4);
    o->w3 = (uint16_t)MesRd16(rec + 6);
    o->pages = 0;
    if (MesStopWord(o->w0) || MesStopWord(o->w1) || MesStopWord(o->w2) || MesStopWord(o->w3)) return false;
    uint32_t p = 4, lines = 0;
    for (;;) {
        if (p >= maxWords) return false;
        if (MesStopWord(MesRd16(rec + p * 2))) return false;  // the width word of the line
        ++p;
        ++lines;
        uint32_t c;
        for (;;) {
            if (p >= maxWords) return false;
            c = MesRd16(rec + p * 2);
            ++p;
            if (c < kMesFirstControl || c == kMesSizeBack || c == kMesColourBack || c == kMesMarker) continue;
            if (MesStopWord(c)) break;
            const uint32_t params = c == kMesColour ? 3 : (c == kMesSize || c == kMesIcon) ? 1 : 0xFF;
            if (params == 0xFF || p + params > maxWords) return false;
            for (uint32_t i = 0; i < params; ++i)
                if (MesStopWord(MesRd16(rec + (p + i) * 2))) return false;
            if (c == kMesIcon && icons) {
                const uint32_t icon = MesRd16(rec + p * 2);
                if (icon < 256) icons[icon >> 3] |= (uint8_t)(1u << (icon & 7));
            }
            p += params;
        }
        if (c == kMesEnd) break;
        if (c == kMesPage) {
            if (p >= maxWords) return false;
            const uint32_t t = MesRd16(rec + p * 2);
            if (MesStopWord(t)) return false;
            if (o->pages < kMesMaxPages) o->duration[o->pages] = (uint16_t)t;
            ++o->pages;
            ++p;
        }
    }
    o->words = p;
    o->lines = lines;
    return true;
}

// ---- The tagged text ----------------------------------------------------------------------------------------------
//   {n}             new line                     {page}  {page 120}   the page ends here (120 = frames for the next page)
//   {color R G B}   colour 0..255, {/color} back {size 48}  glyph size in pixels, {/size} back to 32
//   {icon 3}        a button icon                {sp}    a space (for one at the very start or end of the text)
//   A {page} in a window of two-line pages closes a single line with an empty second one by itself.
//   {#1A3}          the atlas cell 1A3h          {FFF5}  the "no window background" marker (first thing in the text)
//   {{  }}          the braces themselves
// Every other character goes through the glyph map.
enum MesError {
    kMesOk = 0,
    kMesBadUtf8,       // not UTF-8
    kMesNoGlyph,       // value = the character without a cell
    kMesBadTag,        // an unknown tag or one without its closing brace
    kMesStrayBrace,    // a '}' alone
    kMesBadValue,      // value = the number that is out of range (or FFFFFFFFh: no number where one is needed)
    kMesBadIcon,       // value = an icon number the loaded file never uses
    kMesPageLines,     // value = the odd number of lines (3, 5..) in front of a {page} in a window of two-line pages
    kMesNoDuration,    // value = which {page} has no number while the original record has no duration for it
    kMesLineTooLong,   // value = glyphs in the line
    kMesTooLong,       // more words than the limit
    // from MesEncodeFor, before any text is looked at:
    kMesNoSuchId,      // value = the last id the file has
    kMesNoRecord,      // the id has no record in the file
    kMesNotHandled,    // the record in the file is not one MesReadOriginal understands
    kMesTemplateGrew,  // value = the words the record may have
};

struct MesErrorInfo {
    uint32_t column;  // byte offset in the text where it went wrong
    uint32_t value;
};

struct MesWriter {
    uint8_t* out;
    uint32_t cap, n;  // in words
    bool full;
    void Put(uint32_t w) {
        if (n < cap) MesWr16(out + n * 2, w);
        else full = true;
        ++n;
    }
};

inline bool MesTagIs(const char* p, const char* end, const char* name) {
    for (; *name; ++name, ++p) {
        if (p == end) return false;
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 0x20);
        if (c != *name) return false;
    }
    // the name ends here: a blank, a comma or the end of the tag follows
    return p == end || *p == ' ' || *p == ',';
}

// The numbers of a tag, separated by blanks or commas. False unless exactly `count` of them fill the rest of the tag.
inline bool MesTagNumbers(const char* p, const char* end, uint32_t count, uint32_t* out) {
    for (uint32_t i = 0; i < count; ++i) {
        while (p < end && (*p == ' ' || *p == ',')) ++p;
        if (!MesParseNumber(p, end, &out[i])) return false;
    }
    while (p < end && *p == ' ') ++p;
    return p == end;
}

// Encodes the text of one record. `orig` gives the header words that are kept (w0, w2, w3) and the page durations a
// {page} without a number takes; `icons` is the bit set of the icon numbers the loaded file uses (the icon code indexes
// tables of the game, so only numbers the game's own data uses are let through). At most capWords words are written.
inline MesError MesEncode(const char* text, const char* end, const GlyphMap& map, const MesOriginal& orig, const uint8_t* icons,
                          uint8_t* out, uint32_t capWords, uint32_t* words, MesErrorInfo* info) {
    MesWriter w = {out, capWords, 0, false};
    const bool twoLinePages = !MesThreeLineStyle(orig.w0);
    uint32_t lines = 0, linesInPage = 0, page = 0, lineAt = 0, width = 0, glyphs = 0;
    const char* p = text;
    info->column = 0;
    info->value = 0;
    auto fail = [&](MesError e, const char* at, uint32_t value) {
        info->column = (uint32_t)(at - text);
        info->value = value;
        return e;
    };
    auto openLine = [&]() {
        lineAt = w.n;
        w.Put(0);
        width = 0;
        glyphs = 0;
    };
    auto closeLine = [&](uint32_t endCode) {
        if (lineAt < w.cap) MesWr16(w.out + lineAt * 2, width);
        w.Put(endCode);
        ++lines;
        ++linesInPage;
    };
    w.Put(orig.w0);
    w.Put(0);  // the line count, known at the end
    w.Put(orig.w2);
    w.Put(orig.w3);
    openLine();
    while (p < end) {
        const char* at = p;
        uint32_t cell;
        if (*p == '}') {
            if (end - p < 2 || p[1] != '}') return fail(kMesStrayBrace, at, 0);
            p += 2;
            cell = map.cell['}'];
            if (cell == kMesNoCell) return fail(kMesNoGlyph, at, '}');
        } else if (*p == '{' && end - p >= 2 && p[1] == '{') {
            p += 2;
            cell = map.cell['{'];
            if (cell == kMesNoCell) return fail(kMesNoGlyph, at, '{');
        } else if (*p == '{') {
            const char* t = p + 1;
            const char* close = t;
            while (close < end && *close != '}' && *close != '{') ++close;
            if (close == end || *close != '}') return fail(kMesBadTag, at, 0);
            p = close + 1;
            uint32_t v[3];
            if (*t == '#') {
                const char* q = t + 1;
                uint32_t digits = 0;
                cell = 0;
                for (; q < close && digits < 4; ++q, ++digits) {
                    const char c = *q;
                    if (c >= '0' && c <= '9') cell = (cell << 4) | (uint32_t)(c - '0');
                    else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') cell = (cell << 4) | (uint32_t)((c | 0x20) - 'a' + 10);
                    else break;
                }
                if (!digits || q != close) return fail(kMesBadTag, at, 0);
                if (cell >= kMesAtlasCells) return fail(kMesBadValue, at, cell);
            } else if (MesTagIs(t, close, "sp") && close - t == 2) {
                cell = map.cell[' '];
                if (cell == kMesNoCell) return fail(kMesNoGlyph, at, ' ');
            } else {
                if (MesTagIs(t, close, "n") && close - t == 1) {
                    closeLine(kMesNewLine);
                    openLine();
                } else if (MesTagIs(t, close, "page")) {
                    uint32_t duration;
                    if (close - t == 4) {
                        if (page < orig.pages && page < kMesMaxPages) duration = orig.duration[page];
                        else if (!(orig.w2 & 0x8000)) duration = 0;  // every FFF2 of the stock records without a timer has 0
                        else return fail(kMesNoDuration, at, page + 1);
                    } else if (!MesTagNumbers(t + 4, close, 1, &duration)) {
                        return fail(kMesBadValue, at, 0xFFFFFFFF);
                    } else if (duration >= kMesFirstControl) {
                        return fail(kMesBadValue, at, duration);
                    }
                    if (twoLinePages && !(linesInPage & 1)) {  // an odd number of lines with the open one
                        if (linesInPage) return fail(kMesPageLines, at, linesInPage + 1);
                        closeLine(kMesNewLine);  // a single line: the empty second one, as the stock records have it
                        openLine();
                    }
                    closeLine(kMesPage);
                    w.Put(duration);
                    linesInPage = 0;
                    ++page;
                    openLine();
                } else if (MesTagIs(t, close, "color") || MesTagIs(t, close, "colour")) {
                    const char* args = t + (MesTagIs(t, close, "colour") ? 6 : 5);
                    if (!MesTagNumbers(args, close, 3, v)) return fail(kMesBadValue, at, 0xFFFFFFFF);
                    for (uint32_t i = 0; i < 3; ++i)
                        if (v[i] > 255) return fail(kMesBadValue, at, v[i]);
                    w.Put(kMesColour);
                    w.Put(v[0]);
                    w.Put(v[1]);
                    w.Put(v[2]);
                } else if ((MesTagIs(t, close, "/color") && close - t == 6) || (MesTagIs(t, close, "/colour") && close - t == 7)) {
                    w.Put(kMesColourBack);
                } else if (MesTagIs(t, close, "size")) {
                    if (!MesTagNumbers(t + 4, close, 1, v)) return fail(kMesBadValue, at, 0xFFFFFFFF);
                    if (v[0] < 8 || v[0] > 128) return fail(kMesBadValue, at, v[0]);
                    w.Put(kMesSize);
                    w.Put(v[0]);
                } else if (MesTagIs(t, close, "/size") && close - t == 5) {
                    w.Put(kMesSizeBack);
                } else if (MesTagIs(t, close, "icon")) {
                    if (!MesTagNumbers(t + 4, close, 1, v)) return fail(kMesBadValue, at, 0xFFFFFFFF);
                    if (v[0] > 255 || !icons || !(icons[v[0] >> 3] & (1u << (v[0] & 7)))) return fail(kMesBadIcon, at, v[0]);
                    w.Put(kMesIcon);
                    w.Put(v[0]);
                    width += 2;
                } else if (MesTagIs(t, close, "fff5") && close - t == 4) {
                    w.Put(kMesMarker);
                } else {
                    return fail(kMesBadTag, at, 0);
                }
                continue;
            }
        } else {
            uint32_t cp;
            if (!Utf8Next(p, end, &cp)) return fail(kMesBadUtf8, at, 0);
            cell = cp <= 0xFFFF ? map.cell[cp] : kMesNoCell;
            if (cell == kMesNoCell) return fail(kMesNoGlyph, at, cp);
        }
        w.Put(cell);
        width += cell < kMesWideFrom ? 1 : 2;
        if (++glyphs > kMesMaxLineGlyphs) return fail(kMesLineTooLong, at, glyphs);
    }
    closeLine(kMesEnd);
    if (w.full) return fail(kMesTooLong, end, w.n);
    MesWr16(out + 2, lines);
    *words = w.n;
    return kMesOk;
}

// ---- A record back to the tagged text (the dump) ------------------------------------------------------------------
struct MesText {
    char* out;
    uint32_t cap, n;
    void Char(char c) {
        if (n < cap) out[n] = c;
        ++n;
    }
    void Str(const char* s) {
        for (; *s; ++s) Char(*s);
    }
    void Dec(uint32_t v) {
        char t[10];
        uint32_t k = 0;
        do {
            t[k++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        while (k) Char(t[--k]);
    }
    void Hex(uint32_t v) {
        uint32_t digits = 1;
        while (digits < 8 && (v >> (digits * 4))) ++digits;
        while (digits) Char("0123456789ABCDEF"[(v >> (--digits * 4)) & 15]);
    }
    void Utf8(uint32_t cp) {  // the basic plane
        if (cp < 0x80) {
            Char((char)cp);
        } else if (cp < 0x800) {
            Char((char)(0xC0 | (cp >> 6)));
            Char((char)(0x80 | (cp & 0x3F)));
        } else {
            Char((char)(0xE0 | (cp >> 12)));
            Char((char)(0x80 | ((cp >> 6) & 0x3F)));
            Char((char)(0x80 | (cp & 0x3F)));
        }
    }
};

// Writes a record that MesReadOriginal accepted as text for MesEncode: with the same glyph map, MesEncode gives the same
// words back (when the record's own N words are the real widths). Returns the length in bytes, more than `cap` when the
// buffer is too small (nothing is written beyond it).
inline uint32_t MesDecode(const uint8_t* rec, uint32_t words, const GlyphMap& map, char* out, uint32_t cap) {
    MesText t = {out, cap, 0};
    uint32_t p = 4;
    while (p < words) {
        ++p;  // the width of the line
        for (;;) {
            const uint32_t c = MesRd16(rec + p * 2);
            ++p;
            if (c < kMesFirstControl) {
                // the character of the cell, when the map gives the same cell back for it
                const uint32_t ch = (c < kMesAtlasCells && map.chr[c] && map.cell[map.chr[c]] == c) ? map.chr[c] : 0;
                // a blank at either end of the text would be trimmed by the reader of the line
                const bool edge = t.n == 0 || MesRd16(rec + p * 2) == kMesEnd;
                if (!ch) { t.Str("{#"); t.Hex(c); t.Char('}'); }
                else if (ch == '{') t.Str("{{");
                else if (ch == '}') t.Str("}}");
                else if (ch == ' ' && edge) t.Str("{sp}");
                else t.Utf8(ch);
            } else if (c == kMesNewLine) {
                t.Str("{n}");
                break;
            } else if (c == kMesPage) {
                t.Str("{page ");
                t.Dec(MesRd16(rec + p * 2));
                t.Char('}');
                ++p;
                break;
            } else if (c == kMesEnd) {
                break;
            } else if (c == kMesColour) {
                t.Str("{color ");
                t.Dec(MesRd16(rec + p * 2));
                t.Char(' ');
                t.Dec(MesRd16(rec + p * 2 + 2));
                t.Char(' ');
                t.Dec(MesRd16(rec + p * 2 + 4));
                t.Char('}');
                p += 3;
            } else if (c == kMesColourBack) {
                t.Str("{/color}");
            } else if (c == kMesSize) {
                t.Str("{size ");
                t.Dec(MesRd16(rec + p * 2));
                t.Char('}');
                ++p;
            } else if (c == kMesSizeBack) {
                t.Str("{/size}");
            } else if (c == kMesIcon) {
                t.Str("{icon ");
                t.Dec(MesRd16(rec + p * 2));
                t.Char('}');
                ++p;
            } else {  // kMesMarker: MesReadOriginal lets no other code through
                t.Str("{FFF5}");
            }
        }
    }
    return t.n;
}

// ---- The file -----------------------------------------------------------------------------------------------------
struct MesFile {
    const uint8_t* data;
    uint32_t size, count, tableEnd;
};

inline bool MesOpen(const uint8_t* data, uint32_t size, MesFile* f) {
    if (!data || size < 16 || data[0] != '.' || data[1] != 'M' || data[2] != 'E' || data[3] != 'S') return false;
    const uint32_t count = MesRd32(data + 8);
    if (count == 0 || count > kMesMaxIds || 12 + count * 4 > size) return false;
    f->data = data;
    f->size = size;
    f->count = count;
    f->tableEnd = 12 + count * 4;
    return true;
}

inline uint32_t MesOffset(const MesFile& f, uint32_t id) { return MesRd32(f.data + 12 + id * 4); }

// The ids that have a record, in the order their records lie in the file (ids that share a record next to each other,
// the lower id first). `order` has room for f.count entries. Returns how many, or FFFFFFFFh when an offset points into
// the header, beyond the file or at an odd address: a file like that is not touched.
inline uint32_t MesPhysicalOrder(const MesFile& f, uint32_t* order) {
    uint32_t n = 0;
    for (uint32_t id = 0; id < f.count; ++id) {
        const uint32_t off = MesOffset(f, id);
        if (!off) continue;
        if (off < f.tableEnd || off >= f.size || (off & 1)) return 0xFFFFFFFF;
        order[n++] = id;
    }
    for (uint32_t gap = n / 2; gap > 0; gap /= 2)  // shell sort by offset, then id
        for (uint32_t i = gap; i < n; ++i) {
            const uint32_t id = order[i], off = MesOffset(f, id);
            uint32_t j = i;
            for (; j >= gap; j -= gap) {
                const uint32_t other = order[j - gap], otherOff = MesOffset(f, other);
                if (otherOff < off || (otherOff == off && other < id)) break;
                order[j] = other;
            }
            order[j] = id;
        }
    return n;
}

// Where the space of the record order[k] ends: at the next record, or at the end of the file.
inline uint32_t MesSpanEnd(const MesFile& f, const uint32_t* order, uint32_t n, uint32_t k) {
    const uint32_t off = MesOffset(f, order[k]);
    for (++k; k < n; ++k) {
        const uint32_t next = MesOffset(f, order[k]);
        if (next != off) return next;
    }
    return f.size;
}

// The length in bytes of the record at the start of a span the way the game's own walker measures it (0045A720): up to
// and with the first FFFF word. 0 when the span has none.
inline uint32_t MesRecordBytes(const uint8_t* rec, uint32_t spanBytes) {
    for (uint32_t i = 0; i + 2 <= spanBytes; i += 2)
        if (MesRd16(rec + i) == kMesEnd) return i + 2;
    return 0;
}

struct MesOverride {
    const uint8_t* data;  // null = the record stays
    uint32_t bytes;
};

// By id: where the space of each record ends (`spanEnd`, f.count entries), and the icon numbers the file uses.
inline void MesIndex(const MesFile& f, const uint32_t* order, uint32_t n, uint32_t* spanEnd, uint8_t* icons) {
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t id = order[k], off = MesOffset(f, id);
        spanEnd[id] = MesSpanEnd(f, order, n, k);
        MesOriginal o;
        MesReadOriginal(f.data + off, (spanEnd[id] - off) / 2, &o, icons);
    }
}

// The two records the drawer does not read from the file but from a 513-word buffer of CMessage (0045CEF9), into which
// FUN_0045A870 copies them together with an item name, with no limit: a replacement may not be longer than the record it
// replaces.
inline bool MesIsTemplate(uint32_t id) { return id == 0x3B70 || id == 0x3B73; }

// Encodes `text` as the replacement of the record `id` of the file: MesEncode with the checks that need the file. `out` has
// room for kMesMaxWords words.
inline MesError MesEncodeFor(const MesFile& f, const uint32_t* spanEnd, uint32_t id, const char* text, const char* end, const GlyphMap& map,
                             const uint8_t* icons, uint8_t* out, uint32_t* words, MesErrorInfo* info) {
    info->column = 0;
    info->value = f.count - 1;
    if (id >= f.count) return kMesNoSuchId;
    const uint32_t off = MesOffset(f, id);
    if (!off) return kMesNoRecord;
    MesOriginal orig;
    // understood token by token, and ending where the game's plain walk over FFFF words ends it
    if (!MesReadOriginal(f.data + off, (spanEnd[id] - off) / 2, &orig, nullptr) ||
        orig.words * 2 != MesRecordBytes(f.data + off, spanEnd[id] - off))
        return kMesNotHandled;
    const bool isTemplate = MesIsTemplate(id);
    const MesError e = MesEncode(text, end, map, orig, icons, out, isTemplate ? orig.words : kMesMaxWords, words, info);
    if (e == kMesTooLong) info->value = isTemplate ? orig.words : kMesMaxWords;
    return (e == kMesTooLong && isTemplate) ? kMesTemplateGrew : e;
}

// Builds the merged file: the header, the offset table and every record in its place, a replaced record instead of
// the first FFFF-terminated run of its span (whatever else the span holds follows it unchanged), all offsets
// recomputed. `ov` is indexed by id. With out = null only the size is computed. Returns the size, 0 when it does not
// fit in `cap`.
inline uint32_t MesMerge(const MesFile& f, const uint32_t* order, uint32_t n, const MesOverride* ov, uint8_t* out, uint32_t cap) {
    uint32_t pos = 0;
    auto copy = [&](const uint8_t* from, uint32_t bytes) {
        if (out && pos + bytes <= cap)
            for (uint32_t i = 0; i < bytes; ++i) out[pos + i] = from[i];
        pos += bytes;
    };
    const uint32_t first = n ? MesOffset(f, order[0]) : f.size;
    copy(f.data, first);  // header, table (the ids without a record keep their 0) and anything in front of the first record
    for (uint32_t k = 0; k < n;) {
        const uint32_t off = MesOffset(f, order[k]), spanEnd = MesSpanEnd(f, order, n, k);
        const uint32_t recBytes = MesRecordBytes(f.data + off, spanEnd - off);
        const uint32_t newOff = pos;
        const MesOverride* use = nullptr;
        uint32_t group = k;
        for (; group < n && MesOffset(f, order[group]) == off; ++group) {
            if (!use && recBytes && ov[order[group]].data) use = &ov[order[group]];
            if (out && 12 + order[group] * 4 + 4 <= cap) MesWr32(out + 12 + order[group] * 4, newOff);
        }
        if (use) {
            copy(use->data, use->bytes);
            copy(f.data + off + recBytes, spanEnd - off - recBytes);
        } else {
            copy(f.data + off, spanEnd - off);
        }
        k = group;
    }
    return (out && pos > cap) ? 0 : pos;
}

}  // namespace ufx
