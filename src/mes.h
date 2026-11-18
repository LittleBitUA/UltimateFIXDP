// MES_ALL.MES, the game's one text file: reading it, the text of one record as the translators' tools write it (the JSON
// of DPMsgTool, which the SWERY Localization Tool edits), and the merged file the text overlay hands to the game.
// No Windows headers and no C library: the host tests it (tests/test_mes.cpp).
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
// The game has no character encoding: a text word is the number of an atlas cell. DPMsgTool writes a cell as the character
// cell + 20h: certain for ASCII (the game's own byte strings use byte - 20h, 0045BB58), and for Latin-1 it is what the
// French and German MES files of the game do (C7h, the C with a cedilla, is the word A7h) and what the font editor of the
// SWERY tool relies on (the carrier BFh draws the cell that shows the A with an acute in the stock atlas, cell 9Fh). The
// cells 5Fh..7Fh and E0h and above have no character here: how DPMsgTool writes them is not known.
// A translation in Cyrillic goes through the tool's glyph map first: each character is replaced by one or two carrier
// characters, in whose cells the translation's own font atlas has the Cyrillic glyphs.
struct GlyphMap {
    uint16_t cell[0x10000];      // by code point (the basic plane); kMesNoCell = no cell
    uint16_t subst[0x10000][2];  // the tool's glyph map: the carrier characters of a code point; [0] = 0: none
    uint16_t brCode;             // the code {BR} stands for, 0 = not known (see MesEncode)
};

inline uint32_t MesCellChar(uint32_t cell) { return (cell <= 0x5E || (cell >= 0x80 && cell <= 0xDF)) ? cell + 0x20 : 0; }

inline void GlyphMapDefault(GlyphMap& m) {
    for (uint32_t i = 0; i < 0x10000; ++i) {
        m.cell[i] = kMesNoCell;
        m.subst[i][0] = m.subst[i][1] = 0;
    }
    for (uint32_t cell = 0; cell <= 0xDF; ++cell)
        if (MesCellChar(cell)) m.cell[MesCellChar(cell)] = (uint16_t)cell;
    m.brCode = 0;
}

// A character given a cell of its own (glyphmap.txt): it no longer goes through a carrier.
inline void GlyphMapSet(GlyphMap& m, uint32_t cp, uint32_t cell) {
    m.cell[cp] = (uint16_t)cell;
    m.subst[cp][0] = m.subst[cp][1] = 0;
}

inline void GlyphMapSubst(GlyphMap& m, uint32_t cp, uint32_t first, uint32_t second) {
    m.subst[cp][0] = (uint16_t)first;
    m.subst[cp][1] = (uint16_t)second;
}

