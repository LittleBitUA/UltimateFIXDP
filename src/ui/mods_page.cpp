// The "Mods" page of the F10 menu: the mods in UltimateFix\mods\ (mods.cpp), switched on and off and put in order. Everything is
// saved at once and used from the next start, because the text and the textures are loaded when the game starts.
#include "../base.h"
#include "ui.h"

namespace ufx {
namespace ui {
namespace {

constexpr u32 kText = 0xFFF2F2F5, kSoft = 0xFFC4C4CE, kDim = 0xFF9696A4, kAccent = 0xFFD0343C, kLine = 0xFF2A2A34,
              kRowSel = 0xFF1D1D26, kOn = 0xFF35C27A, kOff = 0xFF3C3C48, kAmber = 0xFFE5A83A;
constexpr float kRowH = 52, kSwitchW = 46, kSwitchH = 22, kButtonW = 30, kButtonH = 26;

int g_sel = 0;     // the selected row: 0..ModCount()-1 are mods, ModCount() is "open the folder"
int g_scroll = 0;  // the first row shown
// geometry of the last frame, for the mouse
float g_left = 0, g_top = 0, g_right = 0;
int g_shown = 0;

bool English() { return g_cfg.language == 1; }
const wchar_t* T(const wchar_t* uk, const wchar_t* en) { return English() ? en : uk; }

void Fitted(const wchar_t* text, float x, float y, u32 color, int px, float maxW) {
    wchar_t cut[260];
    int len = lstrlenW(text);
    if (len > 250) len = 250;
    int n = TextFit(text, len, maxW, px);
    if (n < len) {
        n = TextFit(text, len, maxW - TextWidth(L"…", px), px);
        while (n > 0 && text[n - 1] == L' ') --n;
    }
    if (n <= 0) return;
    for (int i = 0; i < n; ++i) cut[i] = text[i];
    if (n < len) cut[n++] = L'…';
    cut[n] = 0;
    DrawText(cut, x, y, color, px);
}

// What a mod brings, in one line.
void Contents(const ModInfo& m, int index, wchar_t* out) {
    wchar_t text[120] = L"", tex[60] = L"";
    if (m.textJson || m.textFiles) {
        const int source = ModTextSource();
        const wchar_t* what = m.textJson ? T(L"текст (mes_all.json)", L"text (mes_all.json)") : T(L"виправлення тексту", L"text corrections");
        if (!m.enabled) lstrcpyW(text, what);
        else if (source == index) lstrcpyW(text, what);
        else wsprintfW(text, T(L"%s, не діє: текст береться з першого мода з текстом", L"%s, not used: text comes from the first mod with text"), what);
    }
    if (m.textures) wsprintfW(tex, T(L"текстури: %d", L"textures: %d"), m.textures);
    if (!text[0] && !tex[0]) {
        lstrcpyW(out, T(L"немає ні text\\, ні textures\\ (нічого не підміняє)", L"no text\\ or textures\\ (replaces nothing)"));
        return;
    }
    wsprintfW(out, L"%s%s%s", text, text[0] && tex[0] ? L" · " : L"", tex);
}

void Switch(float x, float y, bool on) {
    FillRect(x, y, kSwitchW, kSwitchH, on ? kOn : kOff);
    FillRect(on ? x + kSwitchW - kSwitchH + 3 : x + 3, y + 3, kSwitchH - 6, kSwitchH - 6, 0xFFFFFFFF);
}

void Button(float x, float y, const wchar_t* label, bool enabled) {
    FillRect(x, y, kButtonW, kButtonH, enabled ? 0xFF2E2E3A : 0xFF1A1A22);
    const float w = TextWidth(label, 15);
    DrawText(label, x + (kButtonW - w) / 2, y + 4, enabled ? kText : kOff, 15);
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
        if (g_sel < count) ModToggle(g_sel);
        else OpenModsFolder();
    }
    if ((raise || lower) && g_sel < count) {
        const int dir = raise ? -1 : 1;
        if (g_sel + dir >= 0 && g_sel + dir < count) {
            ModMove(g_sel, dir);
            g_sel += dir;
        }
    }
    if (!click || mx < g_left || mx > g_right || my < g_top) return;
    const int row = g_scroll + (int)((my - g_top) / kRowH);
    if (row < g_scroll || row >= g_scroll + g_shown || row > count) return;
    g_sel = row;
    if (row == count) {
        OpenModsFolder();
        return;
    }
    const float y = g_top + (float)(row - g_scroll) * kRowH;
    if (mx >= g_left && mx < g_left + kSwitchW + 8) ModToggle(row);                       // the switch
    else if (mx >= g_right - 2 * kButtonW - 8 && mx < g_right - kButtonW - 8 && my < y + kButtonH + 8 && row > 0) {  // up
        ModMove(row, -1);
        --g_sel;
    } else if (mx >= g_right - kButtonW && my < y + kButtonH + 8 && row + 1 < count) {  // down
        ModMove(row, 1);
        ++g_sel;
    }
}

void ModsPage(float left, float top, float right, float bottom) {
    const int count = ModCount();
    ClampSelection();
    DrawText(T(L"↑ ↓ вибір     Enter вмк./вимк.     ← → вище / нижче     Tab розділ     F10 або Esc закрити",
               L"↑ ↓ select     Enter on/off     ← → higher / lower     Tab section     F10 or Esc close"), left, bottom + 8, kDim, 13);

    float y = top;
    if (ModsChanged()) {
        Fitted(T(L"Зміни збережено. Вони почнуть діяти після перезапуску гри: текст і текстури вантажаться на старті.",
                 L"Saved. The changes take effect after the game is restarted: text and textures are loaded at start."), left, y, kAmber, 15, right - left);
    } else {
        Fitted(T(L"Мод вище в списку перекриває ті, що нижче. Текст береться з першого ввімкненого мода з текстом.",
                 L"A mod higher in the list wins over the ones below. Text comes from the first mod that is on and has text."), left, y, kSoft, 15, right - left);
    }
    y += 28;
    if (count == 0) {
        Fitted(T(L"Модів ще немає. Кожен мод це тека в UltimateFix\\mods\\ (поруч із DP.exe):", L"No mods yet. Each mod is a folder in UltimateFix\\mods\\ (next to DP.exe):"),
               left, y, kText, 15, right - left);
        Fitted(T(L"  text\\       mes_all.json і glyphmap.json із SWERY Localization Tool (переклад)", L"  text\\       mes_all.json and glyphmap.json from the SWERY Localization Tool (a translation)"),
               left, y + 24, kSoft, 15, right - left);
        Fitted(T(L"  textures\\   заміни текстур <хеш>.dds або .png", L"  textures\\   texture replacements <hash>.dds or .png"), left, y + 46, kSoft, 15, right - left);
        Fitted(T(L"  mod.ini     Name=, Author=, Version=, Description= (необов'язково, UTF-8)", L"  mod.ini     Name=, Author=, Version=, Description= (optional, UTF-8)"),
               left, y + 68, kSoft, 15, right - left);
        Fitted(T(L"Хеші текстур: DumpTextures=1 у [Debug] UltimateFix.ini, гра скине всі текстури в UltimateFix\\textures\\dump\\ з готовими назвами",
                 L"Texture hashes: DumpTextures=1 in [Debug] of UltimateFix.ini dumps every texture to UltimateFix\\textures\\dump\\ named by its hash"),
               left, y + 96, kDim, 13, right - left);
        y += 124;
    }
    FillRect(left, y - 6, right - left, 1, kLine);

    const int rows = count + 1;
    const int fit = (int)((bottom - y) / kRowH);
    if (g_sel < g_scroll) g_scroll = g_sel;
    if (g_sel >= g_scroll + fit) g_scroll = g_sel - fit + 1;
    if (g_scroll < 0) g_scroll = 0;
    g_left = left;
    g_top = y;
    g_right = right;
    g_shown = 0;
    for (int r = g_scroll; r < rows && g_shown < fit; ++r, ++g_shown) {
        const float ry = y + (float)g_shown * kRowH;
        const bool sel = r == g_sel;
        if (sel) {
            FillRect(left - 8, ry - 4, right - left + 16, kRowH - 4, kRowSel);
            FillRect(left - 8, ry - 4, 3, kRowH - 4, kAccent);
        }
        if (r == count) {  // the action row
            DrawText(T(L"Відкрити теку модів", L"Open the mods folder"), left, ry + 8, sel ? kText : kSoft, 18);
            continue;
        }
        const ModInfo& m = *ModAt(r);
        Switch(left, ry + 2, m.enabled);
        wchar_t title[200];
        if (m.version[0] && m.author[0]) wsprintfW(title, L"%s  %s  ·  %s", m.name, m.version, m.author);
        else if (m.version[0]) wsprintfW(title, L"%s  %s", m.name, m.version);
        else if (m.author[0]) wsprintfW(title, L"%s  ·  %s", m.name, m.author);
        else lstrcpyW(title, m.name);
        const float textX = left + kSwitchW + 14, textW = right - textX - 2 * kButtonW - 20;
        Fitted(title, textX, ry, m.enabled ? kText : kDim, 18, textW);
        wchar_t line[300];
        Contents(m, r, line);
        if (m.description[0]) {
            wchar_t both[420];
            wsprintfW(both, L"%s  —  %s", line, m.description);
            Fitted(both, textX, ry + 24, kDim, 13, textW);
        } else {
            Fitted(line, textX, ry + 24, kDim, 13, textW);
        }
        Button(right - 2 * kButtonW - 8, ry, L"▲", r > 0);
        Button(right - kButtonW, ry, L"▼", r + 1 < count);
    }
}

}  // namespace ui
}  // namespace ufx
