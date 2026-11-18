// Tools for translators. All of the game's text is one file, MES_ALL.MES, drawn by one function (FUN_0045C9F0, "the drawer"
// below) with one font atlas (FONTWIDE). Four things, each behind its own setting in [Language]; the overlay and the glyph
// log report to UltimateFix\loc.log:
//
//   TextOverlay    UltimateFix\text\mes_all.json and glyphmap.json are the two files of the SWERY Localization Tool (the JSON of
//                  DPMsgTool and the tool's glyph map), taken as they are; *.txt files with lines "0x1234 = text" in the same
//                  marker language can correct single records over them. When the game binds MES_ALL.MES (the call at 0045F679,
//                  its only load point; all 18 reads go through the pointer stored right after it) it gets a merged copy
//                  instead: every record in its place, the ones whose text differs encoded anew. Nothing is written to the
//                  game's files. Without those files there is no patch at all.
//   TextDump       writes the file the game loaded in the form of that JSON to UltimateFix\mes_dump.json.
//   ShowMessageId  the drawer is wrapped; after each record it drew, its id is printed over it in the game's small font.
//   GlyphLog       the one function every glyph quad goes through (FUN_0045A760) is watched: a cell that is empty in the atlas
//                  or lies beyond it, and a draw that fills the batch of 300h quads, are reported once per message and cell.
#include "../base.h"
#include "../sites.h"
#include "../mes.h"

// The callers of the glyph function keep values on the x87 stack across the call (their compiler knew the function has no
// floating point), so what runs inside that call may not touch the x87 registers: with this attribute GCC emits no x87 and
// no SSE instruction in a function.
#define INTEGER_ONLY __attribute__((target("general-regs-only")))

extern "C" {
INTEGER_ONLY void __cdecl LocGlyphWorker(ufx::u32 message, const ufx::u32* uv, ufx::u32 caller);
INTEGER_ONLY void __cdecl LocDrawEnter(ufx::u32 message, const ufx::u32* args);
void __cdecl LocDrawLeave(const ufx::u32* args);
}