// The glyph map the SWERY tool uses when the translator has not made one (DP1_DEFAULT_GLYPHMAP in its electron/main.cjs):
// the Ukrainian letters that look like Latin ones on those, the rest on Latin-1 carriers, and the dash.
inline void GlyphMapToolDefaults(GlyphMap& m) {
    static const uint16_t pairs[67][2] = {
        {0x041E, 0x4F}, {0x043E, 0x6F}, {0x0410, 0x41}, {0x0430, 0x61}, {0x0420, 0x50}, {0x0440, 0x70}, {0x0421, 0x43}, {0x0441, 0x63},
        {0x041C, 0x4D}, {0x0412, 0x42}, {0x0415, 0x45}, {0x0435, 0x65}, {0x041D, 0x48}, {0x0422, 0x54}, {0x0406, 0x49}, {0x0456, 0x69},
        {0x0407, 0xCD}, {0x0457, 0xEF}, {0x0425, 0x58}, {0x0445, 0x78}, {0x0443, 0x79}, {0x0411, 0xD4}, {0x0413, 0xBF}, {0x0490, 0xD9},
        {0x0414, 0xC1}, {0x0404, 0xC2}, {0x0416, 0xC3}, {0x0417, 0xC4}, {0x0418, 0xC5}, {0x0419, 0xC6}, {0x041A, 0xC7}, {0x041B, 0xC8},
        {0x041F, 0xC9}, {0x0423, 0xCA}, {0x0424, 0xCB}, {0x0426, 0xCC}, {0x0427, 0xCE}, {0x0428, 0xCF}, {0x0429, 0xD0}, {0x042C, 0xD1},
        {0x042E, 0xD2}, {0x042F, 0xD3}, {0x0431, 0xE0}, {0x0432, 0xE1}, {0x0433, 0xE2}, {0x0491, 0xE3}, {0x0434, 0xE4}, {0x0454, 0xE5},
        {0x0436, 0xE6}, {0x0437, 0xE7}, {0x0438, 0xE8}, {0x0439, 0xE9}, {0x043A, 0xEA}, {0x043B, 0xEB}, {0x043C, 0xEC}, {0x043D, 0xED},
        {0x043F, 0xEE}, {0x0442, 0xFA}, {0x0444, 0xF1}, {0x0446, 0xF2}, {0x0447, 0xF3}, {0x0448, 0xF4}, {0x0449, 0xF5}, {0x044F, 0xF9},
        {0x044E, 0xF8}, {0x044C, 0xFB}, {0x2014, 0xDA},
    };
    for (const auto& pair : pairs) GlyphMapSubst(m, pair[0], pair[1], 0);
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

// A line of glyphmap.txt:  <character> = <cell>   with the character written as itself or as U+XXXX, the cell as 0x..
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

// A line of a text file:  0x1234 = text   (the record number in hexadecimal with 0x). The text is everything after the
// '=' and one blank up to the end of the line: other blanks belong to it. A line that starts with ';' is a comment.
inline MesLine MesParseLine(const char* p, const char* end, uint32_t* id, const char** text, const char** textEnd) {
    while (end > p && end[-1] == '\r') --end;
    MesSkipBlank(p, end);
    if (p == end || *p == ';') return kLineEmpty;
    if (end - p < 3 || p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) return kLineBad;
    if (!MesParseNumber(p, end, id)) return kLineBad;
    MesSkipBlank(p, end);
    if (p == end || *p != '=') return kLineBad;
    ++p;
    if (p < end && *p == ' ') ++p;
    *text = p;
    *textEnd = end;
    return kLineEntry;
}

// ---- One record of the loaded file --------------------------------------------------------------------------------
// What the codes are, by the three places of the game that walk a record token by token (the drawer's measuring pass, table
// 0045EFEC; its drawing pass, table 0045F010; the page timer of records without the timer flag, table 0045AB10):
// FFFA has three parameters, FFF1 FFF2 FFF4 one, FFF0 FFF5 FFF9 none, FFF6 two counted runs. The others (FFEF, FFF3,
// FFF7, FFF8, FFFB..FFFD) are skipped by the drawer, but the page timer does not agree with it about FFEF and FFF3; none
// of them occurs in the game's files, and a record with one is left alone here.
enum MesKind { kKindSegment, kKindFrame, kKindIcon, kKindColour, kKindColourBack, kKindSpacing, kKindBr, kKindSizeBack, kKindCount };

struct MesOriginal {
    const uint8_t* rec;
    uint16_t w0, w1, w2, w3;
    uint32_t words;              // the whole record: the four header words up to and with the FFFF
    uint32_t lines;              // the lines it really has (w1 says the same in all but a few stock records)
    uint32_t codes[kKindCount];  // how many of each code it has (kKindBr stays 0: no code is known for {BR})
};

inline bool MesStopWord(uint32_t w) { return w == kMesEnd || w == kMesNewLine || w == kMesPage; }

// True for the styles whose window turns three lines at a time (0045A5E7..0045A5FE give them -2 and -3).
inline bool MesThreeLineStyle(uint32_t w0) { return w0 == 0x38 || w0 == 0x3E || w0 == 0x3F; }

inline uint32_t MesCodeParams(uint32_t c) { return c == kMesColour ? 3 : (c == kMesSize || c == kMesIcon) ? 1 : 0; }
inline bool MesPlainCode(uint32_t c) { return c == kMesSizeBack || c == kMesColourBack || c == kMesMarker; }

// The kind a code inside a line is counted as; kKindCount for FFF5, which has no kind of its own.
inline uint32_t MesCodeKind(uint32_t c) {
    switch (c) {
        case kMesIcon: return kKindIcon;
        case kMesColour: return kKindColour;
        case kMesColourBack: return kKindColourBack;
        case kMesSize: return kKindSpacing;
        case kMesSizeBack: return kKindSizeBack;
        default: return kKindCount;
    }
}

// Reads the record at `rec` (at most maxWords words belong to it) token by token. False when it is not a record this
// code understands completely: no FFFF inside the limit, a header word or a parameter that the game's plain scanners
// would take for a line end, FFF6 or one of the codes the game's files never use. `icons` (32 bytes, may be null)
// collects the icon numbers below 256 as a bit set.
inline bool MesReadOriginal(const uint8_t* rec, uint32_t maxWords, MesOriginal* o, uint8_t* icons) {
    if (maxWords < 6) return false;
    o->rec = rec;
    o->w0 = (uint16_t)MesRd16(rec);
    o->w1 = (uint16_t)MesRd16(rec + 2);
    o->w2 = (uint16_t)MesRd16(rec + 4);
    o->w3 = (uint16_t)MesRd16(rec + 6);
    for (uint32_t& count : o->codes) count = 0;
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
            if (c < kMesFirstControl) continue;
            if (MesStopWord(c)) break;
            const uint32_t params = MesCodeParams(c);
            if ((!params && !MesPlainCode(c)) || p + params > maxWords) return false;
            for (uint32_t i = 0; i < params; ++i)
                if (MesStopWord(MesRd16(rec + (p + i) * 2))) return false;
            if (c == kMesIcon && icons) {
                const uint32_t icon = MesRd16(rec + p * 2);
                if (icon < 256) icons[icon >> 3] |= (uint8_t)(1u << (icon & 7));
            }
            const uint32_t kind = MesCodeKind(c);
            if (kind < kKindCount) ++o->codes[kind];
            p += params;
        }
        if (c == kMesEnd) break;
        if (c == kMesPage) {
            if (p >= maxWords || MesStopWord(MesRd16(rec + p * 2))) return false;
            ++o->codes[kKindFrame];
            ++p;
        } else {
            ++o->codes[kKindSegment];
        }
    }
    o->words = p;
    o->lines = lines;
    return true;
}

// A copy of a record that MesReadOriginal accepted with its width words and its line count made what they should be (a
// few stock records have them wrong). `out` has room for `words` words.
inline void MesNormalise(const uint8_t* rec, uint32_t words, uint8_t* out) {
    for (uint32_t i = 0; i < words * 2; ++i) out[i] = rec[i];
    uint32_t p = 4, lines = 0;
    for (;;) {
        const uint32_t widthAt = p++;
        uint32_t width = 0, c;
        ++lines;
        for (;;) {
            c = MesRd16(rec + p * 2);
            ++p;
            if (c < kMesFirstControl) width += c < kMesWideFrom ? 1 : 2;
            else if (MesStopWord(c)) break;
            else {
                if (c == kMesIcon) width += 2;
                p += MesCodeParams(c);
            }
        }
        MesWr16(out + widthAt * 2, width);
        if (c == kMesPage) ++p;
        if (c == kMesEnd) break;
    }
    MesWr16(out + 2, lines);
}

