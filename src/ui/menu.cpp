// The F10 settings menu and the blocking of the game's own input while it is open.
//
// Layout (1280x720 coordinates): a sidebar with the sections on the left, the settings of the chosen section on the right
// (grouped, with switches and sliders, scrollable), a description of the selected setting at the bottom.
// Keys: F10 opens/closes, Up/Down selects, Left/Right changes (on a row of action buttons it only moves the highlight),
// Enter/Space toggles or presses the highlighted button, Backspace puts the selected setting back to its default (a changed
// one has a grey dot after its label), F1 shows the selected row's whole description on a card (a click on the description
// box too), Tab / PgUp / PgDn switches the section, Esc closes. The mouse
// works too: click a section, a switch, a slider, a button or the value; the wheel scrolls.
// A gamepad ([Menu] PadControl) does the same: View + Menu (Create + Options) held together opens and closes, the D-pad or the
// left stick moves and changes (held, it repeats, and a slider speeds up), A / Cross is Enter, B / Circle is Esc, Y / Triangle
// is Backspace, R3 is F1, LB / RB the section, LT / RT a screen of rows. The hints at the bottom name the pad's buttons while
// it is used. On the photo page the right mouse button or L3, held, fades the panel away to show the picture (a peek).
// A change of a setting that has an ini key is written to UltimateFix.ini at once (a slider when the drag ends). Rows
// without a key (most of the photo mode and the cheats) last for the session only.
#include "ui.h"
#include "theme.h"
#include "../sites.h"
#include "../story.h"
#include "../menupad.h"
#include "../item_names.h"
#include <mmsystem.h>

namespace ufx {
bool PadZCarriesTriggers(unsigned id);  // sony_layout.cpp: Z of this WinMM pad is the shared trigger axis
bool PadIsRawSony(unsigned id);         // sony_layout.cpp: a Sony pad left in its own layout (SonyLayout=0)
namespace ui {
namespace {

volatile bool g_open = false;

enum Kind { kBool, kInt };
// A text in both languages of the menu: Ukrainian and English.
struct Tr {
    const wchar_t* uk;
    const wchar_t* en;
};
const wchar_t* Tx(const Tr& t) { return (g_cfg.language == 1 && t.en) ? t.en : t.uk; }

// "Not in the default ini": GetPrivateProfileIntW gives it back for a missing key (no <climits> here for INT_MIN).
constexpr int kNoDefault = -999999;

struct Item {
    Tr label;
    int* value;
    Kind kind;
    int lo, hi, step;
    bool restart;  // takes effect after the game is restarted
    const wchar_t* section;
    const wchar_t* key;
    void (*apply)();  // called after a change, may be null
    const Tr* names;  // text for the values lo..hi instead of a number (may be null)
    Tr hint;          // description under the list; '\n' separates lines
    Tr group;         // heading shown above this item (uk == null: same group as the previous item)
    const int* presets = nullptr;                 // ready values shown as buttons
    int presetCount = 0;
    const Tr* presetNames = nullptr;              // their names
    void (*format)(int value, wchar_t* out) = nullptr;  // text for the value
    bool resolution = false;                      // typed numbers below 1280 are heights (1080 -> 1920x1080)
    void (*running)(wchar_t* out) = nullptr;      // what actually runs now, when a setting waits for a restart (empty = same)
    int def = kNoDefault;  // the value of the default ini, read once at the first opening (LoadDefaults); kNoDefault = none
    int boot = kNoDefault;  // a restart row: the value this run started with, which is what runs (SnapshotBoot); kNoDefault = none
};

const Tr kGlyphTr[4] = {{L"Вимк.", L"Off"}, {L"Авто", L"Auto"}, {L"Xbox", L"Xbox"}, {L"PlayStation", L"PlayStation"}};
const Tr kAntialiasModeTr[2] = {{L"FXAA", L"FXAA"}, {L"SMAA", L"SMAA"}};
const Tr kSharpenModeTr[2] = {{L"Звичайна", L"Unsharp mask"}, {L"Адаптивна (AMD RCAS)", L"Adaptive (AMD RCAS)"}};
const Tr kSmaaQualityTr[3] = {{L"Низька", L"Low"}, {L"Середня", L"Medium"}, {L"Висока", L"High"}};
const Tr kGameAliasTr[3] = {{L"Вимк. (як на PC)", L"Off (as on PC)"}, {L"Як у шейдері гри", L"As in the game's shader"}, {L"Під роздільність 3D", L"For the 3D resolution"}};
const Tr kMapOrientTr[3] = {{L"Обертається", L"Rotates"}, {L"Північ угорі", L"North up"}, {L"Південь угорі", L"South up"}};
const Tr kDifficultyTr[3] = {{L"Легка", L"Easy"}, {L"Звичайна", L"Normal"}, {L"Важка", L"Hard"}};
const Tr kSurroundTr[3] = {{L"Вимк.", L"Off"}, {L"Авто", L"Auto"}, {L"Авто, боки навпаки", L"Auto, sides swapped"}};
const Tr kPhysStepsTr[4] = {{L"Як у грі (до 4)", L"As the game (up to 4)"}, {L"1 крок", L"1 step"}, {L"2 кроки", L"2 steps"}, {L"3 кроки", L"3 steps"}};
const Tr kAoModeTr[2] = {{L"Простий (12 точок)", L"Plain (12 taps)"}, {L"По горизонту, м'який", L"Horizon-based, soft"}};
const Tr kPacingTr[4] = {{L"Вимк.", L"Off"}, {L"Ліміт 60 к/с", L"Limit 60 fps"}, {L"30 к/с (як Xbox)", L"30 fps (like Xbox)"}, {L"60 к/с, цілі кроки", L"60 fps, whole steps"}};
const Tr kTriggerTestTr[7] = {{L"Вимк.", L"Off"}, {L"Клік пістолета", L"Pistol click"}, {L"Важкий клік", L"Heavy click"},
                              {L"Автомат: вібрація", L"Machine gun: vibration"}, {L"Опір", L"Resistance"}, {L"Повільна вібрація", L"Slow rumble"},
                              {L"Обидва триґери", L"Both triggers"}};
const Tr kXboxContrastTr[4] = {{L"Вимк.", L"Off"}, {L"А: як у ZachFix", L"A: as ZachFix"}, {L"Б: навколо сірого", L"B: around mid grey"},
                               {L"В: розтяг рівнів", L"C: level stretch"}};
const Tr kXboxChromaTr[3] = {{L"Вимк.", L"Off"}, {L"А: 0 = повний колір", L"A: 0 = full colour"}, {L"Б: 1 = повний колір", L"B: 1 = full colour"}};
const int kResPresets[5] = {0, 1280, 1920, 2560, 3840};
const Tr kResNames[5] = {{L"Вимк.", L"Off"}, {L"720p", L"720p"}, {L"1080p", L"1080p"}, {L"2K", L"2K"}, {L"4K", L"4K"}};

// "1920×1080".
// The height an output width gets: always 16:9, as the output module makes it (output_resolution.cpp ignores OutputHeight).
int WantedHeight(int w) { return w * 9 / 16; }
// The height of what runs now: the output module's own, or the game's 720.
int RunningHeight(int runningW) {
    if (g_outputW && g_outputH) return runningW * g_outputH / g_outputW;
    return (runningW * 9 + 8) / 16;
}

void FormatResolution(int w, wchar_t* out) {
    if (w <= 0) {
        lstrcpyW(out, g_cfg.language == 1 ? L"Off" : L"Вимк.");
        return;
    }
    wsprintfW(out, L"%d×%d", w, WantedHeight(w));
}

// What runs right now (the settings below only change after a restart), shown when it differs from what is chosen.
void RunningOutput(wchar_t* out) {
    out[0] = 0;
    int running = g_outputW ? g_outputW : 1280;
    int wanted = g_cfg.outputWidth ? g_cfg.outputWidth : 1280;
    if (running != wanted) wsprintfW(out, g_cfg.language == 1 ? L"now running: %d×%d" : L"зараз працює: %d×%d", running, RunningHeight(running));
}

void RunningScene(wchar_t* out) {
    out[0] = 0;
    int baseWanted = g_cfg.outputWidth ? g_cfg.outputWidth : 1280;
    int wanted = baseWanted * (g_cfg.renderScale < 100 ? 100 : g_cfg.renderScale) / 100;
    if (wanted > 7680) wanted = 7680;
    int running = g_sceneWidth ? g_sceneWidth : (g_outputW ? g_outputW : 1280);
    int baseH = g_cfg.outputWidth ? WantedHeight(baseWanted) : 720;
    int wantedH = wanted * baseH / baseWanted;
    if (running != wanted) wsprintfW(out, g_cfg.language == 1 ? L"will be %d×%d, now %d×%d" : L"буде %d×%d, зараз %d×%d", wanted, wantedH, running, RunningHeight(running));
    else if (!g_cfg.outputWidth && g_cfg.renderScale > 100)  // a roundabout: the scene is drawn big, shrunk to 720p, then stretched
        lstrcpyW(out, g_cfg.language == 1 ? L"tip: set the output to 1080p" : L"порада: поставте вихід 1080p");
    else wsprintfW(out, L"= %d×%d", wanted, wantedH);
}

// "200%  ·  3840×2160": the size of the scene that percent gives.
int g_dollPutPick = 0, g_dollGonePick = 0;
const int kDollValues[1] = {1};
const Tr kDollPutNames[1] = {{L"Поставити", L"Put"}};
const Tr kDollGoneNames[1] = {{L"Прибрати", L"Remove"}};
const Tr kFaceTargetTr[2] = {{L"Йорк", L"York"}, {L"Персонаж", L"The character"}};
void DollPut() {
    g_dollPutPick = 0;
    RequestDoll(1);
    PhotoThaw(120);  // the loader needs the game running
}
void DollGone() {
    g_dollGonePick = 0;
    RequestDoll(2);
    PhotoThaw(6);
}
void DollThaw() { PhotoThaw(6); }
const Tr kPlayAsTr[5] = {{L"Йорк", L"York"}, {L"Емілі", L"Emily"}, {L"Йорк у дитинстві", L"York as a child"}, {L"CBS02 (імовірно, вбивця)", L"CBS02 (probably the killer)"}, {L"Зак", L"Zach"}};
void PlayAsChanged() { g_cfg.playAsOn = 1; PhotoThaw(180); }  // the model loads from disk
void FormatDoll(int v, wchar_t* out) {
    char name[16];
    if (DollName(v, name)) wsprintfW(out, L"%d %S", v, name);
    else wsprintfW(out, L"%d", v);
}
void FormatMove(int v, wchar_t* out) { wsprintfW(out, L"%s%d.%d", v < 0 ? L"-" : v > 0 ? L"+" : L"", (v < 0 ? -v : v) / 10, (v < 0 ? -v : v) % 10); }
void FormatTenths(int v, wchar_t* out) { wsprintfW(out, L"%d.%d", v / 10, v % 10); }
void FormatStops(int v, wchar_t* out) {
    const int a = v < 0 ? -v : v;
    wsprintfW(out, L"%s%d.%d EV", v < 0 ? L"-" : (v > 0 ? L"+" : L""), a / 10, a % 10);
}

void FormatScale(int v, wchar_t* out) {
    int base = g_cfg.outputWidth ? g_cfg.outputWidth : 1280;
    int baseH = g_cfg.outputWidth ? WantedHeight(base) : 720;
    wsprintfW(out, L"%d%%  ·  %d×%d", v, base * v / 100, baseH * v / 100);
}

const int kScalePresets[4] = {100, 150, 200, 300};
const Tr kScaleNames[4] = {{L"1×", L"1x"}, {L"1,5×", L"1.5x"}, {L"2×", L"2x"}, {L"3×", L"3x"}};

// ---- Quick quality presets: one click sets the whole picture block (some of it applies after a restart).
int g_qualityPick = 0;
const Tr kRiverTr[3] = {{L"Вимкнено", L"Off"}, {L"Завжди", L"Always"}, {L"Лише в сцені біля річки", L"Only in the river scene"}};
const int kQualityValues[3] = {1, 2, 3};
const Tr kQualityNames[3] = {{L"Легкий", L"Light"}, {L"Баланс", L"Balanced"}, {L"Якість", L"Quality"}};

void PutValue(int* field, const wchar_t* section, const wchar_t* key, int v) {
    *field = v;
    wchar_t text[16];
    wsprintfW(text, L"%d", v);
    WritePrivateProfileStringW(section, key, text, g_paths.iniFile);
}

// The output cannot be larger than the monitor (a bigger one leaves a white strip in the loading screens): a larger choice is cut back.
void ApplyOutputLimit() {
    int fit = MonitorFitWidth();
    if (g_cfg.outputWidth > fit) {
        Log("menu: output %d is larger than the monitor, set to %d (use the 3D multiple for a bigger picture)", g_cfg.outputWidth, fit);
        PutValue(&g_cfg.outputWidth, L"Graphics", L"OutputWidth", fit);
    }
}

void ApplyQuality() {
    int q = g_qualityPick;
    g_qualityPick = 0;
    if (q < 1 || q > 3) return;
    //                       light  balanced  quality
    static const int render[3] = {100, 125, 200}, shadow[3] = {100, 200, 200}, aniso[3] = {8, 16, 16};
    static const int aa[3] = {60, 40, 0}, sharp[3] = {20, 20, 25}, ao[3] = {0, 40, 50}, blur[3] = {0, 1, 1};
    int i = q - 1;
    PutValue(&g_cfg.outputWidth, L"Graphics", L"OutputWidth", MonitorFitWidth());  // the monitor: the upscale is the 3D multiplier
    PutValue(&g_cfg.outputHeight, L"Graphics", L"OutputHeight", 0);
    PutValue(&g_cfg.renderScale, L"Graphics", L"RenderScale", render[i]);
    PutValue(&g_cfg.shadowScale, L"Graphics", L"ShadowScale", shadow[i]);
    PutValue(&g_cfg.anisotropy, L"Graphics", L"Anisotropy", aniso[i]);
    PutValue(&g_cfg.antialias, L"Graphics", L"Antialias", aa[i]);
    PutValue(&g_cfg.sharpen, L"Graphics", L"Sharpen", sharp[i]);
    PutValue(&g_cfg.ambientOcclusion, L"Graphics", L"AmbientOcclusion", ao[i]);
    PutValue(&g_cfg.keepBlurLook, L"Graphics", L"KeepBlurLook", blur[i]);
    PutValue(&g_cfg.framePacing, L"Time", L"FramePacing", 0);
    ApplyGraphicsTweaks();
    Log("menu: quality preset %d applied (resolution, shadows and blur need a restart)", q);
}

#define NOTR {nullptr, nullptr}

// ---- "Restore defaults" button, the last row of every settings page: puts this page's settings back to the values of
// the default ini (stored in the DLL).
int g_resetPick = 0;
const int kResetValues[2] = {1, 2};
const Tr kResetNames[2] = {{L"Ця сторінка", L"This page"}, {L"Усі налаштування", L"Everything"}};
void ResetPageDefaults();
#define RESET_ITEM \
    {{L"Відновити типові налаштування", L"Restore default settings"}, &g_resetPick, kInt, 0, 2, 1, false, nullptr, nullptr, ResetPageDefaults, nullptr, \
     {L"Повертає налаштування до типових: тільки цієї сторінки або всіх. Перед цим мод запитає підтвердження. Те, що потребує перезапуску, діятиме після нього. Свої значення збережіть, скопіювавши UltimateFix.ini.", \
      L"Puts settings back to their defaults: this page only or all of them. You are asked to confirm first. Anything that needs a restart takes effect after it. To keep your own values, copy UltimateFix.ini first."}, \
     {L"Типові", L"Defaults"}, kResetValues, 2, kResetNames, nullptr, false},


Item g_pageDisplay[] = {
    {{L"Режим без рамки", L"Borderless window"}, &g_cfg.borderless, kBool, 0, 1, 1, false, L"Display", L"Borderless", nullptr, nullptr,
     {L"Вікно без рамки на весь екран: миттєвий Alt+Tab, без зміни режиму екрана.", L"A window without a border over the whole screen: instant Alt+Tab, no screen mode change."},
     {L"Вікно", L"Window"}},
    {{L"Розтягувати картинку на весь екран", L"Stretch the picture to the screen"}, &g_cfg.scaleToWindow, kBool, 0, 1, 1, false, L"Display", L"ScaleToWindow", nullptr, nullptr,
     {L"Картинка 16:9 розтягується на вікно без рамки зі збереженням пропорцій (чорні смуги на інших моніторах). Вимкнено: гра сама розтягує картинку на все вікно, не зберігаючи пропорцій.", L"The 16:9 picture fills the borderless window keeping its proportions (black bars on other monitors). Off: the game itself stretches the picture over the whole window without keeping the proportions."}, NOTR},
    {{L"Ховати курсор миші", L"Hide the mouse cursor"}, &g_cfg.hideCursor, kBool, 0, 1, 1, false, L"Display", L"HideCursor", nullptr, nullptr,
     {L"Курсор не показується над вікном гри. У меню F10 він видимий, поки меню керують не ґеймпадом (тоді він з'являється з першим рухом миші).", L"The cursor is hidden over the game window. In the F10 menu it is visible unless the menu is used with a gamepad (then it comes back as soon as the mouse moves)."}, NOTR},
    {{L"Вимкнути масштабування Windows (DPI)", L"Disable Windows scaling (DPI)"}, &g_cfg.dpiAware, kBool, 0, 1, 1, true, L"Display", L"DpiAware", nullptr, nullptr,
     {L"Потрібно на екранах із масштабом 125% і більше, інакше вікно виходить розмитим.", L"Needed on screens scaled 125% or more, otherwise the window is blurry."},
     {L"Система", L"System"}},
    {{L"Журнал показу (діагностика)", L"Display log (diagnostics)"}, &g_cfg.displayDebug, kBool, 0, 1, 1, false, L"Display", L"Debug", nullptr, nullptr,
     {L"У режимі без рамки пише в журнал перші кадри й кожну зміну способу показу (з розміром буфера), а завжди: розміри вікна протягом 4 секунд після кожного натискання Esc. Для пошуку проблем із показом.", L"In the borderless window, writes the first frames and every change of the way the picture is presented (with the buffer size) to the log, and always the window geometry for 4 seconds after each Esc press. For tracking down display problems."}, NOTR},
    {{L"Непрозорість панелі меню, %", L"Menu panel opacity, %"}, &g_cfg.menuOpacity, kInt, 20, 100, 5, false, L"Menu", L"Opacity", nullptr, nullptr,
     {L"Чим менше, тим більше крізь меню видно гру. Текст лишається чітким. Змінюється одразу.", L"The lower, the more of the game shows through the menu. The text stays sharp. Changes at once."},
     {L"Вигляд меню", L"Look of the menu"}},
    {{L"Анімація меню", L"Menu animation"}, &g_cfg.menuAnimations, kBool, 0, 1, 1, false, L"Menu", L"Animations", nullptr, nullptr,
     {L"Панель з'являється плавно, виділення ковзає до вибраного рядка й розділу, повзунок перемикача пересувається. Вимкнено: усе змінюється миттєво. Закривається меню завжди одразу.",
      L"The panel fades in, the selection glides to the chosen row and section, the knob of a switch slides across. Off: everything changes at once. The menu always closes at once."}, NOTR},
    {{L"Керування меню ґеймпадом", L"Gamepad control of the menu"}, &g_cfg.menuPadControl, kBool, 0, 1, 1, false, L"Menu", L"PadControl", nullptr, nullptr,
     {L"Меню F10 працює й на ґеймпаді: View + Menu разом (на DualSense Create + Options) відкриває і закриває його, хрестовина або лівий стік ходять, A / хрестик вибирає, B / коло закриває, LB / RB (L1 / R1) перемикають розділ, LT / RT гортають на екран, Y / трикутник повертає типове, R3 показує весь опис, X / квадрат на сторінці «Моди» відкриває теку мода, на інших перезапускає гру, коли налаштування чекають перезапуску (спершу запитає), затиснутий L3 на сторінці фоторежиму ховає меню, щоб побачити кадр. Гра в цей час ґеймпада не бачить. Діє з наступного відкриття меню.",
      L"The F10 menu also works on a gamepad: View + Menu together (Create + Options on a DualSense) opens and closes it, the D-pad or the left stick moves, A / Cross picks, B / Circle closes, LB / RB (L1 / R1) switch the section, LT / RT scroll a screen, Y / Triangle restores the default, R3 shows the whole description, X / Square opens a mod's folder on the Mods page and, on the others, restarts the game while settings wait for a restart (it asks first), and L3 held on the photo mode page hides the menu to show the picture. The game does not see the pad meanwhile. Takes effect the next time the menu opens."}, NOTR},
    {{L"Перевіряти оновлення", L"Check for updates"}, &g_cfg.checkUpdates, kBool, 0, 1, 1, true, L"Menu", L"CheckUpdates", nullptr, nullptr,
     {L"На старті один анонімний запит до GitHub: чи вийшла нова версія. Якщо так, плашка на старті й сторінка «Про мод» пропонують F9: мод сам завантажить її, замінить X3DAudio1_7.dll та іконки й перезапустить гру. Налаштування, моди й копії збережень лишаються.",
      L"One anonymous request to GitHub at the start: is a new version out. If so, the start card and the About page offer F9: the mod downloads it, replaces X3DAudio1_7.dll and the icons and restarts the game. Settings, mods and save backups stay."}, NOTR},
    {{L"Панель статистики кадру", L"Frame statistics panel"}, &g_cfg.frameStats, kBool, 0, 1, 1, false, L"Debug", L"FrameStats", nullptr, nullptr,
     {L"Окрема панель справа вгорі, як у Windows: всього, гра (процесор), з них фізика, Present (драйвер, очікування відеокарти й екрана), найгірший кадр за 30 кадрів, час проходів мода, FPS, частота екрана й графік останніх 120 кадрів. Червоні стовпчики: кадри, що помітно довші за середній. Для пошуку лагів.",
      L"A separate panel top right, like the Windows one: total, game (CPU), of which physics, Present (driver, waiting for the video card and screen), the worst frame of 30, the time of the mod's passes, FPS, screen refresh rate and a graph of the last 120 frames. Red bars: frames clearly longer than average. For finding lag."}, {L"Діагностика", L"Diagnostics"}},
    {{L"Показувати FPS і час кадру", L"Show FPS and frame time"}, &g_cfg.perfOverlay, kBool, 0, 1, 1, false, L"Debug", L"PerfOverlay", nullptr, nullptr,
     {L"У кутку: FPS, час кадру, скільки Present чекає (чим більше, тим сильніше впирається відеокарта) і скільки відеокарта витрачає на проходи мода (SSAO, фільтри). Для пошуку, що гальмує.", L"In the corner: FPS, frame time, how long Present waits (the more, the more the video card is the limit) and how much video time the mod's passes take (SSAO, filters). For finding what slows things down."}, {L"Діагностика", L"Diagnostics"}},
    RESET_ITEM
};
constexpr int kFilterCountForMenu = 18;  // kFilterCount of the photo page below (14 filters + 4 own presets)
void FormatFilterForMenu(int v, wchar_t* out);

Item g_pageGraphics[] = {
    {{L"Швидкий вибір якості", L"Quick quality preset"}, &g_qualityPick, kInt, 0, 3, 1, false, nullptr, nullptr, ApplyQuality, nullptr,
     {L"Клік по кнопці (або вибір стрілками й Enter) виставляє весь блок картинки під ваш монітор: вихід стає найбільшою роздільністю 16:9, що вміщується в монітор (до 3840), а «Крок часу і ліміт кадрів» стає «Ліміт 60 к/с». Легкий: слабкі відеокарти; Баланс: суперсемплінг 125%; Якість: 200% (на моніторі 1920×1080 це точно 2×2, на 1440p і 4K набагато важче). Роздільність, тіні й розмиття підхопляться після перезапуску; різкість, згладжування, SSAO й анізотропія працюють одразу. Потім усе можна підправити нижче.",
      L"A click on a button (or picking one with the arrows and Enter) sets the whole picture block for your monitor: the output becomes the largest 16:9 resolution that fits the monitor (up to 3840) and 'Time step and frame limit' becomes 'Limit 60 fps'. Light: weak video cards; Balanced: 125% supersampling; Quality: 200% (exactly 2×2 on a 1920×1080 monitor, much heavier at 1440p and 4K). Resolution, shadows and blur apply after a restart; sharpening, smoothing, SSAO and anisotropy work at once. Then fine-tune below."},
     {L"Швидкий вибір", L"Quick choice"}, kQualityValues, 3, kQualityNames, nullptr, false},
    {{L"Вихідна роздільність", L"Output resolution"}, &g_cfg.outputWidth, kInt, 0, 3840, 320, true, L"Graphics", L"OutputWidth", ApplyOutputLimit, nullptr,
     {L"Реальна роздільність картинки й чіткого інтерфейсу: типово розмір вашого монітора, і більше за монітор не дозволено (інакше під час завантаження внизу біла смужка). Якщо хочете чіткішу картинку, обирайте «Роздільність 3D» (множник): сцена рахується більшою й стискається до монітора.\nЩоб ввести своє значення, натисніть Enter на цьому пункті й введіть ширину (або висоту, наприклад 1080). Вимк. = як у грі (1280×720).",
      L"The real resolution of the picture and the sharp interface: by default the size of your monitor, and more than the monitor is not allowed (otherwise a white strip appears at the bottom in loading screens). For a sharper picture choose the 3D resolution (multiple): the scene is computed bigger and shrunk to the monitor.\nTo type your own value, press Enter on this row and type a width (or a height, e.g. 1080). Off = as the game (1280×720)."},
     {L"Роздільність", L"Resolution"}, kResPresets, 5, kResNames, FormatResolution, true, RunningOutput},
    {{L"Роздільність 3D", L"3D resolution"}, &g_cfg.renderScale, kInt, 100, 400, 25, true, L"Graphics", L"RenderScale", nullptr, nullptr,
     {L"Суперсемплінг сцени: 1× = рівно розмір виходу, 2× = вдвічі більше за шириною й висотою (при виході 1920 це точно 2×2, найчистіше), 3× дуже важко. 2× — це 4 відліки на кожен піксель екрана: та сама ідея, що в DLAA, але без шлейфів за рухомим. Поруч із назвою видно, який розмір сцени вийде.\nСцена обмежена 7680 пікселями завширшки. Іншого множника: клік по останній кнопці й число у відсотках (100–400).",
      L"Supersampling of the scene: 1x = exactly the output size, 2x = twice the width and height (with a 1920 output exactly 2x2, the cleanest), 3x is very heavy. 2x means 4 samples for every screen pixel: the idea behind DLAA, without trails behind moving things. Next to the name you can see the size of the scene that comes out.\nThe scene is limited to 7680 pixels wide. Another multiple: click the last button and type a percentage (100-400)."},
     {L"Множник від виходу", L"Multiple of the output"}, kScalePresets, 4, kScaleNames, FormatScale, false, RunningScene},
    {{L"Збільшувати буфери розмиття й свічення", L"Enlarge blur and bloom buffers"}, &g_cfg.scalePostBuffers, kBool, 0, 1, 1, true, L"Graphics", L"ScalePostBuffers", nullptr, nullptr,
     {L"Розмиття та свічення (bloom) рахуються в тій самій роздільності, що й сцена, а не в оригінальній.", L"Blur and bloom are computed at the resolution of the scene, not the original one."}, NOTR},
    {{L"Розмір мап тіней, % (100 = оригінал)", L"Shadow map size, % (100 = original)"}, &g_cfg.shadowScale, kInt, 100, 400, 100, true, L"Graphics", L"ShadowScale", nullptr, nullptr,
     {L"Чіткість тіней: 200 = 1024/2048, 400 = 2048/4096. Більше значення: більше відеопам'яті.", L"Shadow sharpness: 200 = 1024/2048, 400 = 2048/4096. More: more video memory."},
     {L"Тіні", L"Shadows"}},
    {{L"Глибина тіней 32-біт", L"32-bit shadow depth"}, &g_cfg.shadowPrecision, kBool, 0, 1, 1, true, L"Graphics", L"ShadowPrecision", nullptr, nullptr,
     {L"Прибирає смуги й «танцюючі» краї тіней (як у DPfix і ZachFix). Якщо тіні зникли чи стали жорсткими, вимкніть.", L"Removes banding and dancing shadow edges (as DPfix and ZachFix do). If shadows vanish or turn hard, turn it off."}, NOTR},
    {{L"Розмиття не меншає з роздільністю", L"Keep blur size at high resolution"}, &g_cfg.keepBlurLook, kBool, 0, 1, 1, true, L"Graphics", L"KeepBlurLook", nullptr, nullptr,
     {L"Свічення, глибина різкості й розмиття лишаються такими ж за розміром на екрані, як у 720p. Без цього при RenderScale вони стають вужчими. Потребує перезапуску.", L"Bloom, depth of field and blur keep the same size on screen as at 720p. Without it they get narrower with RenderScale. Needs a restart."},
     {L"Чіткість і вигляд", L"Sharpness and look"}},
    {{L"Затінення в кутках (SSAO), %", L"Ambient occlusion (SSAO), %"}, &g_cfg.ambientOcclusion, kInt, 0, 100, 10, false, L"Graphics", L"AmbientOcclusion", nullptr, nullptr,
     {L"Притемнює щілини й місця, де предмети стикаються. Рахується з буфера глибини гри. Змінюється одразу. Починайте з 40–60.", L"Darkens creases and contact areas. Computed from the game's depth buffer. Changes at once. Start with 40–60."},
     {L"Чіткість і вигляд", L"Sharpness and look"}},
    {{L"Радіус затінення, пікселів", L"Occlusion radius, pixels"}, &g_cfg.aoRadius, kInt, 2, 40, 2, false, L"Graphics", L"AoRadius", nullptr, nullptr,
     {L"Наскільки далеко від кута шукається перекриття (при 720p; на більших роздільностях росте пропорційно). Для простого способу більше за 20 не діє.", L"How far from a corner occlusion is searched (at 720p; scales up with the resolution). The plain way stops at 20."}, NOTR},
    {{L"Спосіб затінення", L"Occlusion method"}, &g_cfg.aoMode, kInt, 0, 1, 1, false, L"Graphics", L"AoMode", nullptr, kAoModeTr,
     {L"По горизонту (типово): 8 напрямків і розмиття з урахуванням глибини, м'які тіні в кутах і під предметами, без смуг навіть на великому радіусі; трохи важчий. Простий: старий спосіб, 12 точок без розмиття. Змінюється одразу.",
      L"Horizon-based (default): 8 directions and a depth-aware blur, soft shade in corners and under objects, no stripes even at a large radius; a little heavier. Plain: the old way, 12 taps without a blur. Changes at once."}, NOTR},
    {{L"Показати лише затінення (перевірка)", L"Show the occlusion only (check)"}, &g_cfg.aoView, kBool, 0, 1, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Замість картинки гри видно тільки затінення: біле = без затінення, темне = затінено. Так видно, чи смужка чи пляма від затінення, чи від самої гри. Лише для перевірки, у файл не пишеться.",
      L"Instead of the game's picture only the occlusion shows: white = none, dark = shaded. It tells whether a strip or a spot comes from the occlusion or from the game itself. For checking only, not saved."}, NOTR},
    {{L"Згладжування країв, %", L"Edge smoothing, %"}, &g_cfg.antialias, kInt, 0, 100, 10, false, L"Graphics", L"Antialias", nullptr, nullptr,
     {L"Прибирає «драбинки» на краях. Власний фільтр гри (рядок «Власний фільтр країв гри») на PC вимкнений. Змінюється одразу. Спосіб обирається рядком нижче: з FXAA поєднуйте з різкістю 20–30, бо воно трохи розмиває; зі SMAA ставте 100.", L"Removes jagged edges. The game's own filter (the 'The game's own edge filter' row) is off on PC. Changes at once. The method is chosen in the row below: with FXAA combine it with sharpening 20–30, since it blurs slightly; with SMAA set 100."},
     {L"Чіткість і вигляд", L"Sharpness and look"}},
    {{L"Спосіб згладжування", L"Smoothing method"}, &g_cfg.antialiasMode, kInt, 0, 1, 1, false, L"Graphics", L"AntialiasMode", nullptr, kAntialiasModeTr,
     {L"FXAA: швидке, але трохи розмиває всю картинку. SMAA: знаходить самі «драбинки» на межах предметів і вирівнює лише їх, тож текстури й текст лишаються чіткішими; трохи важче для відеокарти. Діє, коли «Згладжування країв» більше 0. Якщо SMAA не запуститься на вашій відеокарті, мод сам повернеться до FXAA (цей рядок теж перемкнеться на FXAA) і запише причину в журнал. Змінюється одразу. У грі ще не перевірено.", L"FXAA: fast, but blurs the whole picture slightly. SMAA: finds the jagged steps on the borders of objects and straightens only them, so textures and text stay sharper; a little heavier for the video card. Works while 'Edge smoothing' is above 0. If SMAA cannot start on your video card, the mod goes back to FXAA by itself (this row switches to FXAA too) and writes the reason to the log. Changes at once. Not yet verified in the game."}, NOTR},
    {{L"Якість SMAA", L"SMAA quality"}, &g_cfg.smaaQuality, kInt, 0, 2, 1, false, L"Graphics", L"SmaaQuality", nullptr, kSmaaQualityTr,
     {L"Наскільки далеко вздовж межі шукається її кінець: низька близько 8 пікселів, середня 16, висока 32. Що далі, то рівніші майже горизонтальні й майже вертикальні лінії (дахи, дроти, край дороги). Діє лише зі способом SMAA. Змінюється одразу; під час перемикання гра може на мить пригальмувати.", L"How far along a border its end is looked for: low about 8 pixels, medium 16, high 32. The farther, the smoother nearly horizontal and nearly vertical lines get (roofs, wires, the edge of the road). Only with the SMAA method. Changes at once; the game may hitch for a moment when you switch."}, NOTR},
    {{L"Власний фільтр країв гри", L"The game's own edge filter"}, &g_cfg.gameAlias, kInt, 0, 2, 1, false, L"Graphics", L"GameAliasFilter", nullptr, kGameAliasTr,
     {L"Власний фільтр самої гри, який вона ніколи не вмикає: у шейдера фінальної картинки є варіант, що бере кожен піксель сцени з чотирьох точок навскіс навколо нього й усереднює їх (м'якші краї, менше мерехтіння). PC-версія досі щокадру рахує для нього зсув, але вибирає варіант без нього. Чи малювала з ним версія для Xbox 360, не підтверджено. Вимк. = як на PC. Як у шейдері гри: зсув, який рахує гра, картинка трохи м'якша. Під роздільність 3D: ті самі чотири точки на чверть пікселя екрана; найбільше дає з «Роздільністю 3D» понад 1× (менше мерехтять дроти, паркани й краї дахів), на 1× картинка лише трохи м'якшає. Працює і з «Виглядом Xbox 360». Змінюється одразу. У грі ще не перевірено.",
      L"The game's own filter that it never switches on: the shader of the final picture has a variant that takes every scene pixel from four diagonal points around it and averages them (softer edges, less shimmer). The PC version still computes its offset every frame but picks the variant without it. Whether the Xbox 360 version drew with it is not confirmed. Off = as on PC. As in the game's shader: the offset the game computes, a slightly softer picture. For the 3D resolution: the same four points a quarter of a screen pixel apart; it does the most with a '3D resolution' above 1x (wires, fences and roof edges shimmer less), at 1x the picture only gets slightly softer. Works with the 'Xbox 360 look' too. Changes at once. Not yet verified in the game."}, NOTR},
    {{L"Різкість фінальної картинки, %", L"Sharpening of the final picture, %"}, &g_cfg.sharpen, kInt, 0, 100, 10, false, L"Graphics", L"Sharpen", nullptr, nullptr,
     {L"Підвищення різкості готового кадру. Спосіб обирається рядком нижче. Починайте з 30–50; звичайна різкість понад це дає ореоли, з адаптивною вони значно слабші. Змінюється одразу.", L"Sharpens the finished frame. The method is chosen in the row below. Start with 30–50; the unsharp mask gives halos above that, the adaptive one keeps them much weaker. Changes at once."},
     {L"Чіткість і вигляд", L"Sharpness and look"}},
    {{L"Спосіб різкості", L"Sharpening method"}, &g_cfg.sharpenMode, kInt, 0, 1, 1, false, L"Graphics", L"SharpenMode", nullptr, kSharpenModeTr,
     {L"Адаптивна (типово): наш код алгоритму RCAS з AMD FidelityFX FSR 1. Нерізкі дрібні деталі підсилює на повну, а контрастні краї (гілки на тлі неба, дроти) лише трохи, тож навколо них немає світлих ореолів, а шум і зерно не підсилюються. Найкраще разом із «Роздільністю 3D» 150–200%. Звичайна: стара різкість, однаково підсилює всі краї. Сила: рядок вище. Якщо адаптивна не запуститься на вашій відеокарті, мод сам повернеться до звичайної (цей рядок теж перемкнеться) і запише причину в журнал. Змінюється одразу. У грі ще не перевірено.",
      L"Adaptive (default): our own code of the RCAS algorithm from AMD FidelityFX FSR 1. Soft fine detail gets the full strength and contrasty edges (branches against the sky, wires) only a little, so they get no bright halos, and noise and grain are not raised. Best with a '3D resolution' of 150–200%. Unsharp mask: the old sharpening, it pushes every edge the same. Strength: the row above. If the adaptive one cannot start on your video card, the mod goes back to the unsharp mask by itself (this row switches too) and writes the reason to the log. Changes at once. Not yet verified in the game."}, NOTR},
    {{L"Фільтри під інтерфейсом", L"Filters under the interface"}, &g_cfg.filtersUnderHud, kBool, 0, 1, 1, false, L"Graphics", L"FiltersUnderHud", nullptr, nullptr,
     {L"Згладжування, різкість і «Вигляд кадру в грі» діють лише на 3D-картинку, ще до того, як гра малює свій інтерфейс: субтитри, значки, мапа й меню гри лишаються чіткими, без ореолів і розмиття (на консолі інтерфейс теж не фільтрувався). Гамма телевізора й повзунки фоторежиму, як і раніше, діють на весь кадр. Кадри без 3D-картинки гри (титульний екран, завантаження, відео) фільтруються цілими, як раніше. Вимкнено: усе на готовому кадрі разом з інтерфейсом. Змінюється одразу. У грі ще не перевірено.",
      L"Smoothing, sharpening and 'Look of the picture in play' apply to the 3D picture only, before the game draws its interface: subtitles, icons, the map and the game's menus stay sharp, with no halos or blur (the console never filtered its interface either). The TV gamma and the photo mode sliders still cover the whole frame. Frames without the game's 3D picture (title screen, loading, videos) are filtered whole as before. Off: everything on the finished frame with the interface. Changes at once. Not yet verified in the game."}, NOTR},
    {{L"Сила світіння (bloom), %", L"Glow (bloom), %"}, &g_cfg.bloomStrength, kInt, 0, 300, 10, false, L"Graphics", L"BloomStrength", nullptr, nullptr,
     {L"Власне світіння гри навколо яскравих місць. 100 = як у грі, 0 = вимкнено, більше = сильніше. Змінюється одразу.",
      L"The game's own glow around bright spots. 100 = as the game, 0 = off, more = stronger. Changes at once."}, NOTR},
    {{L"Плавне світіння й розфокус", L"Smooth glow and depth of field"}, &g_cfg.smoothBloom, kBool, 0, 1, 1, false, L"Graphics", L"SmoothBloom", nullptr, nullptr,
     {L"Свічення (bloom) навколо ламп і фар та розмиття глибини різкості гра рахує в малих буферах і розтягує на екран одним простим відліком, тому на 1440p і 4K їхні краї мають квадратні сходинки. Мод читає ці буфери плавним бікубічним фільтром (чотири відліки замість одного): ореоли й розмиті краї стають круглими й рівними. Яскравість та сама, світіння трохи ширше. Найбільше дає з вимкненим «Збільшувати буфери розмиття й свічення»: з ним буфери вже більші й сходинки менші. Фінальну картинку тоді малює копія шейдера гри, як для «Вигляду Xbox 360»; без нього кольори лишаються як на PC. Покращення, на консолі такого не було. Змінюється одразу. У грі ще не перевірено.",
      L"The game computes the glow (bloom) around lamps and headlights and the blur of its depth of field in small buffers and stretches them over the screen with one plain tap, so at 1440p and 4K their edges show square steps. The mod reads these buffers with a smooth bicubic filter (four taps instead of one): halos and blurred edges become round and even. The same brightness, a slightly wider glow. It does the most with 'Enlarge blur and bloom buffers' off: with it the buffers are already larger and the steps smaller. The final picture is then drawn by a copy of the game's shader, as for the 'Xbox 360 look'; without that look the colours stay as on PC. An improvement, the console did not have it. Changes at once. Not yet verified in the game."}, NOTR},
    {{L"Дизеринг проти смуг", L"Dither against banding"}, &g_cfg.dither, kBool, 0, 1, 1, false, L"Graphics", L"Dither", nullptr, nullptr,
     {L"Гра записує фінальну картинку з 8 бітами на колір, тому плавні переходи (нічне небо, туман, темні кімнати, краї ореолів) розпадаються на смуги-сходинки. Мод перед цим записом додає дуже дрібний нерухомий шум на пів рівня яскравості: смуги зникають, а сам шум не видно як зернистість (він не рухається від кадру до кадру). Чисто чорне й чисто біле не змінюються. З «Виглядом Xbox 360» той самий шум додається ще й після гамми телевізора, яка знову розтягує темні рівні. Фінальну картинку тоді малює копія шейдера гри, як для «Плавного світіння й розфокусу»; без «Вигляду Xbox 360» кольори лишаються як на PC. Покращення, на консолі такого не було. Змінюється одразу. У грі ще не перевірено.",
      L"The game stores the final picture with 8 bits per colour, so smooth gradients (a night sky, fog, dark rooms, the edges of halos) break into stepped bands. Right before that write the mod adds a very fine, still noise of half a brightness level: the bands go away, and the noise itself does not show as grain (it does not move from frame to frame). Pure black and pure white do not change. With the 'Xbox 360 look' the same noise is also added after the TV gamma, which stretches the dark levels apart again. The final picture is then drawn by a copy of the game's shader, as for 'Smooth glow and depth of field'; without the 'Xbox 360 look' the colours stay as on PC. An improvement, the console did not have it. Changes at once. Not yet verified in the game."}, NOTR},
    {{L"Експозиція (яскравість сцени)", L"Exposure (scene brightness)"}, &g_cfg.exposureComp, kInt, -30, 30, 1, false, L"Graphics", L"ExposureComp", nullptr, nullptr,
     {L"Поправка до автоекспозиції гри в кроках діафрагми: +1.0 удвічі яскравіше, -1.0 удвічі темніше. Гра й далі сама підлаштовується під світло. Змінюється одразу.",
      L"A correction to the game's auto exposure in stops: +1.0 twice as bright, -1.0 half as bright. The game still adapts to the light by itself. Changes at once."},
     NOTR, nullptr, 0, nullptr, FormatStops},
    {{L"Вигляд кадру в грі", L"Look of the picture in play"}, &g_cfg.gameLook, kInt, 0, kFilterCountForMenu - 1, 1, false, L"Graphics", L"GameLook", nullptr, nullptr,
     {L"Фільтр фоторежиму, що діє весь час, не лише у фоторежимі: тепла, холодна, кіно, плівка, нуар, ваші пресети «Мій 1..4» тощо. «Без» = картинка гри. У фоторежимі діють його власні повзунки. Змінюється одразу.",
      L"A photo mode filter that stays on all the time, not only in photo mode: warm, cold, cinema, film, noir, your presets \"Mine 1..4\" and more. \"None\" = the game's picture. In photo mode its own sliders apply. Changes at once."},
     NOTR, nullptr, 0, nullptr, FormatFilterForMenu},
    {{L"Анізотропна фільтрація текстур (макс.)", L"Anisotropic texture filtering (max)"}, &g_cfg.anisotropy, kInt, 1, 16, 1, false, L"Graphics", L"Anisotropy", ApplyGraphicsTweaks, nullptr,
     {L"Чіткість текстур під кутом (дорога, стіни). Гра за замовчуванням 8.", L"Sharpness of textures at an angle (road, walls). The game uses 8."}, NOTR},
    {{L"Ближня площина камери", L"Camera near plane"}, &g_cfg.nearPlane, kInt, 10, 200, 5, false, L"Graphics", L"NearPlane", nullptr, nullptr,
     {L"Проти блимання далеких поверхонь (дорога, дахи, написи «перебивають» одне одного). Більше = точніша глибина вдалині, але занадто велике значення зрізає те, що впритул до камери. Гра: 1.0; типово 20.0 (найточніше, так далекі текстури не блимають). Якщо щось впритул до камери зрізається, зменшіть. Катсцени не змінюються. Діє одразу.",
      L"Against far surfaces flickering (roads, roofs, decals fighting each other). Higher = more depth precision far away, but too high cuts what is right at the camera. Game: 1.0; default 20.0 (the most precise: far textures do not flicker). If something right at the camera is cut, lower it. Cutscenes are not changed. Changes at once."},
     NOTR, nullptr, 0, nullptr, FormatTenths},
    {{L"Стабільніші далекі тіні", L"Steadier far shadows"}, &g_cfg.shadowBiasFix, kBool, 0, 1, 1, false, L"Graphics", L"ShadowBiasFix", ApplyShadowBias, nullptr,
     {L"Більше зміщення глибини для двох дальніх каскадів тіней: вдалині не повзуть смуги тіней, коли рухається камера. Діє одразу.",
      L"More depth bias on the two far shadow cascades: no crawling shadow stripes in the distance as the camera moves. Changes at once."}, NOTR},
    {{L"Згладжені далекі тіні", L"Smoother far shadows"}, &g_cfg.farShadowFilter, kBool, 0, 1, 1, false, L"Graphics", L"FarShadowFilter", nullptr, nullptr,
     {L"Дальній каскад сонячних тіней (приблизно від 200 одиниць) гра читає одним відліком, тому вдалині краї тіней стовпів, парканів і будинків жорсткі, «сходинками» й повзуть, коли рухається камера. Мод фільтрує його тими самими чотирма відліками, що й гра два ближчі каскади. Це виправлення шейдера гри, не відновлення з консолі. Змінюється одразу. У грі ще не перевірено.",
      L"The game reads the far sun shadow cascade (from about 200 units out) with a single tap, so far shadow edges of poles, fences and houses are hard, stair-stepped and crawl as the camera moves. The mod filters it with the same four taps the game uses for the two near cascades. A fix to the game's shader, not a console restoration. Changes at once. Not yet verified in the game."}, NOTR},
    {{L"Міпмапи для текстур без них", L"Mipmaps for textures without them"}, &g_cfg.generateMips, kBool, 0, 1, 1, true, L"Graphics", L"GenerateMips", nullptr, nullptr,
     {L"Частина текстур гри збережена без зменшених копій (міпмапів): вдалині вони мерехтять і «кипять», коли камера рухається. Мод будує ці копії під час завантаження. Потрібен перезапуск; трохи більше відеопам'яті.",
      L"Some of the game's textures are stored without their smaller copies (mipmaps): far away they shimmer as the camera moves. The mod builds those copies when they load. Restart needed; a little more video memory."}, NOTR},
    {{L"Кут огляду в грі, %", L"Field of view in play, %"}, &g_cfg.gameFov, kInt, 70, 150, 5, false, L"Graphics", L"FieldOfView", nullptr, nullptr,
     {L"У відсотках від кута самої гри (близько 65°): 100 як у грі, 120 приблизно 78°, 140 приблизно 91°. Наближення при прицілюванні й камера в машині зберігаються, лише ширші. Катсцени й фоторежим не змінюються. Змінюється одразу.",
      L"In percent of the game's own angle (about 65°): 100 as the game, 120 about 78°, 140 about 91°. The zoom when aiming and the car camera stay, only wider. Cutscenes and the photo mode are not changed. Changes at once."}, NOTR},
    {{L"Зсув відбиттів (експеримент)", L"Reflection offset fix (experiment)"}, &g_cfg.reflectionBiasFix, kBool, 0, 1, 1, false, L"Graphics", L"ReflectionBiasFix", ApplyGraphicsTweaks, nullptr,
     {L"Матриця зсуву відбиттів двічі брала ширину замість ширини й висоти (помилка PC-порту), тож відбиття в калюжах і склі були трохи зсунуті по вертикалі. Виправлення знайдене нами в дизасемблері гри; у грі ще не підтверджене. Змінюється одразу.", L"The reflection offset matrix took the width twice instead of width and height (a bug of the PC port), so reflections in puddles and glass were shifted slightly vertically. The fix was found by us in the game disassembly; not yet confirmed in the game. Changes at once."}, NOTR},
    {{L"Вигляд Xbox 360 (експеримент)", L"Xbox 360 look (experiment)"}, &g_cfg.xboxLook, kBool, 0, 1, 1, false, L"Graphics", L"XboxLook", nullptr, nullptr,
     {L"Повертає те, що картинка мала на консолі, а на PC зникло: кольорокорекцію сцени (контраст, насиченість і відтінок для години доби й погоди: гра досі рахує ці числа щокадру, але її шейдер їх не читає) і гамму телевізора. Сила й варіанти нижче. У головному меню гри не діє. У грі ще не перевірено, тому типово вимкнено. Для повного вигляду ввімкніть ще «Експозиція як на Xbox». Змінюється одразу.",
      L"Brings back what the picture had on the console and lost on PC: the scene colour grading (contrast, saturation and tint for the hour and the weather: the game still computes these numbers every frame, but its shader does not read them) and the TV gamma. Strength and variants are below. Does nothing in the game's main menu. Not checked in the game yet, so off by default. For the full look also turn on 'Xbox exposure'. Changes at once."},
     {L"Вигляд Xbox 360", L"Xbox 360 look"}},
    {{L"Кольорокорекція сцени, %", L"Scene colour grading, %"}, &g_cfg.xboxGrade, kInt, 0, 100, 10, false, L"Graphics", L"XboxGrade", nullptr, nullptr,
     {L"Наскільки сильно застосовувати контраст, насиченість і колірний відтінок, які гра задає для години доби й погоди. 0 = кольори як на PC. Діє лише з увімкненим «Вигляд Xbox 360». Змінюється одразу.",
      L"How strongly the contrast, saturation and colour tint the game sets for the hour and the weather are applied. 0 = the PC colours. Works only with 'Xbox 360 look' on. Changes at once."}, NOTR},
    {{L"Контраст: варіант формули", L"Contrast: formula variant"}, &g_cfg.xboxContrast, kInt, 0, 3, 1, false, L"Graphics", L"XboxContrastFormula", nullptr, kXboxContrastTr,
     {L"Шейдера консолі в нас немає, тому точна формула невідома: є три прочитання тих самих чисел гри. А: так формулу Xbox описує ZachFix (додатне значення затемнює). Б: контраст навколо середнього сірого (наша здогадка). В: розтяг між точками чорного й білого. Перемикайте в одній і тій самій сцені й порівнюйте. Числа гри пишуться в ufx.log (рядки «xboxlook: ENV»): надішліть їх разом із враженнями.",
      L"We do not have the console's shader, so the exact formula is unknown: there are three readings of the same numbers of the game. A: how ZachFix describes the Xbox formula (a positive value darkens). B: contrast around mid grey (our guess). C: a stretch between a black and a white point. Switch them in one and the same scene and compare. The game's numbers go to ufx.log (lines 'xboxlook: ENV'): send them along with what you see."}, NOTR},
    {{L"Насиченість: варіант формули", L"Saturation: formula variant"}, &g_cfg.xboxChroma, kInt, 0, 2, 1, false, L"Graphics", L"XboxChromaFormula", nullptr, kXboxChromaTr,
     {L"Невідомо, яке число гри означає повний колір: 0 чи 1. Якщо з одним варіантом картинка стала чорно-білою, правильний інший.",
      L"It is not known which number of the game means full colour: 0 or 1. If the picture turns black and white with one variant, the other one is right."}, NOTR},
    {{L"Гамма телевізора (HDTV), %", L"TV gamma (HDTV), %"}, &g_cfg.xboxGamma, kInt, 0, 100, 10, false, L"Graphics", L"XboxGamma", nullptr, nullptr,
     {L"Консоль виводила картинку на телевізор через криву BT.709: тіні темніші й глибші, ніж на моніторі. Діє на весь кадр разом з інтерфейсом гри (меню F10 не зачіпає). 0 = вимкнено. Це загальна властивість консолі, для цієї гри не підтверджена. Діє лише з увімкненим «Вигляд Xbox 360». Змінюється одразу.",
      L"The console sent the picture to a TV through the BT.709 curve: darker, deeper shadows than on a monitor. Applies to the whole frame with the game's interface (not to the F10 menu). 0 = off. A general property of the console, not confirmed for this game. Works only with 'Xbox 360 look' on. Changes at once."}, NOTR},
    {{L"М'які світлі ділянки, %", L"Soft highlights, %"}, &g_cfg.xboxShoulder, kInt, 0, 100, 10, false, L"Graphics", L"XboxShoulder", nullptr, nullptr,
     {L"Яскраві місця (небо, лампи) не вибілюються різко, а стискаються плавно. На консолі цього не було: це покращення, а не відновлення. 0 = вимкнено. Діє лише з увімкненим «Вигляд Xbox 360». Змінюється одразу.",
      L"Bright areas (sky, lamps) roll off smoothly instead of clipping to white. The console did not have this: an improvement, not a restoration. 0 = off. Works only with 'Xbox 360 look' on. Changes at once."}, NOTR},
    {{L"Експозиція як на Xbox", L"Xbox exposure"}, &g_cfg.xboxExposure, kBool, 0, 1, 1, false, L"Graphics", L"XboxExposure", ApplyGraphicsTweaks, nullptr,
     {L"PC-версія затемнює картинку на 15% (множник 0,85), консоль ні. Увімкнення знімає множник. Працює й без перемикача «Вигляд Xbox 360».", L"The PC version darkens the picture by 15% (a 0.85 factor), the console did not. Turning it on removes the factor. Works without the 'Xbox 360 look' switch too."}, NOTR},
    {{L"Іконки кнопок ґеймпада", L"Gamepad button icons"}, &g_cfg.glyphTheme, kInt, 0, 3, 1, true, L"Graphics", L"GlyphTheme", nullptr, kGlyphTr,
     {L"Іконки підказок при грі з ґеймпадом. Авто: PlayStation для DualSense, інакше Xbox.\nПотрібні файли в UltimateFix\\glyphs\\gamepad.", L"Hint icons when playing with a gamepad. Auto: PlayStation for a DualSense, else Xbox.\nThe files must be in UltimateFix\\glyphs\\gamepad."},
     {L"Інтерфейс", L"Interface"}},
    {{L"Затемнення до останнього пікселя", L"Fades to the last pixel"}, &g_cfg.fullscreenEdgeFix, kBool, 0, 1, 1, false, L"Graphics", L"FullscreenEdgeFix", ApplyEdgeFix, nullptr,
     {L"На роздільності понад 1280×720 затемнення, екрани завантаження й сповіщення про предмет лишали смужку в 1 піксель справа й знизу. Тепер вони накривають екран повністю. Змінюється одразу.",
      L"Above 1280x720 fades, loading screens and the item notice left a 1-pixel strip at the right and the bottom. Now they cover the whole screen. Changes at once."}, NOTR},
    {{L"Півпіксельний зсув 2D", L"2D half-pixel offset"}, &g_cfg.pixelOffsetFix, kBool, 0, 1, 1, false, L"Graphics", L"FixPixelOffset", nullptr, nullptr,
     {L"Для чіткого 2D у вищій роздільності: підтискає сусідні спрайти атласу. Якщо інтерфейс поплив, вимкніть. Діє лише тоді, коли задано вихідну роздільність (не 1280×720). Змінюється одразу.", L"For sharp 2D at a higher resolution: stops neighbouring atlas sprites from bleeding in. If the interface drifts, turn it off. Works only when an output resolution is set (not 1280×720). Changes at once."}, NOTR},
    RESET_ITEM
};
Item g_pageWorld[] = {
    {{L"Дальність, клас 3 (гра 5000)", L"Draw distance, class 3 (game: 5000)"}, &g_cfg.farClass3, kInt, 5000, 10000, 1000, false, L"World", L"FarClass3", ApplyWorldDistance, nullptr,
     {L"Далі цієї відстані об'єкти класу 3 не малюються. Числа це одиниці світу: усе місто близько 80000 завширшки, а гра завантажує навколо вас область приблизно 10000. Тому більше 10000 поставити не можна: це нічого не додало б. Типово 5000 (як у грі).", L"Class 3 objects farther than this are not drawn. The numbers are world units: the whole town is about 80000 wide and the game loads an area of about 10000 around you, so the value stops at 10000: more would add nothing. Default 5000 (as the game)."}, {L"Дальність малювання", L"Draw distance"}},
    {{L"Дальність, клас 4 (гра 1000)", L"Draw distance, class 4 (game: 1000)"}, &g_cfg.farClass4, kInt, 1000, 5000, 500, false, L"World", L"FarClass4", ApplyWorldDistance, nullptr,
     {L"Відстань, на якій зникають об'єкти класу 4 (двері, манекени тощо). Гра 1000, найбільше 5000: далі нічого не змінюється, бо завантажена область обмежена.", L"Distance at which class 4 objects (doors, mannequins, ...) vanish. Game 1000, at most 5000: beyond that nothing changes, the loaded area is limited."}, NOTR},
    {{L"Будки й автомати (гра 500)", L"Booths and vending machines (game 500)"}, &g_cfg.farClass5, kInt, 500, 10000, 500, false, L"World", L"FarClass5", ApplyWorldDistance, nullptr,
     {L"Будки й автомати у грі зникали на 500; порт це «забув» виправити. Типово 2000; якщо вони ще зникають на очах, ставте більше (найбільше 10000, далі гра нічого не вантажить).", L"Booths and vending machines vanished at 500 in the game; the port forgot to fix it. Default 2000; if they still vanish in sight, set more (at most 10000: the game loads nothing beyond)."}, NOTR},
    {{L"Радіус активації об'єктів (×, гра: 1)", L"Object activation radius (×, game: 1)"}, &g_cfg.activationScale, kInt, 1, 3, 1, false, L"World", L"ActivationScale", ApplyWorldExtra, nullptr,
     {L"Радіус, у якому об'єкти оновлюються (ШІ, анімація). Більше значення: більше навантаження.", L"Radius in which objects are updated (AI, animation). More: more load."}, {L"Об'єкти", L"Objects"}},
    {{L"Дальність деталей LOD (×, гра 1)", L"Model detail distance, LOD (×, game: 1)"}, &g_cfg.lodScale, kInt, 1, 4, 1, false, L"World", L"LodScale", ApplyWorldExtra, nullptr,
     {L"Детальні моделі лишаються далі від камери. Більше значення: більше трикутників.", L"Detailed models stay farther from the camera. More: more triangles."}, NOTR},
    {{L"Повні далекі моделі (×, гра 1)", L"Full model of far objects (×, game: 1)"}, &g_cfg.alt3dScale, kInt, 1, 3, 1, false, L"World", L"Alternate3DScale", nullptr, nullptr,
     {L"Проти появи будинків і предметів перед носом: далекі об'єкти, які гра малює спрощеними, отримують повну модель з більшої відстані. 2 це ще одне кільце клітинок, 3 вся завантажена область. Потребує памʼяті (4 ГБ, мод ставить сам). ЕКСПЕРИМЕНТАЛЬНО: далекі об'єкти можуть мерехтіти; тоді поставте 1. Змінюється одразу.", L"Against buildings and props popping in right in front of you: far objects that the game draws in a simplified form get their full model from farther away. 2 is one more ring of cells, 3 the whole loaded area. Needs memory (4 GB, set by the mod). EXPERIMENTAL: far objects may flicker; if so, set 1. Changes at once."}, NOTR},
    {{L"Повна деталізація зовнішніх клітинок", L"Full detail in the outer cells"}, &g_cfg.highDetail, kBool, 0, 1, 1, false, L"World", L"HighDetail", ApplyWorldExtra, nullptr,
     {L"Зовнішнє кільце світу вантажиться з повним набором ресурсів. Потребує багато пам'яті (4 ГБ, мод ставить сам).", L"The outer ring of the world loads the full resource set. Needs a lot of memory (4 GB, set by the mod)."}, NOTR},
    {{L"Видимість в інтер'єрах", L"Visibility indoors"}, &g_cfg.interiorVisibility, kInt, 0, 2, 1, false, L"World", L"InteriorVisibility", ApplyWorldExtra, nullptr,
     {L"Подушки й речі біля стін і дзеркал зникали через хибний тест. 0 як у грі, 1 міст на першому місці, 2 на обох.", L"Pillows and props near walls and mirrors vanished because of a faulty test. 0 as the game, 1 bridge at the first place, 2 at both."},
     {L"Інтер'єри", L"Interiors"}},
    RESET_ITEM
};
Item g_pageGameplay[] = {
    {{L"Складність (нова гра і зараз)", L"Difficulty (new game and now)"}, &g_cfg.difficulty, kInt, 0, 2, 1, false, L"Gameplay", L"Difficulty", ApplyDifficulty, kDifficultyTr,
     {L"PC-версія завжди стартувала на легкій, консоль на звичайній. Змінюється одразу, навіть у грі; екран вибору нової гри відкривається на цій складності. Діє лише з DifficultyControl=1 в UltimateFix.ini (так типово).", L"The PC version always started on easy, the console on normal. Changes at once, even in the game; the new-game selection screen opens on it. Works only with DifficultyControl=1 in UltimateFix.ini (the default)."},
     {L"Складність", L"Difficulty"}},
    {{L"Відбиття на мокрих поверхнях", L"Wet surface reflections"}, &g_cfg.wetReflections, kBool, 0, 1, 1, false, L"Gameplay", L"WetReflections", ApplyGameplay, nullptr,
     {L"Вмикає недосяжний у PC-коді шлях вологих відбиттів: калюжі й мокрий асфальт відбивають світло. Ідея й адреса з ZachFix; перевірено в грі. Змінюється одразу.", L"Turns on the wet reflection path that the PC code never reaches: puddles and wet asphalt reflect light. The idea and the address come from ZachFix; checked in the game. Changes at once."}, {L"Вигляд і бій", L"Look and combat"}},
    {{L"Допуск кута прицілу (при збоях)", L"Aim angle tolerance (only for glitches)"}, &g_cfg.aimTolerance, kBool, 0, 1, 1, true, L"Gameplay", L"AimTolerance", nullptr, nullptr,
     {L"Порівняння меж кута камери з допуском. Потрібне лише коли приціл «залипає» біля межі.", L"Compares the camera angle limits with a tolerance. Only needed when the aim sticks at the limit."}, NOTR},
    {{L"Об'ємний звук 5.1 / 7.1", L"Surround sound 5.1 / 7.1"}, &g_cfg.surround, kInt, 0, 2, 1, false, L"Audio", L"Surround", ApplySurround, kSurroundTr,
     {L"Діє лише тоді, коли звуковий пристрій у Windows має більше двох каналів (5.1, 7.1, просторовий звук для навушників); на стерео нічого не змінюється. Гра щокадру задає кожному звуку гучність у вигляді, придатному лише для стерео. Пристрій із більшою кількістю каналів, найімовірніше, такого не приймає, і звук лишається там і з тією гучністю, з якими почався (гравці описували звук гальм, що не стихає). «Авто» робить як на Xbox 360: звук у світі чутно з того боку, де його джерело, решта звуків іде на передні й тилові колонки. Якщо ліве й праве переплутані, оберіть «Авто, боки навпаки». У грі ще не перевірено; що знайдено й зроблено, пише UltimateFix\\ufx.log (рядки «audio:»). Змінюється одразу.",
      L"Acts only when the Windows sound device has more than two channels (5.1, 7.1, spatial sound for headphones); on stereo nothing is changed. Every frame the game gives each sound a volume made for stereo only, which a wider device should not take: the sound then keeps the place and the volume it started with (players described a brake sound that never stops). 'Auto' does what the Xbox 360 did: a sound in the world is heard from the side its source is on, other sounds go to the front and rear speakers. If left and right are mixed up, choose 'Auto, sides swapped'. Not yet checked in the game; UltimateFix\\ufx.log says what was found and done (lines starting with 'audio:'). Changes at once."},
     {L"Звук", L"Sound"}},
    RESET_ITEM
};
// The waypoint to the game's objective: the key (always with Ctrl) and a button that does the same, for the pad.
const int kObjectiveKeys[3] = {0, VK_F7, VK_F8};
const Tr kObjectiveKeyNames[3] = {{L"Вимк.", L"Off"}, {L"Ctrl+F7", L"Ctrl+F7"}, {L"Ctrl+F8", L"Ctrl+F8"}};
void FormatObjectiveKey(int v, wchar_t* out) { wsprintfW(out, g_cfg.language == 1 ? L"Ctrl + key %d" : L"Ctrl + клавіша %d", v); }
int g_objectivePick = 0;
const int kObjectiveValues[1] = {1};
const Tr kObjectiveNames[1] = {{L"Поставити", L"Set"}};
void DoObjective() { g_objectivePick = 0; WaypointToObjective(); }

Item g_pageMapItems[] = {
    {{L"Мітка (F7): на мапі й у світі", L"Waypoint (F7): map and world"}, &g_cfg.waypoint, kBool, 0, 1, 1, true, L"Waypoint", L"Enabled", nullptr, nullptr,
     {L"F7 на мапі паузи ставить мітку в центрі мапи (де хрестик), F7 у світі ставить її там, де ви стоїте (або де вільна камера); Shift+F7 прибирає. У світі над місцем мітки висить напис «N Yards» ігровим шрифтом (як у самій грі), за краєм екрана він стоїть на краю зі стрілкою; у катсценах напису немає. На мапі червоний X, коли увімкнено «Усі плитки мапи» і мапа не обертається (північ або південь угорі). Те саме робить натискання тачпада DualSense. Потрібен перезапуск.",
      L"F7 on the pause map sets the waypoint at the middle of the map (the cross), F7 in the world sets it where you stand (or where the free camera is); Shift+F7 removes it. In the world an \"N Yards\" label in the game's font hangs over the place (like the game's own), off the screen it sits on the edge with an arrow; in cutscenes there is no label. On the map a red X, when 'All map tiles' is on and the map does not turn (north or south up). A DualSense touchpad click does the same. Needs a restart."},
     {L"Мітка", L"Waypoint"}},
    {{L"Мітка як ціль гри (мінімапа)", L"Waypoint as a game objective"}, &g_cfg.waypointMinimap, kBool, 0, 1, 1, true, L"Waypoint", L"Minimap", nullptr, nullptr,
     {L"Мітка стає точкою цілі в рідному списку мапи гри: на мінімапі з'являються червоні стрілки гри на краю в бік мітки (чим далі, тим їх більше), на мапі паузи іконка цілі. Так само, як гра показує свої завдання. Потрібен перезапуск.",
      L"The waypoint becomes an objective point in the game's own map list: the minimap shows the game's red arrows on its edge towards the waypoint (more of them the farther it is), the pause map shows the objective icon. Just like the game shows its own goals. Needs a restart."},
     NOTR},
    {{L"Стрілки гри над міткою", L"The game's arrows over the waypoint"}, &g_cfg.waypointBeacon, kBool, 0, 1, 1, true, L"Waypoint", L"Beacon", nullptr, nullptr,
     {L"Поки місце мітки видно на екрані, над ним стоять рідні червоні стрілки гри з написом «N Yards», як над сюжетними цілями (чим далі, тим стрілок більше; ближче 15 ярдів вони зелені). Напис мода лишається, коли мітка за краєм екрана чи позаду, і поки гра не завантажила картинку стрілок. Вимкнути можна одразу; щоб увімкнути знову, потрібен перезапуск.",
      L"While the waypoint's place is on the screen, the game's own red arrows with \"N Yards\" stand over it, as over story goals (more arrows the farther it is; green within 15 yards). The mod's label stays for a waypoint off the screen or behind you, and while the game has not loaded the arrow picture. Turning it off works at once; turning it back on needs a restart."},
     NOTR},
    {{L"Мітка на ціль гри: клавіша", L"Waypoint to the game's objective: key"}, &g_cfg.waypointObjectiveKey, kInt, 0, 254, 1, false, L"Waypoint", L"ObjectiveKey", nullptr, nullptr,
     {L"Ctrl і ця клавіша ставлять мітку на поточну ціль гри (спершу сюжетне місце призначення, потім інші точки цілей на мапі), тож відстань і стрілка на краю екрана працюють і для сюжетних цілей. Ще одне натискання: наступна ціль, якщо їх кілька. Якщо цілі на мапі немає, нічого не зміниться. Там, де гра вже показує над ціллю свої стрілки, мод других не малює. Enter: свій код клавіші. Змінюється одразу.",
      L"Ctrl and this key put the waypoint on the game's current objective (the story destination first, then the other objective points of the map), so the distance and the arrow on the screen edge work for story goals too. Press again for the next one when there are several. With no objective on the map nothing changes. Where the game already shows its own arrows over the goal, the mod draws no second ones. Enter: a key code of your own. Changes at once."},
     NOTR, kObjectiveKeys, 3, kObjectiveKeyNames, FormatObjectiveKey},
    {{L"Мітка на поточну ціль гри", L"Waypoint to the game's current objective"}, &g_objectivePick, kInt, 0, 1, 1, false, nullptr, nullptr, DoObjective, nullptr,
     {L"Те саме, що Ctrl+F7, кнопкою: зручно з ґеймпадом. Працює під час гри; якщо цілі на мапі немає, мітка лишається як була. Що гра зараз вважає ціллю, видно в розділі «Сюжет».",
      L"The same as Ctrl+F7, as a button: handy with a gamepad. Works while playing; with no objective on the map the waypoint stays as it was. The Story section shows what the game counts as the goal now."},
     NOTR, kObjectiveValues, 1, kObjectiveNames, nullptr, false},
    {{L"Орієнтація мапи", L"Map orientation"}, &g_cfg.mapOrientation, kInt, 0, 2, 1, false, L"Map", L"Orientation", ApplyMapFix, kMapOrientTr,
     {L"Обертається як у грі, або північ чи південь завжди угорі (стрілка Йорка обертається замість мапи).", L"Rotates as in the game, or north or south is always up (York's arrow turns instead of the map)."}, {L"Мапа", L"Map"}},
    {{L"Найменший масштаб, %", L"Smallest zoom, %"}, &g_cfg.mapMinZoom, kInt, 10, 100, 5, false, L"Map", L"MinZoom", ApplyMapFix, nullptr,
     {L"Наскільки можна віддалити мапу (гра: 100). 20 показує все місто.", L"How far the map can zoom out (game: 100). 20 shows the whole town."}, NOTR},
    {{L"Масштаб при відкритті, %", L"Zoom when opened, %"}, &g_cfg.mapDefaultZoom, kInt, 10, 350, 10, false, L"Map", L"DefaultZoom", ApplyMapFix, nullptr,
     {L"З яким масштабом відкривається мапа.", L"The zoom the map opens with."}, NOTR},
    {{L"Усі плитки мапи", L"All map tiles"}, &g_cfg.mapFix, kBool, 0, 1, 1, true, L"Map", L"Enabled", nullptr, nullptr,
     {L"Малює всі 64 плитки одразу й тримає їх у пам'яті (близько 100 МБ). Вимкнення повертає мапу гри.", L"Draws all 64 tiles at once and keeps them in memory (about 100 MB). Turning it off restores the game's map."}, NOTR},
    RESET_ITEM
};
Item g_pageInput[] = {
    {{L"Автоперемикання клавіатура / ґеймпад", L"Auto switch keyboard / gamepad"}, &g_cfg.autoSwitchInput, kBool, 0, 1, 1, false, L"Input", L"AutoSwitch", nullptr, nullptr,
     {L"Без цього гра в режимі клавіатури ігнорує ґеймпад. Режим міняється за тим, чим ви щойно скористались.", L"Without it the game in keyboard mode ignores the gamepad. The mode follows whatever you used last."},
     {L"Ґеймпад", L"Gamepad"}},
    {{L"DualSense / DualShock як Xbox 360", L"DualSense / DualShock as an Xbox 360 pad"}, &g_cfg.sonyLayout, kBool, 0, 1, 1, false, L"Input", L"SonyLayout", nullptr, nullptr,
     {L"Переводить осі й кнопки Sony в розкладку Xbox, яку чекає гра (камера не крутиться сама).", L"Converts Sony axes and buttons to the Xbox layout the game expects (the camera no longer spins by itself)."}, NOTR},
    {{L"Бокове переміщення в бою на LB/RB", L"Combat strafe on LB/RB"}, &g_cfg.combatStrafe, kBool, 0, 1, 1, false, L"Input", L"CombatStrafe", nullptr, nullptr,
     {L"Як на консолі: у режимі прицілювання на ціль LB/RB роблять крок убік. Ці кнопки також зайняті: LB прицілювання, RB постріл (з «Приціл на L2, стрільба на R2»: L1 біг, R1 затримка дихання й захоплення цілі), тож натискання будь-якої з них під час захоплення цілі теж робить крок убік. Змінюється одразу. Експериментально.", L"As on the console: while locked on a target LB/RB sidestep. These buttons are also bound: LB aim, RB fire (with 'Aim on L2, fire on R2': L1 run, R1 hold breath and lock-on), so pressing either while locked on also sidesteps. Changes at once. Experimental."}, NOTR},
    {{L"Швидкий розворот, як на Xbox 360", L"Quick turn, as on the Xbox 360"}, &g_cfg.quickTurn, kBool, 0, 1, 1, false, L"Input", L"QuickTurn", nullptr, nullptr,
     {L"Як на консолі: Йорк стоїть чи йде спиною до камери, ви тягнете лівий стік назад і водночас (щонайпізніше за п'яту частину секунди) натискаєте кнопку бігу, і він розвертається на місці. Пізніше Йорк уже встигає повернутися сам. Кнопка бігу: L1 з пунктом «Приціл на L2, стрільба на R2», лівий триґер у розкладці гри; на клавіатурі S разом із лівим Shift. Якщо біг уже затиснутий, нічого не міняється: Йорк повертається сам, як і раніше. Не діє під час прицілювання, в машині, у катсценах і з відкритим цим меню. Змінюється одразу. Експериментально.", L"As on the console: with York standing or walking with his back to the camera, pull the left stick back and press Run at the same moment (at the latest a fifth of a second later), and he turns around on the spot. Later York has already turned by himself. Run is L1 with 'Aim on L2, fire on R2', the left trigger in the game's layout; on the keyboard S together with Left Shift. If Run is already held nothing changes: York turns by himself as before. Not while aiming, in a car, in cutscenes or with this menu open. Changes at once. Experimental."}, NOTR},
    {{L"Приціл на L2, стрільба на R2", L"Aim on L2, fire on R2"}, &g_cfg.triggerFire, kBool, 0, 1, 1, false, L"Input", L"TriggerFire", nullptr, nullptr,
     {L"Приціл на лівому триґері, стрільба на правому; біг переходить на L1, затримка дихання й захоплення цілі на R1. У машині діє розкладка гри, і так само на ґеймпаді Sony, який гра читає через Windows із вимкненим пунктом «DualSense / DualShock як ґеймпад Xbox 360». Вимкнено: приціл L1, стрільба R1, як у грі. Змінюється одразу.", L"Aim on the left trigger, fire on the right; run moves to L1, hold breath and lock-on to R1. Behind the wheel the game's layout is used, and so it is on a Sony pad read through Windows with 'DualSense / DualShock as an Xbox 360 pad' off. Off: aim L1, fire R1, as the game. Changes at once."}, NOTR},
    {{L"Поміняти місцями триґери L2/R2", L"Swap the L2/R2 triggers"}, &g_cfg.swapTriggers, kBool, 0, 1, 1, false, L"Input", L"SwapTriggers", nullptr, nullptr,
     {L"Міняє місцями L2 і R2: кнопки, вісь триґерів і ефекти DualSense. Не діє на ґеймпад Sony з вимкненим пунктом «DualSense / DualShock як ґеймпад Xbox 360» і на 4-осьові ґеймпади DirectInput, де вісь Z це правий стік.", L"Swaps L2 and R2: the buttons, the trigger axis and the DualSense effects. Not on a Sony pad with 'DualSense / DualShock as an Xbox 360 pad' off, nor on 4-axis DirectInput pads, whose Z axis is the right stick."}, NOTR},
    {{L"Ґеймпад через XInput (запасний)", L"Gamepad through XInput (fallback)"}, &g_cfg.xinputFallback, kBool, 0, 1, 1, false, L"Input", L"XInputFallback", nullptr, nullptr,
     {L"Страховка для Steam Input, DS4Windows, Xbox-ґеймпада.", L"A fallback for Steam Input, DS4Windows, an Xbox pad."}, NOTR},
    {{L"Відсутній ґеймпад питати у фоні", L"Ask about a missing gamepad in the background"}, &g_cfg.padPollThread, kBool, 0, 1, 1, false, L"Input", L"PadPollThread", nullptr, nullptr,
     {L"Коли ґеймпада немає, Windows на деяких системах відповідає на питання про нього 18-60 мс, і гра смикалась двічі на секунду. Увімкнено: про відсутній ґеймпад двічі на секунду питає окремий фоновий потік, а гра одразу отримує його останню відповідь. Щойно ґеймпад з'явиться, гра знову сама читає його щокадру (підключений ґеймпад відповідає швидко). Вимкнено: питає сама гра, двічі на секунду. Змінюється одразу.",
      L"With no gamepad, Windows takes 18-60 ms to answer a question about it on some systems, and the game hitched twice a second. On: a background thread asks about the missing gamepad twice a second, and the game gets its last answer at once. As soon as a gamepad appears, the game reads it itself every frame again (a connected gamepad answers fast). Off: the game asks itself, twice a second. Changes at once."}, NOTR},
    {{L"DualSense напряму через HID", L"DualSense directly over HID"}, &g_cfg.nativeDualSense, kBool, 0, 1, 1, true, L"Input", L"NativeDualSense", nullptr, nullptr,
     {L"Типово увімкнено: мод сам шукає DualSense і, якщо Windows дає відкрити пристрій, читає його напряму, а гра отримує ці дані замість ґеймпада з Windows. Так буває й з увімкненим Steam Input, і тоді його розкладка для гри не діє: щоб нею користуватися, вимкніть цей пункт. Через Bluetooth вібрація й адаптивні триґери працюють лише з цим пунктом; через USB працюють і без нього. Без DualSense нічого не змінює, гра читає ґеймпад як раніше. Потрібен перезапуск.",
      L"On by default: the mod looks for a DualSense and, when Windows lets it open the device, reads it directly, and the game gets that instead of the pad Windows shows it. That also happens with Steam Input on, and then Steam Input's mapping for the game does not apply: to use it, turn this off. Over Bluetooth, rumble and adaptive triggers work only with this on; over USB they work without it too. With no DualSense nothing changes and the game reads the pad as before. Needs a restart."}, NOTR},
    {{L"Адаптивні триґери DualSense", L"DualSense adaptive triggers"}, &g_cfg.adaptiveTriggers, kBool, 0, 1, 1, false, L"Input", L"AdaptiveTriggers", nullptr, nullptr,
     {L"Триґер пострілу відчувається по-різному для різної зброї (пістолет, дробовик, автомат, гранатомет...). Діє лише з увімкненим «Приціл на L2, стрільба на R2»; з «Поміняти місцями триґери L2/R2» ефект переходить на лівий триґер. За кермом ефекту немає. Потрібен DualSense, який Windows бачить як Sony (Steam Input для гри вимкнено), USB або Bluetooth. Змінюється одразу.", L"The fire trigger feels different for different weapons (pistol, shotgun, SMG, RPG...). Works only with 'Aim on L2, fire on R2' on; with 'Swap the L2/R2 triggers' the effect moves to the left trigger. Behind the wheel there is no effect. Needs a DualSense that Windows sees as a Sony pad (Steam Input off for the game), USB or Bluetooth. Changes at once."},
     {L"Адаптивні триґери", L"Adaptive triggers"}},
    {{L"Сила ефекту, %", L"Effect strength, %"}, &g_cfg.adaptiveStrength, kInt, 0, 100, 10, false, L"Input", L"AdaptiveStrength", nullptr, nullptr,
     {L"Загальна сила опору й вібрації триґерів. 0 вимикає ефекти.", L"Overall strength of the trigger resistance and vibration. 0 turns the effects off."}, NOTR},
    {{L"Легкий опір триґера прицілу", L"Light resistance of the aim trigger"}, &g_cfg.adaptiveAim, kBool, 0, 1, 1, false, L"Input", L"AdaptiveAim", nullptr, nullptr,
     {L"Триґер прицілу трохи тримає, коли ви цілитесь (лише з «Приціл на L2, стрільба на R2»; з «Поміняти місцями триґери L2/R2» це правий триґер).", L"The aim trigger pushes back a little while you aim (only with 'Aim on L2, fire on R2'; with 'Swap the L2/R2 triggers' it is the right one)."}, NOTR},
    {{L"Тест ефекту (відчуйте триґер)", L"Effect test (feel the trigger)"}, &g_cfg.triggerTest, kInt, 0, 6, 1, false, nullptr, nullptr, nullptr, kTriggerTestTr,
     {L"Діє, поки відкрите меню, навіть із вимкненими адаптивними триґерами: виберіть ефект стрілками й натискайте правий триґер. Клік пістолета, важкий клік, вібрація автомата, опір, повільна вібрація, обидва триґери. Вимк. або закриття меню знімає ефект. Якщо нічого не відчувається, надішліть журнал: рядки dualsense output.", L"Works while the menu is open, also with adaptive triggers off: pick an effect with the arrows and press the right trigger. Pistol click, heavy click, machine-gun vibration, resistance, slow rumble, both triggers. Off or closing the menu removes the effect. If you feel nothing, send the log: the dualsense output lines."}, NOTR},
    {{L"Профіль стіків Xbox 360", L"Xbox 360 stick profile"}, &g_cfg.stickProfile, kBool, 0, 1, 1, false, L"Input", L"StickProfile", nullptr, nullptr,
     {L"Плавна мертва зона 24% без згладжування, залежного від FPS. PC-версія реагувала лише після 40% ходу стіка.", L"A smooth 24% dead zone without frame-rate dependent smoothing. The PC version reacted only past 40% of the stick travel."},
     {L"Стіки й триґери", L"Sticks and triggers"}},
    {{L"Аналогові триґери авто (експеримент)", L"Analog car triggers (experiment)"}, &g_cfg.analogTriggers, kBool, 0, 1, 1, false, L"Input", L"AnalogTriggers", nullptr, nullptr,
     {L"Ідея ZachFix. У Director's Cut гальмо й прискорення авто на плечових кнопках; вмикайте лише для досліду.", L"A ZachFix idea. In the Director's Cut the car's brake and boost are on the shoulder buttons; turn on only to try."}, NOTR},
    {{L"Чутливість миші, %", L"Mouse sensitivity, %"}, &g_cfg.mouseSensitivity, kInt, 25, 400, 25, false, L"Input", L"MouseSensitivity", ApplyInputExtras, nullptr,
     {L"Швидкість огляду мишею понад налаштування самої гри. 100 = як у грі; 400 = вчетверо швидше, і найбільша швидкість повороту теж. Діє одразу.", L"Mouse look speed over the game's own setting. 100 = as the game; less is slower, more is faster. Changes at once."},
     {L"Миша", L"Mouse"}},
    {{L"Інвертувати мишу по горизонталі", L"Invert the mouse horizontally"}, &g_cfg.invertMouseX, kBool, 0, 1, 1, false, L"Input", L"InvertMouseX", ApplyInputExtras, nullptr,
     {L"Рух миші вліво повертає камеру вправо і навпаки.", L"Moving the mouse left turns the camera right and back."}, NOTR},
    {{L"Інвертувати мишу по вертикалі", L"Invert the mouse vertically"}, &g_cfg.invertMouseY, kBool, 0, 1, 1, false, L"Input", L"InvertMouseY", ApplyInputExtras, nullptr,
     {L"Рух миші вгору опускає камеру і навпаки, як у льотних іграх.", L"Moving the mouse up looks down and back, as in flight games."}, NOTR},
    {{L"Менше затримки вводу", L"Lower input latency"}, &g_cfg.lowLatencyInput, kBool, 0, 1, 1, false, L"Input", L"LowLatencyInput", ApplyInputExtras, nullptr,
     {L"Опитує ґеймпад перед фіксацією стану: на один такт менше затримки.", L"Polls the gamepad before committing its state: one tick less delay."}, NOTR},
    {{L"Вібрація ґеймпада", L"Gamepad vibration"}, &g_cfg.vibration, kBool, 0, 1, 1, false, L"Input", L"Vibration", nullptr, nullptr,
     {L"Передає рушійну вібрацію гри на ґеймпад (XInput, а для DualSense напряму через HID).", L"Sends the game's rumble to the gamepad (XInput, and for a DualSense directly over HID)."}, {L"Вібрація", L"Vibration"}},
    {{L"Вібрація авто й пострілів (DualSense)", L"Car and shot haptics (DualSense)"}, &g_cfg.synthHaptics, kBool, 0, 1, 1, false, L"Input", L"SynthHaptics", nullptr, nullptr,
     {L"Легкий гул двигуна в авто (за правим триґером) і поштовх на кожен постріл: сильніший для дробовика, магнума й РПГ, дрібніший для пістолета, гудіння для автоматичної зброї. Робиться зі стану гри на двох моторах ґеймпада, до вібрації самої гри (удари, аварії). Працює й без вібрації самої гри, коли гра на передньому плані в режимі ґеймпада; гул авто потребує «DualSense напряму через HID». Це не тонкий haptic DualSense, якому потрібен аудіопотік. Змінюється одразу.", L"A light engine hum in a car (by the right trigger) and a kick for every shot: harder for the shotgun, magnum and RPG, lighter for the pistol, a buzz for automatic weapons. Made from the game state on the pad's two motors, added to the game's own vibration (hits, crashes). Works with the game's vibration off too, while the game is in front and in gamepad mode; the car hum needs 'DualSense directly over HID'. It is not the DualSense's fine haptics, which need an audio stream. Changes at once."}, NOTR},
    {{L"Сила вібрації, %", L"Vibration strength, %"}, &g_cfg.vibrationStrength, kInt, 0, 100, 10, false, L"Input", L"VibrationStrength", nullptr, nullptr,
     {L"Загальний множник вібрації.", L"Overall vibration multiplier."}, NOTR},
    RESET_ITEM
};
Item g_pageFixes[] = {
    {{L"Пауза гри, поки відкрите меню", L"Pause the game while the menu is open"}, &g_cfg.pauseInMenu, kBool, 0, 1, 1, false, L"Time", L"PauseInMenu", nullptr, nullptr,
     {L"Час у грі зупиняється, поки відкрите F10. Якщо після закриття щось зависло (катсцена), вимкніть.", L"Game time stops while F10 is open. If something hangs after closing it (a cutscene), turn it off."}, NOTR},
    {{L"Автовідновлення після лагу", L"Recover by itself after lag"}, &g_cfg.autoRecover, kBool, 0, 1, 1, false, L"Time", L"AutoRecover", nullptr, nullptr,
     {L"Експеримент, користь не доведена. Якщо 60 кадрів поспіль повільні (понад 30 мс, це близько 2 секунд; у режимі 30 к/с понад 60 мс, близько 4 секунд), мод на 4 кадри притримує годинник гри й фізику. Задумано як повтор того, що робило відкриття й закриття F10, але FPS тоді повертався з іншої причини: поки меню відкрите, мод не опитував відсутній ґеймпад (це тепер окремо лікує ThrottleMissingPad в ini). Не частіше раза на 20 секунд, пише в журнал «pacer: recover».",
      L"An experiment, not shown to help. When 60 frames in a row are slow (over 30 ms, about 2 seconds; over 60 ms in the 30 fps mode, about 4 seconds), the mod holds the game clock and physics for 4 frames. It was meant to repeat what opening and closing F10 did, but the frame rate came back then for another reason: while the menu was open the mod did not poll the missing pad (ThrottleMissingPad in the ini now handles that on its own). At most once per 20 seconds, logged as 'pacer: recover'."}, NOTR},
    {{L"Кроків фізики на кадр (проти зависань)", L"Physics steps per frame (against lag)"}, &g_cfg.physicsSteps, kInt, 0, 3, 1, false, L"Time", L"PhysicsSteps", nullptr, kPhysStepsTr,
     {L"Гра робить 1–4 кроки фізики на кадр залежно від часу кадру, а кожен зайвий крок подовжує кадр і викликає ще більше кроків: порочне коло, що закінчується близько 20 к/с. Це лише гіпотеза: справжню причину лагів знайдено в іншому місці (опитування ґеймпада), тож типово 0, тобто як у грі. 1–3 обмежують фізику, але машини й предмети йдуть повільніше за реальний час при низькому FPS. Змінюється одразу.", L"The game takes 1-4 physics steps per frame depending on the frame time, and each extra step lengthens the frame and asks for still more steps: a spiral that ends near 20 fps. This is only a guess: the real cause of the lag was found elsewhere (the pad poll), so the default is 0, the game's own. 1-3 cap physics, but cars and props run slower than real time at a low frame rate. Changes at once."}, NOTR},
    {{L"Фізика в реальному часі понад 120 к/с", L"Real-time physics above 120 fps"}, &g_cfg.highFpsPhysics, kBool, 0, 1, 1, false, L"Time", L"HighFpsPhysics", nullptr, nullptr,
     {L"Коли ліміт кадрів вимкнено й гра видає понад 120 к/с, кадр коротший за пів кроку фізики, а гра однаково робить цілий крок на кожен кадр. Через це авто їде у стільки разів швидше, у скільки FPS більший за 60, пальне згоряє швидше, а внутрішні таймери авто завмирають. З цією опцією короткі кадри ділять цілі кроки, як на Xbox 360: час накопичується, і крок робиться, коли набігає ціла 1/60 с. Фізика, керування авто, пальне, пробіг і звук шин та двигуна йдуть у реальному часі. Авто й предмети тоді рухаються 60 разів на секунду, хоч скільки кадрів малюється, тож у машині картинка може здаватися менш плавною. Кадри завдовжки в цілий крок і більше (60 к/с і нижче, ліміт кадрів) не зачіпаються. Змінюється одразу. У грі ще не перевірено.",
      L"With the frame limit off and more than 120 fps, a frame is shorter than half a physics step, yet the game takes a whole step every frame. So a car drives as many times too fast as the frame rate is above 60, fuel burns faster, and the car's internal timers stand still. With this on, short frames share whole steps as on the Xbox 360: their time is gathered and a step is taken when a whole 1/60 s has passed. Physics, car control, fuel, mileage and the tyre and engine sounds keep real time. Cars and props then move 60 times per second however many frames are drawn, so the picture in a car may look less smooth. Frames of a whole step or longer (60 fps and below, the frame limit) are not touched. Changes at once. Not yet checked in the game."}, NOTR},
    {{L"Крок часу і ліміт кадрів", L"Time step and frame limit"}, &g_cfg.framePacing, kInt, 0, 3, 1, false, L"Time", L"FramePacing", nullptr, kPacingTr,
     {L"Вимк. (типово): кадри не обмежені й ідуть за частотою екрана. На екранах 61–139 Гц гра без ліміту біжить надто швидко (75 Гц: у 1,25 раза, 120 Гц: удвічі), тому там мод сам тримає ліміт 60 і пише про це в журнал; від 140 Гц кадри розблоковані, а машину тримає «Фізика в реальному часі понад 120 к/с». Ліміт 60 к/с: у середньому 60 кадрів на секунду за будь-якої частоти екрана, час гри не відстає від справжнього. «60 к/с, цілі кроки» і «30 к/с» підганяють час цілими кроками 1/60 с і смикаються, коли гра нижче 60 к/с. Змінюється одразу.",
      L"Off (default): the frames are not limited and follow the screen. On screens of 61–139 Hz an unlimited game runs too fast (75 Hz: 1.25 times, 120 Hz: twice), so there the mod keeps the 60 limit by itself and says so in the log; from 140 Hz the frames are free, and \"Real-time physics above 120 fps\" keeps the car right. Limit 60 fps: 60 frames a second on average at any refresh rate, the game clock keeps real time. \"60 fps, whole steps\" and \"30 fps\" move time in whole 1/60 s steps and hitch below 60 fps. Changes at once."},
     {L"Час", L"Time"}},
    {{L"Відносний час (проти зависань)", L"Relative time (against slowdowns)"}, &g_cfg.relativeQpc, kBool, 0, 1, 1, true, L"Time", L"RelativeQpc", nullptr, nullptr,
     {L"Лікує падіння FPS після днів роботи ПК: гра тримала час у єдиній точності.", L"Cures the FPS drop after days of uptime: the game kept time in single precision."}, NOTR},
    {{L"Захист від нульової дельти кадру", L"Zero frame delta guard"}, &g_cfg.zeroDeltaGuard, kBool, 0, 1, 1, true, L"Fixes", L"ZeroDeltaGuard", nullptr, nullptr,
     {L"Нульовий крок кадру ставив швидкість у NaN і вішав гру.", L"A zero frame step turned speed into NaN and froze the game."}, {L"Помилки гри", L"Game bugs"}},
    {{L"Меню й ящик без зависань на нульовому кадрі", L"Menus and the toolbox survive a zero frame"}, &g_cfg.zeroFrameMenus, kBool, 0, 1, 1, true, L"Fixes", L"ZeroFrameMenus", nullptr, nullptr,
     {L"Панель, що відкривається, починає з нульової непрозорості. Кадр нульової тривалості (дні роботи Windows без перезавантаження, сотні FPS, пауза мода) лишав її на нулі, і гра вважала панель закритою: ящик повертав до вибору WEAPONS/ITEMS («не беруться речі»), а список побічних завдань і перегляд «mission» на карті зависали на порожньому екрані з мертвими кнопками. Тепер панель просто чекає наступного кадру. Потрібен перезапуск. У грі ще не перевірено.",
      L"A panel that opens starts at zero opacity. A frame of zero length (days of Windows uptime, hundreds of FPS, the mod's own pause) left it at zero, and the game took the panel for closed: the toolbox fell back to the WEAPONS/ITEMS choice (\"cannot take items out\"), and the side missions list and the map's mission view hung on an empty screen with dead buttons. Now the panel just waits for the next frame. Restart needed. Not yet checked in the game."}, NOTR},
    {{L"Захист від зависання на NaN", L"Guard against the NaN freeze"}, &g_cfg.nanTrapGuard, kBool, 0, 1, 1, true, L"Fixes", L"NanTrapGuard", nullptr, nullptr,
     {L"Порт навмисно зациклюється назавжди, якщо кут стає «не числом» (так висла катсцена після лікарні). Тепер кут стає 0, гра йде далі, а в ufx.log з'являється рядок, звідки прийшло погане число.",
      L"The port loops forever on purpose when an angle is not a number (that is how the cutscene after the hospital hung). Now the angle becomes 0, the game goes on, and ufx.log names where the bad number came from."}, NOTR},
    {{L"Тримати звукові бібліотеки в пам'яті", L"Keep the sound libraries loaded"}, &g_cfg.pinAudio, kBool, 0, 1, 1, true, L"Fixes", L"PinAudio", nullptr, nullptr,
     {L"Після кожного ролика гра закриває COM на один раз більше, ніж відкрила, і Windows може вивантажити XAudio2 посеред гри (виліт «XAudio2_7.dll unloaded»). З цим XAudio2 і XACT лишаються в пам'яті до кінця гри.",
      L"After every video the game closes COM once more than it opened it, and Windows may unload XAudio2 in the middle of the game (the \"XAudio2_7.dll unloaded\" crash). With this XAudio2 and XACT stay loaded until the game ends."}, NOTR},
    {{L"Точність обчислень як у Direct3D 9", L"Maths precision as with Direct3D 9"}, &g_cfg.pinFpu, kBool, 0, 1, 1, false, L"Fixes", L"PinFpu", nullptr, nullptr,
     {L"Тримає точність обчислень головного потоку такою, яку ставить звичайний Direct3D 9. Від неї залежить, наприклад, межа прицілу (через це в режимі сумісності XP Йорк не міг цілитись угору). Діє одразу.",
      L"Keeps the precision of the main thread's maths at what a plain Direct3D 9 sets. The aim limit depends on it, for example (why York could not aim up in XP compatibility mode). Changes at once."}, NOTR},
    {{L"Швидше підвантаження світу", L"Faster world streaming"}, &g_cfg.fasterLoading, kBool, 0, 1, 1, true, L"Fixes", L"FasterLoading", nullptr, nullptr,
     {L"Потік завантаження гри між запитами чекав 16 мс, і кожне підвантаження могло коштувати цілий такт таймера. Тепер 1 мс: менше ривків, коли світ підвантажується. Потрібен перезапуск.",
      L"The game's loader thread waited 16 ms between requests, so every load could cost a whole timer tick. Now 1 ms: fewer hitches while the world streams in. Restart needed."}, NOTR},
    {{L"Автоприціл у груди", L"Lock-on aims at the chest"}, &g_cfg.lockOnAim, kBool, 0, 1, 1, true, L"Fixes", L"LockOnAim", nullptr, nullptr,
     {L"Автоприціл гри цілиться в пояс ворога й опускає точку на десяту частину відстані: за кілька метрів це вже стегна чи коліна. З цим точка не опускається і стоїть трохи вище пояса (висота: LockOnHeight в UltimateFix.ini). Потрібен перезапуск.",
      L"The game's lock-on aims at the enemy's waist and lowers the point by a tenth of the distance: a few metres away that is the hips or the knees. With this the point does not drop and sits a little above the waist (height: LockOnHeight in UltimateFix.ini). Restart needed."}, NOTR},
    {{L"Вода річки, як на Xbox 360", L"River water as on the Xbox 360"}, &g_cfg.riverWater, kInt, 0, 2, 1, false, L"Fixes", L"RiverWater", nullptr, kRiverTr,
     {L"На ПК річка пласка й синя: гра має для неї запис «_river» (відбиття, освітлені хвилі, відблиски), але не застосовує його. Мод дає річці значення з цього запису і збільшує карту хвиль у 16 разів, бо з масштабом гри хвилі дрібніші за піксель. Масштаб підібрано на око за знімками Xbox 360. «Лише в сцені біля річки»: вода виправлена тільки в сцені «Ласкаво просимо до Грінвейла» (приїзд Йорка), а всюди далі лишається як у порті. Змінюється одразу.",
      L"On PC the river is flat and blue: the game has a \"_river\" record for it (reflection, lit waves, sparkle) but never applies it. The mod gives the river the values of that record and makes the wave map 16 times larger, because at the game's scale the waves are smaller than a pixel. The scale was picked by eye against Xbox 360 pictures. \"Only in the river scene\": the water is fixed only in the scene \"Welcome to Greenvale\" (York's arrival) and stays as the port draws it everywhere else. Changes at once."}, NOTR},
    {{L"QTE на високому FPS", L"QTE at high FPS"}, &g_cfg.qteFix, kBool, 0, 1, 1, true, L"Fixes", L"QteFix", nullptr, nullptr,
     {L"У QTE «розхитування» (A↔D або стік) гра множила рух на час кадру, тож вище 60 FPS кожне перемикання рахувалось менше. З цим перемикання рахується як на 60 FPS. Потрібен перезапуск.",
      L"In the waggle QTE (A/D or the stick) the game multiplied the travel by the frame time, so above 60 FPS every swap counted less. With this a swap counts as at 60 FPS. Restart needed."}, NOTR},
    {{L"4 ГБ пам'яті автоматично", L"4 GB of memory automatically"}, &g_cfg.autoLargeAddress, kBool, 0, 1, 1, true, L"Fixes", L"AutoLargeAddress", nullptr, nullptr,
     {L"Гра 32-бітна і бачить лише 2 ГБ: з великим масштабом рендеру їх бракує. Мод сам ставить DP.exe прапор 4 ГБ, діє з наступного запуску. Оригінал лишається як DP.exe.bak; після перевірки файлів у Steam мод зробить це знову.",
      L"The game is 32-bit and sees only 2 GB: a big render scale runs out of it. The mod gives DP.exe the 4 GB flag itself, from the next start. The original stays as DP.exe.bak; after Steam verifies the files the mod does it again."}, NOTR},
    {{L"Потоки гри по черзі (світ)", L"Game threads in turn (world)"}, &g_cfg.serialSetThread, kBool, 0, 1, 1, true, L"Fixes", L"SerialSetThread", nullptr, nullptr,
     {L"Фоновий потік гри будує об'єкти світу, поки головний вже оновлює наступний кадр, без жодного замка. Тепер головний чекає, поки той закінчить (до 1 мс на кадр, лише коли є що будувати). Те, що давав режим сумісності Windows 98, без втрати швидкості. Потрібен перезапуск.",
      L"A worker thread of the game builds world objects while the main thread already updates the next frame, with no lock. Now the main thread waits for it to finish (up to 1 ms a frame, only when there is something to build). What the Windows 98 compatibility mode gave, without the slowdown. Restart needed."}, NOTR},
    {{L"Маршрути жителів при зміні локації", L"Resident routes on a level change"}, &g_cfg.routeFlushWait, kBool, 0, 1, 1, true, L"Fixes", L"RouteFlushWait", nullptr, nullptr,
     {L"Коли ви заходите в приміщення, гра прибирає жителів, а потік маршрутів ще може дописувати шлях у одного з них: псування пам'яті або житель, що назавжди «чекає маршрут». Тепер зміна локації чекає, поки він закінчить (зазвичай частки мілісекунди, щонайбільше 250 мс). Потрібен перезапуск.",
      L"When you enter a building the game clears the residents while the route thread may still write a path into one of them: memory damage, or a resident who waits for a route forever. Now the level change waits for it to finish (usually a fraction of a millisecond, at most 250 ms). Restart needed."}, NOTR},
    {{L"День і ніч у будівлях", L"Day and night in buildings"}, &g_cfg.houseListFix, kBool, 0, 1, 1, true, L"Fixes", L"HouseListFix", nullptr, nullptr,
     {L"Виправляє байтовий порядок HOUSE_LIST.NOD: у будівлях знову працює світло дня й ночі.", L"Fixes the byte order of HOUSE_LIST.NOD: day and night lighting works in buildings again."}, NOTR},
    {{L"Прибрати зайве опитування ґеймпада", L"Remove the stray joystick poll"}, &g_cfg.removeExtraJoyPoll, kBool, 0, 1, 1, true, L"Fixes", L"RemoveExtraJoyPoll", nullptr, nullptr,
     {L"Гра опитувала ґеймпад зі сміттям у пам'яті; це заморожувало стіки.", L"The game polled the joystick with garbage in memory; this froze the sticks."}, NOTR},
    {{L"Alt+Tab без зависання", L"Alt+Tab without freezing"}, &g_cfg.resetRefreshFix, kBool, 0, 1, 1, true, L"Fixes", L"ResetRefreshFix", nullptr, nullptr,
     {L"Частота оновлення не копіюється у параметри Reset: гра не вішається після Alt+Tab.", L"The refresh rate is not copied into the Reset parameters: the game does not hang after Alt+Tab."}, NOTR},
    {{L"Захист читання файлів гри", L"Game file read guard"}, &g_cfg.resourceReadGuard, kBool, 0, 1, 1, true, L"Fixes", L"ResourceReadGuard", nullptr, nullptr,
     {L"Запакований файл гри, який не розпаковується (пошкоджений або диск його не читає), гра читала знову й знову без кінця: мовчазне зависання на екрані завантаження. З цим пунктом спроб щонайбільше 5, далі файл вважається відсутнім, а його назва потрапляє в журнал; гра може піти далі без нього або зупинитися зі звітом про збій. Розпаковування також не виходить за межі відведеної пам'яті. Цілі файли читаються так само, як раніше. У грі ще не перевірено; у SafeMode вимкнено.",
      L"A packed game file that does not unpack (damaged, or the disk cannot read it) was read again and again without end: a silent hang on a loading screen. With this on there are 5 attempts at most, then the file counts as missing and its name goes to the log; the game may go on without it or stop with a crash report. Unpacking also cannot write past its buffer. Files that are fine are read exactly as before. Not yet checked in the game; off in SafeMode."}, NOTR},
    {{L"Пропускати заставки", L"Skip the intro"}, &g_cfg.skipIntro, kBool, 0, 1, 1, true, L"Fixes", L"SkipIntro", nullptr, nullptr,
     {L"Одразу до титульного екрана.", L"Straight to the title screen."}, NOTR},
    {{L"Текст із теки (накладка перекладу)", L"Text from a folder (translation overlay)"}, &g_cfg.textOverlay, kBool, 0, 1, 1, true, L"Language", L"TextOverlay", nullptr, nullptr,
     {L"Покладіть у теку UltimateFix\\text файл mes_all.json (переклад, як його зберігає SWERY Localization Tool) і, якщо ви її змінювали, мапу glyphmap.json. Повідомлення, текст яких відрізняється від завантаженого грою, замінюються в пам'яті; файли гри не змінюються. Окремі рядки можна виправити файлами *.txt із рядками «0x1234 = текст». Що відхилено й чому, записано в UltimateFix\\loc.log. Якщо в теці немає файлів, нічого не змінюється. У грі ще не перевірено.",
      L"Put mes_all.json (the translation as the SWERY Localization Tool saves it) and, if you changed it, the glyph map glyphmap.json into the folder UltimateFix\\text. The messages whose text differs from what the game loaded are replaced in memory; the game's files stay as they are. Single lines can be corrected with *.txt files that hold lines '0x1234 = text'. What was refused and why is in UltimateFix\\loc.log. With no files in the folder nothing changes. Not tested in the game yet."},
     {L"Інструменти перекладача", L"Translator tools"}},
    {{L"Показувати номери повідомлень", L"Show message numbers"}, &g_cfg.showMessageId, kBool, 0, 1, 1, true, L"Language", L"ShowMessageId", nullptr, nullptr,
     {L"Над кожним текстом гри дрібно пишеться його номер (наприклад 0x1A2B). Саме цей номер пишуть у файлах накладки. Увімкнути можна лише з перезапуском, вимкнути одразу. У грі ще не перевірено.",
      L"The number of every game text is printed over it in small type (0x1A2B, for example). It is the number the overlay files use. Switching it on needs a restart, switching it off works at once. Not tested in the game yet."}, NOTR},
    {{L"Журнал відсутніх літер", L"Missing glyph log"}, &g_cfg.glyphLog, kBool, 0, 1, 1, true, L"Language", L"GlyphLog", nullptr, nullptr,
     {L"У файл UltimateFix\\loc.log записується номер повідомлення й клітинка шрифту, якщо ця клітинка порожня в атласі шрифту або лежить за його межами, а також повідомлення, яке не вмістилося в один прохід малювання (768 прямокутників). Кожна знахідка один раз. У грі ще не перевірено.",
      L"UltimateFix\\loc.log gets the message number and the font cell whenever the cell is empty in the font atlas or lies beyond it, and every message that does not fit into one draw (768 quads). Each finding once. Not tested in the game yet."}, NOTR},
    {{L"Зберегти весь текст гри у файл", L"Save all game text to a file"}, &g_cfg.textDump, kBool, 0, 1, 1, true, L"Language", L"TextDump", nullptr, nullptr,
     {L"Під час запуску весь MES_ALL.MES, який завантажила гра, записується в UltimateFix\\mes_dump.json у тому самому вигляді, що й mes_all.json інструмента перекладу. Його можна порівняти з файлом, який дає DPMsgTool, і так перевірити, що мод читає текст правильно. Файл пишеться за кожного запуску, тож потім вимкніть.",
      L"At start the whole MES_ALL.MES the game loaded is written to UltimateFix\\mes_dump.json in the same form as the translation tool's mes_all.json. Compare it with the file DPMsgTool gives to check that the mod reads the text right. It is written at every start, so switch it off afterwards."}, NOTR},
    RESET_ITEM
};
Item g_pageSave[] = {
    {{L"Захист файлу збереження", L"Save file protection"}, &g_cfg.saveGuard, kBool, 0, 1, 1, true, L"Save", L"Guard", nullptr, nullptr,
     {L"Запис через тимчасовий файл і атомарна заміна: збій посередині не пошкодить dp.sav.", L"Written through a temporary file and replaced atomically: a failure halfway cannot damage dp.sav."}, {L"Збереження", L"Saves"}},
    {{L"Скільки резервних копій тримати", L"Backups to keep"}, &g_cfg.saveBackups, kInt, 1, 200, 5, false, L"Save", L"Backups", nullptr, nullptr,
     {L"Копії в UltimateFix\\savebackups, кожна підписана датою й місцем у сюжеті (епізод, розділ, час гри). Типово 60.",
      L"Copies in UltimateFix\\savebackups, each named by its date and where the story stood (episode, chapter, play time). Default 60."}, NOTR},
    {{L"Стискати резервні копії", L"Compress the backups"}, &g_cfg.compressBackups, kBool, 0, 1, 1, false, L"Save", L"CompressBackups", nullptr, nullptr,
     {L"Тека savebackups стискається засобами Windows (NTFS): 60 копій по 8 МБ займають у рази менше місця. Читаються як звичайні файли. Нові копії стискаються одразу, ті, що вже є, під час наступного запуску.",
      L"The savebackups folder is compressed by Windows (NTFS): 60 copies of 8 MB take a fraction of the space. They read as plain files. New copies are compressed at once, the ones already there at the next start."}, NOTR},
    {{L"Відновлювати пошкоджений файл", L"Restore a damaged file"}, &g_cfg.saveRestoreDamaged, kBool, 0, 1, 1, false, L"Save", L"RestoreDamaged", nullptr, nullptr,
     {L"Якщо dp.sav має неправильний розмір, береться найновіша копія.", L"If dp.sav has the wrong size, the newest backup is used."}, NOTR},
    {{L"Показувати повідомлення про збереження", L"Show save messages"}, &g_cfg.saveWarnings, kBool, 0, 1, 1, false, L"Save", L"Warnings", nullptr, nullptr,
     {L"Вікно, коли збереження не вдалось або відновлене з копії.", L"A message when a save failed or was restored from a backup."}, NOTR},
    RESET_ITEM
};

// ---- Photo mode page: the free camera, frozen time, filters, a screenshot button.
constexpr int kBuiltinFilters = 14, kOwnPresets = 4, kFilterCount = kBuiltinFilters + kOwnPresets;
const Tr kFilterTr[kFilterCount] = {{L"Без", L"None"}, {L"Тепла", L"Warm"}, {L"Холодна", L"Cold"}, {L"Чорно-біла", L"B&W"},
                                    {L"Кіно", L"Cinema"}, {L"Вінтаж", L"Vintage"}, {L"Сепія", L"Sepia"}, {L"Нуар", L"Noir"},
                                    {L"Плівка", L"Film"}, {L"Знебарвлена", L"Bleach bypass"}, {L"Сон", L"Dream"},
                                    {L"Відеокасета", L"VHS"}, {L"Ніч", L"Night"}, {L"Туман", L"Fog"},
                                    {L"Мій 1", L"Mine 1"}, {L"Мій 2", L"Mine 2"}, {L"Мій 3", L"Mine 3"}, {L"Мій 4", L"Mine 4"}};
// The sliders of the look, in the order of the presets below and of the [PhotoPresets] lines.
constexpr int kLookCount = 11;
int* const kLook[kLookCount] = {&g_cfg.photoBright, &g_cfg.photoContrast, &g_cfg.photoSat, &g_cfg.photoVignette, &g_cfg.photoTemp,
                                &g_cfg.photoTint, &g_cfg.photoSepia, &g_cfg.photoFade, &g_cfg.photoGrain, &g_cfg.photoAberration,
                                &g_cfg.photoBars};
const int kLookLo[kLookCount] = {-100, -100, -100, 0, -100, -100, 0, 0, 0, 0, 0};
//                                        bright contr  sat  vign  temp  tint sepia fade grain  ca  bars
const signed char kFilterLook[kBuiltinFilters][kLookCount] = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},           // none
                                                              {0, 0, 10, 0, 70, 0, 0, 0, 0, 0, 0},         // warm
                                                              {0, 0, 5, 0, -75, 20, 0, 0, 0, 0, 0},        // cold
                                                              {0, 10, -100, 20, 0, 0, 0, 0, 0, 0, 0},      // black and white
                                                              {-5, 20, -20, 45, 35, 0, 0, 0, 10, 0, 60},   // cinema
                                                              {5, 10, -40, 50, 80, 0, 25, 25, 25, 0, 0},   // vintage
                                                              {0, 5, -100, 35, 0, 0, 100, 10, 15, 0, 0},   // sepia
                                                              {-10, 45, -100, 60, 0, 0, 0, 0, 35, 0, 30},  // noir
                                                              {0, 5, -10, 30, 30, -5, 0, 30, 40, 10, 0},   // film
                                                              {0, 35, -55, 30, -10, 0, 0, 0, 10, 0, 0},    // bleach bypass
                                                              {10, -25, -10, 40, 10, 35, 0, 35, 0, 0, 0},  // dream
                                                              {0, -10, 20, 25, 10, -10, 0, 15, 45, 60, 0}, // VHS
                                                              {-15, 15, -30, 55, -60, -10, 0, 0, 15, 0, 0}, // night
                                                              {10, -35, -35, 20, -15, 0, 0, 45, 5, 0, 0}};  // fog
int g_savePick = 0;
const int kSaveValues[kOwnPresets] = {1, 2, 3, 4};
const Tr kSaveNames[kOwnPresets] = {{L"Мій 1", L"Mine 1"}, {L"Мій 2", L"Mine 2"}, {L"Мій 3", L"Mine 3"}, {L"Мій 4", L"Mine 4"}};

int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// [PhotoPresets] MineN = 11 numbers separated by commas; false when the line is not there (an empty slot).
bool ReadOwnPreset(int slot, int* out) {
    wchar_t key[16], text[200];
    wsprintfW(key, L"Mine%d", slot);
    if (GetPrivateProfileStringW(L"PhotoPresets", key, L"", text, 200, g_paths.iniFile) == 0) return false;
    const wchar_t* p = text;
    for (int i = 0; i < kLookCount; ++i) {
        while (*p == L' ' || *p == L',') ++p;
        bool neg = *p == L'-';
        if (neg) ++p;
        if (*p < L'0' || *p > L'9') return false;
        int v = 0;
        while (*p >= L'0' && *p <= L'9') v = v * 10 + (*p++ - L'0');
        out[i] = Clamp(neg ? -v : v, kLookLo[i], 100);
    }
    return true;
}

// The look of the picture outside photo mode ([Graphics] GameLook): one of the filters or own presets, values in kLook order.
// An own preset is read from the ini when the choice changes (or after a preset is saved), not every frame.
int g_gameLookCached = -1;
int g_gameLookValues[kLookCount];
bool g_gameLookValid = false;

bool GameLookValuesImpl(int* out) {
    const int f = g_cfg.gameLook;
    if (f <= 0 || f >= kFilterCount) return false;
    if (f != g_gameLookCached) {
        g_gameLookCached = f;
        if (f < kBuiltinFilters) {
            for (int i = 0; i < kLookCount; ++i) g_gameLookValues[i] = kFilterLook[f][i];
            g_gameLookValid = true;
        } else {
            g_gameLookValid = ReadOwnPreset(f - kBuiltinFilters + 1, g_gameLookValues);
        }
    }
    if (!g_gameLookValid) return false;
    for (int i = 0; i < kLookCount; ++i) out[i] = g_gameLookValues[i];
    return true;
}

void ResetLook() {
    g_cfg.photoFilter = 0;
    for (int i = 0; i < kLookCount; ++i) *kLook[i] = 0;
}

int g_shotPick = 0;
struct Batch {
    bool active;
    int next, wait, savedFilter;
    int saved[11];
} g_batch = {};
const int kShotValues[2] = {1, 2};
const Tr kShotNames[2] = {{L"Зробити знімок", L"Take a picture"}, {L"З кожним фільтром", L"With every filter"}};


void ApplyFov() { g_cfg.photoFovOn = 1; }  // touching the slider means: use my value

void ApplyFilter() {
    // a filter is a set of the sliders below; the own presets come from [PhotoPresets] in the ini (an empty one changes nothing)
    const int f = Clamp(g_cfg.photoFilter, 0, kFilterCount - 1);
    if (f < kBuiltinFilters) {
        for (int i = 0; i < kLookCount; ++i) *kLook[i] = kFilterLook[f][i];
        return;
    }
    int v[kLookCount];
    if (!ReadOwnPreset(f - kBuiltinFilters + 1, v)) return;
    for (int i = 0; i < kLookCount; ++i) *kLook[i] = v[i];
}

void SaveOwnPreset() {
    const int slot = g_savePick;
    g_savePick = 0;
    if (slot < 1 || slot > kOwnPresets) return;
    wchar_t key[16], text[200];
    wsprintfW(key, L"Mine%d", slot);
    wsprintfW(text, L"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", *kLook[0], *kLook[1], *kLook[2], *kLook[3], *kLook[4], *kLook[5], *kLook[6],
              *kLook[7], *kLook[8], *kLook[9], *kLook[10]);
    WritePrivateProfileStringW(L"PhotoPresets", key, text, g_paths.iniFile);
    g_gameLookCached = -1;  // the in-game look may use this preset
    g_cfg.photoFilter = kBuiltinFilters + slot - 1;
    Log("photo mode: own preset %d saved (%S)", slot, text);
}

void FormatFilter(int v, wchar_t* out) {
    const int f = Clamp(v, 0, kFilterCount - 1);
    lstrcpyW(out, Tx(kFilterTr[f]));
    int dummy[kLookCount];
    if (f >= kBuiltinFilters && !ReadOwnPreset(f - kBuiltinFilters + 1, dummy))
        lstrcatW(out, g_cfg.language == 1 ? L" (empty)" : L" (порожній)");
}

void FormatFilterForMenu(int v, wchar_t* out) { FormatFilter(v, out); }
static_assert(kFilterCountForMenu == kFilterCount, "the in-game look lists every filter");


void FormatSet(int v, wchar_t* out) { wsprintfW(out, g_cfg.language == 1 ? L"Set %d" : L"Набір %d", v); }

void FormatSuit(int v, wchar_t* out) {
    if (v < 1) lstrcpyW(out, g_cfg.language == 1 ? L"untouched" : L"не змінювати");
    else wsprintfW(out, g_cfg.language == 1 ? L"Suit %d" : L"Костюм %d", v);
}

const Tr kPoseTr[10] = {{L"не змінювати", L"untouched"}, {L"Спокій", L"Calm"}, {L"Дія 1", L"Action 1"}, {L"Дія 2", L"Action 2"}, {L"Дія 3", L"Action 3"},
                        {L"Дія 4", L"Action 4"}, {L"Дія 5", L"Action 5"}, {L"Дія 6", L"Action 6"}, {L"Дія 7", L"Action 7"}, {L"Зброя в руках", L"Weapon in hand"}};
const int kPoseState[10] = {-1, 0, 4, 5, 6, 7, 8, 0xC, 0xE, 999};  // 999 / 1000: a state of the equipped weapon (see PoseTick)  // the game's states that need no weapon, no target and no object
int g_applyPick = 0;
const int kApplyValues[1] = {1};
const Tr kApplyNames[1] = {{L"Застосувати", L"Apply"}};
void ApplyCostumeNow();
void ApplyPoseNow();
// ---- York's face on the photo page: one named pick per part of the face, each with its strength, mixed together ----
// The channels were named from York's pose library (CHARA/11_FC/CPL01.XFE: which face bones each of its 60 poses moves). The
// picks are turned into the 58 channel weights that combat_strafe.cpp adds to the face each frame.
int g_faceSel[4] = {0, 0, 0, 0}, g_faceAmt[4] = {100, 100, 100, 100}, g_faceNose = 0;
const int kFaceCh0[] = {0, 42, 21, 22, 28, 30, 29, 40, 44, 45, 24, 39, 34, 23, 35, 31, 36, 37, 32, 33, 48};
const Tr kFaceTr0[] = {{L"як у грі", L"the game's"}, {L"Легка усмішка", L"Slight smile"}, {L"Усмішка (закрита)", L"Smile, closed"}, {L"Широка усмішка (з зубами)", L"Broad smile"}, {L"Усмішка з примруженням", L"Grin with a squint"}, {L"Сміх", L"Laugh"}, {L"Усмішка лівим кутиком", L"Smirk left"}, {L"Самовпевнена усмішка", L"Cocky smirk right"}, {L"Легка зацікавленість", L"Mild attentive"}, {L"М'який погляд", L"Soft look"}, {L"Сум", L"Sad"}, {L"Ридання", L"Sob"}, {L"Біль / плач", L"Pain / crying"}, {L"Страх / розпач", L"Fear / distress"}, {L"Переляканий оскал", L"Scared grimace"}, {L"Подив (рот «о»)", L"Surprise (mouth in an o)"}, {L"Огида", L"Disgust"}, {L"Гримаса праворуч", L"Wince right"}, {L"Гнівний крик", L"Angry shout"}, {L"Оскал люті", L"Snarl"}, {L"Крик / ревіння", L"Scream / roar"}};
const int kFaceCh1[] = {0, 16, 18, 17, 19, 20, 43};
const Tr kFaceTr1[] = {{L"як у грі", L"the game's"}, {L"Внутрішні кінці брів вгору", L"Inner brows up"}, {L"Сумні брови (будиночком)", L"Sad brows"}, {L"Насуплені брови", L"Frown (both brows)"}, {L"Насуплена права брова", L"Frown right brow"}, {L"Насуплена ліва брова", L"Frown left brow"}, {L"Скептичні брови", L"Skeptical brows"}};
const int kFaceCh2[] = {0, 41, 2, 3, 12, 13, 14};
const Tr kFaceTr2[] = {{L"як у грі", L"the game's"}, {L"Заплющені очі", L"Eyes closed"}, {L"Моргання правим оком", L"Blink right"}, {L"Моргання лівим оком", L"Blink left"}, {L"Широко розплющені очі", L"Eyes wide"}, {L"Нижня повіка права вгору", L"Lower lid right up"}, {L"Нижня повіка ліва вгору", L"Lower lid left up"}};
const int kFaceCh3[] = {0, 4, 5, 6, 7, 8, 9, 25, 26, 27, 38, 10, 11};
const Tr kFaceTr3[] = {{L"як у грі", L"the game's"}, {L"Фонема А (відкритий рот)", L"Viseme A (jaw open)"}, {L"Фонема Е (кутики назад)", L"Viseme E (corners back)"}, {L"Фонема О (округлений рот)", L"Viseme O (round)"}, {L"Фонема У (губи трубочкою)", L"Viseme U (pucker)"}, {L"Фонема І (розтягнуті губи)", L"Viseme I (stretched)"}, {L"Стиснуті губи (М/Б/П)", L"Lips pressed (M/B/P)"}, {L"Надуті губи", L"Pout"}, {L"Розтулені губи, зуби", L"Lips parted, teeth"}, {L"Губи закочені всередину", L"Lips rolled inward"}, {L"Вишкірені зуби", L"Teeth bared"}, {L"Вищир праворуч", L"Sneer right"}, {L"Вищир ліворуч", L"Sneer left"}};
void FaceCompose() {
    const int* const tables[4] = {kFaceCh0, kFaceCh1, kFaceCh2, kFaceCh3};
    bool any = g_faceNose != 0;
    for (int& w : g_cfg.photoFaceW) w = 0;
    for (int g = 0; g < 4; ++g) {
        const int ch = tables[g][g_faceSel[g]];
        if (!ch) continue;
        g_cfg.photoFaceW[ch - 2] += g_faceAmt[g];
        any = true;
    }
    g_cfg.photoFaceW[15 - 2] += g_faceNose;  // channel 15: the nose wrinkle
    g_cfg.photoFaceOn = any;
    PhotoThaw(6);
}
void FaceForget() {  // the photo mode ended: the rows show the game's own face again
    for (int& v : g_faceSel) v = 0;
    g_faceNose = 0;
}
void FormatFace(int v, wchar_t* out) { wsprintfW(out, L"%d %%", v); }
const Tr kBodySetTr[4] = {{L"Йорк", L"York"}, {L"Жителі (день)", L"Residents (day)"}, {L"Катсцени", L"Cutscenes"}, {L"Вбивця в дощовику", L"Raincoat Killer"}};
// A change of the motion or of the face has to be worked out by the game: a few frames of running time, as for a pose
void PhotoAnimChanged() { PhotoThaw(6); }
void FormatBodyNo(int v, wchar_t* out) {
    if (v) wsprintfW(out, L"№ %d", v);
    else lstrcpyW(out, g_cfg.language == 1 ? L"untouched" : L"не змінювати");
}



void ApplyCostumeNow() {
    g_applyPick = 0;
    if (g_cfg.photoCostume < 1) return;
    PhotoThaw(120);  // the loaders need the game running
    // one row of 30: the game keeps suits as a set (1..3) of ten
    g_cfg.photoSuitSet = (g_cfg.photoCostume - 1) / 10 + 1;
    RequestSuit((g_cfg.photoCostume - 1) % 10 + 1);
}

// The suit is worn as it is scrolled to, with no "Wear" button. A suit loads from disk (up to a second), so the one the row
// has stood on for a third of a second is worn, not every one passed on the way. Called every frame (BatchTick's place).
void CostumeTick() {
    static int seenSuit = 0, seenSet = 0, wornSuit = 0, wornSet = 0;
    static DWORD changedAt = 0;
    const int suit = g_cfg.photoCostume, set = g_cfg.photoSuitSet;
    if (suit != seenSuit || set != seenSet) {
        seenSuit = suit;
        seenSet = set;
        changedAt = GetTickCount();
        return;
    }
    if (suit < 1) {  // "untouched" (also after leaving the photo mode): the next choice is worn again
        wornSuit = 0;
        return;
    }
    if ((suit == wornSuit && set == wornSet) || GetTickCount() - changedAt < 330) return;
    wornSuit = suit;
    wornSet = set;
    ApplyCostumeNow();
}

void ApplyPoseNow() {
    g_applyPick = 0;
    int i = g_cfg.photoAnim;
    if (i < 1 || i > 9) return;
    PhotoThaw(60);
    RequestPose(kPoseState[i] + 1);
}

void ApplyShot() {
    const int pick = g_shotPick;
    g_shotPick = 0;
    if (pick == 1) {
        RequestScreenshot();
        return;
    }
    // every filter in turn (own presets that are saved too), one shot each, then the look the player had
    if (pick == 2 && !g_batch.active) {
        g_batch.active = true;
        g_batch.next = 1;
        g_batch.wait = 0;
        g_batch.savedFilter = g_cfg.photoFilter;
        for (int i = 0; i < kLookCount; ++i) g_batch.saved[i] = *kLook[i];
        Log("photo mode: a shot with every filter started");
    }
}

// Runs every frame from OnFrame (after the screenshot pass of this frame).
void BatchTick() {
    CostumeTick();
    if (!g_batch.active) return;
    if (g_batch.wait > 0) {
        if (--g_batch.wait == 0) {
            wchar_t suffix[48];
            wsprintfW(suffix, L"_f%02d", g_batch.next - 1);
            RequestScreenshotNamed(suffix);
        }
        return;
    }
    while (g_batch.next < kFilterCount) {
        const int f = g_batch.next++;
        int v[kLookCount];
        if (f >= kBuiltinFilters && !ReadOwnPreset(f - kBuiltinFilters + 1, v)) continue;  // an empty own preset
        g_cfg.photoFilter = f;
        ApplyFilter();
        g_batch.wait = 3;  // the filter is drawn two frames before the shot is taken
        return;
    }
    g_cfg.photoFilter = g_batch.savedFilter;
    for (int i = 0; i < kLookCount; ++i) *kLook[i] = g_batch.saved[i];
    g_batch.active = false;
    Log("photo mode: a shot with every filter done (UltimateFix\\screenshots, files _f01.._f17)");
}

Item g_pagePhoto[] = {
    {{L"Вільна камера", L"Free camera"}, &g_cfg.photoActive, kBool, 0, 1, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Камера летить за мишею й WASD (Space/C вгору й вниз, Shift швидше, Ctrl повільніше). Закрийте меню (F10 чи Esc), щоб керувати нею; F10 відкриває його знову. Затисніть праву кнопку миші (L3 на ґеймпаді), щоб побачити кадр без меню. F6 вмикає й вимикає весь фоторежим.",
      L"The camera flies with the mouse and WASD (Space/C up and down, Shift faster, Ctrl slower). Close the menu (F10 or Esc) to steer it; F10 opens it again. Hold the right mouse button (L3 on a gamepad) to see the picture without the menu. F6 switches the whole photo mode on and off."},
     {L"Камера", L"Camera"}},
    // The most used action, right under the camera: it was the last of 30 rows, below the costumes and the poses.
    {{L"Знімок", L"Picture"}, &g_shotPick, kInt, 0, 2, 1, false, nullptr, nullptr, ApplyShot, nullptr,
     {L"Зберігає PNG кадру без меню в UltimateFix\\screenshots (клавіша F11 теж; Shift+F11 — без інтерфейсу гри). «З кожним фільтром» знімає ту саму сцену з усіма фільтрами й вашими пресетами, по файлу на кожен; заморозьте час, щоб сцена не змінювалась.",
      L"Saves a PNG of the frame without the menu to UltimateFix\\screenshots (the F11 key too; Shift+F11 without the game's HUD). \"With every filter\" shoots the same scene with all filters and your presets, a file each; freeze time so that the scene stays."},
     NOTR, kShotValues, 2, kShotNames, nullptr, false},
    {{L"Заморозити час гри", L"Freeze game time"}, &g_cfg.photoFreeze, kBool, 0, 1, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Усе в грі завмирає: персонажі, машини, вода. Камера літає далі.", L"Everything in the game stands still: characters, cars, water. The camera keeps flying."}, NOTR},
    {{L"Сховати інтерфейс", L"Hide the interface"}, &g_cfg.photoHud, kBool, 0, 1, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Ховає HUD, радар і підказки. Також клавіша F4.", L"Hides the HUD, radar and prompts. Also the F4 key."}, NOTR},
    {{L"Швидкість камери", L"Camera speed"}, &g_cfg.freeCamSpeed, kInt, 1, 500, 5, false, L"FreeCam", L"Speed", nullptr, nullptr,
     {L"Одиниць світу за секунду. Із Shift утричі швидше.", L"World units per second. Three times faster with Shift."}, NOTR},
    {{L"Гарячі клавіші F5 / F4 / F6", L"Hotkeys F5 / F4 / F6"}, &g_cfg.freeCam, kBool, 0, 1, 1, false, L"FreeCam", L"Enabled", nullptr, nullptr,
     {L"F5 вмикає вільну камеру, F4 ховає інтерфейс (клавіші змінюються в ini). F6 вмикає весь фоторежим і працює завжди.", L"F5 switches the free camera, F4 hides the interface (keys are set in the ini). F6 switches the whole photo mode and always works."}, NOTR},
    {{L"Свій кут огляду", L"Own field of view"}, &g_cfg.photoFovOn, kBool, 0, 1, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Вимкнено: кут огляду як у грі (близько 65°). Увімкніть, щоб задати свій повзунком нижче.", L"Off: the game's own field of view (about 65°). Turn on to set your own with the slider below."}, NOTR},
    {{L"Кут огляду (FOV)", L"Field of view"}, &g_cfg.freeCamFov, kInt, 30, 120, 5, false, nullptr, nullptr, ApplyFov, nullptr,
     {L"Від 30° (вузький, як телеоб'єктив) до 120° (широкий). Працює, коли ввімкнено «Свій кут огляду». Діє, поки ввімкнена вільна камера (F5 або фоторежим): коли її вимкнути, «Свій кут огляду» теж вимикається. Меню значення у файл не пише; [FreeCam] Fov в UltimateFix.ini задає кут від запуску гри до першого вимкнення вільної камери.", L"From 30° (narrow, like a telephoto) to 120° (wide). Works when \"Own field of view\" is on. Lasts while the free camera is on (F5 or the photo mode): switching it off also turns \"Own field of view\" off. The menu does not write the value to the file; [FreeCam] Fov in UltimateFix.ini sets it from the start of the game until the free camera is first switched off."}, NOTR},
    {{L"Нахил камери", L"Camera tilt"}, &g_cfg.photoRoll, kInt, -45, 45, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Нахиляє горизонт на кут у градусах.", L"Tilts the horizon by an angle in degrees."}, NOTR},
    {{L"Фільтр", L"Filter"}, &g_cfg.photoFilter, kInt, 0, kFilterCount - 1, 1, false, nullptr, nullptr, ApplyFilter, nullptr,
     {L"Готовий вигляд кадру (← → гортати). Нижче його можна підправити вручну. «Мій 1..4» це ваші пресети: зберігаються кнопками нижче.",
      L"A ready look for the picture (← → to browse). Fine-tune it by hand below. \"Mine 1..4\" are your own presets, saved with the buttons below."},
     {L"Вигляд кадру", L"Look of the picture"}, nullptr, 0, nullptr, FormatFilter, false},
    {{L"Яскравість", L"Brightness"}, &g_cfg.photoBright, kInt, -100, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Контраст", L"Contrast"}, &g_cfg.photoContrast, kInt, -100, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Насиченість", L"Saturation"}, &g_cfg.photoSat, kInt, -100, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Віньєтка", L"Vignette"}, &g_cfg.photoVignette, kInt, 0, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Температура (холодніше / тепліше)", L"Temperature (colder / warmer)"}, &g_cfg.photoTemp, kInt, -100, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Відтінок (зелений / пурпуровий)", L"Tint (green / magenta)"}, &g_cfg.photoTint, kInt, -100, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Сепія", L"Sepia"}, &g_cfg.photoSepia, kInt, 0, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Вицвітання", L"Fade"}, &g_cfg.photoFade, kInt, 0, 100, 5, false, nullptr, nullptr, nullptr, nullptr,
     {L"Світліші тіні й тьмяніші світла, як на старій плівці.", L"Lighter shadows and dimmer highlights, as on old film."}, NOTR},
    {{L"Зерно плівки", L"Film grain"}, &g_cfg.photoGrain, kInt, 0, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Хроматична аберація", L"Chromatic aberration"}, &g_cfg.photoAberration, kInt, 0, 100, 5, false, nullptr, nullptr, nullptr, nullptr,
     {L"Кольорові облямівки, що ростуть до країв кадру, як у дешевого об'єктива.", L"Colour fringes growing towards the edges, as with a cheap lens."}, NOTR},
    {{L"Кінорамка", L"Letterbox"}, &g_cfg.photoBars, kInt, 0, 100, 5, false, nullptr, nullptr, nullptr, nullptr,
     {L"Чорні смуги згори й знизу, як у широкоекранному кіно.", L"Black bars at the top and the bottom, as in widescreen cinema."}, NOTR},
    {{L"Зберегти як мій пресет", L"Save as my preset"}, &g_savePick, kInt, 0, kOwnPresets, 1, false, nullptr, nullptr, SaveOwnPreset, nullptr,
     {L"Записує всі повзунки вигляду вище в UltimateFix.ini ([PhotoPresets]). Потім пресет вибирається у «Фільтр» як «Мій 1..4».",
      L"Writes every slider of the look above to UltimateFix.ini ([PhotoPresets]). Then pick it in \"Filter\" as \"Mine 1..4\"."},
     {nullptr, nullptr}, kSaveValues, kOwnPresets, kSaveNames, nullptr, false},
    {{L"Різкість, %", L"Sharpening, %"}, &g_cfg.sharpen, kInt, 0, 100, 10, false, L"Graphics", L"Sharpen", nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Згладжування країв, %", L"Edge smoothing, %"}, &g_cfg.antialias, kInt, 0, 100, 10, false, L"Graphics", L"Antialias", nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Затінення в кутках, %", L"Ambient occlusion, %"}, &g_cfg.ambientOcclusion, kInt, 0, 100, 10, false, L"Graphics", L"AmbientOcclusion", nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Костюм", L"Suit"}, &g_cfg.photoCostume, kInt, 0, 30, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Йорк одягає костюм, щойно ви на ньому зупинилися (за третину секунди), окремої кнопки немає. ЕКСПЕРИМЕНТАЛЬНО, зробіть збереження. Костюм вантажиться з диска, на це йде до секунди (час гри на цей час запускається). Усі костюми гри в одному рядку: Йорк має 20 (далі повторюються), Зак 21. Костюми 2, 12 і 22 це слот додаткового пакета DLC і можуть бути порожніми. Не в катсцені й не в машині.",
      L"York wears the suit as soon as you stop on it (a third of a second), there is no separate button. EXPERIMENTAL, save first. The suit loads from disk, which takes up to a second (game time runs for that moment). All the game's suits in one row: York has 20 (they repeat after that), Zach 21. Suits 2, 12 and 22 are the DLC add-on slot and may be empty. Not in a cutscene or a car."},
     NOTR, nullptr, 0, nullptr, FormatSuit},
    {{L"Поза Йорка (експериментально)", L"York's pose (experimental)"}, &g_cfg.photoAnim, kInt, 0, 9, 1, false, nullptr, nullptr, nullptr, kPoseTr,
     {L"Виберіть позу й натисніть «Застосувати». «Спокій» і «Дія 1–7» не потребують зброї (це стани гри 4–8, 12, 14, назви невідомі). «Зброя в руках» бере стан класу зброї, яку Йорк тримає (потрібна зброя в руках). Зробіть збереження. «Спокій» повертає Йорка як було.",
      L"Pick a pose and press \"Apply\". \"Calm\" and \"Action 1-7\" need no weapon (game states 4-8, 12, 14, names unknown). \"Weapon in hand\" uses the state of the class of the weapon York holds (a weapon must be in his hand). Save first. \"Calm\" puts York back."},
     NOTR},
    {{L"Застосувати позу", L"Apply the pose"}, &g_applyPick, kInt, 0, 1, 1, false, nullptr, nullptr, ApplyPoseNow, nullptr, {nullptr, nullptr}, NOTR,
     kApplyValues, 1, kApplyNames, nullptr, false},
    {{L"Персонаж (експериментально)", L"Character (experimental)"}, &g_cfg.photoDollId, kInt, 1, 49, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Житель Грінвейла, якого поставити поруч із Йорком як нерухому «ляльку» для знімка: без поведінки, без розкладу, у збереження не потрапляє. Ім'я поруч із номером гра показує своє внутрішнє (англійською). Зробіть збереження перед спробами. У грі ще не перевірено.",
      L"A Greenvale resident to put next to York as a still \"doll\" for the picture: no behaviour, no schedule, never in a save. The name beside the number is the game's internal one. Save before trying. Not yet verified in the game."},
     NOTR, nullptr, 0, nullptr, FormatDoll},
    {{L"Поставити персонажа", L"Put the character"}, &g_dollPutPick, kInt, 0, 1, 1, false, nullptr, nullptr, DollPut, nullptr,
     {L"Ставить вибраного жителя за два кроки перед Йорком, обличчям до нього. Модель вантажиться з диска, тож на мить час гри запускається. Лише у вільній грі, пішки, не в катсцені. Попередній персонаж зникає: одночасно стоїть один.",
      L"Puts the chosen resident two steps in front of York, facing him. The model loads from disk, so the game's time runs for a moment. Free play on foot only, not in a cutscene. The one before disappears: one stands at a time."},
     NOTR, kDollValues, 1, kDollPutNames, nullptr, false},
    {{L"Прибрати персонажа", L"Remove the character"}, &g_dollGonePick, kInt, 0, 1, 1, false, nullptr, nullptr, DollGone, nullptr,
     {L"Прибирає поставленого персонажа. Він також зникає сам, коли фоторежим закінчується або починається катсцена.",
      L"Removes the character that was put. It also goes by itself when the photo mode ends or a cutscene begins."},
     NOTR, kDollValues, 1, kDollGoneNames, nullptr, false},
    {{L"Персонаж: ліворуч / праворуч", L"Character: left / right"}, &g_cfg.dollMoveX, kInt, -300, 300, 1, false, nullptr, nullptr, DollThaw, nullptr,
     {L"Пересуває персонажа вбік від місця, де його поставлено (відносно того, куди він тоді дивився).", L"Moves the character sideways from where it was put (relative to where it faced then)."},
     NOTR, nullptr, 0, nullptr, FormatMove},
    {{L"Персонаж: назад / вперед", L"Character: back / forward"}, &g_cfg.dollMoveZ, kInt, -300, 300, 1, false, nullptr, nullptr, DollThaw, nullptr,
     {L"Пересуває персонажа вперед або назад.", L"Moves the character forward or back."}, NOTR, nullptr, 0, nullptr, FormatMove},
    {{L"Персонаж: нижче / вище", L"Character: lower / higher"}, &g_cfg.dollMoveY, kInt, -300, 300, 1, false, nullptr, nullptr, DollThaw, nullptr,
     {L"Піднімає або опускає персонажа. Землі під ним гра не шукає: він стоїть на висоті Йорка, поправте тут.", L"Raises or lowers the character. The game does not find the ground for it: it stands at York's height, adjust it here."},
     NOTR, nullptr, 0, nullptr, FormatMove},
    {{L"Персонаж: поворот, градуси", L"Character: turn, degrees"}, &g_cfg.dollTurn, kInt, -180, 180, 5, false, nullptr, nullptr, DollThaw, nullptr,
     {L"Повертає персонажа навколо себе.", L"Turns the character on the spot."}, NOTR},
    {{L"Йорк: ліворуч / праворуч", L"York: left / right"}, &g_cfg.photoMoveX, kInt, -300, 300, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Пересуває Йорка вбік від місця, де він стояв (відносно того, куди він тоді дивився). Одиниця = десята частина одиниці гри; утримуйте ← → для швидшого руху. 0 в усіх чотирьох рядках повертає його на місце. Поки Йорк пересунутий, гра ним не керує, а земля й стіни його не тримають. Не за кермом і не в катсцені. У грі ще не перевірено.",
      L"Moves York sideways from where he stood (relative to where he faced then). One step is a tenth of a game unit; hold the arrows to go faster. 0 in all four rows puts him back. While York is moved the game does not control him, and the ground and walls do not hold him. Not at the wheel and not in a cutscene. Not yet verified in the game."},
     NOTR, nullptr, 0, nullptr, FormatMove},
    {{L"Йорк: назад / вперед", L"York: back / forward"}, &g_cfg.photoMoveZ, kInt, -300, 300, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Пересуває Йорка вперед або назад від місця, де він стояв.", L"Moves York forward or back from where he stood."},
     NOTR, nullptr, 0, nullptr, FormatMove},
    {{L"Йорк: нижче / вище", L"York: lower / higher"}, &g_cfg.photoMoveY, kInt, -300, 300, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Піднімає або опускає Йорка: посадити на паркан, поставити на дах, втопити в землю.", L"Raises or lowers York: sit him on a fence, stand him on a roof, sink him into the ground."},
     NOTR, nullptr, 0, nullptr, FormatMove},
    {{L"Йорк: поворот, градуси", L"York: turn, degrees"}, &g_cfg.photoTurn, kInt, -180, 180, 5, false, nullptr, nullptr, nullptr, nullptr,
     {L"Повертає Йорка навколо себе від напрямку, куди він дивився.", L"Turns York on the spot from the direction he faced."}, NOTR},
    {{L"Набір анімацій тіла (експериментально)", L"Body motion set (experimental)"}, &g_cfg.photoBodySet, kInt, 0, 3, 1, false, nullptr, nullptr, PhotoAnimChanged, kBodySetTr,
     {L"Звідки брати анімацію тіла: Йорк (322), жителі вдень (134), додаткові з катсцен (244), Вбивця в дощовику (56). Лише набори, які гра тримає в пам'яті. Назв анімації не мають, лише номери.",
      L"Where the body motion comes from: York (322), residents by day (134), cutscene extras (244), the Raincoat Killer (56). Only sets the game keeps in memory. The motions have numbers, not names."}, NOTR},
    {{L"Анімація тіла", L"Body motion"}, &g_cfg.photoBodyNo, kInt, 0, 322, 1, false, nullptr, nullptr, PhotoAnimChanged, nullptr,
     {L"0 = не змінювати. Інакше Йорк отримує анімацію з цим номером із вибраного набору; обличчя нижче керується окремо. Не за кермом і не в катсцені. Збережіться перед спробами. У грі ще не перевірено.",
      L"0 = untouched. Otherwise York gets the motion with this number of the chosen set; the face below is controlled on its own. Not at the wheel and not in a cutscene. Save first. Not yet verified in the game."},
     NOTR, nullptr, 0, nullptr, FormatBodyNo},
    {{L"Кадр анімації, %", L"Frame of the motion, %"}, &g_cfg.photoFrame, kInt, 0, 100, 1, false, nullptr, nullptr, PhotoAnimChanged, nullptr,
     {L"Який момент анімації тримати: 0 початок, 100 кінець. Діє, поки «Програвати анімацію» вимкнено.", L"Which moment of the motion to hold: 0 the start, 100 the end. Used while \"Play the motion\" is off."}, NOTR},
    {{L"Програвати анімацію", L"Play the motion"}, &g_cfg.photoPlay, kBool, 0, 1, 1, false, nullptr, nullptr, PhotoAnimChanged, nullptr,
     {L"Увімкнено: анімація йде сама (вимкніть «Зупинити час», інакше вона стоїть). Вимкнено: тримається кадр із повзунка вище.", L"On: the motion runs by itself (turn \"Freeze time\" off or it stands). Off: the frame of the slider above is held."}, NOTR},
    {{L"Міміка для", L"The face of"}, &g_cfg.photoFaceTarget, kInt, 0, 1, 1, false, nullptr, nullptr, PhotoAnimChanged, kFaceTargetTr,
     {L"Кому дістається міміка з рядків нижче: Йоркові чи поставленому персонажеві. Другий тим часом має власне обличчя гри.", L"Who gets the face of the rows below: York or the character that was put. The other one keeps the game's own face meanwhile."}, NOTR},
    {{L"Емоція", L"Emotion"}, &g_faceSel[0], kInt, 0, 20, 1, false, nullptr, nullptr, FaceCompose, kFaceTr0,
     {L"Вираз усього обличчя: усмішка, сум, сміх, страх, огида, крик. Окремо від анімації тіла. Нижче можна додати брови, повіки й рот поверх.", L"The expression of the whole face: smile, sadness, laughter, fear, disgust, shout. Apart from the body motion. Brows, lids and mouth below can be added over it."}, {L"Міміка Йорка (експериментально)", L"York's face (experimental)"}},
    {{L"Емоція: сила", L"Emotion: strength"}, &g_faceAmt[0], kInt, -100, 150, 5, false, nullptr, nullptr, FaceCompose, nullptr,
     {L"100 = повний рух, як у грі; більше перебільшує, від'ємне рухає у зворотний бік.", L"100 = the full movement as the game uses it; more exaggerates, a negative value moves the other way."},
     NOTR, nullptr, 0, nullptr, FormatFace},
    {{L"Брови", L"Brows"}, &g_faceSel[1], kInt, 0, 6, 1, false, nullptr, nullptr, FaceCompose, kFaceTr1,
     {L"Брови поверх емоції: підняті, сумні, насуплені, скептичні.", L"Brows over the emotion: raised, sad, frowning, skeptical."}, NOTR},
    {{L"Брови: сила", L"Brows: strength"}, &g_faceAmt[1], kInt, -100, 150, 5, false, nullptr, nullptr, FaceCompose, nullptr,
     {L"100 = повний рух, як у грі; більше перебільшує, від'ємне рухає у зворотний бік.", L"100 = the full movement as the game uses it; more exaggerates, a negative value moves the other way."},
     NOTR, nullptr, 0, nullptr, FormatFace},
    {{L"Повіки", L"Lids"}, &g_faceSel[2], kInt, 0, 6, 1, false, nullptr, nullptr, FaceCompose, kFaceTr2,
     {L"Повіки: заплющені очі, моргання одним оком (підморгування), широко розплющені, примруження.", L"Lids: eyes closed, one eye shut (a wink), eyes wide, a squint."}, NOTR},
    {{L"Повіки: сила", L"Lids: strength"}, &g_faceAmt[2], kInt, -100, 150, 5, false, nullptr, nullptr, FaceCompose, nullptr,
     {L"100 = повний рух, як у грі; більше перебільшує, від'ємне рухає у зворотний бік.", L"100 = the full movement as the game uses it; more exaggerates, a negative value moves the other way."},
     NOTR, nullptr, 0, nullptr, FormatFace},
    {{L"Рот", L"Mouth"}, &g_faceSel[3], kInt, 0, 12, 1, false, nullptr, nullptr, FaceCompose, kFaceTr3,
     {L"Форма рота: фонеми А Е О У І, стиснуті чи надуті губи, вишкірені зуби, вищир.", L"The shape of the mouth: visemes A E O U I, pressed or pouting lips, bared teeth, a sneer."}, NOTR},
    {{L"Рот: сила", L"Mouth: strength"}, &g_faceAmt[3], kInt, -100, 150, 5, false, nullptr, nullptr, FaceCompose, nullptr,
     {L"100 = повний рух, як у грі; більше перебільшує, від'ємне рухає у зворотний бік.", L"100 = the full movement as the game uses it; more exaggerates, a negative value moves the other way."},
     NOTR, nullptr, 0, nullptr, FormatFace},
    {{L"Зморщений ніс", L"Nose wrinkle"}, &g_faceNose, kInt, 0, 150, 5, false, nullptr, nullptr, FaceCompose, nullptr,
     {L"Додає зморщений ніс до будь-якого виразу.", L"Adds a wrinkled nose to any expression."}, NOTR, nullptr, 0, nullptr, FormatFace},
    {{L"Поверх міміки гри", L"Over the game's face"}, &g_cfg.photoFaceAdd, kBool, 0, 1, 1, false, nullptr, nullptr, PhotoAnimChanged, nullptr,
     {L"Вимкнено: обличчя лише з рядків вище. Увімкнено: вони додаються до того, що гра робить сама (розмова, біль).", L"Off: the face comes from the rows above alone. On: they are added to what the game does itself (talking, pain)."}, NOTR},
    {{L"Без моргання", L"No blinking"}, &g_cfg.photoNoBlink, kBool, 0, 1, 1, false, nullptr, nullptr, PhotoAnimChanged, nullptr,
     {L"Йорк не моргає: для знімка з відкритими очима.", L"York does not blink: for a picture with open eyes."}, NOTR},
};

// ---- Cheats and world control. Each button is a one-shot action: the pick variable is put back to 0 at once.
int g_cheatPick = 0;
const int kMoneyValues[3] = {1, 2, 3};
const Tr kMoneyNames[3] = {{L"+1000", L"+1000"}, {L"+10000", L"+10000"}, {L"Максимум", L"Maximum"}};
const int kTimeValues[5] = {1, 2, 3, 4, 5};
const Tr kTimeNames[5] = {{L"Північ", L"Midnight"}, {L"Ранок 7:00", L"Morning 7:00"}, {L"День 12:00", L"Noon 12:00"}, {L"Вечір 18:00", L"Evening 18:00"}, {L"Ніч 23:00", L"Night 23:00"}};
const int kWeatherValues[4] = {1, 2, 3, 4};
const Tr kWeatherNames[4] = {{L"Ясно", L"Clear"}, {L"Хмарно", L"Cloudy"}, {L"Дощ", L"Rain"}, {L"Як у грі", L"The game's"}};
const int kOneValue[1] = {1};
const Tr kAmmoNames[1] = {{L"Поповнити", L"Refill"}};
const Tr kCameraNames[1] = {{L"Перемістити", L"Move"}};

void FormatSpeed(int v, wchar_t* out) { wsprintfW(out, L"×%d.%d", v / 10, v % 10); }

void DoMoney() { int p = g_cheatPick; g_cheatPick = 0; CheatMoney(p == 1 ? 1000 : p == 2 ? 10000 : -1); }
void DoTime() {
    static const int hours[6] = {0, 0, 7, 12, 18, 23};
    int p = g_cheatPick;
    g_cheatPick = 0;
    if (p >= 1 && p <= 5) CheatSetHour(hours[p]);
}
void DoWeather() { int p = g_cheatPick; g_cheatPick = 0; CheatWeather(p); }
void DoAmmo() { g_cheatPick = 0; CheatAmmo(); }
// the spawner
int g_carPick = 0, g_itemPick = 1, g_itemCount = 1, g_carDo = 0, g_itemDo = 0;
const int kCarNumbers[16] = {7, 8, 90, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89};
const Tr kCarTr[16] = {{L"Патрульна Йорка", L"York's squad car"}, {L"Патрульна, другий тип", L"Squad car, second type"}, {L"Патрульна (невразлива)", L"Patrol car (invulnerable)"},
    {L"Carmen GT", L"Carmen GT"}, {L"Machina", L"Machina"}, {L"Elephant T-3", L"Elephant T-3"}, {L"Tough chap", L"Tough chap"}, {L"AppleStar", L"AppleStar"},
    {L"Mezger C600", L"Mezger C600"}, {L"Rockabilly 58'", L"Rockabilly 58'"}, {L"Woodland", L"Woodland"}, {L"Jogcargo", L"Jogcargo"}, {L"Victoria", L"Victoria"},
    {L"SR sportwagon", L"SR sportwagon"}, {L"Wave R2000", L"Wave R2000"}, {L"S33 Condor", L"S33 Condor"}};
const Tr kCarDoNames[1] = {{L"Поставити", L"Put"}};
const Tr kItemDoNames[1] = {{L"Дати", L"Give"}};
void DoCar() { g_carDo = 0; if (g_carPick >= 0 && g_carPick < 16) CheatSpawnCar(kCarNumbers[g_carPick]); }
void DoItem() { g_itemDo = 0; CheatGiveItem(g_itemPick, g_itemCount); }
void FormatItem(int v, wchar_t* out) {
    const char* n = v >= 0 && v < kItemNameCount ? kItemNames[v] : "";
    if (*n) wsprintfW(out, L"%d %S", v, n);
    else wsprintfW(out, L"%d (%s)", v, g_cfg.language == 1 ? L"empty" : L"порожньо");
}
void DoCamera() { g_cheatPick = 0; CheatYorkToCamera(); }

Item g_pageCheats[] = {
    {{L"Гроші", L"Money"}, &g_cheatPick, kInt, 0, 3, 1, false, nullptr, nullptr, DoMoney, nullptr,
     {L"Додає гроші (до 99999). Змінюється одразу. Деякі розділи можуть змінювати їх самі.", L"Adds money (up to 99999). Applies at once. Some chapters may change it themselves."},
     {L"Гроші й здоров'я", L"Money and health"}, kMoneyValues, 3, kMoneyNames, nullptr, false},
    {{L"Невразливість (повне здоров'я)", L"Invulnerability (full health)"}, &g_cfg.cheatGod, kBool, 0, 1, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Здоров'я щокадру повертається до максимуму. Анімація смерті від падіння чи вибуху може все одно відіграти.", L"Health is put back to the maximum every frame. A death animation from a fall or an explosion may still play."}, NOTR},
    {{L"Година доби", L"Time of day"}, &g_cheatPick, kInt, 0, 5, 1, false, nullptr, nullptr, DoTime, nullptr,
     {L"Переходить до години. Якщо вибрана година раніша за поточну, настає наступний день. Скидає список подій дня, тож не робіть цього під час квестів. Краще на вулиці й не в катсцені.", L"Jumps to the hour. An hour earlier than the current one means the next day. Resets the day's event list, so not during quests. Best outdoors and outside a cutscene."},
     {L"Час і погода", L"Time and weather"}, kTimeValues, 5, kTimeNames, nullptr, false},
    {{L"Зупинити годинник", L"Stop the clock"}, &g_cfg.cheatClockStop, kBool, 0, 1, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Час доби не рухається.", L"The time of day does not move."}, NOTR},
    {{L"Швидкість годинника", L"Clock speed"}, &g_cfg.cheatClockSpeed, kInt, 1, 100, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"10 = як у грі (×1.0). Менше повільніше, більше швидше.", L"10 = as the game (×1.0). Less is slower, more is faster."}, NOTR, nullptr, 0, nullptr, FormatSpeed},
    {{L"Погода", L"Weather"}, &g_cheatPick, kInt, 0, 4, 1, false, nullptr, nullptr, DoWeather, nullptr,
     {L"Ставить погоду й тримає її, поки не оберете «Як у грі». Сніг у грі не підтримується.", L"Sets the weather and keeps it until you pick \"The game's\". Snow does not exist in the game."},
     NOTR, kWeatherValues, 4, kWeatherNames, nullptr, false},
    {{L"Набої зброї в руках", L"Ammunition of the weapon in hand"}, &g_cheatPick, kInt, 0, 1, 1, false, nullptr, nullptr, DoAmmo, nullptr,
     {L"Магазин зброї, яку Йорк тримає, до максимуму.", L"Fills the magazine of the weapon York holds."},
     {L"Предмети й переміщення", L"Items and moving"}, kOneValue, 1, kAmmoNames, nullptr, false},
    {{L"Грати за (експериментально)", L"Play as (experimental)"}, &g_cfg.photoPlayAs, kInt, 0, 4, 1, false, nullptr, nullptr, PlayAsChanged, kPlayAsTr,
     {L"Ви граєте за іншого персонажа, за якого гра сама дає грати: зі своєю моделлю й анімаціями, можна бігати й робити все як завжди. Це власний перемикач гри. Діє у вільній грі, пішки; модель вантажиться з диска секунду-дві. Персонаж лишається, доки ви не виберете іншого або сюжет сам не змінить його. ГРА ЗАПИСУЄ У ЗБЕРЕЖЕННЯ, КИМ ВИ ГРАЄТЕ: перед збереженням поверніть свого персонажа, якщо не хочете лишитися іншим.",
      L"You play another character the game itself lets you play, with its own model and motions: run and do everything as usual. It is the game's own switch. Works in free play on foot; the model takes a second or two to load. The character stays until you pick another or the story changes it. THE GAME WRITES WHO YOU PLAY INTO THE SAVE: put your own character back before saving unless you want to stay someone else."},
     {L"Персонаж", L"Character"}},
    {{L"Машина", L"Car"}, &g_carPick, kInt, 0, 15, 1, false, nullptr, nullptr, nullptr, kCarTr,
     {L"Яку машину поставити. Це власні машини гри: патрульні та ті, що Йорк отримує за завдання.", L"Which car to put. They are the game's own cars: the squad cars and the ones York gets for missions."},
     {L"Спавнер (експериментально)", L"Spawner (experimental)"}},
    {{L"Поставити машину перед Йорком", L"Put the car in front of York"}, &g_carDo, kInt, 0, 1, 1, false, nullptr, nullptr, DoCar, nullptr,
     {L"Гра показує вибрану машину і мод ставить її за кілька кроків перед Йорком, боком до нього. Лише у вільній грі надворі у Грінвейлі. Кожна машина у грі одна: якщо вона вже десь стоїть, вона переїде сюди. Збережіться перед спробою. У грі ще не перевірено.",
      L"The game shows the chosen car and the mod puts it a few steps in front of York, side on. Free play outdoors in Greenvale only. The game has one of each car: if it stands somewhere already, it moves here. Save before trying. Not yet verified in the game."},
     NOTR, kOneValue, 1, kCarDoNames, nullptr, false},
    {{L"Предмет", L"Item"}, &g_itemPick, kInt, 1, 365, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Який предмет дати: 1–66 зброя, 67–73 набої, далі їжа, аптечки, ключі, картки та інше. Назви англійською, з даних гри.", L"Which item to give: 1-66 weapons, 67-73 ammunition, then food, first aid, keys, cards and the rest. The names come from the game's data."},
     NOTR, nullptr, 0, nullptr, FormatItem},
    {{L"Кількість", L"Count"}, &g_itemCount, kInt, 1, 20, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Скільки разів дати; для набоїв і подібного гра множить на розмір пачки.", L"How many to give; for ammunition and the like the game multiplies by the pack size."}, NOTR},
    {{L"Дати предмет", L"Give the item"}, &g_itemDo, kInt, 0, 1, 1, false, nullptr, nullptr, DoItem, nullptr,
     {L"Власний виклик гри «видати предмет», без вікна «отримано». Якщо в інвентарі немає місця, предмет іде у сховище. Сюжетні предмети (ключі, докази) можуть зламати проходження: давайте їх обережно. У грі ще не перевірено.",
      L"The game's own give-item call, without the \"obtained\" window. With no room in the inventory the item goes to the storage. Story items (keys, evidence) can break the story: give them with care. Not yet verified in the game."},
     NOTR, kOneValue, 1, kItemDoNames, nullptr, false},
    {{L"Політ Йорка (F3)", L"Fly York (F3)"}, &g_cfg.cheatFly, kBool, 0, 1, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Йорк летить, куди ведете: WASD відносно камери, мишка або Q/E повертають, Space вгору, Ctrl (або C) вниз, Shift учетверо швидше. Поки летить, гра не бачить клавіш і пада, не рухає його й не дає впасти. Вимкніть біля землі, бо з висоти він упаде (згодиться невразливість). Не в машині й не в катсцені. Клавіша й швидкість: [Cheats] FlyKey, FlySpeed в UltimateFix.ini.",
      L"York flies where you steer: WASD relative to the camera, the mouse or Q/E turn, Space up, Ctrl (or C) down, Shift four times faster. While he flies the game sees no keys or pad, neither moves him nor lets him fall. Turn it off near the ground, from a height he falls (invulnerability helps). Not in a car or a cutscene. Key and speed: [Cheats] FlyKey, FlySpeed in UltimateFix.ini."}, NOTR},
    {{L"Швидкість польоту", L"Flying speed"}, &g_cfg.flySpeed, kInt, 10, 500, 10, false, L"Cheats", L"FlySpeed", nullptr, nullptr,
     {L"Одиниць гри за секунду (120 за замовчуванням). Shift під час польоту учетверо швидше. Діє одразу.",
      L"Game units per second (120 by default). Shift while flying is four times faster. Changes at once."}, NOTR},
    {{L"Йорк до камери", L"York to the camera"}, &g_cheatPick, kInt, 0, 1, 1, false, nullptr, nullptr, DoCamera, nullptr,
     {L"Спершу вмикніть вільну камеру в фоторежимі й підлетіть, куди треба. Кнопка переставляє гравця туди. Не в приміщенні й не в машині. Зробіть збереження.", L"First turn on the free camera in the photo mode and fly where you want. The button puts the player there. Not indoors or in a car. Save first."},
     NOTR, kOneValue, 1, kCameraNames, nullptr, false},
};

// ---- Diagnostics: the long-session hint and two buttons (the folder with the logs, a report file).
int g_diagPick = 0;
const Tr kOpenNames[1] = {{L"Відкрити", L"Open"}};
const Tr kWriteNames[1] = {{L"Записати", L"Write"}};

void FormatHours(int v, wchar_t* out) {
    if (v < 1) lstrcpyW(out, g_cfg.language == 1 ? L"Off" : L"Вимк.");
    else wsprintfW(out, g_cfg.language == 1 ? L"%d h" : L"%d год", v);
}

void DoOpenLogs() { g_diagPick = 0; OpenLogFolder(); }
void DoReport() { g_diagPick = 0; WriteDiagnosticReport(); }

Item g_pageDiag[] = {
    {{L"Нагадування про довгу сесію", L"Long session reminder"}, &g_cfg.sessionReminderHours, kInt, 0, 24, 1, false, L"General", L"SessionReminderHours", nullptr, nullptr,
     {L"Через стільки годин гри мод один раз покаже вгорі екрана коротку підказку: збережіться й перезапустіть гру, коли буде зручно. Довгі сесії цієї гри, за відгуками, бувають нестабільними; причини в коді гри ми не підтвердили. Та сама підказка з'явиться раніше, якщо звукова система гри зайняла 48 зі своїх 64 комірок завантаження (гра не перевіряє, чи вони скінчилися; кількість щогодини пишеться в журнал). «Вимк.»: ніколи. Змінюється одразу.",
      L"After this many hours of play the mod shows a short hint at the top of the screen, once: save, and restart the game when convenient. Long sessions of this game are reported to become unstable; we did not confirm a cause in its code. The same hint comes earlier if the game's sound system holds 48 of its 64 load slots (the game does not check for running out; the number is logged every hour). Off: never. Changes at once."},
     NOTR, nullptr, 0, nullptr, FormatHours},
    {{L"Тека журналів", L"Log folder"}, &g_diagPick, kInt, 0, 1, 1, false, nullptr, nullptr, DoOpenLogs, nullptr,
     {L"Відкриває в Провіднику теку UltimateFix поруч із DP.exe: журнали ufx.log і ufx-prev.log (попередній запуск), звіти про збої crash_*.dmp, резервні копії збережень, знімки екрана, діагностичний звіт. Гра при цьому згорнеться або сховається за вікном Провідника.",
      L"Opens the UltimateFix folder next to DP.exe in Explorer: the logs ufx.log and ufx-prev.log (the previous run), crash dumps crash_*.dmp, save backups, screenshots, the diagnostic report. The game is minimised or goes behind the Explorer window."},
     NOTR, kOneValue, 1, kOpenNames, nullptr, false},
    {{L"Діагностичний звіт", L"Diagnostic report"}, &g_diagPick, kInt, 0, 1, 1, false, nullptr, nullptr, DoReport, nullptr,
     {L"Записує файл UltimateFix\\ufx_report.txt: версія мода, Windows, пам'ять, DXVK чи звичайний Direct3D 9, прапор великої адресації (LAA), які місця гри змінено, а які лишилися як були, і ваші налаштування. У файлі немає імені користувача, назви комп'ютера й назв тек. Додавайте його разом з ufx.log, коли повідомляєте про помилку.",
      L"Writes the file UltimateFix\\ufx_report.txt: the mod version, Windows, memory, DXVK or plain Direct3D 9, the large address flag (LAA), which places of the game are patched and which are untouched, and your settings. The file holds no user name, computer name or folder names. Attach it together with ufx.log when you report a problem."},
     NOTR, kOneValue, 1, kWriteNames, nullptr, false},
};

struct Page {
    Tr title;
    Tr subtitle;
    Item* items;
    int count;
    bool about;  // the "About" page has no settings
    void (*custom)() = nullptr;  // what draws a page without settings (null: the About text)
    bool residents = false;  // a page without settings that shows the residents' timetable instead (residents.cpp)
    bool mods = false;       // the mod manager (mods_page.cpp)
};
void DrawStory();
constexpr int kPhotoPage = 3;  // the photo mode section (the camera keys and the preview of the filters know it)
// Eight sections, most used first. Some are several pages of settings put together once at start (MergePages), each part under its heading.
Page g_pages[] = {
    {{L"Екран і графіка", L"Screen and graphics"}, {L"Вікно, роздільність, тіні, різкість, іконки", L"Window, resolution, shadows, sharpness, icons"}, nullptr, 0, false},
    {{L"Керування", L"Controls"}, {L"Ґеймпад, стіки, вібрація", L"Gamepad, sticks, vibration"}, g_pageInput, (int)(sizeof(g_pageInput) / sizeof(Item)), false},
    {{L"Гра і світ", L"Game and world"}, {L"Складність, бій, дальність, мапа", L"Difficulty, combat, distance, map"}, nullptr, 0, false},
    {{L"Фоторежим і читкоди", L"Photo mode and cheats"}, {L"Камера, фільтри, знімок, гроші, час, погода", L"Camera, filters, picture, money, time, weather"}, nullptr, 0, false},
    {{L"Виправлення", L"Fixes"}, {L"Час, помилки гри, мова, захист dp.sav, діагностика", L"Time, game bugs, language, protection of dp.sav, diagnostics"}, nullptr, 0, false},
    {{L"Сюжет", L"Story"}, {L"Епізод, побічні завдання, поточна ціль (лише перегляд)", L"Episode, side quests, current destination (read-only)"}, nullptr, 0, true, DrawStory},
    {{L"Жителі", L"Residents"}, {L"Де хто зараз за розкладом гри і куди піде далі", L"Where everyone is now by the game's timetable, and where next"}, nullptr, 0, true, nullptr, true},
    {{L"Моди", L"Mods"}, {L"Переклади, текстури й інші моди з теки UltimateFix\\mods", L"Translations, textures and other mods from UltimateFix\\mods"}, nullptr, 0, true, nullptr, false, true},
    {{L"Про мод", L"About"}, {L"Автор і подяки", L"Author and credits"}, nullptr, 0, true},
};
constexpr int kPageCount = (int)(sizeof(g_pages) / sizeof(Page));

// The sidebar has 8 sections instead of 13: some are several pages of settings put together once at start (each part keeps its heading).
struct PageSource {
    Item* items;
    int count;
    Tr title;
};
constexpr int Count(int bytes) { return bytes / (int)sizeof(Item); }
constexpr int kScreenMax = Count(sizeof(g_pageDisplay)) + Count(sizeof(g_pageGraphics));
constexpr int kGameMax = Count(sizeof(g_pageWorld)) + Count(sizeof(g_pageGameplay)) + Count(sizeof(g_pageMapItems));
constexpr int kFixesMax = Count(sizeof(g_pageFixes)) + Count(sizeof(g_pageSave)) + Count(sizeof(g_pageDiag));
constexpr int kPhotoMax = Count(sizeof(g_pagePhoto)) + Count(sizeof(g_pageCheats));
alignas(Item) unsigned char g_photoStore[kPhotoMax * sizeof(Item)];
alignas(Item) unsigned char g_screenStore[kScreenMax * sizeof(Item)];
alignas(Item) unsigned char g_gameStore[kGameMax * sizeof(Item)];
alignas(Item) unsigned char g_fixesStore[kFixesMax * sizeof(Item)];
bool g_pagesMerged = false;

void MergeInto(Page& page, unsigned char* storage, const PageSource* src, int n) {
    Item* out = (Item*)storage;
    int cnt = 0;
    const Item* reset = nullptr;  // only one "Restore defaults" row, at the end
    for (int s = 0; s < n; ++s) {
        bool first = true;
        for (int i = 0; i < src[s].count; ++i) {
            const Item& it = src[s].items[i];
            if (it.value == &g_resetPick) { reset = &it; continue; }
            const unsigned char* from = (const unsigned char*)&it;
            unsigned char* to = (unsigned char*)&out[cnt];
            for (unsigned k = 0; k < sizeof(Item); ++k) to[k] = from[k];
            if (first && !out[cnt].group.uk) out[cnt].group = src[s].title;
            first = false;
            ++cnt;
        }
    }
    if (reset) {
        const unsigned char* from = (const unsigned char*)reset;
        unsigned char* to = (unsigned char*)&out[cnt];
        for (unsigned k = 0; k < sizeof(Item); ++k) to[k] = from[k];
        ++cnt;
    }
    page.items = out;
    page.count = cnt;
}

// Rows whose module, once it was installed at the start, reads the value on every use: switching them off (the icons: to
// another set too) works at once, only switching them on from off waits for the restart.
bool LiveOnceInstalled(const Item& it) {
    return it.value == &g_cfg.waypoint || it.value == &g_cfg.waypointMinimap || it.value == &g_cfg.waypointBeacon ||
           it.value == &g_cfg.showMessageId || it.value == &g_cfg.glyphLog || it.value == &g_cfg.glyphTheme;
}

// What every restart row started this run with. ui::Install merges the pages last of all modules, after LoadConfig clamped
// the values and the DPfix check overrode its own, so g_cfg holds what runs. Before, every restart row showed its amber tag
// all the time, so the colour said nothing; now it marks only a row whose value differs from what runs.
void SnapshotBoot() {
    for (Page& pg : g_pages) {
        if (pg.about || !pg.items) continue;
        for (int i = 0; i < pg.count; ++i) {
            Item& it = pg.items[i];
            if (it.restart && it.value) it.boot = LiveOnceInstalled(it) && *it.value ? kNoDefault : *it.value;  // installed: live
        }
    }
}

// A change of the row needs a restart in this run: a restart row, unless its module is installed and reads it live.
bool RestartRow(const Item& it) { return it.restart && it.value && it.boot != kNoDefault; }
// The row's value differs from what runs: it waits for a restart.
bool Pending(const Item& it) { return RestartRow(it) && *it.value != it.boot; }

int PendingCount() {
    int n = 0;
    for (const Page& pg : g_pages) {
        if (pg.about || !pg.items) continue;
        for (int i = 0; i < pg.count; ++i)
            if (Pending(pg.items[i])) ++n;
    }
    return n;
}

void MergePages() {
    if (g_pagesMerged) return;
    g_pagesMerged = true;
    const PageSource screen[2] = {{g_pageDisplay, (int)(sizeof(g_pageDisplay) / sizeof(Item)), {L"Вікно і екран", L"Window and screen"}},
                                  {g_pageGraphics, (int)(sizeof(g_pageGraphics) / sizeof(Item)), {L"Графіка", L"Graphics"}}};
    const PageSource game[3] = {{g_pageWorld, (int)(sizeof(g_pageWorld) / sizeof(Item)), {L"Світ", L"World"}},
                                {g_pageGameplay, (int)(sizeof(g_pageGameplay) / sizeof(Item)), {L"Ґеймплей", L"Gameplay"}},
                                {g_pageMapItems, (int)(sizeof(g_pageMapItems) / sizeof(Item)), {L"Мапа", L"Map"}}};
    const PageSource fixes[3] = {{g_pageFixes, (int)(sizeof(g_pageFixes) / sizeof(Item)), {L"Виправлення", L"Fixes"}},
                                 {g_pageSave, (int)(sizeof(g_pageSave) / sizeof(Item)), {L"Збереження", L"Saves"}},
                                 {g_pageDiag, (int)(sizeof(g_pageDiag) / sizeof(Item)), {L"Діагностика", L"Diagnostics"}}};
    const PageSource photo[2] = {{g_pagePhoto, (int)(sizeof(g_pagePhoto) / sizeof(Item)), {L"Фоторежим", L"Photo mode"}},
                                 {g_pageCheats, (int)(sizeof(g_pageCheats) / sizeof(Item)), {L"Читкоди", L"Cheats"}}};
    MergeInto(g_pages[0], g_screenStore, screen, 2);
    MergeInto(g_pages[2], g_gameStore, game, 3);
    MergeInto(g_pages[kPhotoPage], g_photoStore, photo, 2);
    MergeInto(g_pages[4], g_fixesStore, fixes, 3);
    SnapshotBoot();
}

int g_page = 0;
int g_sel = 0;     // selected item of the page
int g_scroll = 0;  // first visible row
u8 g_prev[256];    // previous key states, for edge detection
bool g_dragging = false;
bool g_padHolding = false;  // this frame's change is a repeat of a held pad Left/Right: written to the ini once it is let go
volatile int g_wheel = 0;  // wheel steps since the last frame (taken from the game's wheel word)
int g_chipSel = 0;          // the highlighted button on a row of action buttons (the arrows move it, Enter presses it)
int g_chipRow = -1;         // the item that highlight belongs to
int g_hoverRow = -1;        // the item whose button the mouse is over this frame, -1 = none
int g_hoverChip = 0;        // that button
bool g_swallowEsc = false;  // the Esc that closed the menu is still held: the game must not see it as a new press
bool g_swallowRmb = false;  // the same for the right mouse button that held a peek at the photo (the game's aim)
// Hold to peek on the photo page: while the right mouse button or L3 is held, the panel fades away and the picture shows
// clean (the game still sees no input: the menu stays open). g_peekAmount is how far the panel has faded (0 shown, 1 gone).
bool g_peeking = false;
float g_peekAmount = 0.f;

// ---- the gamepad ([Menu] PadControl) ------------------------------------------------------------------------
// The menu reads pad 0 itself once per frame (ReadMenuPad, from the sources the game's read uses) while the game is shown a
// neutral pad, and menupad.h turns the data into the menu's buttons. While the menu is closed the joyGetPosEx hook watches
// for the open pair in what the game is about to get (PadForGame) and asks OnFrame to open it.
enum { kPadNone = 0, kPadFromDualSense = 1, kPadFromWinmm = 2, kPadFromXInput = 3 };
MenuPad g_pad;
bool g_padOn = false;             // this opening of the menu is driven by the pad too (PadControl when it opened)
bool g_padMode = false;           // the last input came from the pad: the hints name its buttons and the mouse cursor is hidden
unsigned g_padRepeat = 0;         // the part of this frame's pad presses that are repeats of a held button
unsigned g_padHeld = 0;           // the logical buttons held now (not those still held from the opening): L3 peeks while held
DWORD g_padGameButtons = 0;       // the buttons of the menu's last read, in the layout the game gets them
int g_padSource = kPadNone;       // where the menu's last read came from
volatile LONG g_padOpenWanted = 0;  // the hook saw the open pair while the menu was closed: OnFrame opens it
volatile LONG g_padSwallow = 0;     // pad buttons the game must not see until they are let go (they opened or closed the menu)
volatile DWORD g_padSeenAt = 0;     // GetTickCount of the game's last read that found a pad (the start card names the pair)
volatile LONG g_padSeenSource = kPadNone;  // and where it came from
bool ReadMenuPad(MenuPadRaw* raw, DWORD* gameButtons, int* source);  // with the hooks below
// The mouse: hover selects a row only when the mouse really moved, so a resting cursor does not take the selection back from
// the keys or the pad every frame (it did: the cursor rests in the middle of the window, over the list, when the menu opens).
float g_mouseX = 0, g_mouseY = 0;
bool g_mouseKnown = false;  // false: the next frame only learns where the cursor is
bool g_mouseMoved = false;  // it moved since the frame before

// ---- layout (1280x720 coordinates) -----------------------------------------------------------------------
// The panel can be moved and resized with the mouse: its place and size are variables and everything else is worked out from them by
// UpdateLayout(). Defaults: 1080 x 616 at 100,52.
constexpr float kPanelW0 = 1080, kPanelH0 = 616, kPanelX0 = 100, kPanelY0 = 52;
constexpr float kPanelWMin = 820, kPanelHMin = 520;
float kPx = kPanelX0, kPy = kPanelY0, kPw = kPanelW0, kPh = kPanelH0;  // panel
constexpr float kSideW = 224;                                           // sidebar width
float kCx = 352, kCr = 1148;                                            // content left / right edge
float kListTop = 170, kListBottom = 508;                                // item list area
constexpr float kRowH = 40, kHeadH = 34;
float kNavH = 44;  // sidebar rows: 44, or less when a small panel would put the last one over the language buttons
float kNavTop = 150;
float kSliderX = 818, kLangY = 612;
constexpr float kConfirmW = 520, kConfirmH = 170;  // the "restore defaults?" box
float kConfirmX = 380, kConfirmY = 275;            // centred on the panel, wherever it was moved
// While settings wait for a restart, the sidebar shows how many over the language buttons, and a button that restarts the
// game. The section rows make room for them; at the default size they still are 44 units tall.
constexpr float kRestartRoom = 56, kRestartW = 176, kRestartH = 30;
bool g_restartShown = false;  // the block is laid out this frame (UpdateLayout)
float kRestartY = 572;        // the top of the button

void UpdateLayout() {
    if (kPw < kPanelWMin) kPw = kPanelWMin;
    if (kPh < kPanelHMin) kPh = kPanelHMin;
    if (kPw > 1280) kPw = 1280;
    if (kPh > 720) kPh = 720;
    if (kPx < 0) kPx = 0;
    if (kPy < 0) kPy = 0;
    if (kPx + kPw > 1280) kPx = 1280 - kPw;
    if (kPy + kPh > 720) kPy = 720 - kPh;
    kCx = kPx + kSideW + 28;
    kCr = kPx + kPw - 32;
    kListTop = kPy + 118;
    kListBottom = kPy + kPh - 160;
    kNavTop = kPy + 98;
    kSliderX = kCr - 330;
    kLangY = kPy + kPh - 56;
    g_restartShown = PendingCount() > 0;
    kRestartY = kLangY - 10 - kRestartH;
    kNavH = (kLangY - 10 - (g_restartShown ? kRestartRoom : 0.f) - kNavTop) / (float)kPageCount;
    if (kNavH > 44) kNavH = 44;
    kConfirmX = SnapWhole(kPx + (kPw - kConfirmW) / 2);  // the panel is at least 820 x 520, so the box is always inside it
    kConfirmY = SnapWhole(kPy + (kPh - kConfirmH) / 2);
}
using namespace theme;  // the colours: theme.h, the website's tokens
constexpr u32 kPanelBg = Alpha(kPanel, 0xF4);  // the panel's own alpha, which the Opacity setting then scales

struct Row {
    int item;               // >= 0: index of the item; -1: a group heading
    Tr heading;
};
Row g_rows[256];
int g_rowCount = 0;

void BuildRows() {
    Page& pg = g_pages[g_page];
    g_rowCount = 0;
    for (int i = 0; i < pg.count; ++i) {
        if (pg.items[i].group.uk && g_rowCount < 254) g_rows[g_rowCount++] = {-1, pg.items[i].group};
        if (g_rowCount < 255) g_rows[g_rowCount++] = {i, {nullptr, nullptr}};
    }
}

float RowHeight(const Row& r) { return r.item < 0 ? kHeadH : kRowH; }

// Row index of the selected item.
int SelectedRow() {
    for (int r = 0; r < g_rowCount; ++r)
        if (g_rows[r].item == g_sel) return r;
    return 0;
}

// Makes the selected row (and the heading above it) visible.
void KeepSelectionVisible() {
    int r = SelectedRow();
    if (r > 0 && g_rows[r - 1].item < 0) --r;  // the heading above the first item of a group
    if (r < g_scroll) g_scroll = r;
    for (;;) {
        float y = kListTop;
        int last = g_scroll;
        for (int i = g_scroll; i < g_rowCount; ++i) {
            y += RowHeight(g_rows[i]);
            if (y > kListBottom) break;
            last = i;
        }
        if (SelectedRow() <= last || g_scroll >= g_rowCount - 1) break;
        ++g_scroll;
    }
}

// Largest first row for which the rest of the rows still fit the list.
int MaxScroll() {
    float y = 0;
    int r = g_rowCount;
    while (r > 0 && y + RowHeight(g_rows[r - 1]) <= kListBottom - kListTop) y += RowHeight(g_rows[--r]);
    return r;
}

void ScrollBy(int rows) {
    g_scroll += rows;
    int mx = MaxScroll();
    if (g_scroll > mx) g_scroll = mx;
    if (g_scroll < 0) g_scroll = 0;
}

bool Scrollable() {
    float y = kListTop;
    for (int i = 0; i < g_rowCount; ++i) y += RowHeight(g_rows[i]);
    return y > kListBottom;
}

// The description box under the list (Draw), and the card that shows its whole text over the list and the box. The card is
// as wide as the panel's content with the list's scroll bar, which it hides.
constexpr float kHintBoxTop = 18, kHintBoxBottom = 118;  // below kListBottom
bool OnHintBox(float mx, float my) {
    return mx >= kCx - 12 && mx < kCr + 12 && my >= kListBottom + kHintBoxTop && my < kListBottom + kHintBoxBottom;
}
void HelpCardRect(float* x, float* y, float* w, float* h) {
    *x = kCx - 12;
    *y = kListTop;
    *w = kCr + 24 - *x;
    *h = kListBottom + kHintBoxBottom - *y;
}

// --- input -------------------------------------------------------------------------------------------
bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// True once per key press.
bool Pressed(int vk) {
    bool now = Down(vk);
    bool was = g_prev[vk] != 0;
    return now && !was;
}

const int kMenuKeys[] = {VK_F10, VK_ESCAPE, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_SPACE, VK_TAB,
                         VK_PRIOR, VK_NEXT, VK_LBUTTON, VK_RBUTTON, VK_BACK, 'Y', 'N', 'R', VK_F9, VK_F1};

void RememberKeys() {
    for (int vk : kMenuKeys) g_prev[vk] = Down(vk) ? 1 : 0;
    for (int d = 0; d < 10; ++d) {
        g_prev[0x30 + d] = Down(0x30 + d) ? 1 : 0;
        g_prev[VK_NUMPAD0 + d] = Down(VK_NUMPAD0 + d) ? 1 : 0;
    }
}

// A key of the menu or a mouse button went down this frame (the hints go back to the keys).
bool AnyKeyPressed() {
    for (int vk : kMenuKeys)
        if (Pressed(vk)) return true;
    for (int d = 0; d < 10; ++d)
        if (Pressed(0x30 + d) || Pressed(VK_NUMPAD0 + d)) return true;
    return false;
}

// ---- Quiet motion ([Menu] Animations) -----------------------------------------------------------------------------
// One clock for the whole menu: MotionFrame reads QueryPerformanceCounter once per drawn frame, so every animation runs in
// seconds and looks the same at 30 fps pacing, at 60 fps or with the physics above 120. Nothing here changes the layout or
// the hit tests: the fade moves and fades what is drawn (SetOverlayFade), the glides only say where a highlight is drawn.
long long g_motionFreq = 0;   // ticks per second
long long g_motionNow = 0;    // the tick of this drawn frame
float g_motionDt = 0.f;       // seconds since the drawn frame before, at most 0.1
unsigned g_motionFrame = 16;  // counts drawn frames; jumps by two at every opening, so nothing glides from where it was at the closing
long long g_openedAt = 0;     // the tick the menu opened at: the fade-in runs from it (closing stays instant)
float g_openAlpha = 1.f, g_openRise = 0.f;  // this frame's fade-in (and peek): the alpha and how far below its place the panel is
Item* g_flipItem = nullptr;   // the switch flipped last and when: its knob slides over
long long g_flipAt = 0;
constexpr float kFadeSeconds = 0.14f, kRiseUnits = 6.f;  // the switch's time is ui.h's kSwitchSeconds
constexpr float kGlideLag = 0.03f;  // the glide's time constant: within a few units of the row after about 90 ms

void MotionFrame() {
    LARGE_INTEGER q;
    if (!g_motionFreq) {
        QueryPerformanceFrequency(&q);
        g_motionFreq = q.QuadPart > 0 ? q.QuadPart : 1;
    }
    QueryPerformanceCounter(&q);
    const long long prev = g_motionNow;
    g_motionNow = q.QuadPart;
    const float dt = prev ? (float)((double)(g_motionNow - prev) / (double)g_motionFreq) : 0.f;
    g_motionDt = dt < 0.f ? 0.f : dt > 0.1f ? 0.1f : dt;
    ++g_motionFrame;
}
}  // namespace

bool MotionOn() { return g_cfg.menuAnimations != 0; }

long long MotionStamp() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

float MotionProgress(long long from, float seconds) {
    if (!MotionOn() || !g_motionFreq || seconds <= 0.f) return 1.f;
    const float t = (float)((double)(g_motionNow - from) / (double)g_motionFreq) / seconds;
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    const float u = 1.f - t;
    return 1.f - u * u * u;  // ease-out cubic: quick at first, settling softly
}

float GlideTo(Glide& g, float target, int index, int context) {
    const int moved = index > g.index ? index - g.index : g.index - index;
    if (!MotionOn() || g.frame + 1 != g_motionFrame || context != g.context || moved > 6) {
        g.pos = target;
    } else {
        // dt / (dt + lag) of the way that is left: it closes in as an exponential would (no exp() without the C runtime),
        // the same in seconds at any frame rate
        g.pos += (target - g.pos) * (g_motionDt / (g_motionDt + kGlideLag));
        const float left = target - g.pos;
        if (left < 0.25f && left > -0.25f) g.pos = target;
    }
    g.index = index;
    g.context = context;
    g.frame = g_motionFrame;
    return g.pos;
}

namespace {

void Save(const Item& it) {
    if (!it.key) return;  // not stored (a test)
    wchar_t value[16];
    wsprintfW(value, L"%d", *it.value);
    WritePrivateProfileStringW(it.section, it.key, value, g_paths.iniFile);
}

Item* g_unsaved = nullptr;  // a slider dragged (or a value held on the pad) right now: written to the ini and the log once, when it ends

void FlushUnsaved() {
    if (!g_unsaved) return;
    Item& it = *g_unsaved;
    g_unsaved = nullptr;
    Save(it);
    Log("menu: %S = %d", it.key, *it.value);
}

void SetValue(Item& it, int v) {
    if (v < it.lo) v = it.lo;
    if (v > it.hi) v = it.hi;
    if (v == *it.value) return;
    *it.value = v;
    if (it.kind == kBool) {  // its knob slides over (DrawItemRow)
        g_flipItem = &it;
        g_flipAt = MotionStamp();
    }
    if (g_unsaved && g_unsaved != &it) FlushUnsaved();
    // Every step of a drag, or every repeat of a held pad direction (about 11 a second), rewrote the whole ini file on the
    // game thread and logged a line.
    if ((g_dragging || g_padHolding) && it.key) {
        g_unsaved = &it;
    } else {
        if (g_unsaved == &it) g_unsaved = nullptr;  // written now: the flush would only write and log the same value again
        Save(it);
        if (it.key) Log("menu: %S = %d", it.key, v);
    }
    if (it.apply) it.apply();
}

int g_confirm = 0;  // 0 nothing, 1 asking about this page, 2 asking about everything, 3 asking to restart the game
constexpr int kConfirmRestart = 3;
// The button of the question Enter / A presses, moved with ← → (keys or pad) or the mouse: 0 yes, 1 no. From the sofa a
// pad has no other way to see which answer it is about to give, so the focused one carries a ring (DrawConfirm).
int g_confirmFocus = 0;

// Every question starts here. Wiping ALL settings and restarting the game are the answers that cannot be taken back (the
// restart loses the unsaved progress), so they start on "no": an A pressed once more out of habit or a double tap (A or X
// has just pressed the chip that asks) keeps the player's values and the game. Y, a click or ← then A still say yes.
void AskConfirm(int what) {
    g_confirm = what;
    g_confirmFocus = (what == 2 || what == kConfirmRestart) ? 1 : 0;
}

// The two answers of the question, side by side under its text: 0 yes, 1 no (the hit boxes and the drawing share these).
constexpr float kConfirmBtnW = 200, kConfirmBtnH = 34, kConfirmBtnTop = 120;
float ConfirmButtonX(int i) { return kConfirmX + 40 + i * 240; }
bool OverConfirmButton(int i, float mx, float my) {
    const float x = ConfirmButtonX(i), y = kConfirmY + kConfirmBtnTop;
    return mx >= x && mx <= x + kConfirmBtnW && my >= y && my <= y + kConfirmBtnH;
}

// The card with the selected row's whole description (F1, R3, a click on the description box): about 15 Ukrainian hints are
// longer than the box's six lines and lost their ending. It is modal like the question: the list under it does not move.
bool g_helpOpen = false;
int g_helpScroll = 0;  // its first visible line
int g_helpMax = 0;     // the largest first line, from the last frame it was drawn
int g_helpPage = 1;    // lines that PgUp / PgDn (LT / RT) move it

void OpenHelp() {
    g_helpOpen = true;
    g_helpScroll = 0;
    g_helpMax = 0;  // until it is drawn once nothing scrolls
}

// Called by the chips: only asks. The work is done by PerformReset after the player confirms.
void ResetPageDefaults() {
    int pick = g_resetPick;
    g_resetPick = 0;
    if (pick == 1 || pick == 2) AskConfirm(pick);
}

int RestorePage(Page& pg, const wchar_t* tmp) {
    int restored = 0;
    if (pg.about || !pg.items) return 0;
    for (int i = 0; i < pg.count; ++i) {
        Item& it = pg.items[i];
        if (!it.key || !it.section || !it.value) continue;
        int v = (int)GetPrivateProfileIntW(it.section, it.key, kNoDefault, tmp);
        if (v == kNoDefault) continue;  // not in the default ini
        SetValue(it, v);
        ++restored;
    }
    return restored;
}

// Writes the default ini (as the DLL writes a new one, OutputWidth set to the monitor) to UltimateFix\defaults.tmp, so the
// defaults can be read with the same GetPrivateProfileIntW as the real ini. The caller deletes the file. False: not written.
bool WriteDefaultsFile(wchar_t* tmp) {
    lstrcpyW(tmp, g_paths.dataDir);
    lstrcatW(tmp, L"defaults.tmp");
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned long length = 0;
    const char* text = DefaultIniText(&length);
    DWORD written;
    WriteFile(h, text, length, &written, nullptr);
    CloseHandle(h);
    return true;
}

void PerformReset(int scope) {
    wchar_t tmp[MAX_PATH];
    if (!WriteDefaultsFile(tmp)) return;
    int restored = 0;
    if (scope == 2) {
        for (Page& pg : g_pages) restored += RestorePage(pg, tmp);
    } else {
        restored = RestorePage(g_pages[g_page], tmp);
    }
    DeleteFileW(tmp);
    Log("menu: defaults restored (%s, %d settings)", scope == 2 ? "everything" : "this page", restored);
}

// ---- "Restart the game": shown in the sidebar while settings wait for a restart (R, X / Square or a click), asked first.
void AskRestart() {
    if (PendingCount() > 0) AskConfirm(kConfirmRestart);
}

// The restart ends the process at once (TerminateProcess, as after an update): never while the game writes dp.sav, which
// CREATE_ALWAYS has emptied by then, and never while an update writes its files.
void PerformRestart() {
    FlushUnsaved();  // every other change is in the ini already
    if (SaveWriteInFlight()) {
        Log("menu: restart refused: the game is writing its save right now");
        ShowNotice(L"Зачекайте", L"Wait a moment", L"Гра саме записує збереження. Спробуйте ще раз за мить.",
                   L"The game is writing its save right now. Try again in a moment.", 4, kNoticeWarning);
        return;
    }
    if (UpdateState() == 2) {
        Log("menu: restart refused: an update is being installed");
        ShowNotice(L"Зачекайте", L"Wait a moment", L"Встановлюється оновлення: після нього гра перезапуститься сама.",
                   L"An update is being installed: the game restarts by itself after it.", 4, kNoticeWarning);
        return;
    }
    Log("menu: restart from the menu (%d settings waited for it)", PendingCount());
    RestartGameNow();
}

// ---- What was changed: every setting with an ini key learns its default once, from the same defaults.tmp as Restore
// defaults (so the marks, the Backspace reset and the full reset always agree, migrated keys included). A row whose value
// differs gets a grey dot after its label, a slider a tick at the default, a preset row a line under the default button,
// and Backspace on the selected row puts that one setting back.
bool Changed(const Item& it) { return it.def != kNoDefault && it.value && *it.value != it.def; }

// How many settings of a page differ from their defaults (the number in the sidebar).
int ChangedCount(const Page& pg) {
    if (pg.about || !pg.items) return 0;
    int n = 0;
    for (int i = 0; i < pg.count; ++i)
        if (Changed(pg.items[i])) ++n;
    return n;
}

void LoadDefaults() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    wchar_t tmp[MAX_PATH];
    if (!WriteDefaultsFile(tmp)) {
        Log("menu: defaults.tmp could not be written: no default marks, Backspace does nothing");
        return;
    }
    int known = 0, changed = 0;
    for (Page& pg : g_pages) {
        if (pg.about || !pg.items) continue;
        for (int i = 0; i < pg.count; ++i) {
            Item& it = pg.items[i];
            if (!it.key || !it.section || !it.value) continue;  // not stored: a test, a button that does something once
            int v = (int)GetPrivateProfileIntW(it.section, it.key, kNoDefault, tmp);
            if (v == kNoDefault) continue;  // not in the default ini
            // SetValue keeps a value inside the row's range, so a default outside it could never be reached by Backspace
            if (v < it.lo) v = it.lo;
            if (v > it.hi) v = it.hi;
            it.def = v;
            ++known;
            if (Changed(it)) ++changed;
        }
    }
    DeleteFileW(tmp);
    Log("menu: defaults known for %d settings, %d changed (grey dot; Backspace restores one)", known, changed);
}

// A value of a row as the row shows its values: "Off", the name, the formatted value or the number.
void FormatValue(const Item& it, int v, wchar_t* out) {
    const bool en = g_cfg.language == 1;
    if (it.kind == kBool) {
        lstrcpyW(out, v ? (en ? L"On" : L"Увімк.") : (en ? L"Off" : L"Вимк."));
        return;
    }
    if (it.names && v >= it.lo && v <= it.hi) {
        lstrcpyW(out, Tx(it.names[v - it.lo]));
        return;
    }
    if (it.format) {
        it.format(v, out);
        return;
    }
    for (int i = 0; i < it.presetCount; ++i)
        if (it.presets[i] == v) {
            lstrcpyW(out, Tx(it.presetNames[i]));
            return;
        }
    wsprintfW(out, L"%d", v);
}
void FormatDefault(const Item& it, wchar_t* out) { FormatValue(it, it.def, out); }

// Stores the place and size of the panel (the ini and g_cfg).
void SavePanelGeometry() {
    g_cfg.menuX = (int)kPx; g_cfg.menuY = (int)kPy; g_cfg.menuW = (int)kPw; g_cfg.menuH = (int)kPh;
    PutValue(&g_cfg.menuX, L"Menu", L"X", (int)kPx);
    PutValue(&g_cfg.menuY, L"Menu", L"Y", (int)kPy);
    PutValue(&g_cfg.menuW, L"Menu", L"Width", (int)kPw);
    PutValue(&g_cfg.menuH, L"Menu", L"Height", (int)kPh);
}

void Change(Item& it, int direction) {
    if (it.kind == kBool) {
        SetValue(it, *it.value ? 0 : 1);
        return;
    }
    int v = *it.value + direction * it.step;
    if (direction > 0 && v > it.hi && *it.value < it.hi) v = it.hi;  // do not skip the last value
    if (direction < 0 && v < it.lo && *it.value > it.lo) v = it.lo;
    // an enumeration wraps around, a number stops at its ends
    if (it.names && v > it.hi) v = it.lo;
    else if (it.names && v < it.lo) v = it.hi;
    SetValue(it, v);
}

bool g_editing = false;

void SelectPage(int page) {
    g_page = (page % kPageCount + kPageCount) % kPageCount;
    g_sel = 0;
    g_chipRow = -1;  // the button highlight of the old page does not carry over to row 0 of this one
    g_scroll = 0;
    g_editing = false;
    g_confirm = 0;
    g_helpOpen = false;
    FlushUnsaved();  // a value held on the pad when RB / LB left its page: the About or Mods page never gets to the flush
    BuildRows();
}

// Mouse position in the game's 1280x720 picture; false if the picture is not under the cursor.
bool MousePosition(float* x, float* y) {
    HWND w = GameWindowHandle();
    POINT p;
    if (!w || !GetCursorPos(&p) || !ScreenToClient(w, &p)) return false;
    // The display module keeps g_fit current on every frame: the 16:9 rectangle while it scales the picture into the
    // borderless window itself, otherwise the whole client area, over which the plain Present stretches it.
    FitRect f = g_fit;
    if (f.w <= 0 || f.h <= 0) return false;
    *x = (float)(p.x - f.x) * 1280.f / (float)f.w;
    *y = (float)(p.y - f.y) * 720.f / (float)f.h;
    return true;
}

// Geometry of the controls on a row.
constexpr float kChipH = 26, kChipGap = 6;  // the switch is ui.h's kSwitchW x kSwitchH

float SliderW(const Item& it) { return it.format ? 150.f : 200.f; }

float SliderPosition(const Item& it, float mx) {  // value for a mouse x on the slider
    float t = (mx - kSliderX) / SliderW(it);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int steps = (it.hi - it.lo) / it.step;
    int n = (int)(t * (float)steps + 0.5f);
    return (float)(it.lo + n * it.step);
}

// The buttons of an item with presets: the presets and a last one for a value of one's own.
int PresetIndex(const Item& it) {  // index of the preset equal to the value, -1 = a custom value
    for (int i = 0; i < it.presetCount; ++i)
        if (it.presets[i] == *it.value) return i;
    return -1;
}

// A row of buttons that each do something once (a quality preset, money, the hour, the weather, Apply ...): its value
// is put back to 0 at once, so it is none of the presets. The arrows only move the highlight on such a row.
bool IsActionRow(const Item& it) { return it.presets && !it.key && !it.format && PresetIndex(it) < 0; }

// The description of a row. A setting without one (the photo sliders, three filter sliders) gets one made from what the row
// knows, so its box is not empty: its range or choices, the default and, for a row without a restart note, that it applies at
// once. `out` holds at least 512 characters. Null for a row of buttons without a text: there is nothing to say about it.
const wchar_t* HintText(const Item& it, wchar_t* out) {
    const wchar_t* h = Tx(it.hint);
    if (h && *h) return h;
    if (!it.value || IsActionRow(it)) return nullptr;
    const bool en = g_cfg.language == 1;
    wchar_t a[64], b[64];
    out[0] = 0;
    if (it.kind == kBool) {
        lstrcpyW(out, en ? L"A switch: on or off." : L"Перемикач: увімкнено або вимкнено.");
    } else if (it.names || it.presets) {
        lstrcpyW(out, en ? L"Choices: " : L"Варіанти: ");
        const int n = it.names ? it.hi - it.lo + 1 : it.presetCount;
        for (int i = 0; i < n; ++i) {
            const wchar_t* name = it.names ? Tx(it.names[i]) : Tx(it.presetNames[i]);
            if (lstrlenW(out) + lstrlenW(name) + 4 > 300) break;  // the rest of the sentence still fits the 512
            if (i) lstrcatW(out, L", ");
            lstrcatW(out, name);
        }
        lstrcatW(out, L".");
    } else {
        FormatValue(it, it.lo, a);
        FormatValue(it, it.hi, b);
        wsprintfW(out, en ? L"From %s to %s" : L"Від %s до %s", a, b);
        // a formatted value (tenths, stops, a scale) would read its step as a raw number
        if (!it.format && it.step > 1) wsprintfW(out + lstrlenW(out), en ? L", in steps of %d" : L", крок %d", it.step);
        lstrcatW(out, L".");
    }
    if (it.def != kNoDefault && !Changed(it)) {  // a changed row names its default on the line under the text already
        FormatValue(it, it.def, a);
        wsprintfW(out + lstrlenW(out), en ? L" Default: %s." : L" Типово: %s.", a);
    }
    if (!RestartRow(it)) {  // a restart row has its own note under the text
        if (it.key) lstrcatW(out, en ? L" Applies at once." : L" Діє одразу.");
        else lstrcatW(out, en ? L" Applies at once, until the game is closed (not saved)." : L" Діє одразу, до виходу з гри (не зберігається).");
    }
    return out;
}

void ChipLayout(const Item& it, float* x, float* w, int* n) {
    // a last chip appears only when the value is none of the presets: it shows that value and a click types a new one
    wchar_t custom[32];
    custom[0] = 0;
    bool extra = it.format && PresetIndex(it) < 0;
    if (extra) it.format(*it.value, custom);
    *n = it.presetCount + (extra ? 1 : 0);
    float total = 0;
    for (int i = 0; i < *n; ++i) {
        w[i] = i < it.presetCount ? TextWidth(Tx(it.presetNames[i]), 15) + 24 : TextWidth(custom, 15) + 24;
        if (w[i] < 54) w[i] = 54;
        total += w[i] + (i ? kChipGap : 0);
    }
    float cx = kCr - total;
    for (int i = 0; i < *n; ++i) {
        x[i] = cx;
        cx += w[i] + kChipGap;
    }
}

// --- typing a number ----------------------------------------------------------------------------------------
wchar_t g_editText[12];
int g_editLen = 0;

// On the pad there are no digits: the box starts with the value and Up/Down (or Left/Right) step it. Such a value is taken
// as it is (no "a number below 1280 is a height"), and it may be negative, which the typed digits cannot be.
bool g_editPad = false;
int g_editValue = 0;

void BeginEdit() {
    g_editing = true;
    g_editPad = false;
    g_editLen = 0;
    g_editText[0] = 0;
}

void ShowPadEditValue() {
    wsprintfW(g_editText, L"%d", g_editValue);
    g_editLen = lstrlenW(g_editText);
}

void BeginPadEdit(const Item& it) {
    BeginEdit();
    g_editPad = true;
    g_editValue = *it.value;
    ShowPadEditValue();
}

// Up/Right one step more, Down/Left one less; a held direction speeds up as a slider does.
void PadEditStep(const Item& it, unsigned pad) {
    const unsigned bit = (pad & kPadUp) ? kPadUp : (pad & kPadDown) ? kPadDown : (pad & kPadRight) ? kPadRight : kPadLeft;
    const int dir = (bit == kPadUp || bit == kPadRight) ? +1 : -1;
    int v = g_editPad ? g_editValue : *it.value;
    if (!g_editPad && g_editLen) {  // digits typed first: step from them
        v = 0;
        for (int i = 0; i < g_editLen; ++i) v = v * 10 + (g_editText[i] - L'0');
    }
    v += dir * it.step * MenuPadStepScale(MenuPadRepeats(g_pad, bit));
    if (v < it.lo) v = it.lo;
    if (v > it.hi) v = it.hi;
    g_editPad = true;
    g_editValue = v;
    ShowPadEditValue();
}

void ConfirmEdit(Item& it) {
    g_editing = false;
    if (g_editPad) {
        g_editPad = false;
        SetValue(it, g_editValue);
        return;
    }
    if (g_editLen == 0) return;
    int n = 0;
    for (int i = 0; i < g_editLen; ++i) n = n * 10 + (g_editText[i] - L'0');
    if (it.resolution && n > 0 && n < 1280) n = (n * 16 + 4) / 9;  // a height: 1080 -> 1920
    SetValue(it, n);
}

void HandleEditKeys(Item& it) {
    if (Pressed(VK_ESCAPE)) { g_editing = false; return; }
    if (Pressed(VK_RETURN)) { ConfirmEdit(it); return; }
    if (Pressed(VK_BACK) && g_editLen > 0) {
        if (g_editPad) {  // the stepped value becomes typed digits (or nothing, if it was negative)
            g_editPad = false;
            if (g_editValue < 0) g_editLen = 1;
        }
        g_editText[--g_editLen] = 0;
    }
    for (int d = 0; d < 10; ++d)
        if (Pressed(0x30 + d) || Pressed(VK_NUMPAD0 + d)) {
            if (g_editPad) {  // a digit after the pad's steps starts a typed number
                g_editPad = false;
                g_editLen = 0;
                g_editText[0] = 0;
            }
            if (g_editLen >= 6) continue;
            g_editText[g_editLen++] = (wchar_t)(L'0' + d);
            g_editText[g_editLen] = 0;
        }
}

bool g_dragScroll = false;
constexpr float kLangChipW = 84;
void SetLanguage(int language);

// The About page's one button, "Open the log folder": where it was last drawn (x, y, w, h; DrawAbout sets it every frame
// and HandleInput reads it), so a person looking for help finds the logs where they look, not only in Fixes > Diagnostics.
float g_aboutLogs[4] = {0, 0, 0, 0};
DWORD g_aboutLogsAt = 0;  // when it was drawn: a click on another page at the same place does nothing

bool AboutClick(float mx, float my) {
    if (GetTickCount() - g_aboutLogsAt > 500) return false;
    if (mx < g_aboutLogs[0] || mx >= g_aboutLogs[0] + g_aboutLogs[2] || my < g_aboutLogs[1] || my >= g_aboutLogs[1] + g_aboutLogs[3]) return false;
    DoOpenLogs();
    return true;
}

// Clicks in the sidebar: a section, the restart button or the language. Returns true when the click was used.
bool SidebarClick(float mx, float my) {
    if (g_restartShown && mx >= kPx + 24 && mx < kPx + 24 + kRestartW && my >= kRestartY && my < kRestartY + kRestartH) {
        AskRestart();
        return true;
    }
    for (int p = 0; p < kPageCount; ++p) {
        float y = kNavTop + p * kNavH;
        if (mx >= kPx && mx < kPx + kSideW && my >= y && my < y + kNavH) {
            SelectPage(p);
            return true;
        }
    }
    for (int i = 0; i < 2; ++i) {
        float x = kPx + 24 + i * 92;
        if (mx >= x && mx < x + kLangChipW && my >= kPy + kPh - 56 && my < kPy + kPh - 26) {
            SetLanguage(i);
            return true;
        }
    }
    return false;
}

// The rows a pad trigger moves the selection by: about a screen of the list.
int PageRows() {
    const int n = (int)((kListBottom - kListTop) / kRowH) - 1;
    return n < 1 ? 1 : n;
}

// This frame's pad: the menu's buttons that went down or repeated (0 without pad control or without a pad).
unsigned PadFrame() {
    g_padRepeat = 0;
    g_padHeld = 0;
    if (!g_padOn) return 0;
    MenuPadRaw raw = {};
    int source = kPadNone;
    if (!ReadMenuPad(&raw, &g_padGameButtons, &source)) g_padGameButtons = 0;
    if (source && source != g_padSource) {
        static int logs = 0;
        if (logs < 4) {
            ++logs;
            Log("menu: gamepad control on (%s)", source == kPadFromDualSense ? "DualSense read directly"
                                                  : source == kPadFromXInput  ? "XInput pad"
                                                                              : "WinMM pad");
        }
    }
    if (source) g_padSource = source;
    const MenuPadFrame f = MenuPadUpdate(g_pad, raw, GetTickCount());
    g_padRepeat = f.repeat;
    g_padHeld = f.down;
    if (f.down | f.pressed) g_padMode = true;
    return f.pressed;
}

// Where the mouse is and whether it moved: a move, a click, the wheel or a key gives the hints back to the keyboard.
void MouseFrame() {
    float x, y;
    g_mouseMoved = false;
    if (!MousePosition(&x, &y)) return;
    const float dx = x - g_mouseX, dy = y - g_mouseY;
    if (g_mouseKnown && dx * dx + dy * dy > 1.f) g_mouseMoved = true;
    g_mouseX = x;
    g_mouseY = y;
    g_mouseKnown = true;
}

// Moving and resizing the panel with the mouse, on every page (the About, Mods, Residents and Story pages return from
// HandleInput before its settings part, which held this until 0.7.9: there the panel could not be moved). True when the mouse
// was used for it this frame.
bool PanelMoveResize() {
    float mx, my;
    if (!MousePosition(&mx, &my)) return false;
    const bool click = Pressed(VK_LBUTTON);
    const bool held = Down(VK_LBUTTON);
    // moving and resizing the panel: the header (the top strip) moves it, the corner resizes it, a double click on the corner resets it
    static bool movingPanel = false, resizingPanel = false;
    static float grabX = 0, grabY = 0;
    static DWORD lastCornerClick = 0;
    if (!held) {
        if (movingPanel || resizingPanel) SavePanelGeometry();
        movingPanel = resizingPanel = false;
    }
    bool onCorner = mx >= kPx + kPw - 24 && mx <= kPx + kPw + 6 && my >= kPy + kPh - 24 && my <= kPy + kPh + 6;
    bool onHeader = mx >= kPx && mx < kPx + kPw && my >= kPy && my < kPy + 84;
    if (click && onCorner) {
        DWORD now = GetTickCount();
        if (now - lastCornerClick < 450) {
            kPx = kPanelX0; kPy = kPanelY0; kPw = kPanelW0; kPh = kPanelH0;
            UpdateLayout();
            SavePanelGeometry();
            BuildRows();
            KeepSelectionVisible();
        } else {
            resizingPanel = true;
        }
        lastCornerClick = now;
        return true;
    }
    if (click && onHeader && !resizingPanel) {
        movingPanel = true;
        grabX = mx - kPx;
        grabY = my - kPy;
        return true;
    }
    // The dragged place and size are kept on whole layout units: a fractional one stayed for the whole session (only the
    // ini got whole numbers) and put every edge of the menu between two units.
    if (movingPanel && held) {
        kPx = SnapWhole(mx - grabX);
        kPy = SnapWhole(my - grabY);
        UpdateLayout();
        return true;
    }
    if (resizingPanel && held) {
        kPw = SnapWhole(mx - kPx + 10);
        kPh = SnapWhole(my - kPy + 10);
        // the size stops at the screen edge; otherwise UpdateLayout moved the panel instead and it crept away frame by frame
        if (kPx + kPw > 1280) kPw = 1280 - kPx;
        if (kPy + kPh > 720) kPh = 720 - kPy;
        UpdateLayout();
        KeepSelectionVisible();
        return true;
    }
    return false;
}

void HandleInput(bool foreground) {
    Page& pg = g_pages[g_page];
    if (!foreground) {
        MenuPadRestart(g_pad);  // what is held when the focus comes back is not a press
        g_mouseKnown = false;
        g_peeking = false;      // the panel comes back behind another window; a button still held there peeks again
        return;
    }
    // The mouse wheel: the game keeps its movement in a 16-bit word (it never reaches our window hook); take it.
    {
        volatile short* word = (volatile short*)(uintptr_t)0x014AFFC0;
        short w = *word;
        if (w) {
            *word = 0;
            int steps = w / 120;
            if (steps == 0) steps = w > 0 ? 1 : -1;
            g_wheel = g_wheel + steps;
        }
    }
    const unsigned pad = PadFrame();
    MouseFrame();
    if (g_mouseMoved || g_wheel || AnyKeyPressed()) g_padMode = false;
    if (pad & kPadOpen) {  // the pair that opened the menu closes it from anywhere, a question or a typed number too
        g_open = false;
        g_wheel = 0;
        return;
    }
    // The photo page previews the filters on the live picture, and the panel covers most of it: held, the right mouse button
    // or L3 fades the panel away (OnFrame) and nothing else of the menu reacts meanwhile (a click or a key would act on rows
    // nobody can see). Not over a question: its answer must stay in sight.
    const bool peeking = g_page == kPhotoPage && !g_confirm && (Down(VK_RBUTTON) || (g_padHeld & kPadPeek));
    if (peeking != g_peeking) {
        g_peeking = peeking;
        static bool logged = false;
        if (peeking && !logged) {
            logged = true;
            Log("menu: photo page peek (the panel hidden while the right mouse button or L3 is held)");
        }
    }
    if (peeking) {
        g_wheel = 0;
        return;
    }
    if (g_confirm) {  // the question (restore defaults, restart): ← → pick a button, Enter / A presses it; Y yes, Esc / N / B no
        g_wheel = 0;
        if (Pressed(VK_LEFT) || (pad & kPadLeft)) g_confirmFocus = 0;
        if (Pressed(VK_RIGHT) || (pad & kPadRight)) g_confirmFocus = 1;
        float mx, my;
        const bool at = MousePosition(&mx, &my);
        if (at && g_mouseMoved) {  // the ring follows the mouse onto a button, as hover selects a row
            if (OverConfirmButton(0, mx, my)) g_confirmFocus = 0;
            else if (OverConfirmButton(1, mx, my)) g_confirmFocus = 1;
        }
        const bool press = Pressed(VK_RETURN) || (pad & kPadAccept);
        bool yes = Pressed('Y') || (press && g_confirmFocus == 0);
        bool no = Pressed(VK_ESCAPE) || Pressed('N') || (pad & kPadBack) || (press && g_confirmFocus == 1);
        if (Pressed(VK_LBUTTON) && at) {
            if (OverConfirmButton(0, mx, my)) yes = true;
            else if (OverConfirmButton(1, mx, my)) no = true;
        }
        if (yes) {
            if (g_confirm == kConfirmRestart) PerformRestart();  // returns only when it was refused (a notice says why)
            else PerformReset(g_confirm);
            g_confirm = 0;
        } else if (no) {
            Log("menu: question %d answered no", g_confirm);  // the yes side logs what it did
            g_confirm = 0;
        }
        return;
    }
    if (g_helpOpen && !pg.about) {  // the description card: ↑ ↓, the wheel and PgUp / PgDn (LT / RT) scroll it
        g_hoverRow = -1;  // no button of the list under it lights up
        int lines = -g_wheel * 2;
        g_wheel = 0;
        if (Pressed(VK_UP) || (pad & kPadUp)) --lines;
        if (Pressed(VK_DOWN) || (pad & kPadDown)) ++lines;
        if (Pressed(VK_PRIOR) || (pad & kPadPageUp)) lines -= g_helpPage;
        if (Pressed(VK_NEXT) || (pad & kPadPageDown)) lines += g_helpPage;
        g_helpScroll += lines;
        if (g_helpScroll > g_helpMax) g_helpScroll = g_helpMax;
        if (g_helpScroll < 0) g_helpScroll = 0;
        // Esc, F1, B, R3 or a click outside the card closes it; the click does nothing else (it lands on the list or the sidebar)
        float mx, my, cx, cy, cw, ch;
        HelpCardRect(&cx, &cy, &cw, &ch);
        const bool outside = Pressed(VK_LBUTTON) && MousePosition(&mx, &my) && (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch);
        if (Pressed(VK_ESCAPE) || Pressed(VK_F1) || (pad & (kPadBack | kPadDetails)) || outside) g_helpOpen = false;
        return;
    }
    if (PanelMoveResize()) {
        g_wheel = 0;
        return;
    }
    const int pageBefore = g_page;
    if (pg.about) {  // nothing to change on this page: keys for the sections, clicks for the sidebar
        if (Pressed(VK_ESCAPE) || (pad & kPadBack)) g_open = false;
        if (Pressed('R') || (!pg.mods && (pad & kPadAction))) AskRestart();  // X / Square is the mod's folder on the Mods page
        if (Pressed(VK_TAB)) SelectPage(g_page + ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? -1 : 1));
        if (Pressed(VK_NEXT) || (pad & kPadNextSection)) SelectPage(g_page + 1);
        if (Pressed(VK_PRIOR) || (pad & kPadPrevSection)) SelectPage(g_page - 1);
        if (g_page != pageBefore) {  // the rest belongs to the page that was left
            g_wheel = 0;
            return;
        }
        if (pg.residents)
            ResidentsScroll(-g_wheel * 2 + ((Pressed(VK_DOWN) || (pad & kPadDown)) ? 1 : 0) - ((Pressed(VK_UP) || (pad & kPadUp)) ? 1 : 0) +
                            ((pad & kPadPageDown) ? PageRows() : 0) - ((pad & kPadPageUp) ? PageRows() : 0));
        float mx = 0, my = 0;
        const bool click = Pressed(VK_LBUTTON) && MousePosition(&mx, &my);
        if (pg.mods) {
            // A held direction moves the selection again and again, but a mod goes up or down the list once per press.
            const unsigned once = pad & ~g_padRepeat;
            ModsPageInput(Pressed(VK_UP) || g_wheel > 0 || (pad & kPadUp), Pressed(VK_DOWN) || g_wheel < 0 || (pad & kPadDown),
                          Pressed(VK_RETURN) || Pressed(VK_SPACE) || (pad & kPadAccept), (pad & kPadAction) != 0, Pressed(VK_LEFT) || (once & kPadLeft),
                          Pressed(VK_RIGHT) || (once & kPadRight), click && !SidebarClick(mx, my), mx, my);
            for (int i = 0; (pad & (kPadPageUp | kPadPageDown)) && i < PageRows(); ++i)
                ModsPageInput((pad & kPadPageUp) != 0, (pad & kPadPageDown) != 0, false, false, false, false, false, 0, 0);
        } else if (click) {
            if (!SidebarClick(mx, my) && !pg.custom && !pg.residents) AboutClick(mx, my);  // only the About text has a button
        }
        g_wheel = 0;
        return;
    }
    if (g_editing) {
        Item& it = pg.items[g_sel];
        if (pad & kPadBack) {  // B cancels the number, as Esc does; A takes it, as Enter does
            g_editing = false;
            g_editPad = false;
            return;
        }
        if (pad & kPadAccept) {
            ConfirmEdit(it);
            return;
        }
        if (pad & (kPadUp | kPadDown | kPadLeft | kPadRight)) PadEditStep(it, pad);
        HandleEditKeys(it);
        if (Pressed(VK_LBUTTON)) g_editing = false;  // a click elsewhere cancels
        return;
    }
    if (Pressed(VK_F1) || (pad & kPadDetails)) {
        OpenHelp();
        g_wheel = 0;
        return;
    }
    if (Pressed(VK_ESCAPE) || (pad & kPadBack)) g_open = false;
    if (Pressed('R') || (pad & kPadAction)) AskRestart();
    int prevSel = g_sel;
    if (Pressed(VK_UP)) g_sel = (g_sel + pg.count - 1) % pg.count;
    if (Pressed(VK_DOWN)) g_sel = (g_sel + 1) % pg.count;
    // The pad's Up/Down wrap around at the ends on a press only: a held one stops there instead of running round again.
    if (pad & kPadUp) {
        if (g_sel > 0) --g_sel;
        else if (!(g_padRepeat & kPadUp)) g_sel = pg.count - 1;
    }
    if (pad & kPadDown) {
        if (g_sel < pg.count - 1) ++g_sel;
        else if (!(g_padRepeat & kPadDown)) g_sel = 0;
    }
    if (pad & kPadPageUp) g_sel = g_sel - PageRows() < 0 ? 0 : g_sel - PageRows();
    if (pad & kPadPageDown) g_sel = g_sel + PageRows() > pg.count - 1 ? pg.count - 1 : g_sel + PageRows();
    if (g_chipRow != g_sel) {
        g_chipRow = g_sel;
        g_chipSel = 0;
    }
    {
        Item& it = pg.items[g_sel];
        bool numeric = it.kind == kInt && !it.names && it.key;
        bool action = IsActionRow(it);
        const bool slider = it.kind == kInt && !it.names && !it.presets;
        int dir = Pressed(VK_RIGHT) ? +1 : Pressed(VK_LEFT) ? -1 : 0;
        int scale = 1;  // steps per change: a slider held on the pad speeds up
        if (!dir && (pad & (kPadLeft | kPadRight))) {
            const unsigned bit = (pad & kPadRight) ? kPadRight : kPadLeft;
            const bool again = (g_padRepeat & bit) != 0;
            if (!(again && it.kind == kBool)) {  // a held direction must not flip a switch on and off ten times a second
                dir = bit == kPadRight ? +1 : -1;
                if (again && slider) scale = MenuPadStepScale(MenuPadRepeats(g_pad, bit));
                g_padHolding = again;  // the first press is written at once, the repeats when the direction is let go
            }
        }
        if (dir) {
            if (action) {  // only the highlight moves: a stray arrow must not apply a preset or spend money
                g_chipSel += dir;
                if (g_chipSel < 0) g_chipSel = 0;
                if (g_chipSel >= it.presetCount) g_chipSel = it.presetCount - 1;
            } else if (it.presets) {  // step through the presets
                int i = PresetIndex(it);
                if (i < 0) {   // a custom value: the nearest preset in that direction
                    i = dir > 0 ? it.presetCount : -1;
                    for (int k = 0; k < it.presetCount; ++k) {
                        if (dir > 0 && it.presets[k] > *it.value && k < i) i = k;
                        if (dir < 0 && it.presets[k] < *it.value && k > i) i = k;
                    }
                } else {
                    i += dir;
                }
                if (i >= 0 && i < it.presetCount) SetValue(it, it.presets[i]);
            } else {
                Change(it, dir * scale);
            }
        }
        g_padHolding = false;  // A, Y or a key below writes at once
        if (action) {  // Enter, Space or A presses the highlighted button
            if (Pressed(VK_RETURN) || Pressed(VK_SPACE) || (pad & kPadAccept)) {
                int i = g_chipSel < 0 ? 0 : (g_chipSel >= it.presetCount ? it.presetCount - 1 : g_chipSel);
                SetValue(it, it.presets[i]);
            }
        } else {
            if (Pressed(VK_RETURN)) {
                if (numeric) BeginEdit();
                else Change(it, +1);
            }
            if (pad & kPadAccept) {  // A: as Enter, but the number box starts with the value, which the pad then steps
                if (numeric) BeginPadEdit(it);
                else Change(it, +1);
            }
            if (Pressed(VK_SPACE)) Change(it, +1);
        }
        // Backspace (Y on the pad): this one setting back to its default, no question (one value, and it is shown in the
        // description). The typing box returned above, so its Backspace never gets here; not during a slider drag, which
        // would take it back.
        if ((Pressed(VK_BACK) || (pad & kPadReset)) && Changed(it) && !g_dragging) SetValue(it, it.def);
    }
    if (Pressed(VK_TAB)) SelectPage(g_page + ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? -1 : 1));
    if (Pressed(VK_NEXT) || (pad & kPadNextSection)) SelectPage(g_page + 1);
    if (Pressed(VK_PRIOR) || (pad & kPadPrevSection)) SelectPage(g_page - 1);
    if (g_page != pageBefore) {
        // The mouse code below works on the rows of the new page (SelectPage rebuilt them) but `pg` is still the old page:
        // an item index of one looked up in the other could even run past its end.
        g_wheel = 0;
        return;
    }

    // the wheel scrolls the list
    int wheel = g_wheel;
    g_wheel = 0;
    if (wheel) ScrollBy(-wheel * 2);

    float mx, my;
    bool click = Pressed(VK_LBUTTON);
    bool held = Down(VK_LBUTTON);
    g_hoverRow = -1;  // found again below while the mouse is over a button
    if (!held) {
        g_dragging = false;
        // A pad hold writes its value when Left/Right is let go, not in the gaps between its repeats.
        if (!g_padOn || !(g_pad.down & (kPadLeft | kPadRight))) FlushUnsaved();
    }
    if (MousePosition(&mx, &my)) {
        if (click && SidebarClick(mx, my)) return;
        if (click && OnHintBox(mx, my)) {  // the description box: its whole text on a card
            OpenHelp();
            return;
        }
        // the scroll bar: click or drag
        if (Scrollable()) {
            bool onBar = mx >= kCr + 4 && mx <= kCr + 30 && my >= kListTop && my <= kListBottom;
            if (click && onBar) g_dragScroll = true;
            if (!held) g_dragScroll = false;
            if (g_dragScroll) {
                float t = (my - kListTop) / (kListBottom - kListTop);
                if (t < 0) t = 0;
                if (t > 1) t = 1;
                g_scroll = (int)(t * (float)MaxScroll() + 0.5f);
                return;
            }
        }
        float y = kListTop;
        for (int r = g_scroll; r < g_rowCount; ++r) {
            float h = RowHeight(g_rows[r]);
            if (y + h > kListBottom) break;
            int idx = g_rows[r].item;
            if (idx >= 0 && my >= y && my < y + h && mx >= kCx - 8 && mx <= kCr + 8) {
                Item& it = pg.items[idx];
                bool slider = it.kind == kInt && !it.names && !it.presets;
                if (it.presets && !g_padMode) {  // the button under the mouse lights up, like a row does
                    float hx[8], hw[8];
                    int hn;
                    ChipLayout(it, hx, hw, &hn);
                    for (int i = 0; i < hn; ++i)
                        if (mx >= hx[i] && mx < hx[i] + hw[i]) {
                            g_hoverRow = idx;
                            g_hoverChip = i;
                        }
                }
                if (click || (held && g_dragging && idx == g_sel)) {
                    g_sel = idx;
                    if (it.kind == kBool) {
                        if (click) Change(it, +1);
                    } else if (it.presets) {
                        if (click) {
                            float cx[8], cw[8];
                            int n;
                            ChipLayout(it, cx, cw, &n);
                            for (int i = 0; i < n; ++i)
                                if (mx >= cx[i] && mx < cx[i] + cw[i]) {
                                    g_chipRow = idx;
                                    g_chipSel = i;
                                    if (i < it.presetCount) SetValue(it, it.presets[i]);
                                    else BeginEdit();  // the last one: a value of one's own
                                }
                        }
                    } else if (slider && mx >= kSliderX - 14 && mx <= kSliderX + SliderW(it) + 14) {
                        g_dragging = true;
                        SetValue(it, (int)SliderPosition(it, mx));
                    } else if (slider && click && mx > kSliderX + SliderW(it) + 14) {
                        BeginEdit();  // a click on the number: type a value
                    } else if (click && it.names) {
                        // only a click on the value changes it (left half back, right half on); a click on the label just selects
                        wchar_t value[40];
                        wsprintfW(value, L"‹  %s  ›", Tx(it.names[*it.value - it.lo]));
                        float vw = TextWidth(value, 18);
                        if (mx >= kCr - vw - 8) Change(it, mx < kCr - vw / 2 ? -1 : +1);
                    }
                } else if (!held && g_mouseMoved) {
                    g_sel = idx;  // hover selects, when the mouse moved over the row (a resting cursor leaves the keys and the pad alone)
                }
            }
            y += h;
        }
        if (held && g_dragging) {
            Item& it = pg.items[g_sel];
            if (it.kind == kInt && !it.names && !it.presets) SetValue(it, (int)SliderPosition(it, mx));
        }
    }
    if (g_sel != prevSel) KeepSelectionVisible();
}

// --- drawing ---------------------------------------------------------------------------------------------
// ---- the pad's buttons in the hints ----------------------------------------------------------------------
// PlayStation names for a DualSense or another Sony pad, as the game's own icons do ([Graphics] GlyphTheme: Xbox and
// PlayStation as chosen, otherwise by the pad).
bool PadSonyFor(int source) {
    if (g_cfg.glyphTheme == 2) return false;
    if (g_cfg.glyphTheme == 3) return true;
    return source == kPadFromDualSense || SonyPadActive() || PadIsRawSony(0);
}
bool PadSony() { return PadSonyFor(g_padSource ? g_padSource : (int)g_padSeenSource); }

// A button's name for a line of plain text (a description, a question): 0 Accept, 1 Back, 2 Reset, 3 the open pair.
const wchar_t* PadWord(int which) {
    const bool en = g_cfg.language == 1;
    if (!PadSony()) {
        static const wchar_t* const xbox[4] = {L"A", L"B", L"Y", L"View + Menu"};
        return xbox[which & 3];
    }
    static const wchar_t* const uk[4] = {L"хрестик", L"коло", L"трикутник", L"Create + Options"};
    static const wchar_t* const enPs[4] = {L"Cross", L"Circle", L"Triangle", L"Create + Options"};
    return (en ? enPs : uk)[which & 3];
}

// One button as a small key cap: a disc for a face button, a rounded box for the others. Only characters every font of
// Windows and Wine has (WGL4: × ○ ▲ □), so no button turns into an empty box. Returns its width; `draw` false only measures.
// The cap is px + 3 high and starts one unit above the line, so the caps of the two hint lines (16 units apart) never overlap.
float PadCap(const wchar_t* label, bool disc, float x, float y, int px, bool draw) {
    const int fpx = px - 2;
    const float h = (float)px + 3.f, tw = TextWidth(label, fpx, kSemibold);
    const float w = disc ? (tw + 7.f > h ? tw + 7.f : h) : tw + 10.f;
    if (draw) {
        FillRoundFramed(x, y - 1.f, w, h, disc ? h / 2.f : 4.f, kHover, kLine2);
        DrawText(label, x + (w - tw) / 2.f, y - 1.f + (h - (float)fpx) / 2.f - 1.f, kText, fpx, kSemibold);
    }
    return w;
}

// A button as the pad's own icon from the mod's glyph atlas (UltimateFix\glyphs\gamepad\xbox.png or playstation.png: the
// pictures the game shows for its buttons, 8x8 cells of 128), the size and place of a key cap, so the menu's hints look like
// the game's. Returns the width, 0 when the atlas cannot be read (the caller then draws a key cap).
float PadIcon(int row, int col, float x, float y, int px, bool draw) {
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%sUltimateFix\\glyphs\\gamepad\\%s", g_paths.gameDir, PadSony() ? L"playstation.png" : L"xbox.png");
    void* tex = ImageTexture(path, nullptr, nullptr);
    if (!tex) return 0.f;
    const float s = (float)px + 5.f;  // the icons fill about 120 of their 128-pixel cell
    if (draw) DrawImage(tex, x, y - 2.f, s, s, (float)col / 8.f, (float)row / 8.f, (float)(col + 1) / 8.f, (float)(row + 1) / 8.f);
    return s;
}

// The cap(s) of a token: {A} {B} {X} {Y} {LB} {RB} {LT} {RT} {R3} {L3} {OPEN}. Returns the width, 0 for an unknown token.
float PadToken(const wchar_t* t, int n, float x, float y, int px, bool draw) {
    auto is = [t, n](const wchar_t* s) {
        int i = 0;
        while (i < n && s[i] && s[i] == t[i]) ++i;
        return i == n && !s[i];
    };
    const bool ps = PadSony();
    // the icon from the atlas cell (row, col), or a key cap with the label when the atlas is missing
    auto button = [&](int row, int col, const wchar_t* label, bool disc, float at) {
        const float w = PadIcon(row, col, at, y, px, draw);
        return w > 0.f ? w : PadCap(label, disc, at, y, px, draw);
    };
    if (is(L"A")) return button(0, 0, ps ? L"×" : L"A", true, x);
    if (is(L"B")) return button(0, 1, ps ? L"○" : L"B", true, x);
    if (is(L"X")) return button(0, 2, ps ? L"□" : L"X", true, x);
    if (is(L"Y")) return button(0, 3, ps ? L"▲" : L"Y", true, x);
    if (is(L"LB")) return button(2, 1, ps ? L"L1" : L"LB", false, x);
    if (is(L"RB")) return button(2, 2, ps ? L"R1" : L"RB", false, x);
    if (is(L"LT")) return button(2, 3, ps ? L"L2" : L"LT", false, x);
    if (is(L"RT")) return button(2, 4, ps ? L"R2" : L"RT", false, x);
    if (is(L"R3")) return button(2, 6, L"R3", false, x);
    if (is(L"L3")) return button(2, 5, L"L3", false, x);
    if (is(L"OPEN")) {
        float w = button(2, 0, ps ? L"Create" : L"View", false, x);
        const float plus = TextWidth(L"+", px);
        if (draw) DrawText(L"+", x + w + 4.f, y, kFaint, px);
        w += 8.f + plus;
        return w + button(1, 7, ps ? L"Options" : L"Menu", false, x + w);
    }
    return 0.f;
}

// A hint line with the pad's buttons: plain text, and tokens in braces drawn as key caps. What does not fit in maxW is left
// out from the first piece that would cross it (a text piece is cut with "…").
void DrawPadLineImpl(const wchar_t* text, float x, float y, u32 color, int px, float maxW) {
    const float right = x + maxW;
    const wchar_t* p = text;
    while (*p) {
        if (*p == L'{') {
            const wchar_t* e = p + 1;
            while (*e && *e != L'}') ++e;
            if (!*e) return;
            const float w = PadToken(p + 1, (int)(e - p - 1), x, y, px, false);
            if (x + w > right) return;
            x += PadToken(p + 1, (int)(e - p - 1), x, y, px, true);
            p = e + 1;
            continue;
        }
        wchar_t piece[256];
        int n = 0;
        while (p[n] && p[n] != L'{' && n < 255) {
            piece[n] = p[n];
            ++n;
        }
        piece[n] = 0;
        p += n;
        const float w = TextWidth(piece, px);
        if (x + w > right) {
            DrawFitted(piece, x, y, color, px, right - x);
            return;
        }
        x += DrawText(piece, x, y, color, px);
    }
}

// The box a number is typed into, right-aligned on a row.
void DrawEditBox(float y) {
    float w = 150;
    FillRoundFramed(kCr - w, y + 6, w, 28, 6, kBg, kAccent, 2);
    wchar_t text[16];
    wsprintfW(text, L"%s|", g_editText);
    float tw = TextWidth(text, 18);
    DrawText(text, kCr - 10 - tw, y + 10, kText, 18);
}

// Where the row's control lies across the row (the switch, the value, the buttons, the slider and its number), as DrawItemRow
// lays it out. False while a number is typed: the edit box has its own red frame.
bool ControlSpan(const Item& it, float* x0, float* x1) {
    *x1 = kCr;
    if (it.kind == kBool) {
        *x0 = kCr - kSwitchW;
    } else if (g_editing) {
        return false;
    } else if (it.names) {
        wchar_t v[40];
        wsprintfW(v, L"‹  %s  ›", Tx(it.names[*it.value - it.lo]));
        *x0 = kCr - TextWidth(v, 18);
    } else if (it.presets) {
        float bx[8], bw[8];
        int bn;
        ChipLayout(it, bx, bw, &bn);
        *x0 = bx[0];
    } else {
        *x0 = kSliderX - 8;  // the knob's half sticks out at the minimum
    }
    return true;
}

// With the pad driving there is no cursor to say where a press goes, and from the sofa the highlight's grey is faint: a
// quiet kFaint outline (the frame of a hovered chip) around the selected row's control shows what ← → and A act on; kLine2
// would be about 1.15:1 on the highlight and vanish into an off switch's track. Drawn with the highlight (Draw), so
// it glides with it, and filled with the highlight's colour, so the control drawn over it looks the same as before.
void DrawPadFocus(const Item& it, float rowY) {
    float x0, x1;
    if (!ControlSpan(it, &x0, &x1)) return;
    const float h = 32, cy = rowY + kRowH / 2;
    FillRoundFramed(x0 - 8, cy - h / 2, x1 - x0 + 16, h, it.kind == kBool ? h / 2 : 8, kSelected, kFaint, 2);
}

// `sel` only picks the colours: the highlight under the selected row is drawn by Draw, which lets it glide between rows.
void DrawItemRow(Item& it, float y, bool sel) {
    // where the control of the row begins (the switch, the value, the first button or the slider): the label stops before it
    float limit = kSliderX - 22;
    if (it.kind == kBool) {
        limit = kCr - kSwitchW - 14;
    } else if (sel && g_editing) {
        limit = kCr - 150 - 14;
    } else if (it.names) {
        wchar_t v[40];
        wsprintfW(v, L"‹  %s  ›", Tx(it.names[*it.value - it.lo]));
        limit = kCr - TextWidth(v, 18) - 14;
    } else if (it.presets) {
        float bx[8], bw[8];
        int bn;
        ChipLayout(it, bx, bw, &bn);
        limit = bx[0] - 14;
    }
    const wchar_t* tag = g_cfg.language == 1 ? L"restart" : L"перезапуск";
    const bool restart = RestartRow(it);
    float tagRoom = restart ? TextWidth(tag, 12) + 10 : 0.f;
    // changed from the default: a small grey dot right after the label (grey, not red: it only says "yours", nothing is wrong)
    const bool changed = Changed(it);
    const float markRoom = changed ? 12.f : 0.f;
    float lw = DrawFitted(Tx(it.label), kCx, y + 10, sel ? kWhite : kText, 18, limit - kCx - tagRoom - markRoom);
    if (changed) FillRound(kCx + lw + 7, y + 22, 5, 5, 2.5f, kMuted);  // on the middle of the lower-case letters
    if (restart) {
        // amber only while the row's value differs from what runs (it waits for the restart); faint, it only says when it applies
        const u32 tagColor = Pending(it) ? kWarn : kFaint;
        float tw = DrawText(tag, kCx + lw + markRoom + 10, y + 14, tagColor, 12);
        if (it.running) {
            wchar_t t[64];
            it.running(t);
            if (t[0]) {  // what is really running now; a shorter wording when the long one would run into the control
                float x0 = kCx + lw + markRoom + 10 + tw + 10, room = limit - x0;
                const wchar_t* shown = t;
                wchar_t shorter[40];
                if (TextWidth(shown, 12) > room) {
                    const wchar_t* colon = t;
                    while (*colon && *colon != L':') ++colon;
                    if (*colon) {
                        wsprintfW(shorter, L"%s%s", g_cfg.language == 1 ? L"now" : L"зараз", colon);
                        shown = shorter;
                        if (TextWidth(shown, 12) > room) shown = colon + (colon[1] == L' ' ? 2 : 1);  // only the value
                    }
                    if (TextWidth(shown, 12) > room) shown = L"";  // not even that fits: leave it out
                }
                DrawText(shown, x0, y + 14, tagColor, 12);  // in the tag's colour
            }
        }
    }

    float cy = y + kRowH / 2;
    if (it.kind == kBool) {
        const float travel = &it == g_flipItem ? MotionProgress(g_flipAt, kSwitchSeconds) : 1.f;
        Switch(kCr - kSwitchW, cy - kSwitchH / 2, *it.value != 0, travel);
        return;
    }
    if (sel && g_editing) {
        DrawEditBox(y);
        return;
    }
    wchar_t value[40];
    if (it.names) {
        wsprintfW(value, L"‹  %s  ›", Tx(it.names[*it.value - it.lo]));
        float w = TextWidth(value, 18);
        DrawText(value, kCr - w, y + 10, sel ? kAccentText : kText, 18);
        return;
    }
    if (it.presets) {  // buttons: the presets and one for a value of one's own
        float cx[8], cw[8];
        int n;
        ChipLayout(it, cx, cw, &n);
        int cur = PresetIndex(it);
        bool actionRow = IsActionRow(it);
        // the button of the default: a preset, or the last one while it shows a default that is none of the presets
        int defChip = -1;
        if (it.def != kNoDefault) {
            for (int i = 0; i < it.presetCount; ++i)
                if (it.presets[i] == it.def) defChip = i;
            if (defChip < 0 && cur < 0 && *it.value == it.def) defChip = it.presetCount;
        }
        for (int i = 0; i < n; ++i) {
            const bool active = i == cur || (i == it.presetCount && cur < 0);  // the preset the value is
            const bool action = it.presetCount == 1 && !it.key && i == 0;  // a button that does something (Apply, Take a picture ...)
            const bool focus = sel && actionRow && g_chipRow == g_sel && i == g_chipSel;  // the button Enter would press
            // the mouse is over this button (hover selects its row, so the hovered row is the selected one)
            const bool hover = sel && g_hoverRow == g_sel && i == g_hoverChip;
            // Quiet raised buttons with a hairline border; red is the chosen preset, and an action button only while Enter
            // would press it.
            u32 fill = kRaised, frame = kLine2, text = action ? kText : kMuted;
            if (active) {
                fill = frame = kAccent;
                text = kWhite;
            }
            if (hover) {  // a lighter fill and frame; the chosen one stays red
                fill = active ? kAccentHi : kHover;
                frame = active ? kAccentHi : kFaint;
                text = kWhite;
            }
            if (focus) {
                fill = frame = kAccentHi;
                text = kWhite;
            }
            FillRoundFramed(cx[i], cy - kChipH / 2, cw[i], kChipH, 6, fill, frame);
            wchar_t label[32];
            if (i < it.presetCount) lstrcpyW(label, Tx(it.presetNames[i]));
            else it.format(*it.value, label);
            float tw = TextWidth(label, 15);
            DrawText(label, cx[i] + (cw[i] - tw) / 2, cy - 9, text, 15);
            if (i == defChip) FillRect(cx[i] + 8, cy + kChipH / 2 + 3, cw[i] - 16, 1, kFaint);  // a hairline under the default
        }
        return;
    }
    // slider + the value (a click on it types a number)
    float sw = SliderW(it);
    float t = (float)(*it.value - it.lo) / (float)(it.hi - it.lo);
    FillRound(kSliderX, cy - 3, sw, 6, 3, kLine2);
    FillRound(kSliderX, cy - 3, sw * t, 6, 3, sel ? kAccent : kFaint);  // red only on the selected row
    if (it.def != kNoDefault && it.hi > it.lo) {  // a thin tick at the default, under the knob (hidden by it while at the default)
        const float td = (float)(it.def - it.lo) / (float)(it.hi - it.lo);
        FillRect(kSliderX + sw * td - 0.5f, cy - 6, 1, 12, kMuted);  // FillRect puts a hairline on a whole pixel
    }
    FillRound(kSliderX + sw * t - 8, cy - 8, 16, 16, 8, kWhite);
    if (it.format) it.format(*it.value, value);
    else wsprintfW(value, L"%d", *it.value);
    float w = TextWidth(value, 18);
    DrawText(value, kCr - w, y + 10, kText, 18);
}

// Two buttons at the bottom of the sidebar: the language of the menu.
void SetLanguage(int language) {
    if (language == g_cfg.language) return;
    g_cfg.language = language;
    WritePrivateProfileStringW(L"Menu", L"Language", language ? L"1" : L"0", g_paths.iniFile);
    Log("menu: language %s", language ? "English" : "Ukrainian");
}

void DrawLanguageChips() {
    const wchar_t* names[2] = {L"УКР", L"ENG"};
    for (int i = 0; i < 2; ++i) {
        float x = kPx + 24 + i * 92;
        bool on = g_cfg.language == i;
        FillRoundFramed(x, kLangY, 84, 30, 6, on ? kHover : kRaised, on ? kLine2 : kLine);  // grey, as on the site
        const int weight = on ? kSemibold : kRegular;
        float tw = TextWidth(names[i], 15, weight);
        DrawText(names[i], x + (84 - tw) / 2, kLangY + 6, on ? kWhite : kMuted, 15, weight);
    }
}

DWORD g_aboutDrawnAt = 0;  // the About page was drawn in the last frames: F9 there updates

// One line under the version: a newer release and F9, the download, or a failure.
void DrawUpdateLine(float x, float y, float width, bool en) {
    const int st = UpdateState();
    if (st == 0) return;
    wchar_t t[200];
    u32 c = kText;
    if (st == 1) wsprintfW(t, en ? L"Version %S is out: F9 updates (the game restarts, save first)" : L"Вийшла версія %S: F9 оновлює (гра перезапуститься, спершу збережіться)", UpdateVersion());
    else if (st == 2) wsprintfW(t, en ? L"Downloading version %S..." : L"Завантажується версія %S...", UpdateVersion());
    else if (st == 3) wsprintfW(t, en ? L"Version %S is installed, restarting" : L"Версію %S встановлено, перезапуск", UpdateVersion());
    else { wsprintfW(t, en ? L"The update failed: F9 tries again" : L"Оновлення не вдалося: F9 пробує ще раз"); c = kError; }
    FillRound(x, y + 3, 4, 14, 2, kAccent);
    DrawFitted(t, x + 12, y, c, 15, width - 12);
}

void DrawAbout() {
    g_aboutDrawnAt = GetTickCount();
    const bool en = g_cfg.language == 1;
    // Everything below fits between the top of the list and the closing hint: on a small panel the gaps shrink (never the letters)
    // and long lines are cut to the panel's width.
    const float top = kListTop - 4, bottom = kPy + kPh - 40, width = kCr - kCx;
    const float natural = 84 + 64 + 62 + 22 + 5 * 24 + 10 + 44 + 26;
    float s = (bottom - top) / natural;
    if (s > 1.f) s = 1.f;
    const float line = 24 * s < 18 ? 18 : 24 * s;
    float y = top;
    DrawText(L"UltimateFIXDP", kCx, y, kText, 30, kSemibold);
    wchar_t ver[48];
    wsprintfW(ver, en ? L"version %S" : L"версія %S", kVersion);
    const float vw = DrawText(ver, kCx, y + 40 * s, kFaint, 15);
    DrawUpdateLine(kCx + vw + 24, y + 40 * s, width - vw - 24, en);
    y += 84 * s;
    DrawText(en ? L"Author" : L"Автор", kCx, y, kAccentText, 13, kSemibold);
    DrawFitted(en ? L"Dmytro Bydlov, the \"Little Bit\" team" : L"Дмитро Бидлов, команда «Little Bit»", kCx, y + 20, kText, 20, width);
    y += 64 * s;
    DrawText(en ? L"Licence" : L"Ліцензія", kCx, y, kAccentText, 13, kSemibold);
    DrawFitted(en ? L"MIT: use and change it freely. All code is our own; other projects gave ideas, facts and addresses."
                  : L"MIT: користуйтесь і змінюйте вільно. Увесь код наш власний; інші проєкти дали ідеї, факти й адреси.",
               kCx, y + 20, kText, 15, width);
    y += 62 * s;
    DrawText(en ? L"Thanks" : L"Подяки", kCx, y, kAccentText, 13, kSemibold);
    const wchar_t* credits[5] = {
        en ? L"ZachFix (h714je): research of the port and many ideas" : L"ZachFix (h714je): дослідження порту й багато ідей",
        en ? L"DPStabilityFix (Cesario67): saves, timing, DualSense layout" : L"DPStabilityFix (Cesario67): збереження, час, розкладка DualSense",
        en ? L"MapFix, CrashFix and FreeCam (Behelit09, MIT): full map, navigation crash, free camera" : L"MapFix, CrashFix і FreeCam (Behelit09, MIT): повна мапа, аварія навігації, вільна камера",
        en ? L"DPfix (Durante): resolution, shadows, texture replacement" : L"DPfix (Durante): роздільність, тіні, підміна текстур",
        en ? L"Kenney Input Prompts (CC0): button icons" : L"Kenney Input Prompts (CC0): іконки кнопок",
    };
    for (int i = 0; i < 5; ++i) DrawFitted(credits[i], kCx, y + 20 + (float)i * line, kMuted, 15, width);
    y += 20 + 5 * line + 10 * s;
    DrawText(en ? L"Testers" : L"Тестувальники", kCx, y, kAccentText, 13, kSemibold);
    DrawFitted(L"Gold flin, Tree Fan, EliParker", kCx, y + 20, kMuted, 15, width);
    y += 44 * s;
    DrawFitted(en ? L"Website, guides and releases: ultimatefixdp.github.io" : L"Сайт, інструкції й випуски: ultimatefixdp.github.io",
               kCx, y + 6, kFaint, 15, width);
    // A quiet button on the line of the closing hint, at the right; the mouse lights it as the other buttons of the menu.
    const wchar_t* logs = en ? L"Open the log folder" : L"Відкрити теку журналів";
    const float bw = TextWidth(logs, 13) + 28, bh = 28, bx = kCr - bw, by = kPy + kPh - 36;
    if (g_padMode) DrawPadLineImpl(en ? L"{B} close     {LB} {RB} section" : L"{B} закрити     {LB} {RB} розділ", kCx, kPy + kPh - 30, kFaint, 13, width - bw - 16);
    else DrawText(en ? L"F10 or Esc close" : L"F10 або Esc закрити", kCx, kPy + kPh - 30, kFaint, 13);
    float mx = 0, my = 0;
    const bool hover = MousePosition(&mx, &my) && mx >= bx && mx < bx + bw && my >= by && my < by + bh;
    FillRoundFramed(bx, by, bw, bh, 6, hover ? kHover : kRaised, hover ? kFaint : kLine2);
    DrawText(logs, bx + 14, by + 6, hover ? kWhite : kText, 13);
    g_aboutLogs[0] = bx;
    g_aboutLogs[1] = by;
    g_aboutLogs[2] = bw;
    g_aboutLogs[3] = bh;
    g_aboutLogsAt = GetTickCount();
}

// True when `size` bytes at `p` can be read: committed pages that allow reading, over the whole range. The Story page reads
// the game's memory through pointers the game made; before a game is started they may be null or point nowhere.
bool Readable(const void* p, size_t size) {
    const u8* at = (const u8*)p;
    const u8* end = at + size;
    if (!p || end < at) return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    while (at < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(at, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || !(mbi.Protect & readable) || (mbi.Protect & PAGE_GUARD)) return false;
        at = (const u8*)mbi.BaseAddress + mbi.RegionSize;
    }
    return true;
}

// Reads the game's story state (story.h) without calling or changing anything of the game. 0 = read, 1 = the game has not set
// its data up yet, 2 = the game's code is not the one the offsets were taken from.
int ReadStory(StoryState* out) {
    static int sitesOk = -1;
    if (sitesOk < 0) {
        sitesOk = 1;
        for (const SiteDef& site : kStorySites)
            if (!BytesMatch(site.va, site.bytes, site.len)) {
                sitesOk = 0;
                Log("story: %s at %08X differs from DP.exe 1.01b, the Story page stays empty", site.name, site.va);
            }
    }
    if (!sitesOk) return 2;
    constexpr u32 kEventCorePtrVa = 0x00BD9FE8;  // CEvCore, the script interpreter that owns the flags (getter FUN_00408960)
    const u8* game = *(const u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    const u8* core = *(const u8* volatile*)(uintptr_t)kEventCorePtrVa;
    if (!Readable(game, kStoryGameSpan) || !Readable(core, 0x12B8)) return 1;
    StoryView view;
    view.game = game;
    view.flags = *(const u8* const volatile*)(core + 0x3C);
    view.flagBytes = *(const volatile u32*)(core + 0x40);
    const u8* project = *(const u8* const volatile*)(core + 0x12AC);
    view.groups = *(const u8* const volatile*)(core + 0x12B4);
    if (view.flagBytes == 0 || view.flagBytes > 0x10000 || !Readable(view.flags, view.flagBytes) || !Readable(project, 0x1C)) return 1;
    view.groupCount = *(const volatile u32*)(project + 0x18);
    if (view.groupCount == 0 || view.groupCount > 0x400 || !Readable(view.groups, view.groupCount * kStoryGroupSize)) return 1;
    view.destAllowed = (const u8*)(uintptr_t)kStoryDestTableVa;
    StoryDecode(view, out);
    return 0;
}

// The Story page: what the game itself keeps about the progress, shown and nothing more.
// The town's story destinations by number (docs/engine/14_destinations.md: named by the event names and the dialogue around
// every script command 0xAC that raises them; numbers no text names are left out and stay numbers).
struct DestinationName {
    int index;
    const wchar_t* uk;
    const wchar_t* en;
};
const DestinationName kDestinations[] = {
    {1, L"відділок шерифа", L"the sheriff's department"},
    {2, L"лікарня", L"the hospital"},
    {3, L"лісовий парк", L"the Forest Park"},
    {4, L"лісопилка", L"the lumbermill"},
    {5, L"театр", L"the playhouse"},
    {6, L"готель", L"the hotel"},
    {7, L"галерея", L"the gallery"},
    {8, L"закусочна A&G", L"the A&G Diner"},
    {10, L"бар зі сценою (Carol)", L"Carol's bar"},
    {11, L"ферма White Spruce", L"White Spruce Farm"},
    {12, L"бар (Richard)", L"Richard's bar"},
    {13, L"маєток (Harry)", L"Harry's mansion"},
    {15, L"кладовище", L"the graveyard"},
    {17, L"лісовий парк", L"the Forest Park"},
    {20, L"дім (George)", L"George's house"},
    {31, L"дім (Thomas, Carol)", L"Thomas and Carol's house"},
    {33, L"дім (Becky)", L"Becky's house"},
    {67, L"театр", L"the playhouse"},
    {71, L"закусочна A&G, задній двір", L"the A&G Diner, back yard"},
};

// The name of a destination, or null. The finale's zone 49 writes the same group: there number 1 is the clock tower.
const wchar_t* DestinationText(int index, bool en) {
    const u8* game = *(const u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    if (index == 1 && Readable(game + 0x834FE0, 4) && *(const volatile int*)(game + 0x834FE0) == 49) return en ? L"the clock tower" : L"годинникова вежа";
    for (const DestinationName& d : kDestinations)
        if (d.index == index) return en ? d.en : d.uk;
    return nullptr;
}

void DrawStory() {
    const bool en = g_cfg.language == 1;
    const float w = kCr - kCx;
    float y = kListTop - 4;
    if (g_padMode)
        DrawPadLineImpl(en ? L"Read-only: the mod changes nothing of the game here.     {B} close     {LB} {RB} section"
                           : L"Лише перегляд: мод тут нічого не змінює в грі.     {B} закрити     {LB} {RB} розділ",
                        kCx, kPy + kPh - 36, kFaint, 13, kCr + 24 - kCx);
    else
        DrawText(en ? L"Read-only: the mod changes nothing of the game here.     F10 or Esc close"
                    : L"Лише перегляд: мод тут нічого не змінює в грі.     F10 або Esc закрити",
                 kCx, kPy + kPh - 36, kFaint, 13);
    StoryState st = {};
    const int result = ReadStory(&st);
    if (result) {
        DrawLines(result == 1 ? (en ? L"The game has not set its story data up yet. Open this page while playing."
                                    : L"Гра ще не підготувала дані сюжету. Відкрийте цю сторінку під час гри.")
                              : (en ? L"This DP.exe keeps its story data elsewhere, so the page stays empty (see UltimateFix\\ufx.log)."
                                    : L"Цей DP.exe тримає дані сюжету в інших місцях, тому сторінка порожня (див. UltimateFix\\ufx.log)."),
                  kCx, y, kText, 17, 22, w, 4, true);
        return;
    }
    wchar_t line[400];

    DrawText(en ? L"Progress" : L"Прогрес", kCx, y, kAccentText, 13, kSemibold);
    if (st.episode > 0) wsprintfW(line, en ? L"Episode %d  ·  chapter %d  ·  step %d" : L"Епізод %d  ·  розділ %d  ·  крок %d", st.episode, st.chapter, st.step);
    else wsprintfW(line, en ? L"Before the first episode (chapter %d, step %d)" : L"Ще до першого епізоду (розділ %d, крок %d)", st.chapter, st.step);
    DrawText(line, kCx, y + 20, kText, 20);
    DrawText(st.finished ? (en ? L"The game has been finished." : L"Гру вже пройдено.") : (en ? L"The game has not been finished yet." : L"Гру ще не пройдено."),
             kCx, y + 48, st.finished ? kText : kFaint, 15);
    y += 80;

    DrawText(en ? L"Side quests" : L"Побічні завдання", kCx, y, kAccentText, 13, kSemibold);
    if (!st.sideKnown) {
        DrawText(en ? L"No data: the game's flag table has no such group." : L"Немає даних: у таблиці прапорів гри немає такої групи.", kCx, y + 20, kFaint, 15);
        y += 52;
    } else {
        wsprintfW(line, en ? L"Taken %d of %d, done %d" : L"Узято %d з %d, виконано %d", st.sideTaken, kStorySideQuests, st.sideDone);
        DrawText(line, kCx, y + 20, kText, 17);
        y += 46;
        if (st.sideOpenCount) {
            int n = wsprintfW(line, en ? L"Taken and not done yet (the game's own numbers, counted from 0): " : L"Узяті й ще не виконані (номери, як їх рахує гра, від 0): ");
            for (int i = 0; i < st.sideOpenCount; ++i) n += wsprintfW(line + n, i ? L", %d" : L"%d", st.sideOpen[i]);
            y += 19.f * (float)DrawLines(line, kCx, y, kMuted, 15, 19, w, 2, true);
        }
        y += 12;
    }

    DrawText(en ? L"Current destination" : L"Поточна ціль", kCx, y, kAccentText, 13, kSemibold);
    if (!st.destKnown) {
        lstrcpyW(line, en ? L"No data: the game's flag table has no such group." : L"Немає даних: у таблиці прапорів гри немає такої групи.");
    } else if (st.destIndex < 0) {
        lstrcpyW(line, en ? L"No story destination in the town right now." : L"Зараз у місті немає сюжетного місця призначення.");
    } else {
        const u8* t = st.destWindow;
        const wchar_t* place = DestinationText(st.destIndex, en);
        int n = place ? wsprintfW(line, en ? L"Go to %s" : L"Іти: %s", place)
                      : wsprintfW(line, en ? L"Story destination %d" : L"Сюжетне місце призначення № %d", st.destIndex);
        if (t[0] | t[1] | t[2] | t[3]) n += wsprintfW(line + n, en ? L", open from %02d:%02d to %02d:%02d" : L", час: з %02d:%02d до %02d:%02d", t[0], t[1], t[2], t[3]);
        else n += wsprintfW(line + n, en ? L", at any time" : L", у будь-який час");
        if (st.destActive > 1) wsprintfW(line + n, en ? L"  (%d active in all)" : L"  (усього активних: %d)", st.destActive);
    }
    DrawFitted(line, kCx, y + 20, kText, 17, w);
    int yards = 0;
    const int points = WaypointObjectives(&yards);
    if (points > 0) wsprintfW(line, en ? L"Objective points on the game's map: %d, the first is %d yards away. The \"Waypoint to the game's current objective\" button in \"Game and world\" (Ctrl+F7 by default) puts the waypoint on it."
                                  : L"Точок цілі на мапі гри: %d, до першої %d ярдів. Мітку на неї ставить кнопка «Мітка на поточну ціль гри» в розділі «Гра і світ» (типово ще й Ctrl+F7).", points, yards);
    else lstrcpyW(line, en ? L"The game's map has no objective point right now." : L"На мапі гри зараз немає точок цілі.");
    y += 46;
    if (points >= 0) y += 19.f * (float)DrawLines(line, kCx, y, kMuted, 15, 19, w, 3, true);  // not with the waypoint switched off
    y += 12;

    DrawText(en ? L"Story flags" : L"Сюжетні прапори", kCx, y, kAccentText, 13, kSemibold);
    wsprintfW(line, en ? L"Raised %d of %d" : L"Піднято %d з %d", st.flagsRaised, st.flagsTotal);
    DrawText(line, kCx, y + 20, kText, 17);
}

// The question of g_confirm (restore defaults, restart the game) over the whole panel.
void DrawConfirm() {
    const bool en = g_cfg.language == 1, restart = g_confirm == kConfirmRestart;
    FillRect(0, 0, 1280, 720, 0x90000000);
    FillRoundFramed(kConfirmX, kConfirmY, kConfirmW, kConfirmH, 12, kRaised, kLine2);  // calm: red is only on the yes button
    const wchar_t* title = restart        ? (en ? L"Restart the game now?" : L"Перезапустити гру зараз?")
                           : g_confirm == 2 ? (en ? L"Restore the defaults of ALL settings?" : L"Відновити типові значення ВСІХ налаштувань?")
                                            : (en ? L"Restore the defaults of this page?" : L"Відновити типові значення цієї сторінки?");
    DrawText(title, kConfirmX + 24, kConfirmY + 24, kText, 20);
    if (restart)  // two lines of 14 px end above the key line, as the one line of 15 px does
        DrawLines(en ? L"The game closes and starts again in a few seconds. Progress since the last save is lost: save first."
                     : L"Гра закриється й за кілька секунд запуститься знову. Незбережений прогрес пропаде: спершу збережіться.",
                  kConfirmX + 24, kConfirmY + 54, kMuted, 14, 16, kConfirmW - 48, 2, true);
    else
        DrawText(en ? L"Your current values will be replaced. This cannot be undone." : L"Ваші поточні значення буде замінено. Скасувати це не вийде.",
                 kConfirmX + 24, kConfirmY + 60, kMuted, 15);
    if (g_padMode)
        DrawPadLineImpl(en ? L"← → pick      {A} press      {B} no" : L"← → вибір      {A} натиснути      {B} ні", kConfirmX + 24, kConfirmY + 90, kFaint, 14,
                        kConfirmW - 48);
    else
        DrawFitted(en ? L"← →: pick      Enter: press      Y: yes      Esc or N: no" : L"← →: вибір      Enter: натиснути      Y: так      Esc або N: ні",
                   kConfirmX + 24, kConfirmY + 88, kFaint, 14, kConfirmW - 48);
    const wchar_t* yes = restart ? (en ? L"Yes, restart" : L"Так, перезапустити") : (en ? L"Yes, restore" : L"Так, відновити");
    const wchar_t* no = restart ? (en ? L"No, later" : L"Ні, пізніше") : (en ? L"No, keep" : L"Ні, лишити");
    const float by = kConfirmY + kConfirmBtnTop;
    // The focus ring: a 2-unit white frame 4 units outside the focused button, the dialog's own colour in the gap, so it
    // reads on the red button and on the grey one alike.
    const float fx = ConfirmButtonX(g_confirmFocus ? 1 : 0);
    FillRoundFramed(fx - 4, by - 4, kConfirmBtnW + 8, kConfirmBtnH + 8, 12, kRaised, kWhite, 2);
    FillRound(ConfirmButtonX(0), by, kConfirmBtnW, kConfirmBtnH, 8, kAccent);
    DrawText(yes, ConfirmButtonX(0) + kConfirmBtnW / 2 - TextWidth(yes, 16) / 2, by + 7, kWhite, 16);
    FillRoundFramed(ConfirmButtonX(1), by, kConfirmBtnW, kConfirmBtnH, 8, g_confirmFocus ? kHover : kSelected, kLine2);
    DrawText(no, ConfirmButtonX(1) + kConfirmBtnW / 2 - TextWidth(no, 16) / 2, by + 7, g_confirmFocus ? kWhite : kText, 16);
}

// Over the language buttons while settings wait for a restart: how many, and the button that restarts the game (it asks
// first). Its key is shown on it: R, or X / Square with the pad (not on the Mods page, where that opens a mod's folder).
void DrawRestartBlock() {
    const bool en = g_cfg.language == 1;
    wchar_t line[64];
    wsprintfW(line, en ? L"Waiting for a restart: %d" : L"Чекають перезапуску: %d", PendingCount());
    DrawFitted(line, kPx + 24, kRestartY - 20, kWarn, 13, kSideW - 48);
    const float x = kPx + 24;
    FillRoundFramed(x, kRestartY, kRestartW, kRestartH, 6, kRaised, Alpha(kWarn, 0x80));
    const wchar_t* label = en ? L"Restart the game" : L"Перезапустити гру";
    const bool padKey = g_padMode && !g_pages[g_page].mods;
    float keyW = 0;
    if (padKey) keyW = PadToken(L"X", 1, 0, 0, 13, false);
    else if (!g_padMode) keyW = TextWidth(L"R", 13, kSemibold);
    const float gap = keyW > 0 ? 8.f : 0.f;
    const float lw = TextWidth(label, 14);
    float tx = x + (kRestartW - lw - gap - keyW) / 2;
    if (tx < x + 8) tx = x + 8;
    tx += DrawFitted(label, tx, kRestartY + 7, kText, 14, x + kRestartW - 8 - keyW - gap - tx) + gap;
    if (padKey) PadToken(L"X", 1, tx, kRestartY + 8, 13, true);
    else if (!g_padMode) DrawText(L"R", tx, kRestartY + 8, kFaint, 13, kSemibold);
}

// The card of g_helpOpen: the selected row's label, its whole description (scrolled by HandleInput) and, under a hairline,
// its default, its value now and when a change applies. Solid, unlike the panel: the list must not show through the text.
void DrawHelpCard(const Item& it) {
    const bool en = g_cfg.language == 1;
    float x, y, w, h;
    HelpCardRect(&x, &y, &w, &h);
    FillRoundFramed(x, y, w, h, 10, kRaised, kLine2);
    const float left = x + 20, textW = w - 40 - 16;  // 16: the scroll bar
    DrawFitted(Tx(it.label), left, y + 18, kText, 18, w - 40, kSemibold);
    wchar_t made[512];
    const wchar_t* hint = HintText(it, made);
    constexpr float kLineH = 20;
    const float textTop = y + 52, factsY = y + h - 32;
    int visible = (int)((factsY - 16 - textTop) / kLineH);
    if (visible < 1) visible = 1;
    const int total = hint ? DrawLines(hint, 0, 0, 0, 15, kLineH, textW, 999, false) : 0;
    g_helpMax = total > visible ? total - visible : 0;
    g_helpPage = visible > 1 ? visible - 1 : 1;  // one line stays in sight across a page
    if (g_helpScroll > g_helpMax) g_helpScroll = g_helpMax;
    if (hint) DrawLinesEx(hint, left, textTop, kText, 15, kLineH, textW, g_helpScroll, visible, true, nullptr, 0);
    else DrawText(en ? L"No description." : L"Опису немає.", left, textTop, kFaint, 15);
    if (g_helpMax > 0) {  // where in the text the card is, as the list's scroll bar
        const float th = kLineH * (float)visible, bx = x + w - 20;
        FillRound(bx, textTop, 4, th, 2, kLine);
        const float frac = (float)visible / (float)total;
        FillRound(bx - 2, textTop + (th - th * frac) * ((float)g_helpScroll / (float)g_helpMax), 8, th * frac, 4, kFaint);
    }
    FillRect(left, factsY - 12, w - 40, 1, kLine);
    if (!it.value || IsActionRow(it)) return;  // a row of buttons has no value to name
    wchar_t facts[160], value[64];
    facts[0] = 0;
    if (it.def != kNoDefault) {
        FormatValue(it, it.def, value);
        wsprintfW(facts, en ? L"Default: %s  ·  " : L"Типово: %s  ·  ", value);
    }
    FormatValue(it, *it.value, value);
    wsprintfW(facts + lstrlenW(facts), en ? L"now: %s  ·  " : L"зараз: %s  ·  ", value);
    const float right = x + w - 20;
    const float fx = left + DrawFitted(facts, left, factsY, kMuted, 14, right - left);
    wchar_t when[128];
    const bool pending = Pending(it);
    if (pending) {
        FormatValue(it, it.boot, value);
        wsprintfW(when, en ? L"applies after a restart (runs: %s)" : L"діє після перезапуску (працює: %s)", value);
    } else if (RestartRow(it)) {
        lstrcpyW(when, en ? L"applies after a restart" : L"діє після перезапуску");
    } else {
        lstrcpyW(when, it.key ? (en ? L"applies at once" : L"діє одразу") : (en ? L"applies at once, not saved" : L"діє одразу, не зберігається"));
    }
    if (right - fx > 24) DrawFitted(when, fx, factsY, pending ? kWarn : kMuted, 14, right - fx);
}

// A key's name for a hint (the photo page's key line, the strip of the flying camera): F1..F24, a letter or a digit. Anything
// else is left out of the hint (false, `out` empty).
bool KeyLabel(int vk, wchar_t* out) {
    if (vk >= VK_F1 && vk <= VK_F24) {
        wsprintfW(out, L"F%d", vk - VK_F1 + 1);
        return true;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        out[0] = (wchar_t)vk;
        out[1] = 0;
        return true;
    }
    out[0] = 0;
    return false;
}

// A background colour with its alpha scaled by the menu opacity setting.
u32 Fade(u32 c) {
    u32 a = (c >> 24) * (u32)g_cfg.menuOpacity / 100;
    return (c & 0x00FFFFFF) | (a << 24);
}

void Draw() {
    if (g_rowCount == 0) BuildRows();  // never an empty page
    // No dimming behind the panel on any page: the photo page must show the real picture (its filters are judged on it), and
    // a dimming on the other pages only made the game's brightness jump when the section changed.
    const bool photo = g_page == kPhotoPage;
    SetOverlayFade(g_openAlpha, g_openRise);
    FillRect(kPx, kPy, kPw, kPh, Fade(kPanelBg));
    // Without the dimming a bright scene shows through a see-through panel and the rows get hard to read: the list, the
    // description box and the two key lines under it keep at least kPhotoListAlpha. The layer ends 4 units above the panel's
    // bottom, as it ends 4 units left of its right edge: an edge any higher would run through the key lines' text. A second layer of the panel's own colour tops its alpha up to that
    // (1 - (1 - a)(1 - b) = target), so the colour stays the panel's and only the scene behind it shows less.
    constexpr u32 kPhotoListAlpha = 0xE0;
    const u32 panelA = Fade(kPanelBg) >> 24;
    if (photo && panelA < kPhotoListAlpha) {
        const u32 b = 255 - (255 - kPhotoListAlpha) * 255 / (255 - panelA);
        FillRound(kCx - 20, kListTop - 8, kCr + 28 - (kCx - 20), kPy + kPh - 4 - (kListTop - 8), 12, Alpha(kPanel, b));
    }
    FillRect(kPx, kPy, kPw, 3, kAccent);
    FillRect(kPx + kPw - 15, kPy + kPh - 5, 11, 2, kFaint);  // the grip in the corner: drag to resize
    FillRect(kPx + kPw - 5, kPy + kPh - 15, 2, 11, kFaint);
    FillRect(kPx, kPy, kSideW, kPh, Fade(kBg));

    DrawText(L"UltimateFIXDP", kPx + 24, kPy + 22, kText, 22, kSemibold);
    wchar_t ver[32];
    wsprintfW(ver, g_cfg.language == 1 ? L"version %S" : L"версія %S", kVersion);
    DrawText(ver, kPx + 24, kPy + 52, kFaint, 13);
    FillRect(kPx + 20, kPy + 84, kSideW - 40, 1, kLine);
    // the section highlight, under the names: it glides from the section before (kept relative to the top of the sidebar)
    static Glide navGlide;
    const float navY = kNavTop + GlideTo(navGlide, (float)g_page * kNavH, g_page, 0);
    FillRect(kPx, navY, kSideW, kNavH, kSelected);
    FillRect(kPx, navY, 4, kNavH, kAccent);
    for (int p = 0; p < kPageCount; ++p) {
        float y = kNavTop + p * kNavH;
        // the content is centred on the row: a small panel with the restart block shrinks the rows below 44, and a name kept
        // at y + 12 would sink to the next row's highlight (at 44 this is exactly y + 12)
        const float ty = y + SnapWhole((kNavH - 20.f) / 2.f);
        bool cur = p == g_page;
        const float nameW = DrawText(Tx(g_pages[p].title), kPx + 26, ty, cur ? kWhite : kMuted, 17);
        // how many settings of the section differ from the default, faint at the right; a dot when the number would touch the name
        const int changed = ChangedCount(g_pages[p]);
        if (changed) {
            wchar_t num[8];
            wsprintfW(num, L"%d", changed);
            const float right = kPx + kSideW - 16, nw = TextWidth(num, 12);
            if (kPx + 26 + nameW + 8 <= right - nw) DrawText(num, right - nw, ty + 5, kFaint, 12);  // on the name's baseline
            else if (kPx + 26 + nameW + 6 <= right - 5) FillRound(right - 5, ty + 11, 5, 5, 2.5f, kFaint);
        }
    }
    if (g_restartShown) DrawRestartBlock();

    Page& pg = g_pages[g_page];
    DrawText(Tx(pg.title), kCx, kPy + 24, kText, 26, kSemibold);
    DrawText(Tx(pg.subtitle), kCx, kPy + 62, kMuted, 15);
    FillRect(kCx, kPy + 100, kCr - kCx, 1, kLine);

    DrawLanguageChips();
    if (pg.about) {
        if (pg.custom) pg.custom();
        else if (pg.residents) ResidentsPage(kCx, kListTop, kCr, kPy + kPh - 44);
        else if (pg.mods) ModsPage(kCx, kListTop, kCr, kPy + kPh - 44);
        else DrawAbout();
        // These pages have no description box: a notice floats over the top of the content, under the page's title (the
        // floating box of the closed menu would land right on it). It is drawn last, over the page.
        DrawNoticeBelow((kCx + kCr) / 2, kPy + 108, kCr - kCx - 32);
        if (g_confirm) DrawConfirm();  // the restart is asked on these pages too
        return;
    }
    // The row highlight first, under the rows: it glides from the row before (relative to the top of the list) and jumps
    // on another page, when the list scrolled or across more than 6 rows. Not drawn while the selected row is out of sight.
    static Glide rowGlide;
    float y = kListTop;
    for (int r = g_scroll; r < g_rowCount; ++r) {
        float h = RowHeight(g_rows[r]);
        if (y + h > kListBottom) break;
        if (g_rows[r].item >= 0 && g_rows[r].item == g_sel) {
            const float hy = kListTop + GlideTo(rowGlide, y - kListTop, r, g_page * 100000 + g_scroll);
            FillRect(kCx - 12, hy + 1, kCr - kCx + 24, kRowH - 2, kSelected);
            FillRect(kCx - 12, hy + 1, 3, kRowH - 2, kAccent);
            if (g_padMode) DrawPadFocus(pg.items[g_sel], hy);
            break;
        }
        y += h;
    }
    y = kListTop;
    int shown = 0;
    for (int r = g_scroll; r < g_rowCount; ++r) {
        float h = RowHeight(g_rows[r]);
        if (y + h > kListBottom) break;
        if (g_rows[r].item < 0) {
            DrawText(Tx(g_rows[r].heading), kCx, y + 11, kAccentText, 13, kSemibold);
            float w = TextWidth(Tx(g_rows[r].heading), 13, kSemibold);  // the same weight, or the hairline runs into the text
            FillRect(kCx + w + 12, y + 19, kCr - kCx - w - 12, 1, kLine);
        } else {
            DrawItemRow(pg.items[g_rows[r].item], y, g_rows[r].item == g_sel);
        }
        y += h;
        ++shown;
    }
    if (Scrollable()) {
        float th = kListBottom - kListTop;
        FillRound(kCr + 14, kListTop, 4, th, 2, kLine);
        float frac = (float)shown / (float)g_rowCount;
        int ms = MaxScroll();
        float pos = ms > 0 ? (float)g_scroll / (float)ms : 0.f;
        FillRound(kCr + 12, kListTop + (th - th * frac) * pos, 8, th * frac, 4, g_dragScroll ? kAccent : kFaint);
    }

    // the description box: word-wrapped to its width, in a smaller font when the text needs more lines than fit
    const float hintTop = kListBottom + 24, hintBottom = kListBottom + 116, hintW = kCr - kCx - 8;
    FillRound(kCx - 12, kListBottom + kHintBoxTop, kCr - kCx + 24, kHintBoxBottom - kHintBoxTop, 10, Fade(kRaised));  // as the site's hint
    Item& cur = pg.items[g_sel];
    const bool en = g_cfg.language == 1;
    // A notice (the answer to a button of this page, a failure, a warning) takes the box for its few seconds, so it appears
    // right under the row that caused it and never over the page's title. Selecting another row after 1.5 s ends it early.
    if (DrawNoticeInBox(kCx - 12, kListBottom + kHintBoxTop, kCr - kCx + 24, kHintBoxBottom - kHintBoxTop, g_page * 100000 + g_sel)) {
        // the selected row's description comes back when the notice is over
    } else if (g_editing) {
        wchar_t line[256];
        if (g_padMode)
            wsprintfW(line, en ? L"\"%s\": up and down change the value (from %d to %d), %s takes it, %s cancels." : L"«%s»: ↑ ↓ змінюють значення (від %d до %d), %s бере його, %s скасовує.",
                      Tx(cur.label), cur.lo, cur.hi, PadWord(0), PadWord(1));
        else
            wsprintfW(line, en ? L"Type a value for \"%s\" (from %d to %d) and press Enter. Esc cancels." : L"Введіть значення для «%s» (від %d до %d) і натисніть Enter. Esc скасовує.",
                      Tx(cur.label), cur.lo, cur.hi);
        int used = DrawLines(line, kCx + 4, hintTop, kText, 15, 19, hintW, 2, true);
        if (cur.resolution && !g_padMode)
            DrawLines(en ? L"A width (1920, 2560, 3840) or a height (1080, 1440, 2160): a number below 1280 is taken as a height."
                         : L"Ширина (1920, 2560, 3840) або висота (1080, 1440, 2160): число менше 1280 вважається висотою.",
                      kCx + 4, hintTop + 19.f * (float)used, kFaint, 15, 19, hintW, 4 - used, true);
    } else {
        wchar_t made[512];
        const wchar_t* hint = HintText(cur, made);
        // a row that waits names what runs until then; any other restart row only says when a change applies
        wchar_t note[128];
        const bool pending = Pending(cur);
        if (pending) {
            wchar_t now[64];
            FormatValue(cur, cur.boot, now);
            wsprintfW(note, en ? L"Waits for a restart (now: %s)." : L"Чекає перезапуску (зараз: %s).", now);
        } else {
            lstrcpyW(note, en ? L"Takes effect after the game is restarted." : L"Набуде чинності після перезапуску гри.");
        }
        // a changed row ends with its default and the key that brings it back: one more line kept free under the text
        const bool changed = Changed(cur);
        const bool restart = RestartRow(cur);
        const int reserved = (restart ? 1 : 0) + (changed ? 1 : 0);
        int px = 15;
        float lh = 18;
        int room = (int)((hintBottom - hintTop) / lh) - reserved;
        int lines = hint ? DrawLines(hint, 0, 0, 0, px, lh, hintW, 99, false) : 0;
        if (lines > room) {
            px = 13;
            lh = 15;
            room = (int)((hintBottom - hintTop) / lh) - reserved;
            lines = DrawLines(hint, 0, 0, 0, px, lh, hintW, 99, false);
        }
        // a text that is still too long ends with how to read all of it (the card, DrawHelpCard)
        const wchar_t* more = lines > room ? (en ? L" F1 / R3: in full" : L" F1 / R3: повністю") : nullptr;
        int used = hint ? DrawLinesEx(hint, kCx + 4, hintTop, kMuted, px, lh, hintW, 0, room, true, more, kFaint) : 0;
        if (restart) DrawFitted(note, kCx + 4, hintTop + lh * (float)used++ + 2, pending ? kWarn : kFaint, 13, hintW);
        if (changed) {
            wchar_t value[64], line[128];
            FormatDefault(cur, value);
            if (g_padMode) wsprintfW(line, en ? L"Default: %s  ·  %s restores" : L"Типово: %s  ·  %s повертає", value, PadWord(2));
            else wsprintfW(line, en ? L"Default: %s  ·  Backspace restores" : L"Типово: %s  ·  Backspace повертає", value);
            DrawFitted(line, kCx + 4, hintTop + lh * (float)used + 2, kFaint, 13, hintW);
        }
    }

    if (g_helpOpen) {  // over the list and the box; the key line under it is the card's
        DrawHelpCard(cur);
        if (g_padMode)
            DrawPadLineImpl(en ? L"↑ ↓ scroll     {LT} {RT} a screen up / down     {B} {R3} close" : L"↑ ↓ гортати     {LT} {RT} на екран вище / нижче     {B} {R3} закрити",
                            kCx, kPy + kPh - 36, kFaint, 13, kCr + 24 - kCx);
        else
            DrawFitted(en ? L"↑ ↓ or the wheel scroll     PgUp PgDn a screen     Esc or F1 close     a click outside the card closes"
                          : L"↑ ↓ або колесо гортати     PgUp PgDn на екран     Esc або F1 закрити     клік поза карткою закриває",
                       kCx, kPy + kPh - 36, kFaint, 13, kCr + 24 - kCx);
        return;
    }
    if (g_confirm) {
        DrawConfirm();
        return;
    }
    if (g_padMode) {  // the last input came from the pad: its buttons instead of the keys
        DrawPadLineImpl(en ? L"↑ ↓ select     ← → change / pick a button     {A} toggle / press / set a number     {B} close     {LB} {RB} section"
                           : L"↑ ↓ вибір     ← → змінити / вибрати кнопку     {A} вибрати / натиснути / число     {B} закрити     {LB} {RB} розділ",
                        kCx, kPy + kPh - 36, kFaint, 13, kCr + 24 - kCx);
        // the photo page names the peek right after the description (a narrow panel cuts the end of the line)
        if (photo)
            DrawPadLineImpl(en ? L"{Y} default     {R3} whole description     {L3} hold: the picture without the menu     {LT} {RT} a screen up / down     {OPEN} open / close the menu"
                               : L"{Y} типове     {R3} увесь опис     {L3} тримати: кадр без меню     {LT} {RT} на екран вище / нижче     {OPEN} відкрити / закрити меню",
                            kCx, kPy + kPh - 20, kFaint, 12, kCr + 24 - kCx);
        else
            DrawPadLineImpl(en ? L"{Y} default     {R3} whole description     {LT} {RT} a screen up / down     {OPEN} open / close the menu"
                               : L"{Y} типове     {R3} увесь опис     {LT} {RT} на екран вище / нижче     {OPEN} відкрити / закрити меню",
                            kCx, kPy + kPh - 20, kFaint, 12, kCr + 24 - kCx);
        return;
    }
    DrawFitted(en ? L"↑ ↓ select     ← → change / pick a button     Enter type a number / press     Tab section     wheel / click     F10 or Esc close"
                  : L"↑ ↓ вибір     ← → змінити / вибрати кнопку     Enter ввести число / натиснути     Tab розділ     колесо / клік     F10 або Esc закрити",
               kCx, kPy + kPh - 36, kFaint, 13, kCr + 24 - kCx);
    // the photo page: the peek and the picture key take the place of moving and resizing the panel
    if (photo) {
        wchar_t line[200], shot[8] = L"";
        KeyLabel(g_cfg.screenshotKey, shot);
        if (shot[0])
            wsprintfW(line, en ? L"Backspace default     F1 whole description     hold the right mouse button: the picture without the menu     %s picture"
                               : L"Backspace типове     F1 увесь опис     затисніть праву кнопку миші: кадр без меню     %s знімок", shot);
        else
            lstrcpyW(line, en ? L"Backspace default     F1 whole description     hold the right mouse button: the picture without the menu"
                              : L"Backspace типове     F1 увесь опис     затисніть праву кнопку миші: кадр без меню");
        DrawFitted(line, kCx, kPy + kPh - 20, kFaint, 12, kCr + 24 - kCx);
        return;
    }
    DrawFitted(en ? L"Backspace default     F1 whole description     drag the header: move     drag the corner: resize     double-click the corner: reset"
                  : L"Backspace типове     F1 увесь опис     заголовок: перенести     куточок: розмір     подвійний клік по куточку: скинути",
               kCx, kPy + kPh - 20, kFaint, 12, kCr + 24 - kCx);
}

// --- hooks that keep the game from seeing input while the menu is open --------------------------------------
SHORT(WINAPI* r_GetAsyncKeyState)(int) = nullptr;
BOOL(WINAPI* r_GetKeyboardState)(PBYTE) = nullptr;
BOOL(WINAPI* r_GetCursorPos)(LPPOINT) = nullptr;
BOOL(WINAPI* r_SetCursorPos)(int, int) = nullptr;
MMRESULT(WINAPI* r_joyGetPosEx)(UINT, LPJOYINFOEX) = nullptr;
int(WINAPI* r_ShowCursor)(BOOL) = nullptr;

// True while the Esc that closed the menu is still held (it is cleared once the key is up). The game saw Esc as up the
// whole time the menu was open, so the same press would reach it as a new one (its own pause or back action).
bool SwallowEsc() {
    if (g_swallowEsc && (r_GetAsyncKeyState(VK_ESCAPE) & 0x8000) == 0) g_swallowEsc = false;
    return g_swallowEsc;
}
// The right mouse button of a peek, when F10 or the pad's pair closed the menu while it was still held: the game would aim.
bool SwallowRmb() {
    if (g_swallowRmb && (r_GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0) g_swallowRmb = false;
    return g_swallowRmb;
}

SHORT WINAPI H_GetAsyncKeyState(int vk) {
    if (g_open || FreeCamBlocksInput()) return 0;
    if (vk == VK_ESCAPE && g_swallowEsc) {
        SwallowEsc();
        return 0;  // also the first call after the release: its "pressed since the last call" bit belongs to the old press
    }
    if (vk == VK_RBUTTON && g_swallowRmb) {
        SwallowRmb();
        return 0;
    }
    return r_GetAsyncKeyState(vk);
}

BOOL WINAPI H_GetKeyboardState(PBYTE state) {
    if (!g_open && !FreeCamBlocksInput()) {
        BOOL ok = r_GetKeyboardState(state);
        if (ok && SwallowEsc()) state[VK_ESCAPE] = 0;
        if (ok && SwallowRmb()) state[VK_RBUTTON] = 0;
        return ok;
    }
    for (int i = 0; i < 256; ++i) state[i] = 0;
    return TRUE;
}

BOOL WINAPI H_GetCursorPos(LPPOINT p) {
    if (!g_open && !FreeCamBlocksInput()) return r_GetCursorPos(p);
    RECT r;
    HWND w = GameWindowHandle();
    if (!w || !GetWindowRect(w, &r)) return r_GetCursorPos(p);
    p->x = (r.left + r.right) / 2;  // the game believes the mouse rests in the middle: no camera spin
    p->y = (r.top + r.bottom) / 2;
    return TRUE;
}

// The game calls ShowCursor(FALSE) twice every frame while it has the focus (TRUE in the background). Passed through,
// the Windows counter ran without limit (hundreds of thousands below zero after an hour), and showing the cursor for
// the menu then took as many calls in one frame. Only a change of visibility is passed on, so the counter stays near 0.
// While the menu is open the game's calls are ignored; we keep the counter at zero (cursor visible) ourselves.
bool g_cursorHidden = false;  // where our calls left the real counter: below zero (hidden) or not

int WINAPI H_ShowCursor(BOOL show) {
    if (g_open) return 0;
    if ((show == FALSE) == g_cursorHidden) return g_cursorHidden ? -1 : 0;  // already as the game wants it
    int count = r_ShowCursor(show);
    g_cursorHidden = count < 0;
    return count;
}

// `arrow` false: the counter is kept the same, but no cursor image is shown (the pad drives the menu; the cursor resting in
// the middle of the panel only covered it). The next mouse move brings the arrow back (MouseFrame, g_padMode).
void EnsureCursorVisible(bool arrow) {
    int count = ShowCursor(TRUE);  // our own import, not the game's hooked one
    while (count < 0) count = ShowCursor(TRUE);
    if (count > 0) ShowCursor(FALSE);
    g_cursorHidden = false;
    SetCursor(arrow ? LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)) : nullptr);
}

BOOL WINAPI H_SetCursorPos(int x, int y) { return (g_open || FreeCamBlocksInput()) ? TRUE : r_SetCursorPos(x, y); }

// --- a missing pad, asked about on a worker thread ------------------------------------------------------------
// With no pad Windows answers joyGetPosEx only after it has looked through the device list: 18-62 ms per call in the
// logs, so even the half-second throttle (ThrottleMissingPad) left a hitch on the game thread twice a second. While a slot
// has no pad, a low-priority worker asks Windows about it every 500 ms and the hook answers at once with the worker's last
// result. When the worker gets an answer with a pad, the hook asks Windows itself again, on the game thread: a present
// pad answers fast and the game needs fresh data every frame. Only the result code is handed over: a failed call gives no
// pad data (the throttle has always answered with the code alone), and the data of the worker's successful call would be
// up to half a second old; the hook's own call in the same poll gets fresh data instead.
constexpr unsigned kPadSlots = 16;      // WinMM joystick ids 0..15
constexpr DWORD kPadPollMs = 500;
constexpr DWORD kPadForgetMs = 2000;    // a slot the game has not asked about for this long is left alone (the menu is open, ...)
volatile LONG g_padMissing[kPadSlots];  // 1 = the hook answers this slot from the worker (written by the game thread only)
volatile LONG g_padFlags[kPadSlots];    // the dwFlags the game asks this slot with
volatile LONG g_padAsked[kPadSlots];    // GetTickCount of the game's last question about it
// The worker's last answer: the take count in the high 16 bits (the game thread raises it each time it hands the slot over),
// the MMRESULT in the low 16. The worker stores its answer only if the count is still the one it read before its call, so
// a slow call that started before the pad came and went cannot report a stale "pad present" for the new take.
volatile LONG g_padAnswer[kPadSlots];
unsigned g_padTakes[kPadSlots];         // game thread only
volatile LONG g_padIdle = 0;            // 1 = the worker waits for g_padWake without a timeout
HANDLE g_padWake = nullptr;             // auto-reset: a slot was handed over, or the game asks again after a pause
int g_padWorker = 0;                    // 0 = not started, 1 = running, -1 = could not be started (game thread only)

DWORD WINAPI PadWorker(void*) {
    LARGE_INTEGER qf;
    QueryPerformanceFrequency(&qf);
    double total = 0, worst = 0;
    int calls = 0;
    bool said = false;
    for (;;) {
        bool any = false;
        if (*(volatile int*)&g_cfg.padPollThread) {  // switched off in the menu: the hook takes the slots back on its next poll
            for (unsigned id = 0; id < kPadSlots; ++id) {
                if (!g_padMissing[id] || GetTickCount() - (DWORD)g_padAsked[id] > kPadForgetMs) continue;
                any = true;
                const LONG before = g_padAnswer[id];
                JOYINFOEX j;
                DWORD* words = (DWORD*)&j;
                for (unsigned i = 0; i < sizeof(j) / sizeof(DWORD); ++i) words[i] = 0;
                j.dwSize = sizeof(j);
                j.dwFlags = (DWORD)g_padFlags[id];
                LARGE_INTEGER q0, q1;
                QueryPerformanceCounter(&q0);
                const MMRESULT r = r_joyGetPosEx(id, &j);
                QueryPerformanceCounter(&q1);
                InterlockedCompareExchange(&g_padAnswer[id], (LONG)(((ULONG)before & 0xFFFF0000u) | ((ULONG)r & 0xFFFFu)), before);
                const double ms = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart;
                total += ms;
                if (ms > worst) worst = ms;
                if (!said && ++calls >= 4) {  // about two seconds in: the first call alone may include the driver's start-up
                    said = true;
                    const double avg = total / calls;
                    Log("input: the missing pad is polled on a worker thread (%d.%02d ms per call, %d.%02d ms at most, %d calls)", (int)avg,
                        (int)(avg * 100.0) % 100, (int)worst, (int)(worst * 100.0) % 100, calls);
                }
            }
        }
        if (any) {
            WaitForSingleObject(g_padWake, kPadPollMs);
        } else {
            // Nothing to ask: sleep until the hook hands a slot over or asks again. Should the hook look at g_padIdle just
            // before it is set, it sees it set on its next poll, a frame later.
            InterlockedExchange(&g_padIdle, 1);
            WaitForSingleObject(g_padWake, INFINITE);
            InterlockedExchange(&g_padIdle, 0);
        }
    }
}

bool StartPadWorker() {
    if (g_padWorker) return g_padWorker > 0;
    g_padWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE t = g_padWake ? CreateThread(nullptr, 0, PadWorker, nullptr, 0, nullptr) : nullptr;
    if (!t) {
        g_padWorker = -1;
        Log("input: the pad worker thread could not be started (error %u); a missing pad is asked about on the game thread", (unsigned)GetLastError());
        return false;
    }
    SetThreadPriority(t, THREAD_PRIORITY_LOWEST);
    CloseHandle(t);
    g_padWorker = 1;
    return true;
}

// A direct call for this slot just failed: hand the slot to the worker. False when the worker is off or cannot run.
bool PadToWorker(UINT id, const JOYINFOEX* info, MMRESULT r) {
    if (id >= kPadSlots || !info || info->dwSize != sizeof(JOYINFOEX) || !g_cfg.padPollThread || r == JOYERR_NOERROR) return false;
    if (!StartPadWorker()) return false;
    InterlockedExchange(&g_padFlags[id], (LONG)info->dwFlags);
    InterlockedExchange(&g_padAsked[id], (LONG)GetTickCount());
    const ULONG take = (++g_padTakes[id] & 0x7FFFu) << 16;
    InterlockedExchange(&g_padAnswer[id], (LONG)(take | ((ULONG)r & 0xFFFFu)));  // the failure just seen, until the worker knows better
    InterlockedExchange(&g_padMissing[id], 1);
    SetEvent(g_padWake);
    return true;
}

// The answer for a slot the worker holds, at once. False (and the slot back on the game thread) when the worker has seen a
// pad or was switched off: the caller then asks Windows itself.
bool PadFromWorker(UINT id, const JOYINFOEX* info, MMRESULT* r) {
    // A call with no record or a record of the wrong size is a parameter error that Windows answers at once: it is passed on
    // and leaves the slot with the worker.
    if (id >= kPadSlots || !g_padMissing[id] || !info || info->dwSize != sizeof(JOYINFOEX)) return false;
    const MMRESULT code = (MMRESULT)((ULONG)g_padAnswer[id] & 0xFFFFu);
    if (code == JOYERR_NOERROR || !g_cfg.padPollThread) {
        InterlockedExchange(&g_padMissing[id], 0);
        return false;
    }
    InterlockedExchange(&g_padAsked[id], (LONG)GetTickCount());
    if ((DWORD)g_padFlags[id] != info->dwFlags) InterlockedExchange(&g_padFlags[id], (LONG)info->dwFlags);
    if (g_padIdle) SetEvent(g_padWake);  // the game asks again after a pause (the menu was open)
    *r = code;
    return true;
}

// With no pad Windows answers a joyGetPosEx(0) only after it has looked through the device list (cfgmgr32, kernel calls): the
// sampling profiler of this mod found 80% of the frame in ntdll because of it. A slot without a pad is asked about by the
// worker above (PadPollThread); with that off, a failed answer for pad 0 is reused for half a second (ThrottleMissingPad),
// so the slow question is asked on the game thread twice a second, not every frame. `asked`: Windows was asked on this
// thread, so the data is fresh (an answer from the worker or the throttle is only a failure code). Used by the hook for the
// game and by the menu's own read of the pad while the game is shown a neutral one.
DWORD g_padFailTick = 0;
MMRESULT g_padFailCode = JOYERR_NOERROR;

MMRESULT AskWinmm(UINT id, LPJOYINFOEX info, bool* asked) {
    MMRESULT r;
    *asked = false;
    if (PadFromWorker(id, info, &r)) return r;
    const DWORD nowTick = GetTickCount();
    if (id == 0 && g_cfg.throttleMissingPad && g_padFailCode != JOYERR_NOERROR && nowTick - g_padFailTick < 500) return g_padFailCode;
    *asked = true;
    r = r_joyGetPosEx(id, info);
    // A slot the worker takes needs no throttle: it is answered from the worker until a pad is there, and then the next poll
    // must ask Windows at once.
    const bool taken = PadToWorker(id, info, r);
    if (id == 0) {
        if (r != JOYERR_NOERROR && !taken) {
            g_padFailCode = r;
            g_padFailTick = nowTick;
        } else {
            g_padFailCode = JOYERR_NOERROR;
        }
    }
    return r;
}

// The bits of the open pair in the pad-0 data of this source, 0 when the layout of the pad is not known: on a 4-axis
// DirectInput pad (right stick on Z/R) buttons 6 and 7 are often the triggers, and aiming while firing must not open the menu.
DWORD OpenPairFor(int source) {
    if (source == kPadFromDualSense || source == kPadFromXInput) return kPadOpenPair;  // made in the Xbox layout
    if (PadIsRawSony(0)) return (1u << 8) | (1u << 9);  // a Sony pad in its own layout: Create, Options
    return PadZCarriesTriggers(0) || (g_cfg.sonyLayout && SonyPadActive()) ? kPadOpenPair : 0;  // Xbox layout, or a converted Sony pad
}

// While the menu is closed: pad 0 as the game is about to get it. The open pair asks OnFrame to open the menu, and buttons that
// opened or closed the menu are kept from the game until they are let go. The first half of the pair is held back for a
// moment (kPairWaitMs), so that Start (the game's pause) or Back does not reach the game when the other half follows; a half
// tapped alone reaches it that moment later, one held alone from then on.
constexpr DWORD kPairWaitMs = 120, kPairTapMs = 60;
DWORD g_pairPrev = 0;      // the buttons of the poll before
DWORD g_pairHeld = 0;      // the half held back now
DWORD g_pairHeldAt = 0;
DWORD g_pairTap = 0;       // a half let go while held back: shown to the game until g_pairTapUntil
DWORD g_pairTapUntil = 0;

void PadForGame(JOYINFOEX* info, int source) {
    const DWORD now = GetTickCount();
    g_padSeenAt = now | 1;
    g_padSeenSource = source;
    const DWORD b = info->dwButtons;
    const DWORD pair = g_cfg.menu && g_cfg.menuPadControl ? OpenPairFor(source) : 0;
    DWORD swallow = (DWORD)g_padSwallow;
    if (!pair) {
        g_pairHeld = g_pairTap = 0;
    } else if ((b & pair) == pair && !(swallow & pair)) {
        InterlockedExchange(&g_padOpenWanted, 1);
        swallow |= pair;
        g_pairHeld = g_pairTap = 0;
    } else {
        if (g_pairHeld && !(b & g_pairHeld)) {  // tapped alone
            g_pairTap = g_pairHeld;
            g_pairTapUntil = now + kPairTapMs;
            g_pairHeld = 0;
        } else if (g_pairHeld && now - g_pairHeldAt >= kPairWaitMs) {  // held alone: the game's
            g_pairHeld = 0;
        }
        const DWORD fresh = b & pair & ~g_pairPrev & ~swallow;
        if (!g_pairHeld && fresh && (b & pair) != pair) {
            g_pairHeld = fresh;
            g_pairHeldAt = now;
        }
    }
    g_pairPrev = b;
    swallow &= b;  // a let-go button is the game's again
    InterlockedExchange(&g_padSwallow, (LONG)swallow);
    DWORD out = b & ~swallow & ~g_pairHeld;
    if (g_pairTap) {
        if ((int)(now - g_pairTapUntil) < 0) out |= g_pairTap;
        else g_pairTap = 0;
    }
    if (out == b) return;
    info->dwButtons = out;
    int count = 0;
    for (int i = 0; i < 32; ++i) count += (out >> i) & 1;
    info->dwButtonNumber = (DWORD)count;
}

MMRESULT WINAPI H_joyGetPosEx(UINT id, LPJOYINFOEX info) {
    if (id != 0 && g_cfg.onlyFirstPad) return JOYERR_UNPLUGGED;  // the game only ever uses slot 0
    if ((!g_open && !FreeCamBlocksInput()) || id != 0 || !info) {
        MMRESULT r;
        int source = kPadNone;
        if (id == 0 && info && g_cfg.nativeDualSense && DualSenseToJoy(info)) {
            r = JOYERR_NOERROR;  // a DualSense read directly: no WinMM call, no conversion needed
            source = kPadFromDualSense;
        } else {
            // Only the WinMM answer is cached (AskWinmm): the XInput fallback below is cheap (it scans for a new pad once a
            // second itself) and still runs every poll, so a pad seen only through XInput does not bring the slow question back.
            static double s_ms = 0, s_worst = 0;
            static int s_calls = 0, s_cached = 0, s_logs = 0;
            LARGE_INTEGER q0, q1, qf;
            QueryPerformanceCounter(&q0);
            bool asked = false;  // Windows was asked on this thread (not answered from the worker or the throttle)
            r = AskWinmm(id, info, &asked);
            QueryPerformanceCounter(&q1);
            if (id == 0) {  // the time this poll spent on the game thread, the answers without a WinMM call included
                QueryPerformanceFrequency(&qf);
                const double ms = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart;
                s_ms += ms;
                if (ms > s_worst) s_worst = ms;
                if (!asked) ++s_cached;
                if (++s_calls >= 120) {
                    if (s_logs < 6) {
                        ++s_logs;
                        const double avg = s_ms / s_calls;
                        Log("input: reading the pad takes %d.%02d ms per call on the game thread on average, %d.%02d ms at most (%d calls, %d answered without asking Windows)",
                            (int)avg, (int)(avg * 100.0) % 100, (int)s_worst, (int)(s_worst * 100.0) % 100, s_calls, s_cached);
                    }
                    s_ms = s_worst = 0;
                    s_calls = s_cached = 0;
                }
            }
            if (asked && r == JOYERR_NOERROR && info) ConvertSonyLayout(id, info);
            if (r == JOYERR_NOERROR) source = kPadFromWinmm;
            if (r != JOYERR_NOERROR && id == 0 && g_cfg.xinputFallback && XInputToJoy(info)) {
                r = JOYERR_NOERROR;
                source = kPadFromXInput;
            }
            static bool s_fallbackOff = false;
            if (r != JOYERR_NOERROR && id == 0 && !g_cfg.xinputFallback && !s_fallbackOff) {
                s_fallbackOff = true;
                Log("input: no WinMM pad, and the XInput fallback is off ([Input] XInputFallback=0): an Xbox pad without Steam Input stays unseen");
            }
        }
        if (id == 0 && r == JOYERR_NOERROR && info && info->dwSize == sizeof(JOYINFOEX) && !g_open) PadForGame(info, source);
        if (id == 0) PadCapture(r == JOYERR_NOERROR ? info : nullptr);
        if (!g_open) AutoInputTick(id, info, (unsigned)r, r_GetAsyncKeyState);
        return r;
    }
    info->dwXpos = info->dwYpos = info->dwZpos = info->dwRpos = info->dwUpos = info->dwVpos = 0x7FFF;
    info->dwButtons = 0;
    info->dwButtonNumber = 0;
    info->dwPOV = 0xFFFF;
    PadCapture(info);
    return JOYERR_NOERROR;
}

// The menu's own read of pad 0, once per frame while it is open (the game then gets a neutral pad from the hook): the sources
// and the order of the game's read above, so the menu sees the pad the game would. `gameButtons` are the buttons in the
// layout the game gets them (they are kept from it after the menu closes, PadForGame); `raw` has them in the Xbox layout
// menupad.h reads. False: no pad.
bool ReadMenuPad(MenuPadRaw* raw, DWORD* gameButtons, int* source) {
    raw->valid = false;
    *source = kPadNone;
    JOYINFOEX j;
    DWORD* words = (DWORD*)&j;
    for (unsigned i = 0; i < sizeof(j) / sizeof(DWORD); ++i) words[i] = 0;
    j.dwSize = sizeof(j);
    j.dwFlags = JOY_RETURNALL;
    if (g_cfg.nativeDualSense && DualSenseToJoy(&j)) {
        *source = kPadFromDualSense;
    } else {
        bool asked = false;
        const MMRESULT r = r_joyGetPosEx ? AskWinmm(0, &j, &asked) : (MMRESULT)JOYERR_UNPLUGGED;
        if (r == JOYERR_NOERROR && asked) {
            ConvertSonyLayout(0, &j);
            *source = kPadFromWinmm;
        } else if (g_cfg.xinputFallback && XInputToJoy(&j)) {
            *source = kPadFromXInput;
        }
    }
    if (!*source) return false;
    *gameButtons = j.dwButtons;
    DWORD b = j.dwButtons;
    bool zTriggers = true;  // DualSenseToJoy, XInputToJoy and ConvertSonyLayout put both triggers on Z
    if (*source == kPadFromWinmm) {
        zTriggers = PadZCarriesTriggers(0) || (g_cfg.sonyLayout && SonyPadActive());
        if (PadIsRawSony(0)) {  // SonyLayout=0: the game gets the Sony order; the menu reads it in the Xbox order (sony_layout.cpp)
            static const u8 from[10] = {1, 2, 0, 3, 4, 5, 8, 9, 10, 11};
            DWORD x = 0;
            for (int i = 0; i < 10; ++i)
                if ((b >> from[i]) & 1) x |= 1u << i;
            if (b & (1u << 6)) x |= kXbLT;
            if (b & (1u << 7)) x |= kXbRT;
            b = x;
            zTriggers = false;  // Z is the right stick there
        }
    }
    raw->valid = true;
    raw->buttons = b;
    raw->pov = (j.dwFlags & JOY_RETURNPOV) ? j.dwPOV : 0xFFFF;
    raw->x = j.dwXpos;
    raw->y = j.dwYpos;
    raw->z = j.dwZpos;
    raw->zTriggers = zTriggers;
    return true;
}

template <class F>
bool Hook(const char* dll, const char* name, F* real, void* replacement) {
    void** slot = FindImportSlot(GetModuleHandleW(nullptr), dll, name);
    if (!slot) return false;
    *real = (F)*slot;
    return SwapImport(slot, replacement, nullptr);
}

}  // namespace

bool IsOpen() { return g_open; }
bool PadHintsShown() { return g_open && g_padMode; }
void DrawPadLine(const wchar_t* text, float x, float y, u32 argb, int px, float maxW) { DrawPadLineImpl(text, x, y, argb, px, maxW); }

// Replaces the game's wheel reader: while the menu is open the wheel steps go to the menu and the game sees none
// (otherwise the weapon changes behind the menu).
short __attribute__((cdecl)) WheelReadHook() {
    volatile short* word = (volatile short*)(uintptr_t)0x014AFFC0;
    short w = *word;
    if (g_open && w) {
        *word = 0;
        int steps = w / 120;
        if (steps == 0) steps = w > 0 ? 1 : -1;
        g_wheel = g_wheel + steps;
        return 0;
    }
    return g_open ? (short)0 : w;
}

bool Install() {
    int n = 0;
    MergePages();
    if (PatchJump(kSiteWheelRead.va, kSiteWheelRead.bytes, kSiteWheelRead.len, (const void*)WheelReadHook)) ++n;
    if (Hook("USER32.dll", "GetAsyncKeyState", &r_GetAsyncKeyState, (void*)H_GetAsyncKeyState)) ++n;
    if (Hook("USER32.dll", "GetKeyboardState", &r_GetKeyboardState, (void*)H_GetKeyboardState)) ++n;
    if (Hook("USER32.dll", "GetCursorPos", &r_GetCursorPos, (void*)H_GetCursorPos)) ++n;
    if (Hook("USER32.dll", "SetCursorPos", &r_SetCursorPos, (void*)H_SetCursorPos)) ++n;
    if (Hook("USER32.dll", "ShowCursor", &r_ShowCursor, (void*)H_ShowCursor)) ++n;
    if (Hook("WINMM.dll", "joyGetPosEx", &r_joyGetPosEx, (void*)H_joyGetPosEx)) ++n;
    if (g_cfg.menu) Log("menu: F10 menu ready (%d input hooks)", n);
    else Log("menu: off (Menu Enabled=0); only the input hooks are installed (%d), for the pad", n);
    return true;
}

namespace {
// The menu has just been opened (before its first HandleInput): whether the pad drives it this time, and the pad and the mouse
// start afresh, so the pair that opened it, still held, does not close it again at once.
void PadOpening(bool byPad) {
    g_padOn = g_cfg.menuPadControl && r_joyGetPosEx;  // without the hook the game would see the pad behind the menu
    static bool said = false;
    if (g_cfg.menuPadControl && !r_joyGetPosEx && !said) {
        said = true;
        Log("menu: gamepad control off: the joyGetPosEx hook is not in place, the game would see the pad behind the menu");
    }
    MenuPadRestart(g_pad);
    g_padMode = byPad && g_padOn;
    g_mouseKnown = false;
    g_pairHeld = g_pairTap = 0;  // a half of the pair held back when F10 opened the menu is not shown to the game later
}

// Everything that must happen when the menu opens or closes, whatever opened or closed it.
void OpenChanged(bool foreground) {
    g_editing = false;  // half-typed digits or a pending question must not wait for the next opening (and hit item 0)
    g_chipRow = -1;     // the button highlight starts on the first button again
    g_confirm = 0;
    g_helpOpen = false;
    g_dragging = false;
    g_peeking = false;  // every opening starts with the panel in sight
    g_peekAmount = 0.f;
    FlushUnsaved();
    Log("menu: %s", g_open ? "opened" : "closed");
    if (g_open) {
        LoadDefaults();  // once, at the first opening: the ini and the pages are ready by now
        g_swallowEsc = false;
        g_swallowRmb = false;
        g_openedAt = MotionStamp();  // the fade-in starts now; the highlights start on their rows (GlideTo)
        g_motionFrame += 2;
        g_flipItem = nullptr;
        static bool motionLogged = false;
        if (MotionOn() && !motionLogged) {
            motionLogged = true;
            Log("menu: animations on (fade-in 140 ms, glide about 90 ms, switch 120 ms)");
        }
        return;
    }
    if (g_cfg.hideCursor) SetCursor(nullptr);
    if (Down(VK_ESCAPE)) g_swallowEsc = true;
    if (Down(VK_RBUTTON)) g_swallowRmb = true;
    // The pad buttons held now (B or the pair that closed the menu) would reach the game as new presses: B is its cancel, Start
    // its pause. They stay the game's once let go (PadForGame). A request to open the menu seen meanwhile is stale.
    if (g_padOn) InterlockedExchange(&g_padSwallow, (LONG)g_padGameButtons);
    g_padGameButtons = 0;
    InterlockedExchange(&g_padOpenWanted, 0);
    // The game turns the mouse movement into camera movement as the distance from the window centre (and puts the cursor
    // back there); while the menu was open it was told the cursor rests in the centre. Put the real one there too, or the
    // first frame after closing reads the whole distance to where the menu was clicked as a camera turn.
    HWND w = GameWindowHandle();
    RECT r;
    if (foreground && w && GetWindowRect(w, &r)) SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);  // our own import
}
}  // namespace

static volatile bool g_wantPhoto = false;
bool PhotoPageOpen() { return g_open && g_page == kPhotoPage; }
void OpenPhotoPage() { g_wantPhoto = true; }

// The hint at the start: a small card in the top right corner that the menu is on F10. It shows for 15 seconds of the first
// frames (fading out in the last one) and goes away for good once the menu was opened. [Menu] StartHint=0 turns it off.
bool g_everOpened = false;
DWORD g_hintStart = 0;

bool g_hintVisible = false;  // the card is on screen this frame: F9 on it updates

void StartHint(void* device) {
    g_hintVisible = false;
    if (!g_cfg.startHint || g_everOpened) return;
    const DWORD now = GetTickCount();
    if (!g_hintStart) g_hintStart = now ? now : 1;
    const DWORD t = now - g_hintStart;
    const int st = UpdateState();
    // with a newer release the card stays 40 seconds, and as long as the download runs
    const DWORD kShow = st == 2 || st == 3 ? t + 2000 : st ? 40000 : 15000, kFade = 1000;
    if (t >= kShow) {
        g_everOpened = true;  // shown once per start
        return;
    }
    float a = t > kShow - kFade ? (float)(kShow - t) / (float)kFade : 1.0f;
    if (t < 250) a = (float)t / 250.0f;  // a quick fade in
    auto alpha = [a](u32 c) { return ((u32)((float)(c >> 24) * a) << 24) | (c & 0xFFFFFF); };
    if (!OverlayBegin(device)) return;
    g_hintVisible = true;
    const bool en = g_cfg.language == 1;
    const wchar_t* title = L"UltimateFIXDP";
    // One card. Without an update: "settings ... press F10". With one: the update line and F9, F10 as a smaller hint.
    wchar_t line[128], sub[128], settings[64];
    const wchar_t* key = L"F10";
    const wchar_t* press = en ? L"press" : L"натисніть";
    u32 lineColor = kMuted;
    sub[0] = 0;
    // With a pad in use (the game read one in the last 3 seconds) the card also names the pad's pair that opens the menu.
    const DWORD seen = g_padSeenAt;
    const bool pad = g_cfg.menuPadControl && r_joyGetPosEx && seen && now - seen < 3000;
    if (pad) wsprintfW(settings, en ? L"F10 or %s: settings" : L"F10 або %s: налаштування", PadWord(3));
    else lstrcpyW(settings, en ? L"F10 settings" : L"F10 налаштування");
    if (st == 1) {
        wsprintfW(line, en ? L"Version %S is out: press F9 to update" : L"Вийшла версія %S: натисніть F9, щоб оновити", UpdateVersion());
        wsprintfW(sub, en ? L"the game restarts by itself  ·  %s" : L"гра перезапуститься сама  ·  %s", settings);
        key = L"F9";
        press = en ? L"update" : L"оновити";
        lineColor = kText;
    } else if (st == 2) {
        wsprintfW(line, en ? L"Downloading version %S..." : L"Завантажується версія %S...", UpdateVersion());
        lstrcpyW(sub, en ? L"a few seconds" : L"кілька секунд");
        key = nullptr;
    } else if (st == 3) {
        wsprintfW(line, en ? L"Version %S is installed, restarting" : L"Версію %S встановлено, перезапуск", UpdateVersion());
        key = nullptr;
    } else if (st == 4) {
        lstrcpyW(line, en ? L"The update failed: F9 tries again" : L"Оновлення не вдалося: F9 спробує ще раз");
        wsprintfW(sub, en ? L"details in UltimateFix\\ufx.log  ·  %s" : L"подробиці в UltimateFix\\ufx.log  ·  %s", settings);
        key = L"F9";
        press = en ? L"retry" : L"ще раз";
        lineColor = kError;
    } else {
        lstrcpyW(line, en ? L"Settings, photo mode and mods" : L"Налаштування, фоторежим і моди");
        if (pad) wsprintfW(sub, en ? L"on the gamepad: %s together" : L"на ґеймпаді: %s разом", PadWord(3));
    }
    // The frame statistics panel (overlay.cpp) owns the top right corner down to 204; with it on, the card goes under it.
    const float w = 520, h = sub[0] ? 104 : 84, x = 1280 - w - 28, y = g_cfg.frameStats ? 216.f : 28.f;
    // A raised card with a hairline border and rounded corners, as the cards of the site; the red mark sits inside it
    FillRound(x + 3, y + 4, w, h, 10, alpha(0x60000000));  // shadow
    FillRoundFramed(x, y, w, h, 10, alpha(kRaised), alpha(kLine2));
    FillRound(x + 10, y + 16, 3, h - 32, 1.5f, alpha(kAccent));
    wchar_t ver[64];
    wsprintfW(ver, L"%S", kVersion);
    const float tw = DrawText(title, x + 22, y + 14, alpha(kWhite), 20, kSemibold);
    DrawText(ver, x + 22 + tw + 10, y + 19, alpha(kFaint), 14);
    const float textW = w - 22 - (key ? 64 + 18 + 16 : 18);
    DrawFitted(line, x + 22, y + 48, alpha(lineColor), 15, textW);
    if (sub[0]) DrawFitted(sub, x + 22, y + 74, alpha(kFaint), 13, textW);
    if (key) {  // the key cap on the right, its label above it
        const float kw = 64, kh = 40, kx = x + w - kw - 18, ky = y + (h - kh) / 2 + 8;
        const float pw = TextWidth(press, 12);
        DrawText(press, kx + (kw - pw) / 2, ky - 20, alpha(kFaint), 12);
        // a key cap like the site's <kbd>: a hairline border and a lip two units deep under it
        FillRound(kx, ky + 2, kw, kh, 7, alpha(kLine2));
        FillRoundFramed(kx, ky, kw, kh, 7, alpha(kHover), alpha(kLine2));
        const float fw = TextWidth(key, 20);
        DrawText(key, kx + (kw - fw) / 2, ky + 9, alpha(kWhite), 20);
    }
    OverlayEnd();
}

namespace {
// ---- The photo page's peek and the keys of the flying camera ---------------------------------------------------------
// The peek moves at the opening's pace: kFadeSeconds there and back with Animations on, at once with them off.
void PeekFrame() {
    const float target = g_peeking ? 1.f : 0.f;
    if (!MotionOn()) {
        g_peekAmount = target;
        return;
    }
    const float step = g_motionDt / kFadeSeconds;
    if (g_peekAmount < target) g_peekAmount = g_peekAmount + step > target ? target : g_peekAmount + step;
    else if (g_peekAmount > target) g_peekAmount = g_peekAmount - step < target ? target : g_peekAmount - step;
}
float PeekEased() { return g_peekAmount * g_peekAmount * (3.f - 2.f * g_peekAmount); }  // smoothstep: soft at both ends

// While the panel is away, one small pill at the bottom centre says how to get it back, so a held button never looks as if
// the menu had closed. It fades in as the panel fades out.
void PeekChip(float amount) {
    SetOverlayFade(amount, 0.f);
    const wchar_t* text = g_cfg.language == 1 ? L"Release to bring the menu back" : L"Відпустіть, щоб повернути меню";
    const float tw = TextWidth(text, 13), w = tw + 36, h = 30, x = SnapWhole((1280 - w) / 2), y = 720 - 28 - h;
    FillRoundFramed(x, y, w, h, h / 2, kRaised, kLine2);
    DrawText(text, x + 18, y + 7, kText, 13);
}

// One hint of the strip: up to two keys as caps (and the pad's pair), then what they do.
struct StripHint {
    wchar_t key1[8], key2[8];
    bool padPair;  // the pad's open pair after the keys (the only pad buttons that work while the camera flies)
    const wchar_t* label;
};

// Draws (or with draw false only measures) the hints in a row from x, with a hairline before hint `split` (the flying keys
// first, then the actions). Returns the width.
float StripRow(const StripHint* hints, int count, int split, float x, float y, int px, bool draw) {
    const float x0 = x;
    for (int i = 0; i < count; ++i) {
        const StripHint& h = hints[i];
        if (i == split && i > 0) {
            if (draw) FillRect(x - 3, y - 1, 1, (float)px + 3, kLine2);
            x += 16;
        }
        if (h.key1[0]) x += PadCap(h.key1, false, x, y, px, draw) + 4;
        if (h.key2[0]) x += PadCap(h.key2, false, x, y, px, draw) + 4;
        if (h.padPair) x += PadToken(L"OPEN", 4, x, y, px, draw) + 4;
        x += 2;
        x += draw ? DrawText(h.label, x, y, kMuted, px) : TextWidth(h.label, px);
        if (i + 1 < count) x += 18;
    }
    return x - x0;
}

// With the menu closed and the free camera on, a quiet strip at the bottom centre names the keys for 6 s: nothing on screen
// said WASD, F11 or how to get back. It comes back for 6 s whenever the menu closes over the flying camera, the camera is
// switched on, or an F key is pressed (F4 showing the interface again, F11 ...). Not while the interface is hidden: that is
// a picture being composed. It is drawn in OnFrame, after this frame's screenshot was taken (display.cpp runs ScreenshotFrame
// first), so it never lands in a picture.
DWORD g_stripFrom = 0;  // GetTickCount when it was (re)started, 0 = not shown

void PhotoKeysStrip(void* device, bool foreground, bool menuJustClosed) {
    static bool wasActive = false;
    static unsigned heldF = 0;  // F1..F12 held in the last frame
    const bool active = g_cfg.photoActive != 0;
    unsigned f = 0;
    for (int i = 0; foreground && i < 12; ++i)
        if (Down(VK_F1 + i)) f |= 1u << i;
    const bool fPressed = (f & ~heldF) != 0;
    heldF = f;
    const DWORD now = GetTickCount();
    if (active && (!wasActive || menuJustClosed || fPressed)) g_stripFrom = now ? now : 1;
    wasActive = active;
    if (!active || g_cfg.photoHud || !g_stripFrom) return;
    constexpr DWORD kShow = 6000, kFadeIn = 200, kFadeOut = 600;
    const DWORD t = now - g_stripFrom;
    if (t >= kShow) {
        g_stripFrom = 0;
        return;
    }
    const float a = t < kFadeIn ? (float)t / (float)kFadeIn : t > kShow - kFadeOut ? (float)(kShow - t) / (float)kFadeOut : 1.f;
    const bool en = g_cfg.language == 1;
    StripHint hints[8];
    int n = 0;
    auto add = [&](const wchar_t* k1, const wchar_t* k2, bool pair, const wchar_t* label) {
        StripHint& h = hints[n++];
        lstrcpynW(h.key1, k1 ? k1 : L"", 8);
        lstrcpynW(h.key2, k2 ? k2 : L"", 8);
        h.padPair = pair;
        h.label = label;
    };
    add(L"WASD", nullptr, false, en ? L"fly" : L"летіти");
    add(L"Space", L"C", false, en ? L"up / down" : L"вгору / вниз");
    add(L"Shift", nullptr, false, en ? L"faster" : L"швидше");
    add(L"Ctrl", nullptr, false, en ? L"slower" : L"повільніше");
    int split = n;
    wchar_t key[8];
    if (g_cfg.screenshotKey && KeyLabel(g_cfg.screenshotKey, key)) add(key, nullptr, false, en ? L"picture" : L"знімок");
    if (g_cfg.freeCam && g_cfg.freeCamHudKey && KeyLabel(g_cfg.freeCamHudKey, key)) add(key, nullptr, false, en ? L"interface" : L"інтерфейс");
    // the pad's pair too while a pad is in use (the game read one in the last 3 seconds), as the start card does
    const DWORD seen = g_padSeenAt;
    const bool pad = g_cfg.menuPadControl && r_joyGetPosEx && seen && now - seen < 3000;
    add(L"F10", nullptr, pad, en ? L"menu" : L"меню");
    add(L"F6", nullptr, false, en ? L"leave" : L"вийти");
    // 13 px when it fits the screen, else 12, else the slower and faster keys go first
    int px = 13;
    const float maxW = 1280 - 2 * 24 - 2 * 18;
    float w = StripRow(hints, n, split, 0, 0, px, false);
    if (w > maxW) w = StripRow(hints, n, split, 0, 0, px = 12, false);
    while (w > maxW && split > 2) {  // keep WASD and Space / C
        for (int i = split - 1; i + 1 < n; ++i) hints[i] = hints[i + 1];
        --split;
        --n;
        w = StripRow(hints, n, split, 0, 0, px, false);
    }
    if (!OverlayBegin(device)) return;
    SetOverlayFade(a, 0.f);
    const float h = (float)px + 21, x = SnapWhole((1280 - w) / 2 - 18), y = 720 - 24 - h;
    FillRoundFramed(x, y, w + 36, h, 10, Alpha(kRaised, 0xC0), kLine2);
    StripRow(hints, n, split, x + 18, y + 10, px, true);
    SetOverlayFade(1.f, 0.f);
    OverlayEnd();
}
}  // namespace

void OnFrame(void* device) {
    MergePages();
    static bool geometryLoaded = false;
    if (!geometryLoaded) {
        geometryLoaded = true;
        kPx = (float)g_cfg.menuX; kPy = (float)g_cfg.menuY; kPw = (float)g_cfg.menuW; kPh = (float)g_cfg.menuH;
    }
    UpdateLayout();
    HWND w = GameWindowHandle();
    if (!w) return;
    bool foreground = GetForegroundWindow() == w;
    const bool wasOpen = g_open;
    if (g_wantPhoto) {
        g_wantPhoto = false;
        g_open = true;
        SelectPage(kPhotoPage);
    }
    if (foreground && Pressed(VK_F10)) {
        g_open = !g_open;
        g_sel = 0;
        if (g_open) {  // the rows are built when a page is chosen: on the very first opening nothing had chosen one yet
            g_scroll = 0;
            BuildRows();
        }
    }
    // The pad's pair, seen by the hook in what the game was about to get (PadForGame); not for a window in the background
    bool byPad = false;
    if (InterlockedExchange(&g_padOpenWanted, 0) && foreground && !g_open && g_cfg.menuPadControl) {
        g_open = true;
        byPad = true;
        g_sel = 0;
        g_scroll = 0;
        BuildRows();
    }
    if (g_open && !wasOpen) PadOpening(byPad);
    // F9: the update, only while the start card shows it or the About page is open (never a stray key in the middle of a game)
    if (foreground && Pressed(VK_F9) && (g_hintVisible || (g_open && GetTickCount() - g_aboutDrawnAt < 500))) UpdateStart();
    if (g_open) SyncDifficultyFromGame();
    if (g_open) EnsureCursorVisible(!g_padMode && !g_peeking);  // no arrow over the picture of a peek
    if (g_open) HandleInput(foreground);
    if (g_open != wasOpen) OpenChanged(foreground);  // F10, Esc or the photo mode key
    if (g_swallowEsc && !Down(VK_ESCAPE)) g_swallowEsc = false;  // a later Esc press is the game's again
    if (g_swallowRmb && !Down(VK_RBUTTON)) g_swallowRmb = false;
    RememberKeys();
    BatchTick();
    if (g_open) g_everOpened = true;
    if (!g_open) {
        StartHint(device);
        PhotoKeysStrip(device, foreground, wasOpen);
        return;
    }

    if (!OverlayBegin(device)) {
        Log("menu: could not set up drawing, closing");
        g_open = false;
        OpenChanged(foreground);
        return;
    }
    MotionFrame();
    PeekFrame();
    // The peek fades the panel with the same alpha as the opening, so the two never fight (an opening peek just stays hidden).
    const float peek = PeekEased();
    g_openAlpha = MotionProgress(g_openedAt, kFadeSeconds) * (1.f - peek);
    g_openRise = (1.f - MotionProgress(g_openedAt, kFadeSeconds)) * kRiseUnits;
    if (peek < 1.f) {  // fully faded it is not drawn at all
        SetOverlayFade(g_openAlpha, g_openRise);
        Draw();
    }
    if (peek > 0.f) PeekChip(peek);
    SetOverlayFade(1.f, 0.f);  // the notice and the performance panels are drawn after the menu in the same frame
    OverlayEnd();
}

void PhotoLookReset() {
    ResetLook();
    FaceForget();
}
bool GameLookValues(int* out) { return GameLookValuesImpl(out); }

}  // namespace ui
}  // namespace ufx
