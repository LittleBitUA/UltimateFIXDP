// The "Mods" page of the F10 menu: the mods in UltimateFix\mods\ (mods.cpp), switched on and off and put in order. Everything is
// saved at once and used from the next start, because the text and the textures are loaded when the game starts.
//
// On the left a compact list (the cover as a thumbnail, the name, version and author, the switch), on the right a card of the
// selected mod: its cover (350x350 pixels at 1080p), name, version and author, whether it is used, what it brings and its
// whole description. The card's parts shrink with the panel (the cover first), so the smallest panel stays tidy. Without
// mods, and on the "open the mods folder" row, the card explains how a mod folder is laid out.
#include "../base.h"
#include "ui.h"
#include "theme.h"

namespace ufx {
namespace ui {
namespace {

using namespace theme;  // the menu's colours (theme.h); the switch is the menu's too (ui.h)
constexpr float kRowH = 54, kThumb = 36;
constexpr float kCover = 350.f / 1.5f;  // 350 back buffer pixels at 1080p (scale 1.5); whole pixels there at every position
constexpr float kHeaderMin = 160;       // the narrowest the column beside the cover may get (the buttons must fit)
constexpr float kButtonH = 28, kChipH = 22;

int g_sel = 0;     // the selected row: 0..ModCount()-1 are mods, ModCount() is "open the folder"
int g_scroll = 0;  // the first row shown
// geometry of the last frame, for the mouse: the list, and the buttons of the card
float g_listX0 = 0, g_listX1 = 0, g_listTop = 0, g_switchX = 0;
int g_shown = 0;
enum Action { kUp, kDown, kOpenMod, kOpenAll };
struct Hit {
    float x, y, w, h;
    Action action;
    bool enabled;
};
Hit g_hits[4];
int g_hitCount = 0;
Glide g_glide;              // the row highlight glides like the menu's (GlideTo)
int g_flipRow = -1;         // the switch flipped last and when: its knob slides over
long long g_flipAt = 0;

bool English() { return g_cfg.language == 1; }
const wchar_t* T(const wchar_t* uk, const wchar_t* en) { return English() ? en : uk; }

void Toggle(int row) {
    ModToggle(row);
    g_flipRow = row;
    g_flipAt = MotionStamp();
}

void Move(int dir) {
    const int count = ModCount();
    if (g_sel >= count || g_sel + dir < 0 || g_sel + dir >= count) return;
    ModMove(g_sel, dir);
    g_sel += dir;
    g_flipRow = -1;  // the rows changed places: the knob must not slide on the other mod
}

void AddHit(float x, float y, float w, float h, Action action, bool enabled) {
    if (g_hitCount < 4) g_hits[g_hitCount++] = {x, y, w, h, action, enabled};
}

bool HasText(const ModInfo& m) { return m.textJson || m.textFiles; }

// "1.2  ·  Author"; empty when the mod gives neither.
void Byline(const ModInfo& m, wchar_t* out) {
    if (m.version[0] && m.author[0]) wsprintfW(out, L"%s  ·  %s", m.version, m.author);
    else if (m.version[0]) wsprintfW(out, T(L"версія %s", L"version %s"), m.version);
    else lstrcpyW(out, m.author);
}

// What a mod brings, short: the list's second line when the mod names no version or author.
void ShortContents(const ModInfo& m, wchar_t* out) {
    wchar_t part[40];
    out[0] = 0;
    if (HasText(m)) lstrcpyW(out, T(L"текст", L"text"));
    if (m.textures) {
        wsprintfW(part, T(L"%sтекстури: %d", L"%stextures: %d"), out[0] ? L"  ·  " : L"", m.textures);
        lstrcatW(out, part);
    }
    if (m.files) {
        wsprintfW(part, T(L"%sфайли гри: %d", L"%sgame files: %d"), out[0] ? L"  ·  " : L"", m.files);
        lstrcatW(out, part);
    }
    if (!out[0]) lstrcpyW(out, T(L"порожній", L"empty"));
}

// The first letter of the name in upper case, for a cover that is not there.
void Initial(const ModInfo& m, wchar_t* out) {
    const wchar_t* p = m.name;
    while (*p == L' ' || *p == L'[' || *p == L'(' || *p == L'_' || *p == L'-') ++p;
    out[0] = *p ? *p : L'?';
    out[1] = 0;
    CharUpperW(out);
}

// The cover's texture (null while it is not loaded or there is none) and the texture coordinates of its centred square: a
// picture that is not square is cropped, never stretched.
void* Cover(const ModInfo& m, float* uv) {
    uv[0] = uv[1] = 0.f;
    uv[2] = uv[3] = 1.f;
    u32 w = 0, h = 0;
    void* tex = m.cover[0] ? ImageTexture(m.cover, &w, &h) : nullptr;
    if (tex && w > h && w) {
        const float cut = (1.f - (float)h / (float)w) * 0.5f;
        uv[0] = cut;
        uv[2] = 1.f - cut;
    } else if (tex && h > w && h) {
        const float cut = (1.f - (float)w / (float)h) * 0.5f;
        uv[1] = cut;
        uv[3] = 1.f - cut;
    }
    return tex;
}

// A letter in the middle of a square, by the cap height of Segoe UI (its capitals sit 0.38 to 1.08 em below the text's top).
void CentredInitial(const ModInfo& m, float x, float y, float size, int px, u32 color) {
    wchar_t letter[2];
    Initial(m, letter);
    const float w = TextWidth(letter, px, kSemibold);
    DrawText(letter, x + (size - w) * 0.5f, y + size * 0.5f - 0.73f * (float)px, color, px, kSemibold);
}

void Thumb(const ModInfo& m, float x, float y) {
    float uv[4];
    if (void* tex = Cover(m, uv)) {
        DrawImage(tex, x, y, kThumb, kThumb, uv[0], uv[1], uv[2], uv[3], m.enabled ? 0xFFFFFFFFu : 0xA0FFFFFFu, 7);
        return;
    }
    FillRoundFramed(x, y, kThumb, kThumb, 7, kRaised, kLine2);
    CentredInitial(m, x, y, kThumb, 15, kFaint);
}

void BigCover(const ModInfo& m, float x, float y, float size) {
    float uv[4];
    if (void* tex = Cover(m, uv)) {
        DrawImage(tex, x, y, size, size, uv[0], uv[1], uv[2], uv[3], 0xFFFFFFFFu, 10);
        return;
    }
    // no picture: a calm square of the same size with the initial, large and faint; never an empty hole. The size of the
    // letter goes in steps of 8, so a panel being resized does not make a font for every size it passes
    FillRound(x, y, size, size, 10, kSelected);
    const int px = ((int)(size * 0.42f) / 8) * 8;
    if (px >= 16) CentredInitial(m, x, y, size, px, Alpha(kFaint, 0x50));
}

float Chip(float x, float y, const wchar_t* label, u32 color, bool draw) {
    const float w = TextWidth(label, 13) + 18;
    if (draw) {
        FillRoundFramed(x, y, w, kChipH, 6, kSelected, kLine2);
        DrawText(label, x + 9, y + 3, color, 13);
    }
    return w;
}

float ButtonWidth(const wchar_t* label) { return TextWidth(label, 14) + 26; }

// A quiet button like the menu's: a hairline border, rounded corners; one that does nothing here is only a faint label.
void Button(float x, float y, const wchar_t* label, Action action, bool enabled) {
    const float w = ButtonWidth(label);
    if (enabled) FillRoundFramed(x, y, w, kButtonH, 6, kSelected, kLine2);
    else FillRoundFramed(x, y, w, kButtonH, 6, kRaised, kLine);
    DrawText(label, x + 13, y + 5, enabled ? kText : Alpha(kFaint, 0x80), 14);
    AddHit(x, y, w, kButtonH, action, enabled);
}

// The red button that Enter presses (the menu's rule: red is the chosen thing, an action only while Enter would press it).
float FolderButton(float x, float y, bool draw) {
    const wchar_t* label = T(L"Відкрити теку модів", L"Open the mods folder");
    const float w = TextWidth(label, 15) + 32;
    if (draw) {
        FillRound(x, y, w, 32, 8, kAccent);
        DrawText(label, x + 16, y + 6, kWhite, 15);
        AddHit(x, y, w, 32, kOpenAll, true);
    }
    return 32;
}

// How a mod folder is laid out, with the button that opens the folder. `level` 0..4 says how much of it is shown, for the
// smaller panels: a smaller font, then without the note, without the introduction, and one line per entry. Returns the height.
constexpr int kHelpLevels = 5;
float FolderHelp(float x, float y, float w, int level, bool draw) {
    const int px = level == 0 ? 14 : 13;
    const float lh = (float)px + 6;
    const int valueLines = level <= 3 ? 2 : 1;
    float cy = y;
    if (draw) DrawText(T(L"Як додати мод", L"How to add a mod"), x, cy, kText, 20, kSemibold);
    cy += 34;
    if (level <= 2) {
        cy += lh * (float)DrawLines(T(L"Кожен мод — окрема тека в UltimateFix\\mods\\ поруч із DP.exe. Що в ній може бути:",
                                      L"Each mod is a folder of its own in UltimateFix\\mods\\ next to DP.exe. What it can hold:"),
                                    x, cy, kMuted, px, lh, w, 3, draw);
        cy += 10;
    }
    const wchar_t* const keys[6] = {L"text\\", L"textures\\", T(L"файли гри", L"game files"), L"mod.txt", L"description.txt", L"cover.png"};
    const wchar_t* const values[6] = {
        T(L"переклад: mes_all.json і glyphmap.json (SWERY Localization Tool)",
          L"a translation: mes_all.json and glyphmap.json (SWERY Localization Tool)"),
        T(L"заміни текстур: <хеш>.dds або .png", L"texture replacements: <hash>.dds or .png"),
        T(L"просто в теці мода: fontwide.xpc, mes_all.mes, FONTWIDE.DDS або шлях UPDATA\\...",
          L"right in the mod folder: fontwide.xpc, mes_all.mes, FONTWIDE.DDS or a path UPDATA\\..."),
        T(L"Назва:, Автор:, Версія:, Опис: (UTF-8; або mod.ini); \\n в описі — новий рядок",
          L"Name:, Author:, Version:, Description: (UTF-8; or mod.ini); \\n in it breaks the line"),
        T(L"довгий опис: увесь текст файлу (або опис.txt)", L"a long description: the whole text of the file"),
        T(L"картинка 350×350, PNG або JPG (або cover.jpg, preview.png)", L"the picture, 350×350, PNG or JPG (or cover.jpg, preview.png)"),
    };
    float keyW = 0;
    for (const wchar_t* key : keys) {
        const float kw = TextWidth(key, px);
        if (kw > keyW) keyW = kw;
    }
    keyW += 16;
    if (keyW > w * 0.45f) keyW = w * 0.45f;
    for (int i = 0; i < 6; ++i) {
        if (draw) DrawFitted(keys[i], x, cy, kText, px, keyW - 8);
        cy += lh * (float)DrawLines(values[i], x + keyW, cy, kMuted, px, lh, w - keyW, valueLines, draw) + 3;
    }
    if (level <= 1) {
        cy += 8;
        cy += 18.f * (float)DrawLines(T(L"Хеші текстур: DumpTextures=1 у [Debug] UltimateFix.ini, гра скине всі текстури в UltimateFix\\textures\\dump\\ з готовими назвами",
                                        L"Texture hashes: DumpTextures=1 in [Debug] of UltimateFix.ini dumps every texture to UltimateFix\\textures\\dump\\ named by its hash"),
                                      x, cy, kFaint, 13, 18, w, 2, draw);
    }
    cy += 16;
    cy += FolderButton(x, cy, draw);
    return cy - y;
}

// The most of FolderHelp that fits in `room`.
int HelpLevel(float w, float room) {
    for (int level = 0; level < kHelpLevels - 1; ++level)
        if (FolderHelp(0, 0, w, level, false) <= room) return level;
    return kHelpLevels - 1;
}

// The description, word-wrapped: in a smaller font when it needs more lines than fit, cut with "…" when even that is not
// enough. Without one, a faint line says how to add one (or that the folder is empty).
void Description(const ModInfo& m, float x, float y, float w, float h) {
    const wchar_t* text = m.description;
    u32 color = kMuted;
    if (!text[0]) {
        color = kFaint;
        if (!HasText(m) && !m.textures && !m.files)
            text = T(L"Тека мода порожня. Покладіть у неї text\\ (переклад), textures\\ (заміни текстур) або файли гри й перезапустіть гру.",
                     L"The mod folder is empty. Put text\\ (a translation), textures\\ (texture replacements) or game files in it and restart the game.");
        else if (m.cover[0])
            text = T(L"Опису немає. Автор мода може додати його рядком «Опис:» у mod.txt або файлом description.txt.",
                     L"No description. The mod's author can add one as a \"Description:\" line in mod.txt or as description.txt.");
        else
            text = T(L"Опису немає. Автор мода може додати його рядком «Опис:» у mod.txt або файлом description.txt, а картинку — файлом cover.png (350×350).",
                     L"No description. The mod's author can add one as a \"Description:\" line in mod.txt or as description.txt, and a picture as cover.png (350×350).");
    }
    static const int kPx[3] = {15, 14, 13};
    static const float kLineH[3] = {22, 20, 18};
    for (int i = 0; i < 3; ++i) {
        const int lines = (int)(h / kLineH[i]);
        if (lines < 1) continue;
        if (i < 2 && DrawLines(text, 0, 0, 0, kPx[i], kLineH[i], w, lines + 1, false) > lines) continue;
        DrawLines(text, x, y, color, kPx[i], kLineH[i], w, lines, true);
        return;
    }
}

// Rows of buttons (or chips) of these widths in `room`, 6 apart.
int RowsFor(const float* widths, int n, float room) {
    int rows = 1;
    float x = 0;
    for (int i = 0; i < n; ++i) {
        if (x > 0 && x + widths[i] > room) {
            ++rows;
            x = 0;
        }
        x += widths[i] + 6;
    }
    return rows;
}

// Lays the items out from x,y in rows of `room` (as RowsFor) and returns the left edge of each in xs, the top in ys.
void PlaceRows(const float* widths, int n, float x, float y, float room, float rowH, float* xs, float* ys) {
    float cx = x;
    for (int i = 0; i < n; ++i) {
        if (cx > x && cx + widths[i] > x + room) {
            cx = x;
            y += rowH + 6;
        }
        xs[i] = cx;
        ys[i] = y;
        cx += widths[i] + 6;
    }
}

// Where the parts of the card's head go: beside the cover, or under it across the whole card (and so every part after that
// one). On the small panel the column beside the cover is narrow, and the chips and the buttons are better off under it than
// squeezed into it; on the big one a part that juts out a little below the cover leaves the description more room beside it.
struct Flow {
    float x, w;            // where the next part starts and how wide it may be
    float y;
    float wideX, wideW;    // across the card
    float coverBottom;
    bool under;
};
// Goes under the cover when a part (`besideH` high in the column, `underH` across the card) ends higher there, and always once
// the column has passed the cover. Returns true when it moved there with this part.
bool Place(Flow& f, float besideH, float underH) {
    if (f.under) return false;
    const float under = (f.y > f.coverBottom + 16 ? f.y : f.coverBottom + 16) + underH;
    const float beside = f.y + besideH > f.coverBottom ? f.y + besideH : f.coverBottom;
    if (f.y < f.coverBottom && beside <= under) return false;
    f.under = true;
    f.x = f.wideX;
    f.w = f.wideW;
    if (f.y < f.coverBottom + 16) f.y = f.coverBottom + 16;
    return true;
}

// The card of the selected mod: the cover top left, beside it the name, version and author, whether it is used, what it
// brings and the buttons, and the description across the card under them.
void Card(const ModInfo& m, int index, float x, float y, float w, float h) {
    const float pad = w < 380 ? 16 : 20, gap = pad;
    float img = kCover;
    if (img > w - 2 * pad - gap - kHeaderMin) img = w - 2 * pad - gap - kHeaderMin;
    if (img > h - 2 * pad - 18 - 3 * 18) img = h - 2 * pad - 18 - 3 * 18;  // room for three lines of the description
    if (img < 64) img = 0;                         // too small to be worth it: the text takes the card
    else if (img < kCover) img = SnapWhole(img);   // the full size stays 350/1.5, exactly 350 pixels at 1080p
    const float ix = x + pad, iy = y + pad, inner = w - 2 * pad;
    if (img > 0) BigCover(m, ix, iy, img);
    Flow f;
    f.wideX = ix;
    f.wideW = inner;
    f.coverBottom = iy + img;
    f.under = img <= 0;
    f.x = f.under ? ix : ix + img + gap;
    f.w = x + w - pad - f.x;
    f.y = iy;

    // the name, in up to two lines (always beside the cover: it is the card's title)
    const int namePx = f.w >= 200 ? 22 : 19;
    const float nameLh = (float)namePx + 7;
    f.y += nameLh * (float)DrawLines(m.name, f.x, f.y, kText, namePx, nameLh, f.w, 2, true, kSemibold) + 2;

    wchar_t line[160];
    Place(f, 22, 22);
    Byline(m, line);
    if (line[0]) DrawFitted(line, f.x, f.y, kMuted, 15, f.w);
    else DrawFitted(T(L"Автор: невідомий", L"Author unknown"), f.x, f.y, kFaint, 15, f.w);
    f.y += 30;

    // on or off, and where the text comes from: this run's source (ModTextSource) or, after a change, the next start's. The
    // second part goes on a line of its own when the column is too narrow for both
    const wchar_t* state = m.enabled ? T(L"Увімкнено", L"On") : T(L"Вимкнено", L"Off");
    const wchar_t* source = nullptr;
    if (HasText(m)) {
        bool shadowed = false;
        for (int k = 0; k < index; ++k) {
            const ModInfo* above = ModAt(k);
            if (above && above->enabled && HasText(*above)) shadowed = true;
        }
        if (ModTextSource() == index)
            source = m.enabled ? T(L"текст звідси", L"text from here") : T(L"текст звідси до перезапуску", L"text from here until the restart");
        else if (m.enabled && shadowed) source = T(L"текст перекрито модом вище", L"text overridden by a mod above");
        else if (m.enabled) source = T(L"текст — з наступного запуску", L"text from the next start");
    }
    const float stateW = 15 + TextWidth(state, 15), sourceW = source ? TextWidth(source, 15) + 18 : 0;
    Place(f, stateW + sourceW <= f.w ? 22 : 44, stateW + sourceW <= f.wideW ? 22 : 44);
    FillRound(f.x, f.y + 8, 7, 7, 3.5f, m.enabled ? kAccent : kLine2);  // on the middle of the capitals of 15 px
    DrawText(state, f.x + 15, f.y, m.enabled ? kText : kMuted, 15);
    if (source && stateW + sourceW <= f.w) {
        wsprintfW(line, L"·  %s", source);
        DrawText(line, f.x + stateW + 8, f.y, kFaint, 15);
    } else if (source) {
        f.y += 22;
        DrawFitted(source, f.x + 15, f.y, kFaint, 15, f.w - 15);
    }
    f.y += 30;

    // what it brings, as chips that wrap
    wchar_t chips[3][40];
    float chipW[3], chipX[3], chipY[3];
    int chipCount = 0;
    if (HasText(m)) lstrcpyW(chips[chipCount++], T(L"Текст", L"Text"));
    if (m.textures) wsprintfW(chips[chipCount++], T(L"Текстури: %d", L"Textures: %d"), m.textures);
    if (m.files) wsprintfW(chips[chipCount++], T(L"Файли гри: %d", L"Game files: %d"), m.files);
    const bool empty = !chipCount;
    if (empty) lstrcpyW(chips[chipCount++], T(L"Порожній", L"Empty"));
    for (int i = 0; i < chipCount; ++i) chipW[i] = Chip(0, 0, chips[i], 0, false);
    Place(f, (float)RowsFor(chipW, chipCount, f.w) * (kChipH + 6) - 6, (float)RowsFor(chipW, chipCount, f.wideW) * (kChipH + 6) - 6);
    PlaceRows(chipW, chipCount, f.x, f.y, f.w, kChipH, chipX, chipY);
    for (int i = 0; i < chipCount; ++i) Chip(chipX[i], chipY[i], chips[i], empty ? kFaint : kMuted, true);
    f.y = chipY[chipCount - 1] + kChipH;

    // the buttons: beside the cover with their last row on its lower edge, or under it
    const wchar_t* labels[3] = {T(L"Вище", L"Up"), T(L"Нижче", L"Down"), T(L"Відкрити теку мода", L"Open its folder")};
    const Action actions[3] = {kUp, kDown, kOpenMod};
    const bool enabled[3] = {index > 0, index + 1 < ModCount(), true};
    float buttonW[3], buttonX[3], buttonY[3];
    for (int i = 0; i < 3; ++i) buttonW[i] = ButtonWidth(labels[i]);
    if (RowsFor(buttonW, 3, f.wideW) > 1) {  // the small panel: one row with the shorter word
        labels[2] = T(L"Тека мода", L"Folder");
        buttonW[2] = ButtonWidth(labels[2]);
    }
    const float besideH = (float)RowsFor(buttonW, 3, f.w) * (kButtonH + 6) - 6;
    float by = f.y + 16;
    if (Place(f, 16 + besideH, 16 + (float)RowsFor(buttonW, 3, f.wideW) * (kButtonH + 6) - 6) && f.y > by) by = f.y;
    else if (!f.under && f.coverBottom - besideH > by) by = f.coverBottom - besideH;
    PlaceRows(buttonW, 3, f.x, by, f.w, kButtonH, buttonX, buttonY);
    for (int i = 0; i < 3; ++i) Button(buttonX[i], buttonY[i], labels[i], actions[i], enabled[i]);
    f.y = buttonY[2] + kButtonH;

    // the description under all of it, across the card
    const float dy = (f.y > f.coverBottom ? f.y : f.coverBottom) + 18, bottom = y + h - pad;
    if (bottom - dy >= 18) {
        FillRect(ix, dy - 9, inner, 1, kLine);
        Description(m, ix, dy, inner, bottom - dy);
    }
}

void List(float x0, float x1, float top, float bottom, int count) {
    const int rows = count + 1;
    int fit = (int)((bottom - top) / kRowH);
    if (fit < 1) fit = 1;
    if (g_sel < g_scroll) g_scroll = g_sel;
    if (g_sel >= g_scroll + fit) g_scroll = g_sel - fit + 1;
    if (g_scroll > rows - fit) g_scroll = rows - fit;
    if (g_scroll < 0) g_scroll = 0;
    const bool thumbs = x1 - x0 >= 250;  // in the narrowest list the names need the room more
    const float switchX = x1 - kSwitchW, textX = thumbs ? x0 + kThumb + 12 : x0;
    g_listX0 = x0 - 12;
    g_listX1 = x1 + 4;
    g_listTop = top;
    g_switchX = switchX;
    g_shown = 0;
    // the highlight first, under the rows, gliding from the row before (relative to the top of the list)
    if (g_sel >= g_scroll && g_sel - g_scroll < fit) {
        const float hy = top + GlideTo(g_glide, (float)(g_sel - g_scroll) * kRowH, g_sel, g_scroll);
        FillRect(x0 - 12, hy + 2, x1 - x0 + 16, kRowH - 4, kSelected);
        FillRect(x0 - 12, hy + 2, 3, kRowH - 4, kAccent);
    }
    for (int r = g_scroll; r < rows && g_shown < fit; ++r, ++g_shown) {
        const float ry = top + (float)g_shown * kRowH;
        const bool sel = r == g_sel;
        if (r == count) {  // the action row, apart from the mods by a hairline
            if (g_shown) FillRect(x0, ry, x1 - x0, 1, kLine);
            DrawFitted(T(L"Відкрити теку модів", L"Open the mods folder"), textX, ry + 17, sel ? kText : kMuted, 15, x1 - textX);
            continue;
        }
        const ModInfo& m = *ModAt(r);
        if (thumbs) Thumb(m, x0, ry + (kRowH - kThumb) / 2);
        Switch(switchX, ry + (kRowH - kSwitchH) / 2, m.enabled, r == g_flipRow ? MotionProgress(g_flipAt, kSwitchSeconds) : 1.f);
        const float tw = switchX - 12 - textX;
        DrawFitted(m.name, textX, ry + 9, sel ? kWhite : m.enabled ? kText : kMuted, 16, tw, sel ? kSemibold : kRegular);
        wchar_t sub[160];
        Byline(m, sub);
        if (!sub[0]) ShortContents(m, sub);
        DrawFitted(sub, textX, ry + 31, kFaint, 13, tw);
    }
    if (rows > fit) {  // the same slim scroll bar as the settings pages, in the gap before the card
        const float trackH = (float)fit * kRowH, frac = (float)fit / (float)rows;
        const float pos = (float)g_scroll / (float)(rows - fit);
        FillRound(x1 + 9, top, 4, trackH, 2, kLine);
        FillRound(x1 + 7, top + (trackH - trackH * frac) * pos, 8, trackH * frac, 4, kFaint);
    }
}

void ClampSelection() {
    const int rows = ModCount() + 1;
    if (g_sel < 0) g_sel = 0;
    if (g_sel >= rows) g_sel = rows - 1;
}

}  // namespace

void ModsPageInput(bool up, bool down, bool toggle, bool raise, bool lower, bool click, float mx, float my) {
    const int count = ModCount();
    if (up) --g_sel;
    if (down) ++g_sel;
    ClampSelection();
    if (toggle) {
        if (g_sel < count) Toggle(g_sel);
        else OpenModsFolder();
    }
    if (raise || lower) Move(raise ? -1 : 1);
    if (!click) return;
    // the card's buttons, as the last frame drew them
    for (int i = 0; i < g_hitCount; ++i) {
        const Hit& b = g_hits[i];
        if (mx < b.x || mx >= b.x + b.w || my < b.y || my >= b.y + b.h) continue;
        if (!b.enabled) return;
        if (b.action == kUp) Move(-1);
        else if (b.action == kDown) Move(1);
        else if (b.action == kOpenMod) ModOpenFolder(g_sel);
        else OpenModsFolder();
        return;
    }
    if (!g_shown || mx < g_listX0 || mx > g_listX1 || my < g_listTop) return;
    const int row = g_scroll + (int)((my - g_listTop) / kRowH);
    if (row >= g_scroll + g_shown || row > count) return;
    g_sel = row;
    if (row == count) OpenModsFolder();
    else if (mx >= g_switchX - 6) Toggle(row);  // the switch (and a little room around it)
}

void ModsPage(float left, float top, float right, float bottom) {
    const int count = ModCount();
    ClampSelection();
    g_hitCount = 0;
    if (count == 0)
        DrawFitted(T(L"Enter відкрити теку модів     Tab розділ     F10 або Esc закрити", L"Enter open the mods folder     Tab section     F10 or Esc close"),
                   left, bottom + 8, kFaint, 13, right - left);
    else
        DrawFitted(T(L"↑ ↓ вибір     Enter вмк./вимк.     ← → вище / нижче     Tab розділ     F10 або Esc закрити",
                     L"↑ ↓ select     Enter on/off     ← → higher / lower     Tab section     F10 or Esc close"), left, bottom + 8, kFaint, 13, right - left);
    const float width = right - left;
    if (count == 0) {  // a calm card in the middle that says how to make one
        g_shown = 0;
        const float w = width < 640 ? width : 640, x = left + (width - w) / 2, pad = width < 640 ? 22 : 28;
        const int level = HelpLevel(w - 2 * pad, bottom - top - 2 * pad);
        const float h = FolderHelp(0, 0, w - 2 * pad, level, false) + 2 * pad;
        float y = top + (bottom - top - h) / 2;
        if (y < top) y = top;
        FillRoundFramed(x, y, w, h, 12, kRaised, kLine2);  // opaque like the menu's dialog: a frame shows through a see-through fill
        FolderHelp(x + pad, y + pad, w - 2 * pad, level, true);
        return;
    }

    if (ModsChanged()) {
        DrawFitted(T(L"Зміни збережено. Вони почнуть діяти після перезапуску гри: текст і текстури вантажаться на старті.",
                     L"Saved. The changes take effect after the game is restarted: text and textures are loaded at start."), left, top, kWarn, 15, width);
    } else {
        DrawFitted(T(L"Мод вище в списку перекриває ті, що нижче. Текст береться з першого ввімкненого мода з текстом.",
                     L"A mod higher in the list wins over the ones below. Text comes from the first mod that is on and has text."), left, top, kMuted, 15, width);
    }
    const float y = top + 32;
    float listW = SnapWhole(width * 0.34f);  // a third: the card needs the cover and a column beside it
    if (listW < 200) listW = 200;
    if (listW > 360) listW = 360;
    const float gap = width < 640 ? 18 : 24, cardX = left + listW + gap, cardW = right - cardX, cardH = bottom - y;
    // the card first: the selected mod's cover is then the picture read in this frame (ImageTexture reads one a frame)
    FillRoundFramed(cardX, y, cardW, cardH, 12, kRaised, kLine2);
    if (g_sel < count) {
        Card(*ModAt(g_sel), g_sel, cardX, y, cardW, cardH);
    } else {
        const float pad = 20;
        FolderHelp(cardX + pad, y + pad, cardW - 2 * pad, HelpLevel(cardW - 2 * pad, cardH - 2 * pad), true);
    }
    List(left, left + listW, y, bottom, count);
}

}  // namespace ui
}  // namespace ufx