// True when the record has the word `w` as a glyph or as a code without parameters (not as a width, a parameter or a line end).
inline bool MesRecordHasWord(const uint8_t* rec, uint32_t words, uint32_t w) {
    uint32_t p = 4;
    while (p < words) {
        ++p;
        for (;;) {
            const uint32_t c = MesRd16(rec + p * 2);
            ++p;
            if (MesStopWord(c)) {
                if (c == kMesPage) ++p;
                break;
            }
            if (c == w && !MesCodeParams(c)) return true;
            if (c >= kMesFirstControl) p += MesCodeParams(c);
        }
    }
    return false;
}

// ---- The text of a record, as DPMsgTool's JSON has it -------------------------------------------------------------
// Characters, and markers in braces. What each marker is in the record, and why that is taken as known:
//   {NEXT_SEGMENT}             FFFE and the width word of the next line. The tools' rule for the field FirstSegmentLenght
//                              ("the letters up to the first {NEXT_SEGMENT} or {NEXT_FRAME}") describes the first line's
//                              width word, and FFFE and FFF2 are the two ways a line ends.
//   {NEXT_FRAME id=N}          FFF2 N and the width word of the next line (N = the duration of the next page in frames).
//                              The other line end, the one with a parameter. A second number (letters=M, which some
//                              version of the tool wrote) is read and not used: the width word is always computed.
//   {ICON id=N}                FFF4 N, the button icon.
//   {SET_COLOR color=R, G, B}  FFFA R G B: the only code with three parameters.
//   {RESET_COLOR}              FFF9, which puts the colour back (0045E837); it follows every FFFA in the game's files.
//   {SET_SPACING value=N}      FFF1 N: of the game's three codes with one parameter (FFF1, FFF2, FFF4) the one that is left.
//                              It is the glyph size in pixels, and with it the step from glyph to glyph.
//   {NO_OP:0xNNNN} {UNK:0xNNNN}  the word NNNN itself. Accepted only when the record being replaced has that word as a
//                              glyph or as a code without parameters: then the marker can only be that word.
//   {BR}                       not known from the tools' source. GlyphMap::brCode says what it is when the caller has
//                              established that from data (the overlay does); otherwise a text with {BR} is refused.
// The names are DPMsgTool's; the labels inside a marker (id=, color=, value=) are not checked, only the numbers count.
// A '{' that does not start a name in capitals is an ordinary character.
// Computed here and never taken from the text: the width word of every line and the line count w1. Kept from the record
// being replaced: w0, w2, w3.
// One correction is made: in a window of two-line pages (every style but 38h, 3Eh, 3Fh) a {NEXT_FRAME} after an odd
// number of lines gets an empty line in front of it, because the window turns two lines at a time (0045F4FE) and the
// page timer finds a page's duration only at the line the page starts with (0045A9F2). The game's own records are all
// made that way, so they come out as they are.
enum MesError {
    kMesOk = 0,
    kMesBadUtf8,       // not UTF-8
    kMesNoGlyph,       // value = the character that has no cell (value2 = its carrier from the glyph map, when that has none)
    kMesBadTag,        // an unknown marker, or one that is not written as the tools write it
    kMesBadValue,      // value = the number that is out of range
    kMesBadIcon,       // value = an icon number the loaded file never uses
    kMesUnknownBr,     // {BR}
    kMesUnknownWord,   // value = the word of a {NO_OP:..} / {UNK:..} that the record being replaced does not have
    kMesLineTooLong,   // value = glyphs in the line
    kMesTooLong,       // more words than the limit
    // from MesEncodeFor, before any text is looked at:
    kMesNoSuchId,      // value = the last id the file has
    kMesNoRecord,      // the id has no record in the file
    kMesNotHandled,    // the record in the file is not one MesReadOriginal understands
    kMesTemplateGrew,  // value = the words the record may have
    kMesTemplateSlots, // value = how many "**" places the loaded template record has
};

struct MesErrorInfo {
    uint32_t column;  // byte offset in the text where it went wrong
    uint32_t value, value2;
    uint32_t padded;  // empty lines added in front of {NEXT_FRAME}
};

// The first seven have the numbers of their MesKind.
enum MesMark { kMarkNone = -2, kMarkUnknown = -1, kMarkSegment = 0, kMarkFrame, kMarkIcon, kMarkColour, kMarkColourBack, kMarkSpacing,
               kMarkBr, kMarkNoOp, kMarkUnk };

// What starts at a '{' (p points behind it): kMarkNone when it is not a marker at all (no name of two or more capitals
// followed by '}', ' ', ':' or the end of the text), kMarkUnknown for a name the tools do not have. *after = behind the name.
inline MesMark MesMarkerName(const char* p, const char* end, const char** after) {
    const char* q = p;
    while (q < end && ((*q >= 'A' && *q <= 'Z') || *q == '_')) ++q;
    if (q - p < 2 || (q < end && *q != '}' && *q != ' ' && *q != ':')) return kMarkNone;
    *after = q;
    if (q == end) return kMarkUnknown;  // a name the text ends in: a marker cut off
    static const char* const names[9] = {"NEXT_SEGMENT", "NEXT_FRAME", "ICON", "SET_COLOR", "RESET_COLOR", "SET_SPACING", "BR", "NO_OP", "UNK"};
    for (int i = 0; i < 9; ++i) {
        const char* n = names[i];
        const char* t = p;
        while (t < q && *n && *t == *n) { ++t; ++n; }
        if (t == q && !*n) return (MesMark)i;
    }
    return kMarkUnknown;
}