namespace ufx {
namespace {

constexpr u32 kMessageVa = 0x00BDA000;     // the CMessage singleton
constexpr u32 kBatchCountOff = 0x12070;    // quads waiting in its batch (0045A760)
constexpr u32 kPenOff = 0x12098;           // pen x, y after a draw; the drawer reads them back after a nested draw (0045D7DE)
constexpr u32 kFontPictureOff = 0x34;      // CRdPicture of FONTWIDE (0045F626)
constexpr u32 kBatchQuads = 0x300;
constexpr u32 kTextPrintFn = 0x0045C540;   // cdecl (CMessage*, float x, float y, flags, rgba, format, ...): 16-pixel glyphs
constexpr u32 kResDataFn = 0x006B2BE0;     // thiscall (CRdData*, id, 0), ret 8: the data of a loaded resource or 0
constexpr u32 kDrawFirst = 0x0045C9F0, kDrawEnd = 0x0045EFCC;  // the drawer's code, up to its jump tables
constexpr u32 kMaxFileBytes = 32u << 20;
constexpr int kMaxTextFiles = 256;
constexpr int kMaxLogLines = 3000;
constexpr int kMaxRefusalLines = 1000;  // of them: refused texts listed one by one; the summary and the glyph log need the rest

GlyphMap g_map;

// ---- loc.log --------------------------------------------------------------------------------------------------------
HANDLE g_loc = INVALID_HANDLE_VALUE;
bool g_locTried = false;
int g_locLines = 0;

// One line (under 1000 characters with its arguments). The file is created at the first line of a run.
void LocLog(const char* fmt, ...) {
    if (!g_locTried) {
        g_locTried = true;
        wchar_t path[MAX_PATH + 16];
        lstrcpyW(path, g_paths.dataDir);
        lstrcatW(path, L"loc.log");
        g_loc = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (g_loc == INVALID_HANDLE_VALUE || g_locLines > kMaxLogLines) return;
    char buf[1100];
    int n;
    if (g_locLines++ == kMaxLogLines) {
        n = wsprintfA(buf, "(%d lines written, the rest of this run is not logged)", kMaxLogLines);
    } else {
        va_list ap;
        va_start(ap, fmt);
        n = wvsprintfA(buf, fmt, ap);  // never more than 1024 bytes
        va_end(ap);
    }
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD written;
    WriteFile(g_loc, buf, (DWORD)n, &written, nullptr);
}

// ---- files ----------------------------------------------------------------------------------------------------------
void TextPath(wchar_t* out, const wchar_t* name) {  // out: MAX_PATH * 2 characters
    ModTextDir(out);  // the text folder of the first mod that is on and carries text, else UltimateFix\text\ (mods.cpp)
    lstrcatW(out, name);
}

// A whole file in memory (VirtualFree it), with one zero byte after it. Null when it cannot be read or is too big.
u8* LoadFile(const wchar_t* path, u32* size) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    DWORD high = 0, low = GetFileSize(f, &high), got = 0;
    u8* data = nullptr;
    if (!high && low <= kMaxFileBytes) data = (u8*)VirtualAlloc(nullptr, low + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (data && (!ReadFile(f, data, low, &got, nullptr) || got != low)) {
        VirtualFree(data, 0, MEM_RELEASE);
        data = nullptr;
    }
    CloseHandle(f);
    *size = low;
    return data;
}

// The next line of a text buffer (without its line feed). False at the end.
bool NextLine(const char*& p, const char* end, const char** line, const char** lineEnd) {
    if (p >= end) return false;
    *line = p;
    while (p < end && *p != '\n') ++p;
    *lineEnd = p;
    if (p < end) ++p;
    return true;
}

// The start of the text of a UTF-8 file (after its byte order mark). Null for a file saved as UTF-16.
const char* TextStart(const u8* data, u32 size) {
    if (size >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) return nullptr;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) return (const char*)data + 3;
    return (const char*)data;
}

// What a character becomes: the plain table of the tools (cell = character - 20h), the tool's glyph map over it (the
// translator's own text\glyphmap.json, or the map the tool itself falls back to), and text\glyphmap.txt for characters
// that are given a cell directly.
void LoadGlyphMap() {
    GlyphMapDefault(g_map);
    wchar_t path[MAX_PATH * 2];
    u32 size = 0;
    TextPath(path, L"glyphmap.json");
    u8* data = LoadFile(path, &size);
    const char* p = data ? TextStart(data, size) : nullptr;
    if (!p) {
        GlyphMapToolDefaults(g_map);
        LocLog(data ? "glyph map: glyphmap.json is saved as UTF-16, save it as UTF-8; the tool's default map (67 characters) is used"
                    : "glyph map: no text\\glyphmap.json, the tool's default map (67 characters) is used");
    } else {
        GlyphJsonReader reader = {{p, (const char*)data + size}, 0};
        u32 key = 0, value[2] = {0, 0}, count = 0;
        int entries = 0, skipped = 0, more;
        while ((more = GlyphJsonNext(reader, &key, value, &count)) == 1) {
            if (key && count >= 1 && count <= 2) {
                GlyphMapSubst(g_map, key, value[0], value[1]);
                ++entries;
            } else if (++skipped <= 20) {
                LocLog("glyph map: glyphmap.json entry %d is not one character with one or two characters as its value, skipped", entries + skipped);
            }
        }
        if (more < 0) LocLog("glyph map: glyphmap.json could not be read to its end (not the JSON the tool writes)");
        if (!entries) GlyphMapToolDefaults(g_map);
        LocLog(entries ? "glyph map: %d characters from glyphmap.json" : "glyph map: glyphmap.json has no \"map\" with entries, the tool's default map is used", entries);
    }
    if (data) VirtualFree(data, 0, MEM_RELEASE);

    TextPath(path, L"glyphmap.txt");
    data = LoadFile(path, &size);
    if (!data) return;
    p = TextStart(data, size);
    const char* end = (const char*)data + size;
    int entries = 0, lineNo = 0;
    if (!p) {
        LocLog("glyph map: glyphmap.txt is saved as UTF-16, save it as UTF-8; not used");
    } else {
        const char *line, *lineEnd;
        while (NextLine(p, end, &line, &lineEnd)) {
            ++lineNo;
            u32 cp = 0, cell = 0;
            const MesLine kind = GlyphMapParseLine(line, lineEnd, &cp, &cell);
            if (kind == kLineEntry) {
                GlyphMapSet(g_map, cp, cell);
                ++entries;
            } else if (kind == kLineBad) {
                LocLog("glyph map: glyphmap.txt line %d is not \"<character> = <cell>\" with a cell of 0..0x97F, skipped", lineNo);
            }
        }
        LocLog("glyph map: %d characters with a cell of their own from glyphmap.txt", entries);
    }
    VirtualFree(data, 0, MEM_RELEASE);
}

// ---- the merged file ------------------------------------------------------------------------------------------------
struct Build {
    MesFile file;
    u32* order;        // ids in the physical order of their records
    u32 n;
    u32* spanEnd;      // by id: where the space of its record ends
    MesOverride* ov;   // by id
    u8* arena;         // the encoded records
    u32 arenaCap, arenaUsed;
    u8 icons[32];      // the icon numbers the loaded file uses
    int applied, refused, same, padded, files;
};

u8 g_encoded[kMesMaxWords * 2], g_normalised[kMesMaxWords * 2];

// Why a text was refused; %u / %04X take the value that comes with the error.
const char* RefusalText(MesError e) {
    switch (e) {
        case kMesBadUtf8: return "the text is not valid UTF-8";
        case kMesBadTag: return "a marker the tools do not have, or one that is not written as they write it";
        case kMesBadValue: return "the number %u of a marker is out of range";
        case kMesBadIcon: return "icon %u is not used anywhere in the game's own text";
        case kMesUnknownBr: return "{BR}: which code it stands for could not be established (see the mes_all.json lines above)";
        case kMesUnknownWord: return "the marker with 0x%04X is accepted only where the loaded record has that word, and it has not";
        case kMesLineTooLong: return "a line has %u characters, more than the 384 one draw can hold";
        case kMesTooLong: return "the record is longer than %u words";
        case kMesNoSuchId: return "the loaded MES_ALL.MES has the record numbers 0..%u";
        case kMesNoRecord: return "this number has no record in the loaded MES_ALL.MES (only existing records are replaced)";
        case kMesNotHandled: return "the record in the loaded file has a code this version does not handle, it is left alone";
        case kMesTemplateShape: return "the game fills this record in: it needs exactly one ** with something after it, as the loaded text has it";
        case kMesEmptyText: return "the text is empty where the loaded record has text: the game is not safe with an empty record (a single space gives a blank one)";
        case kMesTemplateSlots: return "the game puts an item name wherever this record has two words 10 in a row (\"**\", or a line 10 wide that starts with *): "
                                       "it must have %u such place(s), as the loaded one";
        default: return "the game copies this record into a fixed buffer, it may not be longer than the loaded one (%u words)";
    }
}

// Counts a refused text. True while its reason is still to be written: one mistake repeated in thousands of records (a
// character the glyph map does not have) would otherwise use up loc.log before the summary and the glyph log get a line.
bool ListRefusal(Build& b) {
    if (++b.refused <= kMaxRefusalLines) return true;
    if (b.refused == kMaxRefusalLines + 1)
        LocLog("text: more than %d texts refused: the rest are counted in the summary below and not listed", kMaxRefusalLines);
    return false;
}

// The text of one record, from `where` (a file and a place in it, for the log): encoded into the arena and noted for its
// id, left out when it says what the loaded record says, or refused with the reason in loc.log.
void ApplyText(Build& b, const char* where, u32 id, const char* text, const char* textEnd) {
    char reason[300];
    u32 words = 0;
    bool same = false;
    MesErrorInfo info = {0, 0, 0, 0};
    const MesError e = MesEncodeFor(b.file, b.spanEnd, id, text, textEnd, g_map, b.icons, g_encoded, g_normalised, &words, &same, &info);
    if (e != kMesOk || (!same && b.arenaUsed + words * 2 > b.arenaCap)) {
        if (!ListRefusal(b)) return;
        if (e == kMesOk) lstrcpyA(reason, "out of memory for the encoded text");
        else if (e == kMesNoGlyph && info.value2)
            wsprintfA(reason, "the character U+%04X is U+%04X by the glyph map, and that has no cell (the tools' characters are U+0020..7E and U+00A0..FF; "
                              "others need a line in text\\glyphmap.txt)", info.value, info.value2);
        else if (e == kMesNoGlyph)
            wsprintfA(reason, "the character U+%04X has no cell: it is not in the glyph map and not one of the tools' characters (U+0020..7E, U+00A0..FF); "
                              "a line in text\\glyphmap.txt can give it one", info.value);
        else wsprintfA(reason, RefusalText(e), info.value);
        // the column means something for the errors of the text itself, which come first in the list
        if (e != kMesOk && e < kMesTooLong) LocLog("text: %s, record %u (0x%04X), column %u of the text: %s; the record stays as loaded", where, id, id, info.column + 1, reason);
        else LocLog("text: %s, record %u (0x%04X): %s; the record stays as loaded", where, id, id, reason);
        return;
    }
    if (info.padded && ++b.padded <= 200)
        LocLog("text: %s, record %u (0x%04X): {NEXT_FRAME} came after an odd number of lines %u time(s); the window turns two lines at a time, "
               "so an empty line was added in front of it", where, id, id, info.padded);
    if (same) {
        if (b.ov[id].data) {  // an earlier file changed it, this one puts the loaded text back
            b.ov[id].data = nullptr;
            --b.applied;
        }
        ++b.same;
        return;
    }
    if (!b.ov[id].data) ++b.applied;
    u8* at = b.arena + b.arenaUsed;
    for (u32 i = 0; i < words * 2; ++i) at[i] = g_encoded[i];
    b.arenaUsed += words * 2;
    b.ov[id].data = at;
    b.ov[id].bytes = words * 2;
}

void ApplyFile(Build& b, const wchar_t* name, const u8* data, u32 size) {
    const char* p = TextStart(data, size);
    if (!p) {
        LocLog("text: %S is saved as UTF-16, save it as UTF-8; not used", name);
        return;
    }
    const char* end = (const char*)data + size;
    const char *line, *lineEnd, *text, *textEnd;
    int lineNo = 0;
    while (NextLine(p, end, &line, &lineEnd)) {
        ++lineNo;
        u32 id = 0;
        const MesLine kind = MesParseLine(line, lineEnd, &id, &text, &textEnd);
        if (kind == kLineEntry) {
            char where[1040];  // wsprintf writes 1024 characters at most; a name can take two bytes a character in some code pages
            wsprintfA(where, "%S line %d", name, lineNo);
            ApplyText(b, where, id, text, textEnd);
        } else if (kind == kLineBad && ListRefusal(b)) {
            LocLog("text: %S line %d is not \"0x1234 = text\" (a comment starts with ;); line skipped", name, lineNo);
        }
    }
    ++b.files;
}

const char* const kKindNames[kKindCount] = {"{NEXT_SEGMENT}", "{NEXT_FRAME}", "{ICON}", "{SET_COLOR}", "{RESET_COLOR}", "{SET_SPACING}", "{BR}", ""};
const u32 kKindCodes[kKindCount] = {kMesNewLine, kMesPage, kMesIcon, kMesColour, kMesColourBack, kMesSize, kMesSizeBack, kMesSizeBack};
constexpr u32 kJsonTextCap = 1u << 16;

// UltimateFix\text\mes_all.json, the file the SWERY Localization Tool saves (DPMsgTool's JSON): one element per record
// number. First the whole file is held against the loaded MES (MesJsonCheck); only a file that passes is used.
void ApplyJson(Build& b, const u8* data, u32 size) {
    const char* start = TextStart(data, size);
    if (!start) {
        LocLog("text: mes_all.json is saved as UTF-16, the tools write UTF-8; not used");
        return;
    }
    char* text = (char*)VirtualAlloc(nullptr, kJsonTextCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!text) return;
    const char* end = (const char*)data + size;
    MesJsonRecord r, firstBad = {};
    MesErrorInfo info;
    MesOriginal orig;
    MesJsonCheck check = {};
    MesJsonReader reader = {{start, end}, 0};
    int more;
    while ((more = MesJsonNext(reader, &r, text, kJsonTextCap)) == 1) {
        const u32 index = reader.index - 1, before = check.headerDiffers;
        const bool known = r.textLen <= kJsonTextCap && MesOriginalOf(b.file, b.spanEnd, index, &orig, &info) == kMesOk;
        MesJsonCheckAdd(check, index, r, text, r.textLen, known ? &orig : nullptr);
        if (!before && check.headerDiffers) firstBad = r;
    }
    u32 kind = 0;
    const MesJsonVerdict verdict = MesJsonJudge(check, &kind, &g_map.brCode);
    LocLog("text: mes_all.json: %u elements (the loaded MES has %u record numbers), %u compared with their records: Id1/Flag1/Flag2 are other "
           "than the header words in %u, Id2 other than the line count in %u", check.elements, b.file.count, check.compared, check.headerDiffers,
           check.linesDiffer);
    for (u32 k = kKindSegment; k <= kKindSpacing; ++k)
        LocLog("text: mes_all.json: %s and the code %04X: %u records have one of them, in %u of those as many of the one as of the other",
               kKindNames[k], kKindCodes[k], check.either[k], check.agree[k]);
    if (check.brTexts)
        LocLog(g_map.brCode ? "text: mes_all.json: {BR} is taken as the code FFF0: all %u records that have either have as many of the one as of the other"
                            : "text: mes_all.json: {BR} does not line up with the code FFF0 (%u records have either, %u agree): texts with {BR} are refused",
               check.either[kKindBr], check.agree[kKindBr]);
    if (more < 0) {
        LocLog("text: mes_all.json is not used: it could not be read beyond element %u (not the JSON the tools write, or cut off)", reader.index);
    } else if (verdict == kJsonNotThisMes) {
        MesOriginalOf(b.file, b.spanEnd, check.firstHeaderDiffers, &orig, &info);
        LocLog("text: mes_all.json is not used: it was not made from the MES_ALL.MES the game loaded. First difference: record %u has Id1=%d Flag1=%d "
               "Flag2=%d, the loaded record starts with the words %u, %u (lines), %u, %u", check.firstHeaderDiffers, firstBad.id1, firstBad.flag1,
               firstBad.flag2, orig.w0, orig.w1, orig.w2, orig.w3);
    } else if (verdict == kJsonMarkersDiffer) {
        LocLog("text: mes_all.json is not used: %s does not line up with the code %04X in the loaded MES (see the counts above), so this version "
               "does not read the markers of this file right", kKindNames[kind], kKindCodes[kind]);
    } else {
        reader = {{start, end}, 0};
        u32 noText = 0;
        while (MesJsonNext(reader, &r, text, kJsonTextCap) == 1) {
            const u32 index = reader.index - 1;
            if (r.empty) continue;
            if (!(r.have & kJsonText)) {
                ++noText;
            } else if (r.textLen > kJsonTextCap) {
                if (ListRefusal(b))
                    LocLog("text: mes_all.json, record %u (0x%04X): the text is longer than %u bytes; the record stays as loaded", index, index, kJsonTextCap);
            } else if (MesOriginalOf(b.file, b.spanEnd, index, &orig, &info) == kMesOk && !MesJsonHeaderMatches(r, orig)) {
                if (ListRefusal(b))
                    LocLog("text: mes_all.json, record %u (0x%04X): Id1/Flag1/Flag2 (%d, %d, %d) are not the header words of the loaded record (%u, %u, %u); "
                           "the record stays as loaded", index, index, r.id1, r.flag1, r.flag2, orig.w0, orig.w2, orig.w3);
            } else {
                ApplyText(b, "mes_all.json", index, text, text + r.textLen);
            }
        }
        if (noText) LocLog("text: mes_all.json: %u elements have no \"Text\" and are not marked empty; skipped", noText);
        ++b.files;
    }
    if (verdict != kJsonGood || more < 0) Log("text: UltimateFix\\text\\mes_all.json is not used, UltimateFix\\loc.log says why");
    VirtualFree(text, 0, MEM_RELEASE);
}

bool IsTextFileName(const wchar_t* name) { return lstrcmpiW(name, L"glyphmap.txt") != 0; }

// The names of UltimateFix\text\*.txt without glyphmap.txt, sorted, so that "the later line wins" does not depend on the
// order the file system lists them in. Returns how many; `names` may be null to count only.
int ListTextFiles(wchar_t (*names)[MAX_PATH]) {
    wchar_t pattern[MAX_PATH * 2];
    TextPath(pattern, L"*.txt");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !IsTextFileName(fd.cFileName) || n >= kMaxTextFiles) continue;
        if (names) {
            int at = n;
            for (; at > 0 && lstrcmpiW(names[at - 1], fd.cFileName) > 0; --at) lstrcpyW(names[at], names[at - 1]);
            lstrcpyW(names[at], fd.cFileName);
        }
        ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

bool HaveJson() {
    wchar_t path[MAX_PATH * 2];
    TextPath(path, L"mes_all.json");
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// Reads mes_all.json and then every text file into the override table (a line of a text file wins over the JSON). The
// arena is sized from the files: a record takes at most two bytes per byte of its text (a character of one byte is one
// word, a glyph map carrier pair two for a character of two bytes) plus 12 for its fixed words, on a line of at least 6
// bytes; each copy is checked against the arena anyway.
void ReadTextFiles(Build& b) {
    wchar_t (*names)[MAX_PATH] = (wchar_t (*)[MAX_PATH])VirtualAlloc(nullptr, sizeof(wchar_t) * MAX_PATH * kMaxTextFiles,
                                                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!names) return;
    wchar_t path[MAX_PATH * 2];
    TextPath(path, L"mes_all.json");
    u32 jsonSize = 0;
    u8* json = LoadFile(path, &jsonSize);
    if (!json && HaveJson()) LocLog("text: mes_all.json cannot be read (or is larger than 32 MB), not used");
    const int count = ListTextFiles(names);
    u8* data[kMaxTextFiles];
    u32 size[kMaxTextFiles];
    u32 total = json ? jsonSize : 0;
    for (int i = 0; i < count; ++i) {
        TextPath(path, names[i]);
        size[i] = 0;
        data[i] = LoadFile(path, &size[i]);
        if (!data[i]) LocLog("text: %S cannot be read (or is larger than 32 MB), not used", names[i]);
        else if (total + size[i] > kMaxFileBytes) {
            LocLog("text: %S not used, the text files together are larger than 32 MB", names[i]);
            VirtualFree(data[i], 0, MEM_RELEASE);
            data[i] = nullptr;
        } else total += size[i];
    }
    b.arenaCap = total * 3 + 4096;
    b.arena = (u8*)VirtualAlloc(nullptr, b.arenaCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!b.arena) Log("text: no memory for %u bytes of text files, the overlay is not used", total);
    if (json) {
        if (b.arena) ApplyJson(b, json, jsonSize);
        VirtualFree(json, 0, MEM_RELEASE);
    }
    for (int i = 0; i < count; ++i) {
        if (!data[i]) continue;
        if (b.arena) ApplyFile(b, names[i], data[i], size[i]);
        VirtualFree(data[i], 0, MEM_RELEASE);
    }
    VirtualFree(names, 0, MEM_RELEASE);
}

// Buffered writer for the dump.
struct Out {
    HANDLE file;
    char buf[8192];
    u32 n;
    void Flush() {
        DWORD written;
        if (n) WriteFile(file, buf, n, &written, nullptr);
        n = 0;
    }
    void Write(const char* s, u32 len) {
        for (u32 i = 0; i < len; ++i) {
            if (n == sizeof(buf)) Flush();
            buf[n++] = s[i];
        }
    }
    void Str(const char* s) { Write(s, (u32)lstrlenA(s)); }
};

// UltimateFix\mes_dump.json: the loaded file in the form of the tools' mes_all.json, as this code reads it (plain
// characters, no glyph map). Held next to the JSON DPMsgTool makes from the same MES it shows where the two readings
// differ, if anywhere.
void DumpFile(const Build& b) {
    wchar_t path[MAX_PATH + 16];
    lstrcpyW(path, g_paths.dataDir);
    lstrcatW(path, L"mes_dump.json");
    const u32 textCap = 1u << 16, elementCap = 1u << 18;
    Out* out = (Out*)VirtualAlloc(nullptr, sizeof(Out), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    char* text = (char*)VirtualAlloc(nullptr, textCap + elementCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    HANDLE f = (out && text) ? CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
    if (f == INVALID_HANDLE_VALUE) {
        Log("text: TextDump: UltimateFix\\mes_dump.json could not be written");
    } else {
        out->file = f;
        out->n = 0;
        out->Str("[\r\n");
        int records = 0, skipped = 0;
        for (u32 id = 0; id < b.file.count; ++id) {
            const u32 off = MesOffset(b.file, id);
            MesText element = {text + textCap, elementCap, 0};
            MesOriginal orig;
            u32 len = 0;
            const bool known = off && MesReadOriginal(b.file.data + off, (b.spanEnd[id] - off) / 2, &orig, nullptr);
            if (known) len = MesDecode(b.file.data + off, orig.words, text, textCap);
            if (!off) {
                MesJsonRecordOut(element, 0, 0, 0, 0, 0, text, 0, true);
            } else if (!known || len > textCap) {  // a code this version does not handle: the header words, no text
                MesJsonRecordOut(element, MesRd16(b.file.data + off), MesRd16(b.file.data + off + 2), MesRd16(b.file.data + off + 4),
                                 MesRd16(b.file.data + off + 6), 0, text, 0, false);
                ++skipped;
            } else {
                MesJsonRecordOut(element, orig.w0, orig.w1, orig.w2, orig.w3, MesRd16(b.file.data + off + 8), text, len, false);
                ++records;
            }
            if (element.n <= elementCap) out->Write(element.out, element.n);
            out->Str(id + 1 < b.file.count ? ",\r\n" : "\r\n");
        }
        out->Str("]");
        out->Flush();
        CloseHandle(f);
        Log("text: TextDump: %d records written to UltimateFix\\mes_dump.json (%d more could not be shown as text)", records, skipped);
    }
    if (text) VirtualFree(text, 0, MEM_RELEASE);
    if (out) VirtualFree(out, 0, MEM_RELEASE);
}

const u8* g_seenData = nullptr;  // the resource the copy below was made from
u32 g_seenSize = 0;
u8* g_merged = nullptr;

// The text the game is about to bind: dumped when asked, and merged with the text files. Returns the merged copy, or null
// when the game must keep its own (nothing to replace, or anything about the file that is not as expected).
u8* MergedFor(const u8* data, u32 size) {
    if (data == g_seenData && size == g_seenSize) return g_merged;  // bound again without a reload
    g_seenData = data;
    g_seenSize = size;
    // A copy made for an earlier load of the resource is left alone: it is small and nothing proves that no pointer into it
    // is still around.
    g_merged = nullptr;

    Build b = {};
    if (size > kMaxFileBytes || IsBadReadPtr(data, size) || !MesOpen(data, size, &b.file)) {
        Log("text: the loaded MES_ALL.MES is not the format this version knows (size %u), the text tools leave it alone", size);
        return nullptr;
    }
    const u32 count = b.file.count;
    u32* tables = (u32*)VirtualAlloc(nullptr, count * (2 * sizeof(u32) + sizeof(MesOverride)), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!tables) return nullptr;
    b.order = tables;
    b.spanEnd = tables + count;
    b.ov = (MesOverride*)(tables + 2 * count);  // zeroed by VirtualAlloc: no record replaced yet
    b.n = MesPhysicalOrder(b.file, b.order);
    if (b.n == 0xFFFFFFFF || b.n == 0) {
        Log("text: the offset table of the loaded MES_ALL.MES is not as expected, the text tools leave it alone");
        VirtualFree(tables, 0, MEM_RELEASE);
        return nullptr;
    }
    MesIndex(b.file, b.order, b.n, b.spanEnd, b.icons);
    LoadGlyphMap();
    if (g_cfg.textDump) DumpFile(b);
    if (g_cfg.textOverlay) ReadTextFiles(b);

    if (b.files)
        LocLog("text: %d records differ from the loaded MES_ALL.MES and are replaced, %d say the same and stay byte for byte, %d texts refused",
               b.applied, b.same, b.refused);
    if (b.applied) {
        const u32 total = MesMerge(b.file, b.order, b.n, b.ov, nullptr, 0);
        // 64 zero bytes behind the end: the game's scanners have no limit, a few words of slack cost nothing
        u8* merged = total ? (u8*)VirtualAlloc(nullptr, total + 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
        // Checked before it is handed over: the same ids in the same physical order.
        MesFile check;
        bool good = merged && MesMerge(b.file, b.order, b.n, b.ov, merged, total) == total && MesOpen(merged, total, &check) &&
                    check.count == count;
        if (good) {
            u32 prev = 0;
            for (u32 k = 0; k < b.n && good; ++k) {
                const u32 off = MesOffset(check, b.order[k]);
                good = off >= prev && off < total;
                prev = off;
            }
        }
        if (good) {
            g_merged = merged;
        } else {
            if (merged) VirtualFree(merged, 0, MEM_RELEASE);
            Log("text: the merged MES could not be built, the game keeps its own text");
            LocLog("text: the merged MES could not be built, the game keeps its own text");
        }
        if (g_merged) {
            Log("text: overlay active: %d records replaced from %d files in UltimateFix\\text (%d say what the loaded file says, %d texts "
                "refused, see UltimateFix\\loc.log); merged MES %u bytes, the loaded one %u", b.applied, b.files, b.same, b.refused, total, size);
        }
    } else if (b.files) {
        Log("text: no record replaced (%d files read, %d texts say what the loaded file says, %d refused, see UltimateFix\\loc.log), the game "
            "keeps its own text", b.files, b.same, b.refused);
    }
    if (b.arena) VirtualFree(b.arena, 0, MEM_RELEASE);
    VirtualFree(tables, 0, MEM_RELEASE);
    return g_merged;
}

// In place of the call FUN_006B2BE0(0x2400, 0) in the text module's setup (thiscall, ECX = CRdData, ret 8; the result goes
// to CMessage+44h). The size of the data is in the resource's slot: [CRdData+0Ch] + id*30h, +18h the data FUN_006B2BE0
// returns, +20h its size (the loader FUN_006B33F0 stores what the file reader reports there).
typedef void*(__attribute__((thiscall)) * ResDataFn)(void* rd, u32 id, u32 flag);
void* __attribute__((fastcall)) MesDataHook(void* rd, void* edx, u32 id, u32 flag) {
    (void)edx;
    void* data = ((ResDataFn)(uintptr_t)kResDataFn)(rd, id, flag);
    if (!data) return data;
    const u32 slot = *(u32*)((u8*)rd + 0xC) + id * 0x30;
    if (*(void**)(uintptr_t)(slot + 0x18) != data) return data;
    u8* merged = MergedFor((const u8*)data, *(u32*)(uintptr_t)(slot + 0x20));
    return merged ? merged : data;
}

// ---- the glyph log --------------------------------------------------------------------------------------------------
constexpr u32 kIdByteText = 0xFFFF, kIdUnknown = 0xFFFE;       // in place of a message id
constexpr u32 kCellOverflow = 0xFFFF, kCellNowhere = 0xFFFE;   // in place of a cell (the codes end at FFEEh)
constexpr u32 kSeenSlots = 8192, kSeenLimit = 6000, kRingSize = 256;

struct DrawFrame {
    u32 message, id;
};
DrawFrame g_draw[8];       // the drawer calls itself for an item name inside a text: the ids of the running draws
volatile int g_drawDepth = 0;
volatile bool g_inLabel = false;

u8 g_ink[kMesAtlasCells];  // bit 0: the left half of the cell has something drawn, bit 1: the right half
volatile bool g_inkReady = false;
bool g_inkTried = false;

u32 g_seen[kSeenSlots];    // (id << 16 | cell) reported already; 0 = free (cell 0 is the space and never reported)
u32 g_seenCount = 0;
volatile bool g_seenFull = false;
u32 g_ring[kRingSize];     // waiting to be written by the frame function
volatile u32 g_ringHead = 0, g_ringTail = 0;

// value * mul rounded to the nearest integer, from the bits of a float. FFFFFFFFh for a negative value, 256 and above, or NaN.
INTEGER_ONLY u32 FloatTimes(u32 bits, u32 mul) {
    if (bits & 0x80000000u) return (bits << 1) ? 0xFFFFFFFFu : 0;
    const u32 exp = bits >> 23;
    if (exp >= 127 + 8) return 0xFFFFFFFFu;
    if (exp < 126 - 24) return 0;
    const u64 mantissa = (bits & 0x7FFFFF) | 0x800000;
    // the value in 8.24 fixed point: mantissa * 2^(exp - 150 + 24)
    const u64 fixed = exp >= 126 ? mantissa << (exp - 126) : mantissa >> (126 - exp);
    return (u32)((fixed * mul + (1u << 23)) >> 24);
}

INTEGER_ONLY void Note(u32 id, u32 cell) {
    const u32 key = (id << 16) | cell;
    u32 at = (key * 2654435761u) >> 19;  // 13 bits
    for (; g_seen[at]; at = (at + 1) & (kSeenSlots - 1))
        if (g_seen[at] == key) return;
    if (g_seenCount >= kSeenLimit) {
        g_seenFull = true;
        return;
    }
    const u32 head = g_ringHead, next = (head + 1) & (kRingSize - 1);
    if (next == g_ringTail) return;  // the frame function has not caught up: reported at a later draw
    g_seen[at] = key;
    ++g_seenCount;
    g_ring[head] = key;
    g_ringHead = next;
}

// The font atlas the game draws from: CMessage+34h is a CRdPicture, [+8] its texture entries (20h bytes each), the
// IDirect3DTexture9 of the first at +14h (FUN_006B6020 binds exactly that). Created in the managed pool (FUN_006B58D0).
void* FontTexture() {
    const u32 end = UserAddressEnd();
    const u32 message = *(volatile u32*)(uintptr_t)kMessageVa;
    if (message < 0x10000 || message >= end) return nullptr;
    const u32 picture = *(volatile u32*)(uintptr_t)(message + kFontPictureOff);
    if (picture < 0x10000 || picture >= end) return nullptr;
    const u32 entries = *(volatile u32*)(uintptr_t)(picture + 8);
    if (entries < 0x10000 || entries >= end) return nullptr;
    const u32 texture = *(volatile u32*)(uintptr_t)(entries + 0x14);
    return (texture < 0x10000 || texture >= end) ? nullptr : (void*)(uintptr_t)texture;
}

constexpr u32 kInkLevel = 48;  // of 748: red*8 + green*4 + blue*8 of a 5:6:5 colour; below it is the black of an empty cell

u32 Brightness565(u32 c) { return ((c >> 11) & 31) * 8 + ((c >> 5) & 63) * 4 + (c & 31) * 8; }

// The texels of a DXT colour block that are not black, as 16 bits (bit = y * 4 + x).
u32 ColourInk(const u8* block, bool alwaysFour) {
    const u32 c0 = MesRd16(block), c1 = MesRd16(block + 2), index = MesRd32(block + 4);
    u32 level[4] = {Brightness565(c0), Brightness565(c1), 0, 0};
    if (alwaysFour || c0 > c1) {
        level[2] = (2 * level[0] + level[1]) / 3;
        level[3] = (level[0] + 2 * level[1]) / 3;
    } else {
        level[2] = (level[0] + level[1]) / 2;  // and the fourth is transparent black
    }
    u32 ink = 0;
    for (u32 i = 0; i < 16; ++i)
        if (level[(index >> (i * 2)) & 3] >= kInkLevel) ink |= 1u << i;
    return ink;
}

// The texels of a DXT5 alpha block that are not transparent.
u32 AlphaInk5(const u8* block) {
    u32 a[8] = {block[0], block[1], 0, 0, 0, 0, 0, 255};
    if (a[0] > a[1]) {
        for (u32 i = 2; i < 8; ++i) a[i] = ((8 - i) * a[0] + (i - 1) * a[1]) / 7;
    } else {
        for (u32 i = 2; i < 6; ++i) a[i] = ((6 - i) * a[0] + (i - 1) * a[1]) / 5;
    }
    const u64 index = (u64)MesRd32(block + 2) | ((u64)MesRd16(block + 6) << 32);
    u32 ink = 0;
    for (u32 i = 0; i < 16; ++i)
        if (a[(u32)(index >> (i * 3)) & 7] > 16) ink |= 1u << i;
    return ink;
}

// The texels of a DXT3 alpha block (4 bits each) that are not transparent.
u32 AlphaInk3(const u8* block) {
    u32 ink = 0;
    for (u32 i = 0; i < 16; ++i)
        if (((block[i / 2] >> ((i & 1) * 4)) & 15) > 1) ink |= 1u << i;
    return ink;
}

void MarkInk(u32 x, u32 y, u32 width, u32 height) {
    g_ink[(y * 38 / height) * 64 + x * 64 / width] |= (u8)(1u << ((x * 128 / width) & 1));
}

// Reads the atlas once and notes which cells have a glyph. The game maps a cell to the texture by fractions (column / 64,
// row / 38), so the same fractions are used here whatever the size of the texture.
void ReadAtlas(void* texture) {
    typedef HRESULT(WINAPI * GetDescFn)(void*, UINT, u32*);
    typedef HRESULT(WINAPI * LockFn)(void*, UINT, void*, const RECT*, DWORD);
    typedef HRESULT(WINAPI * UnlockFn)(void*, UINT);
    void** vt = *(void***)texture;
    u32 d[8] = {};
    struct { INT pitch; void* bits; } lock = {0, nullptr};
    constexpr u32 kDxt1 = 0x31545844, kDxt3 = 0x33545844, kDxt5 = 0x35545844, kArgb = 21, kXrgb = 22;
    if (((GetDescFn)vt[17])(texture, 0, d) < 0) {  // GetLevelDesc: Format, Type, Usage, Pool, ..., Width, Height
        LocLog("glyph log: the font atlas could not be examined, empty cells are not reported (cells beyond the atlas and full batches are)");
        return;
    }
    const u32 format = d[0], width = d[6], height = d[7];
    const bool dxt = format == kDxt1 || format == kDxt3 || format == kDxt5;
    if ((!dxt && format != kArgb && format != kXrgb) || width < 64 || height < 38 || width > 8192 || height > 8192 ||
        ((LockFn)vt[19])(texture, 0, &lock, nullptr, 0x10 /*D3DLOCK_READONLY*/) < 0) {
        LocLog("glyph log: the font atlas (%ux%u, format %08X) cannot be read, empty cells are not reported (cells beyond the atlas and full batches are)",
               width, height, format);
        return;
    }
    const u8* bits = (const u8*)lock.bits;
    if (!bits) {
        // locked without memory: nothing to read, the count below reports it
    } else if (dxt) {
        const u32 blockBytes = format == kDxt1 ? 8 : 16, across = (width + 3) / 4, down = (height + 3) / 4;
        const u32 pitch = (u32)lock.pitch >= across * blockBytes ? (u32)lock.pitch : across * blockBytes;
        for (u32 by = 0; by < down; ++by)
            for (u32 bx = 0; bx < across; ++bx) {
                const u8* block = bits + by * pitch + bx * blockBytes;
                u32 ink = ColourInk(block + (blockBytes - 8), format != kDxt1);
                if (format == kDxt5) ink &= AlphaInk5(block);
                else if (format == kDxt3) ink &= AlphaInk3(block);
                for (u32 i = 0; ink; ++i, ink >>= 1) {
                    const u32 x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if ((ink & 1) && x < width && y < height) MarkInk(x, y, width, height);
                }
            }
    } else {
        for (u32 y = 0; y < height; ++y) {
            const u8* row = bits + y * (u32)lock.pitch;
            for (u32 x = 0; x < width; ++x) {
                const u8* px = row + x * 4;  // blue, green, red, alpha
                if ((u32)px[0] + px[1] + px[2] >= 48 && (format == kXrgb || px[3] > 16)) MarkInk(x, y, width, height);
            }
        }
    }
    ((UnlockFn)vt[20])(texture, 0);
    u32 filled = 0;
    for (u32 i = 0; i < kMesAtlasCells; ++i) filled += g_ink[i] != 0;
    if (filled < 16) {  // not an atlas this reader understands (glyphs kept in a channel it does not look at)
        LocLog("glyph log: the font atlas (%ux%u, format %08X) looks empty to the reader, empty cells are not reported", width, height, format);
        return;
    }
    LocLog("glyph log: font atlas %ux%u, format %08X: %u of %u cells have a glyph", width, height, format, filled, kMesAtlasCells);
    g_inkReady = true;
}

void WriteNote(u32 key) {
    const u32 id = key >> 16, cell = key & 0xFFFF;
    char who[40];
    if (id == kIdByteText) lstrcpyA(who, "byte text (numbers, formats, credits)");
    else if (id == kIdUnknown) lstrcpyA(who, "message ?");
    else wsprintfA(who, "message 0x%04X", id);
    if (cell == kCellOverflow)
        LocLog("batch: %s: more than 768 glyph quads in one draw, the rest is not drawn (a glyph of any colour but black takes two)", who);
    else if (cell == kCellNowhere)
        LocLog("glyph: %s: a glyph far outside the font atlas (byte text with characters of 80h and above needs [Fixes] ByteTextCyrillic=1)", who);
    else if (cell >= kMesAtlasCells)
        LocLog("glyph: %s: cell 0x%X is beyond the font atlas (its cells are 0x000..0x97F)", who, cell);
    else
        LocLog("glyph: %s: cell 0x%03X (row %u, column %u) is empty in the font atlas", who, cell, cell >> 6, cell & 63);
}

// ---- the id over each drawn record ----------------------------------------------------------------------------------
typedef void(__cdecl* TextPrintFn)(void* message, float x, float y, int flags, const float* rgba, const char* format, ...);
const float kLabelColour[4] = {1.f, 0.9f, 0.2f, 1.f};
u32 g_labelled[64];  // ids labelled in this frame: a record drawn line by line, or twice for a shadow, gets one label
volatile u32 g_labelCount = 0;

float FloatOf(u32 bits) {
    union { u32 u; float f; } v;
    v.u = bits;
    return v.f;
}

// args: the drawer's stack arguments (x, y, flags, rgba, id, plain, first line, lines, scale, icon).
void DrawLabel(u32 message, const u32* args) {
    u32 id = args[4];
    if (id == 0xD33) return;             // the drawer returns at once for this id (0045CA05)
    if (id == 0x4400) id = 0x3F51;       // and shows this record instead (0045CA1A)
    const u32 count = g_labelCount;
    for (u32 i = 0; i < count; ++i)
        if (g_labelled[i] == id) return;
    if (count >= 64) return;
    // With 10000h the drawer left its quads in the batch for a later flush: the label joins them (and gives way when the
    // batch is nearly full). Without it the drawer reset the batch at its start and flushed it, so a draw of its own is safe.
    const u32 flags = args[2];
    const bool append = (flags & 0x10000) != 0;
    if (append && *(volatile u32*)(uintptr_t)(message + kBatchCountOff) > kBatchQuads - 32) return;
    g_labelled[count] = id;
    g_labelCount = count + 1;
    const u32 align = flags & 0x7FF;  // the record's own: 1 right, 2 and 3 centred, anything else from the left
    const int labelFlags = (align == 1 ? 1 : (align == 2 || align == 3) ? 2 : 0) | (append ? 0x10000 : 0);
    u32* pen = (u32*)(uintptr_t)(message + kPenOff);
    const u32 penX = pen[0], penY = pen[1];
    g_inLabel = true;
    ((TextPrintFn)(uintptr_t)kTextPrintFn)((void*)(uintptr_t)message, FloatOf(args[0]), FloatOf(args[1]) - 16.f, labelFlags, kLabelColour,
                                           "0x%04X", id);
    g_inLabel = false;
    pen[0] = penX;  // the caller may go on from where the record ended
    pen[1] = penY;
}

volatile bool g_glyphHooked = false;

}  // namespace

// Replaces the first 8 bytes of the drawer FUN_0045C9F0 (thiscall, ECX = CMessage, ten stack arguments, ret 28h; sub esp,0CCh /
// push ebx / push esi). The id is noted, the drawer runs on a copy of its arguments, then the label is drawn. Every register
// and the flags reach the drawer as the caller left them, and EAX (the result, in AL) goes back untouched.
__attribute__((naked)) void MesDrawStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "pushfd\n"
        "pushad\n"
        "lea eax, [esp+0x28]\n"       // the arguments: 4 flags + 32 registers + 4 return address
        "push eax\n"
        "push ecx\n"
        "call _LocDrawEnter\n"
        "add esp, 8\n"
        "popad\n"
        "popfd\n"
        "push dword ptr [esp+0x28]\n"  // ten times the same offset: each push moves ESP down to the next argument
        "push dword ptr [esp+0x28]\n"
        "push dword ptr [esp+0x28]\n"
        "push dword ptr [esp+0x28]\n"
        "push dword ptr [esp+0x28]\n"
        "push dword ptr [esp+0x28]\n"
        "push dword ptr [esp+0x28]\n"
        "push dword ptr [esp+0x28]\n"
        "push dword ptr [esp+0x28]\n"
        "push dword ptr [esp+0x28]\n"
        "call ufx_mes_draw_body\n"     // returns with ret 28h: the copies are gone
        "pushfd\n"
        "pushad\n"
        "lea eax, [esp+0x28]\n"
        "push eax\n"
        "call _LocDrawLeave\n"
        "add esp, 4\n"
        "popad\n"
        "popfd\n"
        "ret 0x28\n"
        "ufx_mes_draw_body:\n"
        "sub esp, 0xCC\n"
        "push ebx\n"
        "push esi\n"
        "push 0x0045C9F8\n"            // push and ret leave the flags alone
        "ret\n"
        ".att_syntax\n");
}

// Replaces the first 11 bytes of FUN_0045A760 (thiscall, ECX = CMessage, [esp+4] position, [esp+8] colour, [esp+0Ch] texture
// coordinates, ret 0Ch): mov eax,[ecx+12070h] / cmp eax,300h, whose flags the jge at 0045A76B reads. The worker is
// integer code and calls nothing outside this file (see INTEGER_ONLY).
__attribute__((naked)) void GlyphPushStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "pushfd\n"
        "pushad\n"
        "push dword ptr [esp+0x24]\n"  // the caller's return address
        "push dword ptr [esp+0x34]\n"  // the texture coordinates: [esp+0Ch] + 4 flags + 32 registers + the push above
        "push ecx\n"
        "call _LocGlyphWorker\n"
        "add esp, 12\n"
        "popad\n"
        "popfd\n"
        "mov eax, dword ptr [ecx+0x12070]\n"
        "cmp eax, 0x300\n"
        "push 0x0045A76B\n"
        "ret\n"
        ".att_syntax\n");
}

// Once per frame from the Present hook: what the hooks noted is written here, where calling Windows is safe.
void TextToolsFrame() {
    g_labelCount = 0;
    if (!g_glyphHooked) return;
    if (g_cfg.glyphLog && !g_inkTried) {
        void* texture = FontTexture();  // null until the game has loaded its text resources
        if (texture) {
            g_inkTried = true;
            ReadAtlas(texture);
        }
    }
    while (g_ringTail != g_ringHead) {
        WriteNote(g_ring[g_ringTail]);
        g_ringTail = (g_ringTail + 1) & (kRingSize - 1);
    }
    static bool fullLogged = false;
    if (g_seenFull && !fullLogged) {
        fullLogged = true;
        LocLog("glyph log: %u different findings in this run, further ones are not recorded", kSeenLimit);
    }
}

bool InstallTextTools() {
    const bool wantDraw = g_cfg.glyphLog || g_cfg.showMessageId;
    const bool wantData = g_cfg.textDump || (g_cfg.textOverlay && (HaveJson() || ListTextFiles(nullptr) > 0));
    if (!wantDraw && !wantData) return false;  // no text files and no tool asked for: nothing is patched
    if (g_cfg.safeMode) {
        Log("text: SafeMode: the text overlay, the text dump, the glyph log and the message ids are off for this run");
        return false;
    }
    bool any = false;
    if (wantData) {
        const SiteDef& s = kSiteMesData;
        const SiteDef& store = kSiteMesDataStore;  // the result must still go where every reader takes it from
        if (BytesMatch(store.va, store.bytes, store.len) && PatchCall(s.va, s.bytes, s.len, (const void*)MesDataHook)) {
            any = true;
            Log("text: the load of MES_ALL.MES is hooked (text overlay from UltimateFix\\text: %s, dump: %s)",
                g_cfg.textOverlay ? "on" : "off", g_cfg.textDump ? "on" : "off");
        } else {
            Log("text: the call at %08X differs from DP.exe 1.01b, no text overlay and no dump", s.va);
        }
    }
    if (wantDraw) {
        const SiteDef& d = kSiteMesDrawEntry;
        const SiteDef& dn = kSiteMesDrawNext;  // where the stubs go back to
        const SiteDef& g = kSiteGlyphPushEntry;
        const SiteDef& gn = kSiteGlyphPushNext;
        if (!BytesMatch(dn.va, dn.bytes, dn.len) || !PatchJump(d.va, d.bytes, d.len, (const void*)MesDrawStub)) {
            Log("text: the text drawer at %08X differs from DP.exe 1.01b, no message ids and no glyph log", d.va);
        } else {
            any = true;
            if (g_cfg.showMessageId) Log("text: ShowMessageId: the id of every drawn text record is printed over it");
            if (g_cfg.glyphLog) {
                if (BytesMatch(gn.va, gn.bytes, gn.len) && PatchJump(g.va, g.bytes, g.len, (const void*)GlyphPushStub)) {
                    g_glyphHooked = true;
                    Log("text: GlyphLog: empty atlas cells, cells beyond the atlas and full glyph batches go to UltimateFix\\loc.log");
                    LocLog("glyph log: on. Each finding is written once per message and cell.");
                } else {
                    Log("text: the glyph function at %08X differs from DP.exe 1.01b, no glyph log", g.va);
                }
            }
        }
    }
    return any;
}

}  // namespace ufx