// The numbers of a marker up to `close`: separated by blanks or commas, each with or without a label in front (id=).
// False when something else is there or there are more than `max`.
inline bool MesMarkerNumbers(const char* p, const char* close, uint32_t max, uint32_t* out, uint32_t* count) {
    *count = 0;
    for (;;) {
        while (p < close && (*p == ' ' || *p == ',')) ++p;
        if (p == close) return true;
        const char* label = p;
        while (p < close && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')) ++p;
        if (p != label) {
            if (p == close || *p != '=') return false;
            ++p;
            while (p < close && *p == ' ') ++p;
        }
        if (*count == max || !MesParseNumber(p, close, &out[*count])) return false;
        ++*count;
    }
}

// How many of each marker a text has, by name alone (for comparing a whole file with the loaded MES before any of it is used).
inline void MesCountMarkers(const char* p, const char* end, uint32_t counts[kKindCount]) {
    for (uint32_t i = 0; i < kKindCount; ++i) counts[i] = 0;
    for (; p < end; ++p) {
        if (*p != '{') continue;
        const char* after;
        const MesMark m = MesMarkerName(p + 1, end, &after);
        if (m >= kMarkSegment && m <= kMarkBr) ++counts[m];
    }
}

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

// Encodes the text of one record. `orig` is the record being replaced (see above for what is kept from it); `icons` is
// the bit set of the icon numbers the loaded file uses (the icon code indexes tables of the game, so only numbers the
// game's own data uses are let through). At most capWords words are written.
inline MesError MesEncode(const char* text, const char* end, const GlyphMap& map, const MesOriginal& orig, const uint8_t* icons,
                          uint8_t* out, uint32_t capWords, uint32_t* words, MesErrorInfo* info) {
    MesWriter w = {out, capWords, 0, false};
    const bool twoLinePages = !MesThreeLineStyle(orig.w0);
    uint32_t lines = 0, linesInPage = 0, lineAt = 0, width = 0, glyphs = 0;
    const char* p = text;
    info->column = info->value = info->value2 = info->padded = 0;
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
    auto glyph = [&](uint32_t cell) {
        w.Put(cell);
        width += cell < kMesWideFrom ? 1 : 2;
        return ++glyphs <= kMesMaxLineGlyphs;
    };
    w.Put(orig.w0);
    w.Put(0);  // the line count, known at the end
    w.Put(orig.w2);
    w.Put(orig.w3);
    openLine();
    while (p < end) {
        const char* at = p;
        const char* after = nullptr;
        const MesMark mark = *p == '{' ? MesMarkerName(p + 1, end, &after) : kMarkNone;
        if (mark == kMarkNone) {
            uint32_t cp;
            if (!Utf8Next(p, end, &cp)) return fail(kMesBadUtf8, at, 0);
            if (cp > 0xFFFF) return fail(kMesNoGlyph, at, cp);
            const uint16_t self[2] = {(uint16_t)cp, 0};
            const uint16_t* carriers = map.subst[cp][0] ? map.subst[cp] : self;
            for (uint32_t i = 0; i < 2 && carriers[i]; ++i) {
                const uint32_t cell = map.cell[carriers[i]];
                if (cell == kMesNoCell) {
                    info->value2 = carriers[i] != cp ? carriers[i] : 0;
                    return fail(kMesNoGlyph, at, cp);
                }
                if (!glyph(cell)) return fail(kMesLineTooLong, at, glyphs);
            }
            continue;
        }
        const char* close = after;
        while (close < end && *close != '}' && *close != '{') ++close;
        if (mark == kMarkUnknown || close == end || *close != '}') return fail(kMesBadTag, at, 0);
        p = close + 1;
        uint32_t v[3] = {0, 0, 0}, count = 0;
        if (mark == kMarkNoOp || mark == kMarkUnk) {
            const char* q = after;
            if (*q != ':') return fail(kMesBadTag, at, 0);
            ++q;
            if (!MesParseNumber(q, close, &v[0]) || q != close) return fail(kMesBadTag, at, 0);
            const bool usable = v[0] < kMesAtlasCells || MesPlainCode(v[0]);
            if (!usable || !MesRecordHasWord(orig.rec, orig.words, v[0])) return fail(kMesUnknownWord, at, v[0]);
            if (v[0] >= kMesFirstControl) w.Put(v[0]);
            else if (!glyph(v[0])) return fail(kMesLineTooLong, at, glyphs);
            continue;
        }
        if (!MesMarkerNumbers(after, close, 3, v, &count)) return fail(kMesBadTag, at, 0);
        const uint32_t wanted = mark == kMarkColour ? 3 : (mark == kMarkFrame || mark == kMarkIcon || mark == kMarkSpacing) ? 1 : 0;
        if (count != wanted && !(mark == kMarkFrame && count == 2)) return fail(kMesBadTag, at, 0);
        switch (mark) {
            case kMarkSegment:
                closeLine(kMesNewLine);
                openLine();
                break;
            case kMarkFrame:
                if (v[0] >= kMesFirstControl) return fail(kMesBadValue, at, v[0]);
                if (twoLinePages && !(linesInPage & 1)) {  // an odd number of lines with the open one
                    closeLine(kMesNewLine);
                    openLine();
                    ++info->padded;
                }
                closeLine(kMesPage);
                w.Put(v[0]);
                linesInPage = 0;
                openLine();
                break;
            case kMarkIcon:
                if (v[0] > 255 || !icons || !(icons[v[0] >> 3] & (1u << (v[0] & 7)))) return fail(kMesBadIcon, at, v[0]);
                w.Put(kMesIcon);
                w.Put(v[0]);
                width += 2;
                break;
            case kMarkColour:
                for (uint32_t i = 0; i < 3; ++i)
                    if (v[i] > 255) return fail(kMesBadValue, at, v[i]);
                w.Put(kMesColour);
                w.Put(v[0]);
                w.Put(v[1]);
                w.Put(v[2]);
                break;
            case kMarkColourBack:
                w.Put(kMesColourBack);
                break;
            case kMarkSpacing:
                if (v[0] < 8 || v[0] > 128) return fail(kMesBadValue, at, v[0]);
                w.Put(kMesSize);
                w.Put(v[0]);
                break;
            default:  // kMarkBr
                if (!map.brCode) return fail(kMesUnknownBr, at, 0);
                w.Put(map.brCode);
                break;
        }
    }
    closeLine(kMesEnd);
    if (w.full) return fail(kMesTooLong, end, w.n);
    MesWr16(out + 2, lines);
    *words = w.n;
    return kMesOk;
}

// ---- A record as that text ----------------------------------------------------------------------------------------
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
    void Hex4(uint32_t v) {
        for (int shift = 12; shift >= 0; shift -= 4) Char("0123456789ABCDEF"[(v >> shift) & 15]);
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

// Writes a record that MesReadOriginal accepted as the text above. A cell without a character, a '{' and the code FFF0
// (whose marker name is not known) come out as {UNK:0x....}. MesEncode with the plain character table gives the same
// words back, with the width words and the line count as MesNormalise makes them. Returns the length in bytes, more
// than `cap` when the buffer is too small (nothing is written beyond it).
inline uint32_t MesDecode(const uint8_t* rec, uint32_t words, char* out, uint32_t cap) {
    MesText t = {out, cap, 0};
    uint32_t p = 4;
    while (p < words) {
        ++p;  // the width of the line
        for (;;) {
            const uint32_t c = MesRd16(rec + p * 2);
            ++p;
            if (c == kMesNewLine) {
                t.Str("{NEXT_SEGMENT}");
                break;
            } else if (c == kMesPage) {
                t.Str("{NEXT_FRAME id=");
                t.Dec(MesRd16(rec + p * 2));
                t.Char('}');
                ++p;
                break;
            } else if (c == kMesEnd) {
                break;
            } else if (c == kMesColour) {
                t.Str("{SET_COLOR color=");
                t.Dec(MesRd16(rec + p * 2));
                t.Str(", ");
                t.Dec(MesRd16(rec + p * 2 + 2));
                t.Str(", ");
                t.Dec(MesRd16(rec + p * 2 + 4));
                t.Char('}');
                p += 3;
            } else if (c == kMesColourBack) {
                t.Str("{RESET_COLOR}");
            } else if (c == kMesSize) {
                t.Str("{SET_SPACING value=");
                t.Dec(MesRd16(rec + p * 2));
                t.Char('}');
                ++p;
            } else if (c == kMesIcon) {
                t.Str("{ICON id=");
                t.Dec(MesRd16(rec + p * 2));
                t.Char('}');
                ++p;
            } else if (c == kMesMarker) {
                t.Str("{NO_OP:0xFFF5}");
            } else if (MesCellChar(c) && MesCellChar(c) != '{') {
                t.Utf8(MesCellChar(c));
            } else {  // a cell without a character, or FFF0
                t.Str("{UNK:0x");
                t.Hex4(c);
                t.Char('}');
            }
        }
    }
    return t.n;
}

// ---- JSON ---------------------------------------------------------------------------------------------------------
// Just enough of a reader for the two files of the tools: mes_all.json, an array with one element per record number,
//     { "Id1": 1, "Id2": 2, "Flag1": 32768, "Flag2": 229, "FirstSegmentLenght": 29, "Text": "...", "EmptyRecord": false }
// and glyphmap.json, { "version": 1, "map": { "<character>": "<carriers>", ... } }.
struct JsonCursor {
    const char* p;
    const char* end;
};

inline void JsonWs(JsonCursor& c) {
    while (c.p < c.end && (*c.p == ' ' || *c.p == '\t' || *c.p == '\r' || *c.p == '\n')) ++c.p;
}

inline bool JsonHex4(JsonCursor& c, uint32_t* v) {
    if (c.end - c.p < 4) return false;
    *v = 0;
    for (int i = 0; i < 4; ++i) {
        const char ch = *c.p++;
        uint32_t d;
        if (ch >= '0' && ch <= '9') d = (uint32_t)(ch - '0');
        else if ((ch | 0x20) >= 'a' && (ch | 0x20) <= 'f') d = (uint32_t)((ch | 0x20) - 'a' + 10);
        else return false;
        *v = (*v << 4) | d;
    }
    return true;
}

// A string (the cursor is on its opening quote) as UTF-8 into `out`, at most `cap` bytes (out may be null); *len gets its
// whole length. Half of a surrogate pair on its own becomes U+FFFD. False when the string is not well formed.
inline bool JsonString(JsonCursor& c, char* out, uint32_t cap, uint32_t* len) {
    MesText t = {out, out ? cap : 0, 0};
    if (c.p >= c.end || *c.p != '"') return false;
    for (++c.p;;) {
        if (c.p >= c.end) return false;
        const char ch = *c.p++;
        if (ch == '"') break;
        if ((uint8_t)ch < 0x20) return false;
        if (ch != '\\') {
            t.Char(ch);
            continue;
        }
        if (c.p >= c.end) return false;
        const char e = *c.p++;
        if (e == '"' || e == '\\' || e == '/') t.Char(e);
        else if (e == 'n') t.Char('\n');
        else if (e == 'r') t.Char('\r');
        else if (e == 't') t.Char('\t');
        else if (e == 'b') t.Char('\b');
        else if (e == 'f') t.Char('\f');
        else if (e == 'u') {
            uint32_t v, low;
            if (!JsonHex4(c, &v)) return false;
            if (v >= 0xD800 && v <= 0xDBFF && c.end - c.p >= 6 && c.p[0] == '\\' && c.p[1] == 'u') {
                JsonCursor look = {c.p + 2, c.end};
                if (JsonHex4(look, &low) && low >= 0xDC00 && low <= 0xDFFF) {
                    c.p = look.p;
                    v = 0x10000 + ((v - 0xD800) << 10) + (low - 0xDC00);
                    t.Char((char)(0xF0 | (v >> 18)));
                    t.Char((char)(0x80 | ((v >> 12) & 0x3F)));
                    t.Char((char)(0x80 | ((v >> 6) & 0x3F)));
                    t.Char((char)(0x80 | (v & 0x3F)));
                    continue;
                }
            }
            t.Utf8((v >= 0xD800 && v <= 0xDFFF) ? 0xFFFD : v);
        } else {
            return false;
        }
    }
    if (len) *len = t.n;
    return true;
}

// Steps over one value of any kind.
inline bool JsonSkip(JsonCursor& c) {
    JsonWs(c);
    if (c.p >= c.end) return false;
    if (*c.p == '"') return JsonString(c, nullptr, 0, nullptr);
    if (*c.p != '{' && *c.p != '[') {  // a number, true, false, null
        const char* start = c.p;
        while (c.p < c.end && ((*c.p >= '0' && *c.p <= '9') || (*c.p >= 'a' && *c.p <= 'z') || *c.p == '-' || *c.p == '+' || *c.p == '.' || *c.p == 'E'))
            ++c.p;
        return c.p != start;
    }
    uint32_t depth = 0;
    do {
        if (c.p >= c.end) return false;
        const char ch = *c.p;
        if (ch == '"') {
            if (!JsonString(c, nullptr, 0, nullptr)) return false;
            continue;
        }
        if (ch == '{' || ch == '[') ++depth;
        else if (ch == '}' || ch == ']') --depth;
        ++c.p;
    } while (depth);
    return true;
}

// A whole number. False for anything else (a fraction too); the cursor then stays.
inline bool JsonInt(JsonCursor& c, int32_t* v) {
    const char* p = c.p;
    const bool negative = p < c.end && *p == '-';
    if (negative) ++p;
    uint32_t n = 0, digits = 0;
    for (; p < c.end && *p >= '0' && *p <= '9'; ++p, ++digits) {
        if (n > 100000000u) return false;
        n = n * 10 + (uint32_t)(*p - '0');
    }
    if (!digits || (p < c.end && (*p == '.' || *p == 'e' || *p == 'E'))) return false;
    c.p = p;
    *v = negative ? -(int32_t)n : (int32_t)n;
    return true;
}

inline bool JsonKeyIs(const char* key, uint32_t len, const char* name) {
    uint32_t i = 0;
    for (; i < len && name[i]; ++i)
        if (key[i] != name[i]) return false;
    return i == len && !name[i];
}

enum { kJsonId1 = 1, kJsonId2 = 2, kJsonFlag1 = 4, kJsonFlag2 = 8, kJsonFirstSegment = 16, kJsonText = 32, kJsonEmpty = 64 };

struct MesJsonRecord {
    int32_t id1, id2, flag1, flag2, firstSegment;
    bool empty;        // "EmptyRecord": true
    uint32_t have;     // which fields the element has (kJson..), 0 for an element that is not an object
    uint32_t textLen;  // of "Text"; more than the buffer given = it did not fit
};

struct MesJsonReader {
    JsonCursor c;
    uint32_t index;  // elements read so far
};

// The next element of the array. 1 = read (into rec, its "Text" into text), 0 = the array has ended, -1 = not JSON of
// the expected shape (the reader cannot go on).
inline int MesJsonNext(MesJsonReader& r, MesJsonRecord* rec, char* text, uint32_t textCap) {
    JsonCursor& c = r.c;
    JsonWs(c);
    if (c.p >= c.end) return -1;
    if (r.index == 0) {
        if (*c.p != '[') return -1;
        ++c.p;
        JsonWs(c);
        if (c.p < c.end && *c.p == ']') return 0;
    } else {
        if (*c.p == ']') return 0;
        if (*c.p != ',') return -1;
        ++c.p;
        JsonWs(c);
    }
    if (c.p >= c.end) return -1;
    *rec = MesJsonRecord{};
    ++r.index;
    if (*c.p != '{') return JsonSkip(c) ? 1 : -1;
    ++c.p;
    JsonWs(c);
    if (c.p < c.end && *c.p == '}') {
        ++c.p;
        return 1;
    }
    struct { const char* name; int32_t* value; uint32_t bit; } const numbers[5] = {
        {"Id1", &rec->id1, kJsonId1}, {"Id2", &rec->id2, kJsonId2}, {"Flag1", &rec->flag1, kJsonFlag1},
        {"Flag2", &rec->flag2, kJsonFlag2}, {"FirstSegmentLenght", &rec->firstSegment, kJsonFirstSegment}};
    for (;;) {
        char key[32];
        uint32_t keyLen = 0;
        JsonWs(c);
        if (!JsonString(c, key, sizeof(key), &keyLen)) return -1;
        JsonWs(c);
        if (c.p >= c.end || *c.p != ':') return -1;
        ++c.p;
        JsonWs(c);
        if (c.p >= c.end) return -1;
        bool taken = false;
        if (keyLen <= sizeof(key)) {
            for (const auto& number : numbers)
                if (!taken && JsonKeyIs(key, keyLen, number.name) && JsonInt(c, number.value)) {
                    rec->have |= number.bit;
                    taken = true;
                }
            if (!taken && JsonKeyIs(key, keyLen, "Text") && *c.p == '"') {
                if (!JsonString(c, text, textCap, &rec->textLen)) return -1;
                rec->have |= kJsonText;
                taken = true;
            } else if (!taken && JsonKeyIs(key, keyLen, "EmptyRecord") && (*c.p == 't' || *c.p == 'f')) {
                rec->empty = *c.p == 't';
                rec->have |= kJsonEmpty;
            }
        }
        if (!taken && !JsonSkip(c)) return -1;
        JsonWs(c);
        if (c.p >= c.end) return -1;
        if (*c.p == '}') {
            ++c.p;
            return 1;
        }
        if (*c.p != ',') return -1;
        ++c.p;
    }
}

struct GlyphJsonReader {
    JsonCursor c;
    int state;  // 0 = before the file's object, 1 = inside it, 2 = inside "map", 3 = done
};

// The next entry of "map" in glyphmap.json. 1 = an entry: *key is the character (0 when the key is not one character of
// the basic plane), value[0..1] its carriers and *count how many characters the value has (0, or more than two: the
// entry is not usable). 0 = no more entries, -1 = not JSON of the expected shape.
inline int GlyphJsonNext(GlyphJsonReader& r, uint32_t* key, uint32_t value[2], uint32_t* count) {
    JsonCursor& c = r.c;
    char buf[64];
    uint32_t len = 0;
    for (;;) {
        JsonWs(c);
        if (r.state == 3) return 0;
        if (c.p >= c.end) return -1;
        if (r.state == 0) {
            if (*c.p != '{') return -1;
            ++c.p;
            r.state = 1;
            continue;
        }
        if (*c.p == ',') {
            ++c.p;
            continue;
        }
        if (*c.p == '}') {  // the end of "map" or of the file's object: either way there is nothing more to read
            r.state = 3;
            return 0;
        }
        if (!JsonString(c, buf, sizeof(buf), &len)) return -1;
        JsonWs(c);
        if (c.p >= c.end || *c.p != ':') return -1;
        ++c.p;
        JsonWs(c);
        if (c.p >= c.end) return -1;
        if (r.state == 1) {
            if (JsonKeyIs(buf, len, "map") && *c.p == '{') {
                ++c.p;
                r.state = 2;
            } else if (!JsonSkip(c)) {
                return -1;
            }
            continue;
        }
        const char* k = buf;
        const char* kEnd = buf + (len <= sizeof(buf) ? len : 0);
        uint32_t cp = 0;
        *key = (k < kEnd && Utf8Next(k, kEnd, &cp) && k == kEnd && cp >= 0x20 && cp <= 0xFFFF) ? cp : 0;
        value[0] = value[1] = 0;
        *count = 0xFF;
        if (*c.p != '"') return JsonSkip(c) ? 1 : -1;
        if (!JsonString(c, buf, sizeof(buf), &len)) return -1;
        if (len > sizeof(buf)) return 1;
        const char* v = buf;
        const char* vEnd = buf + len;
        uint32_t n = 0;
        while (v < vEnd) {
            if (!Utf8Next(v, vEnd, &cp) || cp < 0x20 || cp > 0xFFFF) return 1;
            if (n < 2) value[n] = cp;
            ++n;
        }
        *count = n;
        return 1;
    }
}

// A string with JSON's escapes, UTF-8 as it is.
inline void MesJsonEscape(MesText& t, const char* s, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        const char ch = s[i];
        if (ch == '"' || ch == '\\') {
            t.Char('\\');
            t.Char(ch);
        } else if ((uint8_t)ch < 0x20) {
            t.Str("\\u00");
            t.Char("0123456789abcdef"[(ch >> 4) & 15]);
            t.Char("0123456789abcdef"[ch & 15]);
        } else {
            t.Char(ch);
        }
    }
}

// One element of mes_all.json the way the tools lay it out (two spaces, CR LF), without the comma or bracket around it.
inline void MesJsonRecordOut(MesText& t, uint32_t id1, uint32_t id2, uint32_t flag1, uint32_t flag2, uint32_t firstSegment,
                             const char* text, uint32_t textLen, bool empty) {
    struct { const char* name; uint32_t value; } const numbers[5] = {
        {"Id1", id1}, {"Id2", id2}, {"Flag1", flag1}, {"Flag2", flag2}, {"FirstSegmentLenght", firstSegment}};
    t.Str("  {\r\n");
    for (const auto& number : numbers) {
        t.Str("    \"");
        t.Str(number.name);
        t.Str("\": ");
        t.Dec(number.value);
        t.Str(",\r\n");
    }
    t.Str("    \"Text\": \"");
    MesJsonEscape(t, text, textLen);
    t.Str("\",\r\n    \"EmptyRecord\": ");
    t.Str(empty ? "true" : "false");
    t.Str("\r\n  }");
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

// The places where FUN_0045A870 puts the item name into a template: it looks at every word of the record from the first,
// width words too, for two words 000Ah in a row (the text "**").
inline uint32_t MesTemplateSlots(const uint8_t* rec, uint32_t words) {
    uint32_t slots = 0;
    for (uint32_t i = 0; i + 1 < words; ++i)
        if (MesRd16(rec + i * 2) == 0x0A && MesRd16(rec + i * 2 + 2) == 0x0A) ++slots;
    return slots;
}

// The record `id` of the file when it is one a text can replace: understood token by token, and ending where the game's
// plain walk over FFFF words ends it. kMesOk, or why not (info->value as the error says).
inline MesError MesOriginalOf(const MesFile& f, const uint32_t* spanEnd, uint32_t id, MesOriginal* orig, MesErrorInfo* info) {
    info->column = info->value2 = info->padded = 0;
    info->value = f.count - 1;
    if (id >= f.count) return kMesNoSuchId;
    const uint32_t off = MesOffset(f, id);
    if (!off) return kMesNoRecord;
    if (!MesReadOriginal(f.data + off, (spanEnd[id] - off) / 2, orig, nullptr) ||
        orig->words * 2 != MesRecordBytes(f.data + off, spanEnd[id] - off))
        return kMesNotHandled;
    return kMesOk;
}

// Encodes `text` as the replacement of the record `id` of the file: MesEncode with the checks that need the file. `out`
// has room for kMesMaxWords words. *same = the text says what the record already says (the same words once the record's
// width words and line count are put right): such a record is left as it is, byte for byte.
inline MesError MesEncodeFor(const MesFile& f, const uint32_t* spanEnd, uint32_t id, const char* text, const char* end, const GlyphMap& map,
                             const uint8_t* icons, uint8_t* out, uint8_t* scratch, uint32_t* words, bool* same, MesErrorInfo* info) {
    MesOriginal orig;
    *same = false;
    MesError e = MesOriginalOf(f, spanEnd, id, &orig, info);
    if (e != kMesOk) return e;
    const bool isTemplate = MesIsTemplate(id);
    e = MesEncode(text, end, map, orig, icons, out, isTemplate ? orig.words : kMesMaxWords, words, info);
    if (e == kMesTooLong) info->value = isTemplate ? orig.words : kMesMaxWords;
    if (e != kMesOk) return (e == kMesTooLong && isTemplate) ? kMesTemplateGrew : e;
    // every slot copies the item name into the fixed buffer: as many as the loaded record has, and none by accident (a
    // line ten half cells wide that starts with '*')
    if (isTemplate && MesTemplateSlots(out, *words) != MesTemplateSlots(orig.rec, orig.words)) {
        info->value = MesTemplateSlots(orig.rec, orig.words);
        return kMesTemplateSlots;
    }
    if (*words == orig.words) {  // scratch: kMesMaxWords words
        MesNormalise(orig.rec, orig.words, scratch);
        *same = true;
        for (uint32_t i = 0; i < orig.words * 2 && *same; ++i) *same = scratch[i] == out[i];
    }
    return kMesOk;
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

// ---- A whole mes_all.json against the loaded file -----------------------------------------------------------------
// Before any text of the JSON is used, the file as a whole is compared with the loaded MES, record number by record
// number: this is what shows, on the translator's own data, that the JSON belongs to this MES and that the markers mean
// what MesEncode takes them to mean.
struct MesJsonCheck {
    uint32_t elements;               // elements of the array
    uint32_t compared;               // of them: not empty, with a record in the MES that is understood
    uint32_t headerDiffers;          // Id1 / Flag1 / Flag2 are not the words w0 / w2 / w3
    uint32_t linesDiffer;            // Id2 is not the word w1 (not held against the file: a tool may recompute it)
    uint32_t firstHeaderDiffers;     // the first such record number, for the log
    uint32_t either[kKindCount];     // records that have the marker or its code (kKindBr: {BR} or the code FFF0)
    uint32_t agree[kKindCount];      // of them: as many markers as codes
    uint32_t brTexts;                // records whose text has {BR}
    bool syntax;                     // the file could not be read to its end
};

// True when the header fields an element has are the header words of the record: Id1 = w0, Flag1 = w2, Flag2 = w3 (as
// 16-bit values: a tool may write 8000h as a negative number). Id2 would be w1, the line count, which a tool may recompute.
inline bool MesJsonHeaderMatches(const MesJsonRecord& r, const MesOriginal& orig) {
    return (!(r.have & kJsonId1) || (uint16_t)r.id1 == orig.w0) && (!(r.have & kJsonFlag1) || (uint16_t)r.flag1 == orig.w2) &&
           (!(r.have & kJsonFlag2) || (uint16_t)r.flag2 == orig.w3);
}

// One element into the check. `orig` = the record of the same number, null when it has none or is not understood.
inline void MesJsonCheckAdd(MesJsonCheck& c, uint32_t index, const MesJsonRecord& r, const char* text, uint32_t textLen, const MesOriginal* orig) {
    ++c.elements;
    if (r.empty || !(r.have & kJsonText) || !orig) return;
    ++c.compared;
    if (!MesJsonHeaderMatches(r, *orig) && !c.headerDiffers++) c.firstHeaderDiffers = index;
    if ((r.have & kJsonId2) && (uint16_t)r.id2 != orig->w1) ++c.linesDiffer;
    uint32_t marks[kKindCount];
    MesCountMarkers(text, text + textLen, marks);
    for (uint32_t k = 0; k < kKindCount; ++k) {
        if (k == kKindSizeBack) continue;
        const uint32_t codes = k == kKindBr ? orig->codes[kKindSizeBack] : orig->codes[k];
        if (!marks[k] && !codes) continue;
        ++c.either[k];
        if (marks[k] == codes) ++c.agree[k];
    }
    if (marks[kKindBr]) ++c.brTexts;
}

enum MesJsonVerdict { kJsonGood, kJsonNotThisMes, kJsonMarkersDiffer };

// The verdict. More than one record in twenty with other header words: the JSON was not made from this MES (or the fields
// are not what they are taken for). A marker that at least 20 records have and that agrees in number with its code in
// fewer than half of them: the marker is not that code (*kind says which). Translators keep the markers of a record, so
// a right reading agrees nearly everywhere and a wrong one nearly nowhere.
// *brCode: FFF0 when {BR} occurs and every record that has {BR} or the code FFF0 has as many of the one as of the other;
// 0 otherwise ({BR} then stays unknown).
inline MesJsonVerdict MesJsonJudge(const MesJsonCheck& c, uint32_t* kind, uint16_t* brCode) {
    *brCode = (c.brTexts && c.agree[kKindBr] == c.either[kKindBr]) ? kMesSizeBack : 0;
    *kind = 0;
    if (c.headerDiffers * 20 > c.compared) return kJsonNotThisMes;
    for (uint32_t k = kKindSegment; k <= kKindSpacing; ++k)
        if (c.either[k] >= 20 && c.agree[k] * 2 < c.either[k]) {
            *kind = k;
            return kJsonMarkersDiffer;
        }
    return kJsonGood;
}

}  // namespace ufx