// Before the drawer runs: which message the glyphs that follow belong to.
extern "C" INTEGER_ONLY void __cdecl LocDrawEnter(ufx::u32 message, const ufx::u32* args) {
    using namespace ufx;
    const int depth = g_drawDepth;
    if (depth >= 0 && depth < 8) {
        g_draw[depth].message = message;
        g_draw[depth].id = args[4];
    }
    g_drawDepth = depth + 1;
}

// After the drawer returned (its arguments are still on the caller's stack).
extern "C" void __cdecl LocDrawLeave(const ufx::u32* args) {
    using namespace ufx;
    const int depth = g_drawDepth - 1;
    g_drawDepth = depth;
    if (g_cfg.showMessageId && depth >= 0 && depth < 8) DrawLabel(g_draw[depth].message, args);
}

// At the entry of the glyph function, for every quad. No calls outside this file: Windows is called from the frame function.
extern "C" INTEGER_ONLY void __cdecl LocGlyphWorker(ufx::u32 message, const ufx::u32* uv, ufx::u32 caller) {
    using namespace ufx;
    if (!g_cfg.glyphLog || g_inLabel) return;
    // from the drawer: the message being drawn; from the two byte-string drawers (FUN_0045B820, FUN_0045BC50): no id
    const bool fromDrawer = caller - kDrawFirst < kDrawEnd - kDrawFirst;
    const int depth = g_drawDepth;
    u32 id = !fromDrawer ? kIdByteText : (depth >= 1 && depth <= 8) ? g_draw[depth - 1].id : kIdUnknown;
    if (fromDrawer && id >= kIdUnknown) id = kIdUnknown;
    if (*(volatile u32*)(uintptr_t)(message + kBatchCountOff) >= kBatchQuads) {  // this quad is dropped (0045A76B)
        Note(id, kCellOverflow);
        return;
    }
    // u0 = column * 32 / 2048 and v0 = row * 32 / 1216 for every drawer (0045E8F9..0045E93F)
    const u32 column = FloatTimes(uv[0], 64), row = FloatTimes(uv[1], 38);
    if (column > 63 || row > 1023) {
        Note(id, kCellNowhere);
        return;
    }
    const u32 cell = row * 64 + column;
    if (cell >= kMesAtlasCells) {
        Note(id, cell);
        return;
    }
    if (!cell || !g_inkReady) return;  // cell 0 is the space
    // a narrow glyph (every byte-string glyph, and the codes below E2h) is the left half of its cell
    const u32 halves = (!fromDrawer || cell < kMesWideFrom) ? 1 : 3;
    if (!(g_ink[cell] & halves)) Note(id, cell);
}
