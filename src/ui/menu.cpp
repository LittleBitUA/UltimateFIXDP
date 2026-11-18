// The F10 settings menu and the blocking of the game's own input while it is open.
//
// Layout (1280x720 coordinates): a sidebar with the sections on the left, the settings of the chosen section on the right
// (grouped, with switches and sliders, scrollable), a description of the selected setting at the bottom.
// Keys: F10 opens/closes, Up/Down selects, Left/Right changes (on a row of action buttons it only moves the highlight),
// Enter/Space toggles or presses the highlighted button, Tab / PgUp / PgDn switches the section, Esc closes. The mouse
// works too: click a section, a switch, a slider, a button or the value; the wheel scrolls.
// A change of a setting that has an ini key is written to UltimateFix.ini at once (a slider when the drag ends). Rows
// without a key (most of the photo mode and the cheats) last for the session only.
#include "ui.h"
#include "../sites.h"
#include <mmsystem.h>

namespace ufx {
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
};

const Tr kGlyphTr[4] = {{L"Вимк.", L"Off"}, {L"Авто", L"Auto"}, {L"Xbox", L"Xbox"}, {L"PlayStation", L"PlayStation"}};
const Tr kMapOrientTr[3] = {{L"Обертається", L"Rotates"}, {L"Північ угорі", L"North up"}, {L"Південь угорі", L"South up"}};
const Tr kDifficultyTr[3] = {{L"Легка", L"Easy"}, {L"Звичайна", L"Normal"}, {L"Важка", L"Hard"}};
const Tr kPhysStepsTr[4] = {{L"Як у грі (до 4)", L"As the game (up to 4)"}, {L"1 крок", L"1 step"}, {L"2 кроки", L"2 steps"}, {L"3 кроки", L"3 steps"}};
const Tr kPacingTr[4] = {{L"Вимк.", L"Off"}, {L"Ліміт 60 к/с", L"Limit 60 fps"}, {L"30 к/с (як Xbox)", L"30 fps (like Xbox)"}, {L"60 к/с, цілі кроки", L"60 fps, whole steps"}};
const Tr kTriggerTestTr[7] = {{L"Вимк.", L"Off"}, {L"Клік пістолета", L"Pistol click"}, {L"Важкий клік", L"Heavy click"},
                              {L"Автомат: вібрація", L"Machine gun: vibration"}, {L"Опір", L"Resistance"}, {L"Повільна вібрація", L"Slow rumble"},
                              {L"Обидва триґери", L"Both triggers"}};
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
void FormatScale(int v, wchar_t* out) {
    int base = g_cfg.outputWidth ? g_cfg.outputWidth : 1280;
    int baseH = g_cfg.outputWidth ? WantedHeight(base) : 720;
    wsprintfW(out, L"%d%%  ·  %d×%d", v, base * v / 100, baseH * v / 100);
}

const int kScalePresets[4] = {100, 150, 200, 300};
const Tr kScaleNames[4] = {{L"1×", L"1x"}, {L"1,5×", L"1.5x"}, {L"2×", L"2x"}, {L"3×", L"3x"}};

// ---- Quick quality presets: one click sets the whole picture block (some of it applies after a restart).
int g_qualityPick = 0;
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
    PutValue(&g_cfg.framePacing, L"Time", L"FramePacing", 1);
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
     {L"Курсор не показується над вікном гри. У меню F10 він видимий завжди.", L"The cursor is hidden over the game window. In the F10 menu it is always visible."}, NOTR},
    {{L"Вимкнути масштабування Windows (DPI)", L"Disable Windows scaling (DPI)"}, &g_cfg.dpiAware, kBool, 0, 1, 1, true, L"Display", L"DpiAware", nullptr, nullptr,
     {L"Потрібно на екранах із масштабом 125% і більше, інакше вікно виходить розмитим.", L"Needed on screens scaled 125% or more, otherwise the window is blurry."},
     {L"Система", L"System"}},
    {{L"Журнал показу (діагностика)", L"Display log (diagnostics)"}, &g_cfg.displayDebug, kBool, 0, 1, 1, false, L"Display", L"Debug", nullptr, nullptr,
     {L"У режимі без рамки пише в журнал перші кадри й кожну зміну способу показу (з розміром буфера), а завжди: розміри вікна протягом 4 секунд після кожного натискання Esc. Для пошуку проблем із показом.", L"In the borderless window, writes the first frames and every change of the way the picture is presented (with the buffer size) to the log, and always the window geometry for 4 seconds after each Esc press. For tracking down display problems."}, NOTR},
    {{L"Прозорість панелі меню, %", L"Menu panel opacity, %"}, &g_cfg.menuOpacity, kInt, 20, 100, 5, false, L"Menu", L"Opacity", nullptr, nullptr,
     {L"Чим менше, тим більше крізь меню видно гру. Текст лишається чітким. Змінюється одразу.", L"The lower, the more of the game shows through the menu. The text stays sharp. Changes at once."},
     {L"Вигляд меню", L"Look of the menu"}},
    {{L"Панель статистики кадру", L"Frame statistics panel"}, &g_cfg.frameStats, kBool, 0, 1, 1, false, L"Debug", L"FrameStats", nullptr, nullptr,
     {L"Окрема панель справа вгорі, як у Windows: всього, гра (процесор), з них фізика, Present (драйвер, очікування відеокарти й екрана), найгірший кадр за 30 кадрів, час проходів мода, FPS, частота екрана й графік останніх 120 кадрів. Червоні стовпчики: кадри, що помітно довші за середній. Для пошуку лагів.",
      L"A separate panel top right, like the Windows one: total, game (CPU), of which physics, Present (driver, waiting for the video card and screen), the worst frame of 30, the time of the mod's passes, FPS, screen refresh rate and a graph of the last 120 frames. Red bars: frames clearly longer than average. For finding lag."}, {L"Діагностика", L"Diagnostics"}},
    {{L"Показувати FPS і час кадру", L"Show FPS and frame time"}, &g_cfg.perfOverlay, kBool, 0, 1, 1, false, L"Debug", L"PerfOverlay", nullptr, nullptr,
     {L"У кутку: FPS, час кадру, скільки Present чекає (чим більше, тим сильніше впирається відеокарта) і скільки відеокарта витрачає на проходи мода (SSAO, фільтри). Для пошуку, що гальмує.", L"In the corner: FPS, frame time, how long Present waits (the more, the more the video card is the limit) and how much video time the mod's passes take (SSAO, filters). For finding what slows things down."}, {L"Діагностика", L"Diagnostics"}},
    RESET_ITEM
};
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
     {L"Суперсемплінг сцени: 1× = рівно розмір виходу, 2× = вдвічі більше за шириною й висотою (при виході 1920 це точно 2×2, найчистіше), 3× дуже важко. Поруч із назвою видно, який розмір сцени вийде.\nСцена обмежена 7680 пікселями завширшки. Іншого множника: клік по останній кнопці й число у відсотках (100–400).",
      L"Supersampling of the scene: 1x = exactly the output size, 2x = twice the width and height (with a 1920 output exactly 2x2, the cleanest), 3x is very heavy. Next to the name you can see the size of the scene that comes out.\nThe scene is limited to 7680 pixels wide. Another multiple: click the last button and type a percentage (100-400)."},
     {L"Множник від виходу", L"Multiple of the output"}, kScalePresets, 4, kScaleNames, FormatScale, false, RunningScene},
    {{L"Збільшувати буфери розмиття й свічення", L"Enlarge blur and bloom buffers"}, &g_cfg.scalePostBuffers, kBool, 0, 1, 1, true, L"Graphics", L"ScalePostBuffers", nullptr, nullptr,
     {L"Розмиття та свічення (bloom) рахуються в тій самій роздільності, що й сцена, а не в оригінальній.", L"Blur and bloom are computed at the resolution of the scene, not the original one."}, NOTR},
    {{L"Розмір мап тіней, % (100 = оригінал)", L"Shadow map size, % (100 = original)"}, &g_cfg.shadowScale, kInt, 100, 400, 100, true, L"Graphics", L"ShadowScale", nullptr, nullptr,
     {L"Чіткість тіней: 200 = 1024/2048, 400 = 2048/4096. Більше значення: більше відеопам'яті.", L"Shadow sharpness: 200 = 1024/2048, 400 = 2048/4096. More: more video memory."},
     {L"Тіні", L"Shadows"}},
    {{L"Глибина тіней 32-біт", L"32-bit shadow depth"}, &g_cfg.shadowPrecision, kBool, 0, 1, 1, true, L"Graphics", L"ShadowPrecision", nullptr, nullptr,
     {L"Прибирає смуги й «танцюючі» краї тіней (як у DPfix і ZachFix). Якщо тіні зникли чи стали жорсткими, вимкніть.", L"Removes banding and dancing shadow edges (as DPfix and ZachFix do). If shadows vanish or turn hard, turn it off."}, NOTR},
    {{L"Зберігати розмір розмиття при великій роздільності", L"Keep blur size at high resolution"}, &g_cfg.keepBlurLook, kBool, 0, 1, 1, true, L"Graphics", L"KeepBlurLook", nullptr, nullptr,
     {L"Свічення, глибина різкості й розмиття лишаються такими ж за розміром на екрані, як у 720p. Без цього при RenderScale вони стають вужчими. Потребує перезапуску.", L"Bloom, depth of field and blur keep the same size on screen as at 720p. Without it they get narrower with RenderScale. Needs a restart."},
     {L"Чіткість і вигляд", L"Sharpness and look"}},
    {{L"Затінення в кутках (SSAO), %", L"Ambient occlusion (SSAO), %"}, &g_cfg.ambientOcclusion, kInt, 0, 100, 10, false, L"Graphics", L"AmbientOcclusion", nullptr, nullptr,
     {L"Притемнює щілини й місця, де предмети стикаються. Рахується з буфера глибини гри. Змінюється одразу. Починайте з 40–60.", L"Darkens creases and contact areas. Computed from the game's depth buffer. Changes at once. Start with 40–60."},
     {L"Чіткість і вигляд", L"Sharpness and look"}},
    {{L"Радіус затінення, пікселів", L"Occlusion radius, pixels"}, &g_cfg.aoRadius, kInt, 2, 40, 2, false, L"Graphics", L"AoRadius", nullptr, nullptr,
     {L"Наскільки далеко від кута шукається перекриття (при 720p; на більших роздільностях росте пропорційно).", L"How far from a corner occlusion is searched (at 720p; scales up with the resolution)."}, NOTR},
    {{L"Згладжування країв (FXAA), %", L"Edge smoothing (FXAA), %"}, &g_cfg.antialias, kInt, 0, 100, 10, false, L"Graphics", L"Antialias", nullptr, nullptr,
     {L"Прибирає «драбинки» на краях. Гра не має власного згладжування. Змінюється одразу; поєднуйте з різкістю 20–30, бо згладжування трохи розмиває.", L"Removes jagged edges. The game has no antialiasing of its own. Changes at once; combine with sharpening 20–30, since smoothing blurs slightly."},
     {L"Чіткість і вигляд", L"Sharpness and look"}},
    {{L"Різкість фінальної картинки, %", L"Sharpening of the final picture, %"}, &g_cfg.sharpen, kInt, 0, 100, 10, false, L"Graphics", L"Sharpen", nullptr, nullptr,
     {L"Підвищення різкості готового кадру. Починайте з 30–50, більше дає ореоли. Змінюється одразу.", L"Sharpens the finished frame. Start with 30–50, more gives halos. Changes at once."},
     {L"Чіткість і вигляд", L"Sharpness and look"}},
    {{L"Анізотропна фільтрація текстур (макс.)", L"Anisotropic texture filtering (max)"}, &g_cfg.anisotropy, kInt, 1, 16, 1, false, L"Graphics", L"Anisotropy", ApplyGraphicsTweaks, nullptr,
     {L"Чіткість текстур під кутом (дорога, стіни). Гра за замовчуванням 8.", L"Sharpness of textures at an angle (road, walls). The game uses 8."}, NOTR},
    {{L"Експозиція як на Xbox", L"Xbox exposure"}, &g_cfg.xboxExposure, kBool, 0, 1, 1, false, L"Graphics", L"XboxExposure", ApplyGraphicsTweaks, nullptr,
     {L"PC-версія затемнює картинку на 15% (множник 0,85), консоль ні. Увімкнення знімає множник.", L"The PC version darkens the picture by 15% (a 0.85 factor), the console did not. Turning it on removes the factor."}, NOTR},
    {{L"Виправлення зсуву відбиттів (експеримент)", L"Reflection offset fix (experiment)"}, &g_cfg.reflectionBiasFix, kBool, 0, 1, 1, false, L"Graphics", L"ReflectionBiasFix", ApplyGraphicsTweaks, nullptr,
     {L"Матриця зсуву відбиттів двічі брала ширину замість ширини й висоти (помилка PC-порту), тож відбиття в калюжах і склі були трохи зсунуті по вертикалі. Виправлення знайдене нами в дизасемблері гри; у грі ще не підтверджене. Змінюється одразу.", L"The reflection offset matrix took the width twice instead of width and height (a bug of the PC port), so reflections in puddles and glass were shifted slightly vertically. The fix was found by us in the game disassembly; not yet confirmed in the game. Changes at once."}, NOTR},
    {{L"Іконки кнопок ґеймпада", L"Gamepad button icons"}, &g_cfg.glyphTheme, kInt, 0, 3, 1, true, L"Graphics", L"GlyphTheme", nullptr, kGlyphTr,
     {L"Іконки підказок при грі з ґеймпадом. Авто: PlayStation для DualSense, інакше Xbox.\nПотрібні файли в UltimateFix\\glyphs\\gamepad.", L"Hint icons when playing with a gamepad. Auto: PlayStation for a DualSense, else Xbox.\nThe files must be in UltimateFix\\glyphs\\gamepad."},
     {L"Інтерфейс", L"Interface"}},
    {{L"Півпіксельний зсув 2D", L"2D half-pixel offset"}, &g_cfg.pixelOffsetFix, kBool, 0, 1, 1, false, L"Graphics", L"FixPixelOffset", nullptr, nullptr,
     {L"Для чіткого 2D у вищій роздільності: підтискає сусідні спрайти атласу. Якщо інтерфейс поплив, вимкніть. Діє лише тоді, коли задано вихідну роздільність (не 1280×720). Змінюється одразу.", L"For sharp 2D at a higher resolution: stops neighbouring atlas sprites from bleeding in. If the interface drifts, turn it off. Works only when an output resolution is set (not 1280×720). Changes at once."}, NOTR},
    RESET_ITEM
};
Item g_pageWorld[] = {
    {{L"Дальність відсікання, клас 3 (гра: 5000)", L"Draw distance, class 3 (game: 5000)"}, &g_cfg.farClass3, kInt, 5000, 20000, 5000, false, L"World", L"FarClass3", ApplyWorldDistance, nullptr,
     {L"Далі цієї відстані об'єкти класу 3 не малюються. Числа це одиниці світу: усе місто близько 80000 завширшки, а гра завантажує навколо вас область приблизно 10000. Тому значення більше 10000 нічого не додасть. Типово 5000 (як у грі).", L"Class 3 objects farther than this are not drawn. The numbers are world units: the whole town is about 80000 wide and the game loads an area of about 10000 around you, so a value above 10000 adds nothing. Default 5000 (as the game)."}, {L"Дальність малювання", L"Draw distance"}},
    {{L"Дальність відсікання, клас 4 (гра: 1000)", L"Draw distance, class 4 (game: 1000)"}, &g_cfg.farClass4, kInt, 1000, 10000, 1000, false, L"World", L"FarClass4", ApplyWorldDistance, nullptr,
     {L"Відстань, на якій зникають об'єкти класу 4 (двері, манекени тощо). Гра 1000; розумно 1000–5000. Вище нічого не дає: завантажена область обмежена.", L"Distance at which class 4 objects (doors, mannequins, ...) vanish. Game 1000; 1000–5000 is sensible. Above that nothing changes: the loaded area is limited."}, NOTR},
    {{L"Дальність, клас 5: будки й автомати (гра: 500)", L"Class 5: booths and vending machines (game: 500)"}, &g_cfg.farClass5, kInt, 500, 10000, 500, false, L"World", L"FarClass5", ApplyWorldDistance, nullptr,
     {L"Будки й автомати у грі зникали на 500; порт це «забув» виправити. Типово 2000; якщо вони ще зникають на очах, ставте більше (наприклад 5000).", L"Booths and vending machines vanished at 500 in the game; the port forgot to fix it. Default 2000; if they still vanish in sight, set more (5000, say)."}, NOTR},
    {{L"Радіус активації об'єктів (×, гра: 1)", L"Object activation radius (×, game: 1)"}, &g_cfg.activationScale, kInt, 1, 3, 1, false, L"World", L"ActivationScale", ApplyWorldExtra, nullptr,
     {L"Радіус, у якому об'єкти оновлюються (ШІ, анімація). Більше значення: більше навантаження.", L"Radius in which objects are updated (AI, animation). More: more load."}, {L"Об'єкти", L"Objects"}},
    {{L"Дальність деталізації моделей LOD (×, гра: 1)", L"Model detail distance, LOD (×, game: 1)"}, &g_cfg.lodScale, kInt, 1, 4, 1, false, L"World", L"LodScale", ApplyWorldExtra, nullptr,
     {L"Детальні моделі лишаються далі від камери. Більше значення: більше трикутників.", L"Detailed models stay farther from the camera. More: more triangles."}, NOTR},
    {{L"Повна модель далеких об'єктів (×, гра: 1)", L"Full model of far objects (×, game: 1)"}, &g_cfg.alt3dScale, kInt, 1, 3, 1, false, L"World", L"Alternate3DScale", nullptr, nullptr,
     {L"Проти появи будинків і предметів перед носом: далекі об'єкти, які гра малює спрощеними, отримують повну модель з більшої відстані. 2 це ще одне кільце клітинок, 3 вся завантажена область. Потребує памʼяті (ufx_laa.exe). ЕКСПЕРИМЕНТАЛЬНО: далекі об'єкти можуть мерехтіти; тоді поставте 1. Змінюється одразу.", L"Against buildings and props popping in right in front of you: far objects that the game draws in a simplified form get their full model from farther away. 2 is one more ring of cells, 3 the whole loaded area. Needs memory (ufx_laa.exe). EXPERIMENTAL: far objects may flicker; if so, set 1. Changes at once."}, NOTR},
    {{L"Повна деталізація зовнішніх клітинок", L"Full detail in the outer cells"}, &g_cfg.highDetail, kBool, 0, 1, 1, false, L"World", L"HighDetail", ApplyWorldExtra, nullptr,
     {L"Зовнішнє кільце світу вантажиться з повним набором ресурсів. Потребує багато пам'яті (ufx_laa.exe).", L"The outer ring of the world loads the full resource set. Needs a lot of memory (ufx_laa.exe)."}, NOTR},
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
    {{L"Допуск у межах кута прицілу (лише при збоях)", L"Aim angle tolerance (only for glitches)"}, &g_cfg.aimTolerance, kBool, 0, 1, 1, true, L"Gameplay", L"AimTolerance", nullptr, nullptr,
     {L"Порівняння меж кута камери з допуском. Потрібне лише коли приціл «залипає» біля межі.", L"Compares the camera angle limits with a tolerance. Only needed when the aim sticks at the limit."}, NOTR},
    RESET_ITEM
};
Item g_pageMapItems[] = {
    {{L"Мітка (F7): на мапі й у світі", L"Waypoint (F7): on the map and in the world"}, &g_cfg.waypoint, kBool, 0, 1, 1, true, L"Waypoint", L"Enabled", nullptr, nullptr,
     {L"F7 на мапі паузи ставить мітку в центрі мапи (де хрестик), F7 у світі ставить її там, де ви стоїте (або де вільна камера); Shift+F7 прибирає. У світі над місцем мітки висить напис «N Yards» ігровим шрифтом (як у самій грі), за краєм екрана він стоїть на краю зі стрілкою; у катсценах напису немає. На мапі червоний X, коли увімкнено «Усі плитки мапи» і мапа не обертається (північ або південь угорі). Те саме робить натискання тачпада DualSense. Потрібен перезапуск.",
      L"F7 on the pause map sets the waypoint at the middle of the map (the cross), F7 in the world sets it where you stand (or where the free camera is); Shift+F7 removes it. In the world an \"N Yards\" label in the game's font hangs over the place (like the game's own), off the screen it sits on the edge with an arrow; in cutscenes there is no label. On the map a red X, when 'All map tiles' is on and the map does not turn (north or south up). A DualSense touchpad click does the same. Needs a restart."},
     {L"Мітка", L"Waypoint"}},
    {{L"Мітка як ціль гри (стрілки на мінімапі)", L"Waypoint as a game objective (minimap arrows)"}, &g_cfg.waypointMinimap, kBool, 0, 1, 1, true, L"Waypoint", L"Minimap", nullptr, nullptr,
     {L"Мітка стає точкою цілі в рідному списку мапи гри: на мінімапі з'являються червоні стрілки гри на краю в бік мітки (чим далі, тим їх більше), на мапі паузи іконка цілі. Так само, як гра показує свої завдання. Потрібен перезапуск.",
      L"The waypoint becomes an objective point in the game's own map list: the minimap shows the game's red arrows on its edge towards the waypoint (more of them the farther it is), the pause map shows the objective icon. Just like the game shows its own goals. Needs a restart."},
     {L"Мітка", L"Waypoint"}},
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
    {{L"DualSense / DualShock як ґеймпад Xbox 360", L"DualSense / DualShock as an Xbox 360 pad"}, &g_cfg.sonyLayout, kBool, 0, 1, 1, false, L"Input", L"SonyLayout", nullptr, nullptr,
     {L"Переводить осі й кнопки Sony в розкладку Xbox, яку чекає гра (камера не крутиться сама).", L"Converts Sony axes and buttons to the Xbox layout the game expects (the camera no longer spins by itself)."}, NOTR},
    {{L"Бокове переміщення в бою на LB/RB", L"Combat strafe on LB/RB"}, &g_cfg.combatStrafe, kBool, 0, 1, 1, false, L"Input", L"CombatStrafe", nullptr, nullptr,
     {L"Як на консолі: у режимі прицілювання на ціль LB/RB роблять крок убік. Ці кнопки також зайняті: LB прицілювання, RB постріл (з «Приціл на L2, стрільба на R2»: L1 біг, R1 затримка дихання й захоплення цілі), тож натискання будь-якої з них під час захоплення цілі теж робить крок убік. Змінюється одразу. Експериментально.", L"As on the console: while locked on a target LB/RB sidestep. These buttons are also bound: LB aim, RB fire (with 'Aim on L2, fire on R2': L1 run, R1 hold breath and lock-on), so pressing either while locked on also sidesteps. Changes at once. Experimental."}, NOTR},
    {{L"Приціл на L2, стрільба на R2", L"Aim on L2, fire on R2"}, &g_cfg.triggerFire, kBool, 0, 1, 1, false, L"Input", L"TriggerFire", nullptr, nullptr,
     {L"Приціл на лівому триґері, стрільба на правому; біг переходить на L1, затримка дихання й захоплення цілі на R1. У машині діє розкладка гри, і так само на ґеймпаді Sony, який гра читає через Windows із вимкненим пунктом «DualSense / DualShock як ґеймпад Xbox 360». Вимкнено: приціл L1, стрільба R1, як у грі. Змінюється одразу.", L"Aim on the left trigger, fire on the right; run moves to L1, hold breath and lock-on to R1. Behind the wheel the game's layout is used, and so it is on a Sony pad read through Windows with 'DualSense / DualShock as an Xbox 360 pad' off. Off: aim L1, fire R1, as the game. Changes at once."}, NOTR},
    {{L"Поміняти місцями триґери L2/R2", L"Swap the L2/R2 triggers"}, &g_cfg.swapTriggers, kBool, 0, 1, 1, false, L"Input", L"SwapTriggers", nullptr, nullptr,
     {L"Міняє місцями L2 і R2: кнопки, вісь триґерів і ефекти DualSense. Не діє на ґеймпад Sony з вимкненим пунктом «DualSense / DualShock як ґеймпад Xbox 360» і на 4-осьові ґеймпади DirectInput, де вісь Z це правий стік.", L"Swaps L2 and R2: the buttons, the trigger axis and the DualSense effects. Not on a Sony pad with 'DualSense / DualShock as an Xbox 360 pad' off, nor on 4-axis DirectInput pads, whose Z axis is the right stick."}, NOTR},
    {{L"Ґеймпад через XInput, якщо WinMM його не бачить", L"Gamepad through XInput if WinMM cannot see it"}, &g_cfg.xinputFallback, kBool, 0, 1, 1, false, L"Input", L"XInputFallback", nullptr, nullptr,
     {L"Страховка для Steam Input, DS4Windows, Xbox-ґеймпада.", L"A fallback for Steam Input, DS4Windows, an Xbox pad."}, NOTR},
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
    {{L"Триґери як аналоговий вхід авто (експеримент)", L"Triggers as analog car input (experiment)"}, &g_cfg.analogTriggers, kBool, 0, 1, 1, false, L"Input", L"AnalogTriggers", nullptr, nullptr,
     {L"Ідея ZachFix. У Director's Cut гальмо й прискорення авто на плечових кнопках; вмикайте лише для досліду.", L"A ZachFix idea. In the Director's Cut the car's brake and boost are on the shoulder buttons; turn on only to try."}, NOTR},
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
    {{L"Крок часу і ліміт кадрів", L"Time step and frame limit"}, &g_cfg.framePacing, kInt, 0, 3, 1, false, L"Time", L"FramePacing", nullptr, kPacingTr,
     {L"Ліміт 60 к/с (типово): у середньому 60 кадрів на секунду за будь-якої частоти екрана. Час гри не відстає від справжнього й не випереджає його більше ніж на пів кроку (мод сам доокруглює короткий кадр до цілого кроку й забирає цей запас із наступного). Це має прибрати надто швидку гру на 75–240 Гц; у грі ще не перевірено. «60 к/с, цілі кроки» і «30 к/с» підганяють час цілими кроками 1/60 с: точніший темп, але нижче 60 к/с дають періодичні ривки (експеримент). Змінюється одразу.", L"Limit 60 fps (default): 60 frames per second on average at any refresh rate. The game clock stays within half a step of real time (the mod rounds a short frame up to a whole step itself and takes that time off the next frame). This should remove the too-fast game at 75-240 Hz; not yet checked in the game. '60 fps, whole steps' and '30 fps' bend the time in whole steps of 1/60 s: a more exact pace but periodic hitches below 60 fps (experimental). Changes at once."},
     {L"Час", L"Time"}},
    {{L"Відносний час (проти зависань)", L"Relative time (against slowdowns)"}, &g_cfg.relativeQpc, kBool, 0, 1, 1, true, L"Time", L"RelativeQpc", nullptr, nullptr,
     {L"Лікує падіння FPS після днів роботи ПК: гра тримала час у єдиній точності.", L"Cures the FPS drop after days of uptime: the game kept time in single precision."}, NOTR},
    {{L"Захист від нульової дельти кадру", L"Zero frame delta guard"}, &g_cfg.zeroDeltaGuard, kBool, 0, 1, 1, true, L"Fixes", L"ZeroDeltaGuard", nullptr, nullptr,
     {L"Нульовий крок кадру ставив швидкість у NaN і вішав гру.", L"A zero frame step turned speed into NaN and froze the game."}, {L"Помилки гри", L"Game bugs"}},
    {{L"День і ніч у будівлях", L"Day and night in buildings"}, &g_cfg.houseListFix, kBool, 0, 1, 1, true, L"Fixes", L"HouseListFix", nullptr, nullptr,
     {L"Виправляє байтовий порядок HOUSE_LIST.NOD: у будівлях знову працює світло дня й ночі.", L"Fixes the byte order of HOUSE_LIST.NOD: day and night lighting works in buildings again."}, NOTR},
    {{L"Прибрати зайве опитування ґеймпада", L"Remove the stray joystick poll"}, &g_cfg.removeExtraJoyPoll, kBool, 0, 1, 1, true, L"Fixes", L"RemoveExtraJoyPoll", nullptr, nullptr,
     {L"Гра опитувала ґеймпад зі сміттям у пам'яті; це заморожувало стіки.", L"The game polled the joystick with garbage in memory; this froze the sticks."}, NOTR},
    {{L"Alt+Tab без зависання", L"Alt+Tab without freezing"}, &g_cfg.resetRefreshFix, kBool, 0, 1, 1, true, L"Fixes", L"ResetRefreshFix", nullptr, nullptr,
     {L"Частота оновлення не копіюється у параметри Reset: гра не вішається після Alt+Tab.", L"The refresh rate is not copied into the Reset parameters: the game does not hang after Alt+Tab."}, NOTR},
    {{L"Пропускати заставки", L"Skip the intro"}, &g_cfg.skipIntro, kBool, 0, 1, 1, true, L"Fixes", L"SkipIntro", nullptr, nullptr,
     {L"Одразу до титульного екрана.", L"Straight to the title screen."}, NOTR},
    {{L"Мова: примусово слот US (для перекладу)", L"Language: force the US slot (for translations)"}, &g_cfg.forceUsSlot, kBool, 0, 1, 1, true, L"Language", L"ForceUsSlot", nullptr, nullptr,
     {L"Переклад підміняє файли слота US; цей пункт тримає його незалежно від config.cnf.", L"A translation replaces the files of the US slot; this keeps it whatever config.cnf says."}, {L"Мова гри", L"Game language"}},
    RESET_ITEM
};
Item g_pageSave[] = {
    {{L"Захист файлу збереження", L"Save file protection"}, &g_cfg.saveGuard, kBool, 0, 1, 1, true, L"Save", L"Guard", nullptr, nullptr,
     {L"Запис через тимчасовий файл і атомарна заміна: збій посередині не пошкодить dp.sav.", L"Written through a temporary file and replaced atomically: a failure halfway cannot damage dp.sav."}, {L"Збереження", L"Saves"}},
    {{L"Скільки резервних копій тримати", L"Backups to keep"}, &g_cfg.saveBackups, kInt, 1, 200, 5, false, L"Save", L"Backups", nullptr, nullptr,
     {L"Датовані копії в UltimateFix\\savebackups.", L"Dated copies in UltimateFix\\savebackups."}, NOTR},
    {{L"Відновлювати пошкоджений файл", L"Restore a damaged file"}, &g_cfg.saveRestoreDamaged, kBool, 0, 1, 1, false, L"Save", L"RestoreDamaged", nullptr, nullptr,
     {L"Якщо dp.sav має неправильний розмір, береться найновіша копія.", L"If dp.sav has the wrong size, the newest backup is used."}, NOTR},
    {{L"Показувати повідомлення про збереження", L"Show save messages"}, &g_cfg.saveWarnings, kBool, 0, 1, 1, false, L"Save", L"Warnings", nullptr, nullptr,
     {L"Вікно, коли збереження не вдалось або відновлене з копії.", L"A message when a save failed or was restored from a backup."}, NOTR},
    RESET_ITEM
};

// ---- Photo mode page: the free camera, frozen time, filters, a screenshot button.
const Tr kFilterTr[6] = {{L"Без", L"None"}, {L"Тепла", L"Warm"}, {L"Холодна", L"Cold"}, {L"Чорно-біла", L"B&W"}, {L"Кіно", L"Cinema"}, {L"Вінтаж", L"Vintage"}};
const int kFilterValues[6] = {0, 1, 2, 3, 4, 5};
int g_shotPick = 0;
const int kShotValues[1] = {1};
const Tr kShotNames[1] = {{L"Зробити знімок", L"Take a picture"}};


void ApplyFov() { g_cfg.photoFovOn = 1; }  // touching the slider means: use my value

void ApplyFilter() {
    // each filter is a set of the sliders below plus a tint (chosen in the pass by the filter number)
    static const int bright[6] = {0, 0, 0, 0, -5, 5}, contrast[6] = {0, 0, 0, 10, 20, 10};
    static const int sat[6] = {0, 10, 5, -100, -20, -40}, vignette[6] = {0, 0, 0, 20, 45, 50};
    int f = g_cfg.photoFilter < 0 || g_cfg.photoFilter > 5 ? 0 : g_cfg.photoFilter;
    g_cfg.photoBright = bright[f];
    g_cfg.photoContrast = contrast[f];
    g_cfg.photoSat = sat[f];
    g_cfg.photoVignette = vignette[f];
}

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



void ApplyCostumeNow() {
    g_applyPick = 0;
    if (g_cfg.photoCostume < 1) return;
    PhotoThaw(120);  // the loaders need the game running
    RequestSuit(g_cfg.photoCostume);
}

void ApplyPoseNow() {
    g_applyPick = 0;
    int i = g_cfg.photoAnim;
    if (i < 1 || i > 9) return;
    PhotoThaw(60);
    RequestPose(kPoseState[i] + 1);
}

void ApplyShot() {
    g_shotPick = 0;
    RequestScreenshot();
}

Item g_pagePhoto[] = {
    {{L"Вільна камера", L"Free camera"}, &g_cfg.photoActive, kBool, 0, 1, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Камера летить за мишею й WASD (Space/C вгору й вниз, Shift швидше, Ctrl повільніше). Закрийте меню (F10 чи Esc), щоб керувати нею; F10 відкриває його знову. F6 вмикає й вимикає весь фоторежим.",
      L"The camera flies with the mouse and WASD (Space/C up and down, Shift faster, Ctrl slower). Close the menu (F10 or Esc) to steer it; F10 opens it again. F6 switches the whole photo mode on and off."},
     {L"Камера", L"Camera"}},
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
    {{L"Фільтр", L"Filter"}, &g_cfg.photoFilter, kInt, 0, 5, 1, false, nullptr, nullptr, ApplyFilter, nullptr,
     {L"Готовий вигляд кадру. Нижче його можна підправити вручну.", L"A ready look for the picture. Fine-tune it by hand below."},
     {L"Вигляд кадру", L"Look of the picture"}, kFilterValues, 6, kFilterTr, nullptr, false},
    {{L"Яскравість", L"Brightness"}, &g_cfg.photoBright, kInt, -100, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Контраст", L"Contrast"}, &g_cfg.photoContrast, kInt, -100, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Насиченість", L"Saturation"}, &g_cfg.photoSat, kInt, -100, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Віньєтка", L"Vignette"}, &g_cfg.photoVignette, kInt, 0, 100, 5, false, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Різкість, %", L"Sharpening, %"}, &g_cfg.sharpen, kInt, 0, 100, 10, false, L"Graphics", L"Sharpen", nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Згладжування країв, %", L"Edge smoothing, %"}, &g_cfg.antialias, kInt, 0, 100, 10, false, L"Graphics", L"Antialias", nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Затінення в кутках, %", L"Ambient occlusion, %"}, &g_cfg.ambientOcclusion, kInt, 0, 100, 10, false, L"Graphics", L"AmbientOcclusion", nullptr, nullptr, {nullptr, nullptr}, NOTR},
    {{L"Набір костюмів", L"Costume set"}, &g_cfg.photoSuitSet, kInt, 1, 3, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Йорк має набори 1 і 2, Зак (коли ви граєте за нього) набори 1, 2 і 3 (у третього один костюм). Який персонаж зараз, видно в журналі.", L"York has sets 1 and 2, Zach (when you play as him) sets 1, 2 and 3 (the third has one suit). The log says which character is current."},
     {L"Персонаж", L"Character"}, nullptr, 0, nullptr, FormatSet},
    {{L"Костюм", L"Suit"}, &g_cfg.photoCostume, kInt, 0, 10, 1, false, nullptr, nullptr, nullptr, nullptr,
     {L"Виберіть костюм і натисніть «Одягти». ЕКСПЕРИМЕНТАЛЬНО, зробіть збереження. Костюм вантажиться з диска, на це йде до секунди (час гри на цей час запускається). Костюм 2 це слот додаткового пакета DLC і може бути порожнім. Не в катсцені й не в машині.",
      L"Pick a suit and press \"Wear\". EXPERIMENTAL, save first. The suit loads from disk, which takes up to a second (game time runs for that moment). Suit 2 is the DLC add-on slot and may be empty. Not in a cutscene or a car."},
     NOTR, nullptr, 0, nullptr, FormatSuit},
    {{L"Одягти вибраний костюм", L"Wear the chosen suit"}, &g_applyPick, kInt, 0, 1, 1, false, nullptr, nullptr, ApplyCostumeNow, nullptr, {nullptr, nullptr}, NOTR,
     kApplyValues, 1, kApplyNames, nullptr, false},
    {{L"Поза Йорка (експериментально)", L"York's pose (experimental)"}, &g_cfg.photoAnim, kInt, 0, 9, 1, false, nullptr, nullptr, nullptr, kPoseTr,
     {L"Виберіть позу й натисніть «Застосувати». «Спокій» і «Дія 1–7» не потребують зброї (це стани гри 4–8, 12, 14, назви невідомі). «Зброя в руках» бере стан класу зброї, яку Йорк тримає (потрібна зброя в руках). Зробіть збереження. «Спокій» повертає Йорка як було.",
      L"Pick a pose and press \"Apply\". \"Calm\" and \"Action 1-7\" need no weapon (game states 4-8, 12, 14, names unknown). \"Weapon in hand\" uses the state of the class of the weapon York holds (a weapon must be in his hand). Save first. \"Calm\" puts York back."},
     NOTR},
    {{L"Застосувати позу", L"Apply the pose"}, &g_applyPick, kInt, 0, 1, 1, false, nullptr, nullptr, ApplyPoseNow, nullptr, {nullptr, nullptr}, NOTR,
     kApplyValues, 1, kApplyNames, nullptr, false},
    {{L"Знімок", L"Picture"}, &g_shotPick, kInt, 0, 1, 1, false, nullptr, nullptr, ApplyShot, nullptr,
     {L"Зберігає PNG кадру без меню в UltimateFix\\screenshots (клавіша F11 теж).", L"Saves a PNG of the frame without the menu to UltimateFix\\screenshots (the F11 key too)."},
     {L"Знімок", L"Picture"}, kShotValues, 1, kShotNames, nullptr, false},
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
void DoCamera() { g_cheatPick = 0; CheatYorkToCamera(); }

Item g_pageCheats[] = {
    {{L"Гроші", L"Money"}, &g_cheatPick, kInt, 0, 3, 1, false, nullptr, nullptr, DoMoney, nullptr,
     {L"Додає гроші (до 99999). Змінюється одразу. Деякі розділи можуть змінювати їх самі.", L"Adds money (up to 99999). Applies at once."},
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
    {{L"Йорк до камери", L"York to the camera"}, &g_cheatPick, kInt, 0, 1, 1, false, nullptr, nullptr, DoCamera, nullptr,
     {L"Спершу вмикніть вільну камеру в фоторежимі й підлетіть, куди треба. Кнопка переставляє гравця туди. Не в приміщенні й не в машині. Зробіть збереження.", L"First turn on the free camera in the photo mode and fly where you want. The button puts the player there. Not indoors or in a car. Save first."},
     NOTR, kOneValue, 1, kCameraNames, nullptr, false},
};

struct Page {
    Tr title;
    Tr subtitle;
    Item* items;
    int count;
    bool about;  // the "About" page has no settings
};
constexpr int kPhotoPage = 3;  // the photo mode section (the camera keys and the preview of the filters know it)
// Six sections, most used first. Some are several pages of settings put together once at start (MergePages), each part under its heading.
Page g_pages[] = {
    {{L"Екран і графіка", L"Screen and graphics"}, {L"Вікно, роздільність, тіні, різкість, іконки", L"Window, resolution, shadows, sharpness, icons"}, nullptr, 0, false},
    {{L"Керування", L"Controls"}, {L"Ґеймпад, стіки, вібрація", L"Gamepad, sticks, vibration"}, g_pageInput, (int)(sizeof(g_pageInput) / sizeof(Item)), false},
    {{L"Гра і світ", L"Game and world"}, {L"Складність, бій, дальність, мапа", L"Difficulty, combat, distance, map"}, nullptr, 0, false},
    {{L"Фоторежим і читкоди", L"Photo mode and cheats"}, {L"Камера, фільтри, знімок, гроші, час, погода", L"Camera, filters, picture, money, time, weather"}, nullptr, 0, false},
    {{L"Виправлення", L"Fixes"}, {L"Час, помилки гри, мова, захист dp.sav", L"Time, game bugs, language, protection of dp.sav"}, nullptr, 0, false},
    {{L"Про мод", L"About"}, {L"Автор і подяки", L"Author and credits"}, nullptr, 0, true},
};
constexpr int kPageCount = (int)(sizeof(g_pages) / sizeof(Page));

// The sidebar has 6 sections instead of 11: some are several pages of settings put together once at start (each part keeps its heading).
struct PageSource {
    Item* items;
    int count;
    Tr title;
};
constexpr int Count(int bytes) { return bytes / (int)sizeof(Item); }
constexpr int kScreenMax = Count(sizeof(g_pageDisplay)) + Count(sizeof(g_pageGraphics));
constexpr int kGameMax = Count(sizeof(g_pageWorld)) + Count(sizeof(g_pageGameplay)) + Count(sizeof(g_pageMapItems));
constexpr int kFixesMax = Count(sizeof(g_pageFixes)) + Count(sizeof(g_pageSave));
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

void MergePages() {
    if (g_pagesMerged) return;
    g_pagesMerged = true;
    const PageSource screen[2] = {{g_pageDisplay, (int)(sizeof(g_pageDisplay) / sizeof(Item)), {L"Вікно і екран", L"Window and screen"}},
                                  {g_pageGraphics, (int)(sizeof(g_pageGraphics) / sizeof(Item)), {L"Графіка", L"Graphics"}}};
    const PageSource game[3] = {{g_pageWorld, (int)(sizeof(g_pageWorld) / sizeof(Item)), {L"Світ", L"World"}},
                                {g_pageGameplay, (int)(sizeof(g_pageGameplay) / sizeof(Item)), {L"Ґеймплей", L"Gameplay"}},
                                {g_pageMapItems, (int)(sizeof(g_pageMapItems) / sizeof(Item)), {L"Мапа", L"Map"}}};
    const PageSource fixes[2] = {{g_pageFixes, (int)(sizeof(g_pageFixes) / sizeof(Item)), {L"Виправлення", L"Fixes"}},
                                 {g_pageSave, (int)(sizeof(g_pageSave) / sizeof(Item)), {L"Збереження", L"Saves"}}};
    const PageSource photo[2] = {{g_pagePhoto, (int)(sizeof(g_pagePhoto) / sizeof(Item)), {L"Фоторежим", L"Photo mode"}},
                                 {g_pageCheats, (int)(sizeof(g_pageCheats) / sizeof(Item)), {L"Читкоди", L"Cheats"}}};
    MergeInto(g_pages[0], g_screenStore, screen, 2);
    MergeInto(g_pages[2], g_gameStore, game, 3);
    MergeInto(g_pages[kPhotoPage], g_photoStore, photo, 2);
    MergeInto(g_pages[4], g_fixesStore, fixes, 2);
}

int g_page = 0;
int g_sel = 0;     // selected item of the page
int g_scroll = 0;  // first visible row
u8 g_prev[256];    // previous key states, for edge detection
bool g_dragging = false;
volatile int g_wheel = 0;  // wheel steps since the last frame (taken from the game's wheel word)
int g_chipSel = 0;          // the highlighted button on a row of action buttons (the arrows move it, Enter presses it)
int g_chipRow = -1;         // the item that highlight belongs to
bool g_swallowEsc = false;  // the Esc that closed the menu is still held: the game must not see it as a new press

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
constexpr float kNavH = 44;  // sidebar rows
float kNavTop = 150;
float kSliderX = 818, kLangY = 612;

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
}
constexpr u32 kAccent = 0xFFD0343C, kPanelBg = 0xF4101014, kSideBg = 0xFF0B0B0F, kRowSel = 0xFF1D1D26, kLine = 0xFF2A2A34,
              kText = 0xFFF2F2F5, kDim = 0xFF9696A4, kOn = 0xFF35C27A, kOff = 0xFF3C3C48, kAmber = 0xFFE5A83A;

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

// --- input -------------------------------------------------------------------------------------------
bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// True once per key press.
bool Pressed(int vk) {
    bool now = Down(vk);
    bool was = g_prev[vk] != 0;
    return now && !was;
}

void RememberKeys() {
    static const int keys[] = {VK_F10, VK_ESCAPE, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_SPACE, VK_TAB,
                               VK_PRIOR, VK_NEXT, VK_LBUTTON, VK_BACK, 'Y', 'N'};
    for (int vk : keys) g_prev[vk] = Down(vk) ? 1 : 0;
    for (int d = 0; d < 10; ++d) {
        g_prev[0x30 + d] = Down(0x30 + d) ? 1 : 0;
        g_prev[VK_NUMPAD0 + d] = Down(VK_NUMPAD0 + d) ? 1 : 0;
    }
}

void Save(const Item& it) {
    if (!it.key) return;  // not stored (a test)
    wchar_t value[16];
    wsprintfW(value, L"%d", *it.value);
    WritePrivateProfileStringW(it.section, it.key, value, g_paths.iniFile);
}

Item* g_unsaved = nullptr;  // a slider dragged right now: written to the ini and the log once, when the drag ends

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
    if (g_unsaved && g_unsaved != &it) FlushUnsaved();
    if (g_dragging && it.key) {
        g_unsaved = &it;  // every step of a drag rewrote the whole ini file on the game thread
    } else {
        Save(it);
        if (it.key) Log("menu: %S = %d", it.key, v);
    }
    if (it.apply) it.apply();
}

int g_confirm = 0;  // 0 nothing, 1 asking about this page, 2 asking about everything

// Called by the chips: only asks. The work is done by PerformReset after the player confirms.
void ResetPageDefaults() {
    int pick = g_resetPick;
    g_resetPick = 0;
    if (pick == 1 || pick == 2) g_confirm = pick;
}

int RestorePage(Page& pg, const wchar_t* tmp) {
    int restored = 0;
    if (pg.about || !pg.items) return 0;
    for (int i = 0; i < pg.count; ++i) {
        Item& it = pg.items[i];
        if (!it.key || !it.section || !it.value) continue;
        int v = (int)GetPrivateProfileIntW(it.section, it.key, -999999, tmp);
        if (v == -999999) continue;  // not in the default ini
        SetValue(it, v);
        ++restored;
    }
    return restored;
}

void PerformReset(int scope) {
    wchar_t tmp[MAX_PATH];
    lstrcpyW(tmp, g_paths.dataDir);
    lstrcatW(tmp, L"defaults.tmp");
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    unsigned long length = 0;
    const char* text = DefaultIniText(&length);
    DWORD written;
    WriteFile(h, text, length, &written, nullptr);
    CloseHandle(h);
    int restored = 0;
    if (scope == 2) {
        for (Page& pg : g_pages) restored += RestorePage(pg, tmp);
    } else {
        restored = RestorePage(g_pages[g_page], tmp);
    }
    DeleteFileW(tmp);
    Log("menu: defaults restored (%s, %d settings)", scope == 2 ? "everything" : "this page", restored);
}

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
constexpr float kConfirmX = 380, kConfirmY = 250;  // the "restore defaults?" box (520 x 170)
constexpr float kSwitchW = 46, kSwitchH = 22;
constexpr float kChipH = 26, kChipGap = 6;

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

void BeginEdit() {
    g_editing = true;
    g_editLen = 0;
    g_editText[0] = 0;
}

void ConfirmEdit(Item& it) {
    g_editing = false;
    if (g_editLen == 0) return;
    int n = 0;
    for (int i = 0; i < g_editLen; ++i) n = n * 10 + (g_editText[i] - L'0');
    if (it.resolution && n > 0 && n < 1280) n = (n * 16 + 4) / 9;  // a height: 1080 -> 1920
    SetValue(it, n);
}

void HandleEditKeys(Item& it) {
    if (Pressed(VK_ESCAPE)) { g_editing = false; return; }
    if (Pressed(VK_RETURN)) { ConfirmEdit(it); return; }
    if (Pressed(VK_BACK) && g_editLen > 0) g_editText[--g_editLen] = 0;
    for (int d = 0; d < 10; ++d)
        if ((Pressed(0x30 + d) || Pressed(VK_NUMPAD0 + d)) && g_editLen < 6) {
            g_editText[g_editLen++] = (wchar_t)(L'0' + d);
            g_editText[g_editLen] = 0;
        }
}

bool g_dragScroll = false;
constexpr float kLangChipW = 84;
void SetLanguage(int language);

// Clicks in the sidebar: a section or the language. Returns true when the click was used.
bool SidebarClick(float mx, float my) {
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

void HandleInput(bool foreground) {
    Page& pg = g_pages[g_page];
    if (!foreground) return;
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
    if (g_confirm) {  // the question about restoring defaults: Enter / Y confirms, Esc / N cancels, or click a button
        g_wheel = 0;
        float mx, my;
        bool click = Pressed(VK_LBUTTON) && MousePosition(&mx, &my);
        bool yes = Pressed(VK_RETURN) || Pressed('Y');
        bool no = Pressed(VK_ESCAPE) || Pressed('N');
        if (click) {
            if (mx >= kConfirmX + 40 && mx <= kConfirmX + 40 + 200 && my >= kConfirmY + 120 && my <= kConfirmY + 154) yes = true;
            else if (mx >= kConfirmX + 280 && mx <= kConfirmX + 280 + 200 && my >= kConfirmY + 120 && my <= kConfirmY + 154) no = true;
        }
        if (yes) {
            PerformReset(g_confirm);
            g_confirm = 0;
        } else if (no) {
            g_confirm = 0;
        }
        return;
    }
    if (pg.about) {  // nothing to change on this page: keys for the sections, clicks for the sidebar
        if (Pressed(VK_ESCAPE)) g_open = false;
        if (Pressed(VK_TAB)) SelectPage(g_page + ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? -1 : 1));
        if (Pressed(VK_NEXT)) SelectPage(g_page + 1);
        if (Pressed(VK_PRIOR)) SelectPage(g_page - 1);
        g_wheel = 0;
        float mx, my;
        if (Pressed(VK_LBUTTON) && MousePosition(&mx, &my)) SidebarClick(mx, my);
        return;
    }
    if (g_editing) {
        HandleEditKeys(pg.items[g_sel]);
        if (Pressed(VK_LBUTTON)) g_editing = false;  // a click elsewhere cancels
        return;
    }
    if (Pressed(VK_ESCAPE)) g_open = false;
    int prevSel = g_sel;
    if (Pressed(VK_UP)) g_sel = (g_sel + pg.count - 1) % pg.count;
    if (Pressed(VK_DOWN)) g_sel = (g_sel + 1) % pg.count;
    if (g_chipRow != g_sel) {
        g_chipRow = g_sel;
        g_chipSel = 0;
    }
    {
        Item& it = pg.items[g_sel];
        bool numeric = it.kind == kInt && !it.names && it.key;
        bool action = IsActionRow(it);
        if (Pressed(VK_LEFT) || Pressed(VK_RIGHT)) {
            int dir = Pressed(VK_RIGHT) ? +1 : -1;
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
                Change(it, dir);
            }
        }
        if (action) {  // Enter or Space presses the highlighted button
            if (Pressed(VK_RETURN) || Pressed(VK_SPACE)) {
                int i = g_chipSel < 0 ? 0 : (g_chipSel >= it.presetCount ? it.presetCount - 1 : g_chipSel);
                SetValue(it, it.presets[i]);
            }
        } else {
            if (Pressed(VK_RETURN)) {
                if (numeric) BeginEdit();
                else Change(it, +1);
            }
            if (Pressed(VK_SPACE)) Change(it, +1);
        }
    }
    if (Pressed(VK_TAB)) SelectPage(g_page + ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? -1 : 1));
    if (Pressed(VK_NEXT)) SelectPage(g_page + 1);
    if (Pressed(VK_PRIOR)) SelectPage(g_page - 1);

    // the wheel scrolls the list
    int wheel = g_wheel;
    g_wheel = 0;
    if (wheel) ScrollBy(-wheel * 2);

    float mx, my;
    bool click = Pressed(VK_LBUTTON);
    bool held = Down(VK_LBUTTON);
    if (!held) {
        g_dragging = false;
        FlushUnsaved();
    }
    if (MousePosition(&mx, &my)) {
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
            return;
        }
        if (click && onHeader && !resizingPanel) {
            movingPanel = true;
            grabX = mx - kPx;
            grabY = my - kPy;
            return;
        }
        if (movingPanel && held) {
            kPx = mx - grabX;
            kPy = my - grabY;
            UpdateLayout();
            return;
        }
        if (resizingPanel && held) {
            kPw = mx - kPx + 10;
            kPh = my - kPy + 10;
            // the size stops at the screen edge; otherwise UpdateLayout moved the panel instead and it crept away frame by frame
            if (kPx + kPw > 1280) kPw = 1280 - kPx;
            if (kPy + kPh > 720) kPh = 720 - kPy;
            UpdateLayout();
            KeepSelectionVisible();
            return;
        }
        if (click && SidebarClick(mx, my)) return;
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
                } else if (!held) {
                    g_sel = idx;  // hover selects
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
// Draws `text` cut with "…" when it is wider than maxW. Returns the width drawn.
float DrawFitted(const wchar_t* text, float x, float y, u32 color, int px, float maxW) {
    int len = lstrlenW(text);
    int n = TextFit(text, len, maxW, px);
    if (n >= len && len < 256) return DrawText(text, x, y, color, px);
    n = TextFit(text, len, maxW - TextWidth(L"…", px), px);
    if (n > 250) n = 250;
    while (n > 0 && text[n - 1] == L' ') --n;
    wchar_t cut[256];
    for (int i = 0; i < n; ++i) cut[i] = text[i];
    cut[n] = L'…';
    cut[n + 1] = 0;
    return DrawText(cut, x, y, color, px);
}

// Word wrap: '\n' starts a new line, and a line that would be wider than maxW breaks at the last space that fits. At most
// maxLines lines; when the text needs more, the last one ends with "…". Returns the number of lines; with draw = false
// it only counts them.
int DrawLines(const wchar_t* text, float x, float y, u32 color, int px, float lineH, float maxW, int maxLines, bool draw) {
    int lines = 0;
    const wchar_t* p = text;
    for (;;) {
        const wchar_t* end = p;
        while (*end && *end != L'\n') ++end;
        do {
            int len = (int)(end - p);
            int n = TextFit(p, len > 250 ? 250 : len, maxW, px);
            if (n < len) {
                int b = n;
                while (b > 0 && p[b] != L' ') --b;
                if (b > 0) n = b;
                else if (n == 0) n = 1;  // one word wider than the line: cut it
            }
            const wchar_t* next = p + n;
            while (next < end && *next == L' ') ++next;
            bool more = next < end || *end != 0;
            if (lines == maxLines - 1 && more) {  // the last line there is room for, and text is left: end it with "…"
                if (draw) {
                    int m = TextFit(p, len > 250 ? 250 : len, maxW - TextWidth(L"…", px), px);
                    if (m < len) {  // cut inside the paragraph: end at a word break
                        int b = m;
                        while (b > 0 && p[b] != L' ') --b;
                        if (b > 0) m = b;
                    }
                    while (m > 0 && p[m - 1] == L' ') --m;
                    wchar_t line[256];
                    for (int i = 0; i < m; ++i) line[i] = p[i];
                    line[m] = L'…';
                    line[m + 1] = 0;
                    DrawText(line, x, y + lineH * (float)lines, color, px);
                }
                return lines + 1;
            }
            if (draw && n > 0) {
                wchar_t line[256];
                for (int i = 0; i < n; ++i) line[i] = p[i];
                line[n] = 0;
                DrawText(line, x, y + lineH * (float)lines, color, px);
            }
            ++lines;
            p = next;
        } while (p < end);
        if (*end == 0) break;
        p = end + 1;
    }
    return lines;
}

// The box a number is typed into, right-aligned on a row.
void DrawEditBox(float y) {
    float w = 150;
    FillRect(kCr - w, y + 6, w, 28, kAccent);
    FillRect(kCr - w + 2, y + 8, w - 4, 24, kSideBg);
    wchar_t text[16];
    wsprintfW(text, L"%s|", g_editText);
    float tw = TextWidth(text, 18);
    DrawText(text, kCr - 10 - tw, y + 10, kText, 18);
}

void DrawItemRow(Item& it, float y, bool sel) {
    if (sel) {
        FillRect(kCx - 12, y + 1, kCr - kCx + 24, kRowH - 2, kRowSel);
        FillRect(kCx - 12, y + 1, 3, kRowH - 2, kAccent);
    }
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
    float tagRoom = it.restart ? TextWidth(tag, 12) + 10 : 0.f;
    float lw = DrawFitted(Tx(it.label), kCx, y + 10, sel ? kText : 0xFFD8D8E0, 18, limit - kCx - tagRoom);
    if (it.restart) {
        float tw = DrawText(tag, kCx + lw + 10, y + 14, kAmber, 12);
        if (it.running) {
            wchar_t t[64];
            it.running(t);
            if (t[0]) {  // what is really running now; a shorter wording when the long one would run into the control
                float x0 = kCx + lw + 10 + tw + 10, room = limit - x0;
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
                DrawText(shown, x0, y + 14, 0xFFFF7A5A, 12);
            }
        }
    }

    float cy = y + kRowH / 2;
    if (it.kind == kBool) {
        bool on = *it.value != 0;
        float x = kCr - kSwitchW;
        FillRect(x, cy - kSwitchH / 2, kSwitchW, kSwitchH, on ? kOn : kOff);
        float kx = on ? x + kSwitchW - kSwitchH + 3 : x + 3;
        FillRect(kx, cy - kSwitchH / 2 + 3, kSwitchH - 6, kSwitchH - 6, 0xFFFFFFFF);
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
        DrawText(value, kCr - w, y + 10, sel ? kAccent : kText, 18);
        return;
    }
    if (it.presets) {  // buttons: the presets and one for a value of one's own
        float cx[8], cw[8];
        int n;
        ChipLayout(it, cx, cw, &n);
        int cur = PresetIndex(it);
        bool actionRow = IsActionRow(it);
        for (int i = 0; i < n; ++i) {
            bool active = i == cur || (i == it.presetCount && cur < 0);
            bool action = it.presetCount == 1 && !it.key && i == 0;  // a button that does something (Apply, Take a picture ...)
            if (sel && actionRow && g_chipRow == g_sel && i == g_chipSel) {  // the button Enter would press
                FillRect(cx[i] - 2, cy - kChipH / 2 - 2, cw[i] + 4, kChipH + 4, kText);
                active = true;
            }
            FillRect(cx[i], cy - kChipH / 2, cw[i], kChipH, action ? kOn : (active ? kAccent : 0xFF23232C));
            if (action) active = true;  // white text
            wchar_t label[32];
            if (i < it.presetCount) lstrcpyW(label, Tx(it.presetNames[i]));
            else it.format(*it.value, label);
            float tw = TextWidth(label, 15);
            DrawText(label, cx[i] + (cw[i] - tw) / 2, cy - 9, active ? 0xFFFFFFFF : kDim, 15);
        }
        return;
    }
    // slider + the value (a click on it types a number)
    float sw = SliderW(it);
    float t = (float)(*it.value - it.lo) / (float)(it.hi - it.lo);
    FillRect(kSliderX, cy - 3, sw, 6, kOff);
    FillRect(kSliderX, cy - 3, sw * t, 6, sel ? kAccent : 0xFF8A2A30);
    FillRect(kSliderX + sw * t - 5, cy - 9, 10, 18, 0xFFFFFFFF);
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
        FillRect(x, kLangY, 84, 30, on ? kAccent : 0xFF23232C);
        float tw = TextWidth(names[i], 15);
        DrawText(names[i], x + (84 - tw) / 2, kLangY + 6, on ? 0xFFFFFFFF : kDim, 15);
    }
}

void DrawAbout() {
    const bool en = g_cfg.language == 1;
    float y = kListTop - 4;
    DrawText(L"UltimateFIXDP", kCx, y, kText, 30);
    wchar_t ver[48];
    wsprintfW(ver, en ? L"version %S" : L"версія %S", kVersion);
    DrawText(ver, kCx, y + 40, kDim, 15);
    y += 84;
    DrawText(en ? L"Author" : L"Автор", kCx, y, kAccent, 13);
    DrawText(en ? L"Dmytro Bydlov, the \"Little Bit\" team" : L"Дмитро Бидлов, команда «Little Bit»", kCx, y + 20, kText, 20);
    y += 64;
    DrawText(en ? L"Licence" : L"Ліцензія", kCx, y, kAccent, 13);
    DrawText(en ? L"MIT: use and change it freely. All code is our own; other projects gave ideas, facts and addresses."
                : L"MIT: користуйтесь і змінюйте вільно. Увесь код наш власний; інші проєкти дали ідеї, факти й адреси.",
             kCx, y + 20, kText, 15);
    y += 62;
    DrawText(en ? L"Thanks" : L"Подяки", kCx, y, kAccent, 13);
    const wchar_t* credits[5] = {
        en ? L"ZachFix (h714je): research of the port and many ideas" : L"ZachFix (h714je): дослідження порту й багато ідей",
        en ? L"DPStabilityFix (Cesario67): saves, timing, DualSense layout" : L"DPStabilityFix (Cesario67): збереження, час, розкладка DualSense",
        en ? L"MapFix, CrashFix and FreeCam (Behelit09, MIT): full map, navigation crash, free camera" : L"MapFix, CrashFix і FreeCam (Behelit09, MIT): повна мапа, аварія навігації, вільна камера",
        en ? L"DPfix (Durante): resolution, shadows, texture replacement" : L"DPfix (Durante): роздільність, тіні, підміна текстур",
        en ? L"Kenney Input Prompts (CC0): button icons" : L"Kenney Input Prompts (CC0): іконки кнопок",
    };
    for (int i = 0; i < 5; ++i) DrawText(credits[i], kCx, y + 22 + i * 24, 0xFFC4C4CE, 15);
    y += 22 + 5 * 24 + 10;
    DrawText(en ? L"Testers" : L"Тестувальники", kCx, y, kAccent, 13);
    DrawText(L"Gold flin, Tree Fan", kCx, y + 22, 0xFFC4C4CE, 15);
    DrawText(en ? L"F10 or Esc close" : L"F10 або Esc закрити", kCx, kPy + kPh - 36, kDim, 13);
}

// A background colour with its alpha scaled by the menu opacity setting.
u32 Fade(u32 c) {
    u32 a = (c >> 24) * (u32)g_cfg.menuOpacity / 100;
    return (c & 0x00FFFFFF) | (a << 24);
}

void Draw() {
    if (g_rowCount == 0) BuildRows();  // never an empty page
    FillRect(0, 0, 1280, 720, Fade(0x50000000));  // the same light dimming on every page, so the picture does not change when you switch pages
    FillRect(kPx, kPy, kPw, kPh, Fade(kPanelBg));
    FillRect(kPx, kPy, kPw, 3, kAccent);
    FillRect(kPx + kPw - 15, kPy + kPh - 5, 11, 2, kDim);  // the grip in the corner: drag to resize
    FillRect(kPx + kPw - 5, kPy + kPh - 15, 2, 11, kDim);
    FillRect(kPx, kPy, kSideW, kPh, Fade(kSideBg));

    DrawText(L"UltimateFIXDP", kPx + 24, kPy + 22, kText, 22);
    wchar_t ver[32];
    wsprintfW(ver, g_cfg.language == 1 ? L"version %S" : L"версія %S", kVersion);
    DrawText(ver, kPx + 24, kPy + 52, kDim, 13);
    FillRect(kPx + 20, kPy + 84, kSideW - 40, 1, kLine);
    for (int p = 0; p < kPageCount; ++p) {
        float y = kNavTop + p * kNavH;
        bool cur = p == g_page;
        if (cur) {
            FillRect(kPx, y, kSideW, kNavH, kRowSel);
            FillRect(kPx, y, 4, kNavH, kAccent);
        }
        DrawText(Tx(g_pages[p].title), kPx + 26, y + 12, cur ? kText : kDim, 17);
    }

    Page& pg = g_pages[g_page];
    DrawText(Tx(pg.title), kCx, kPy + 24, kText, 26);
    DrawText(Tx(pg.subtitle), kCx, kPy + 62, kDim, 15);
    FillRect(kCx, kPy + 100, kCr - kCx, 1, kLine);

    DrawLanguageChips();
    if (pg.about) {
        DrawAbout();
        return;
    }
    float y = kListTop;
    int shown = 0;
    for (int r = g_scroll; r < g_rowCount; ++r) {
        float h = RowHeight(g_rows[r]);
        if (y + h > kListBottom) break;
        if (g_rows[r].item < 0) {
            DrawText(Tx(g_rows[r].heading), kCx, y + 11, kAccent, 13);
            float w = TextWidth(Tx(g_rows[r].heading), 13);
            FillRect(kCx + w + 12, y + 19, kCr - kCx - w - 12, 1, kLine);
        } else {
            DrawItemRow(pg.items[g_rows[r].item], y, g_rows[r].item == g_sel);
        }
        y += h;
        ++shown;
    }
    if (Scrollable()) {
        float th = kListBottom - kListTop;
        FillRect(kCr + 14, kListTop, 4, th, kLine);
        float frac = (float)shown / (float)g_rowCount;
        int ms = MaxScroll();
        float pos = ms > 0 ? (float)g_scroll / (float)ms : 0.f;
        FillRect(kCr + 12, kListTop + (th - th * frac) * pos, 8, th * frac, g_dragScroll ? kAccent : kDim);
    }

    // the description box: word-wrapped to its width, in a smaller font when the text needs more lines than fit
    const float hintTop = kListBottom + 24, hintBottom = kListBottom + 116, hintW = kCr - kCx - 8;
    FillRect(kCx - 12, kListBottom + 18, kCr - kCx + 24, hintBottom - kListBottom - 16, Fade(kSideBg));
    Item& cur = pg.items[g_sel];
    const bool en = g_cfg.language == 1;
    if (g_editing) {
        wchar_t line[256];
        wsprintfW(line, en ? L"Type a value for \"%s\" (from %d to %d) and press Enter. Esc cancels." : L"Введіть значення для «%s» (від %d до %d) і натисніть Enter. Esc скасовує.",
                  Tx(cur.label), cur.lo, cur.hi);
        int used = DrawLines(line, kCx + 4, hintTop, kText, 15, 19, hintW, 2, true);
        if (cur.resolution)
            DrawLines(en ? L"A width (1920, 2560, 3840) or a height (1080, 1440, 2160): a number below 1280 is taken as a height."
                         : L"Ширина (1920, 2560, 3840) або висота (1080, 1440, 2160): число менше 1280 вважається висотою.",
                      kCx + 4, hintTop + 19.f * (float)used, kDim, 15, 19, hintW, 4 - used, true);
    } else {
        const wchar_t* hint = Tx(cur.hint);
        const wchar_t* note = en ? L"Takes effect after the game is restarted." : L"Набуде чинності після перезапуску гри.";
        int px = 15;
        float lh = 18;
        int room = (int)((hintBottom - hintTop) / lh) - (cur.restart ? 1 : 0);
        if (hint && DrawLines(hint, 0, 0, 0, px, lh, hintW, 99, false) > room) {
            px = 13;
            lh = 15;
            room = (int)((hintBottom - hintTop) / lh) - (cur.restart ? 1 : 0);
        }
        int used = hint ? DrawLines(hint, kCx + 4, hintTop, 0xFFC4C4CE, px, lh, hintW, room, true) : 0;
        if (cur.restart) DrawFitted(note, kCx + 4, hintTop + lh * (float)used + 2, kAmber, 13, hintW);
    }

    if (g_confirm) {
        FillRect(0, 0, 1280, 720, 0x90000000);
        FillRect(kConfirmX, kConfirmY, 520, 170, kAccent);
        FillRect(kConfirmX + 2, kConfirmY + 2, 516, 166, kPanelBg);
        DrawText(g_confirm == 2 ? (en ? L"Restore the defaults of ALL settings?" : L"Відновити типові значення ВСІХ налаштувань?")
                                : (en ? L"Restore the defaults of this page?" : L"Відновити типові значення цієї сторінки?"),
                 kConfirmX + 24, kConfirmY + 24, kText, 20);
        DrawText(en ? L"Your current values will be replaced. This cannot be undone." : L"Ваші поточні значення буде замінено. Скасувати це не вийде.",
                 kConfirmX + 24, kConfirmY + 60, 0xFFC4C4CE, 15);
        DrawText(en ? L"Enter or Y: yes      Esc or N: no" : L"Enter або Y: так      Esc або N: ні", kConfirmX + 24, kConfirmY + 88, kDim, 14);
        FillRect(kConfirmX + 40, kConfirmY + 120, 200, 34, kAccent);
        DrawText(en ? L"Yes, restore" : L"Так, відновити", kConfirmX + 40 + 100 - TextWidth(en ? L"Yes, restore" : L"Так, відновити", 16) / 2, kConfirmY + 127, kText, 16);
        FillRect(kConfirmX + 280, kConfirmY + 120, 200, 34, kRowSel);
        DrawText(en ? L"No, keep" : L"Ні, лишити", kConfirmX + 280 + 100 - TextWidth(en ? L"No, keep" : L"Ні, лишити", 16) / 2, kConfirmY + 127, kText, 16);
        return;
    }
    DrawFitted(en ? L"↑ ↓ select     ← → change / pick a button     Enter type a number / press     Tab section     wheel / click     F10 or Esc close"
                  : L"↑ ↓ вибір     ← → змінити / вибрати кнопку     Enter ввести число / натиснути     Tab розділ     колесо / клік     F10 або Esc закрити",
               kCx, kPy + kPh - 36, kDim, 13, kCr + 24 - kCx);
    DrawFitted(en ? L"drag the header: move     drag the corner: resize     double-click the corner: reset" : L"заголовок: перенести     куточок: розмір     подвійний клік по куточку: скинути",
               kCx, kPy + kPh - 20, kDim, 12, kCr + 24 - kCx);
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

SHORT WINAPI H_GetAsyncKeyState(int vk) {
    if (g_open || FreeCamBlocksInput()) return 0;
    if (vk == VK_ESCAPE && g_swallowEsc) {
        SwallowEsc();
        return 0;  // also the first call after the release: its "pressed since the last call" bit belongs to the old press
    }
    return r_GetAsyncKeyState(vk);
}

BOOL WINAPI H_GetKeyboardState(PBYTE state) {
    if (!g_open && !FreeCamBlocksInput()) {
        BOOL ok = r_GetKeyboardState(state);
        if (ok && SwallowEsc()) state[VK_ESCAPE] = 0;
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

void EnsureCursorVisible() {
    int count = ShowCursor(TRUE);  // our own import, not the game's hooked one
    while (count < 0) count = ShowCursor(TRUE);
    if (count > 0) ShowCursor(FALSE);
    g_cursorHidden = false;
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
}

BOOL WINAPI H_SetCursorPos(int x, int y) { return (g_open || FreeCamBlocksInput()) ? TRUE : r_SetCursorPos(x, y); }

MMRESULT WINAPI H_joyGetPosEx(UINT id, LPJOYINFOEX info) {
    if (id != 0 && g_cfg.onlyFirstPad) return JOYERR_UNPLUGGED;  // the game only ever uses slot 0
    if ((!g_open && !FreeCamBlocksInput()) || id != 0 || !info) {
        MMRESULT r;
        if (id == 0 && info && g_cfg.nativeDualSense && DualSenseToJoy(info)) {
            r = JOYERR_NOERROR;  // a DualSense read directly: no WinMM call, no conversion needed
        } else {
            // With no pad Windows answers a joyGetPosEx(0) only after it has looked through the device list (cfgmgr32, kernel calls): the
            // sampling profiler of this mod found 80% of the frame in ntdll because of it. A failed answer for pad 0 is reused for half a
            // second, so the (slow) question is asked twice a second, not every frame.
            // Only the WinMM answer is cached: the XInput fallback below is cheap (it scans for a new pad once a second
            // itself) and still runs every poll, so a pad seen only through XInput does not bring the slow question back.
            static DWORD s_failTick = 0;
            static MMRESULT s_failCode = JOYERR_NOERROR;
            static double s_ms = 0;
            static int s_calls = 0, s_logs = 0;
            DWORD nowTick = GetTickCount();
            if (id == 0 && g_cfg.throttleMissingPad && s_failCode != JOYERR_NOERROR && nowTick - s_failTick < 500) {
                r = s_failCode;
            } else {
                LARGE_INTEGER q0, q1, qf;
                QueryPerformanceCounter(&q0);
                r = r_joyGetPosEx(id, info);
                QueryPerformanceCounter(&q1);
                QueryPerformanceFrequency(&qf);
                if (id == 0) {
                    s_ms += (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart;
                    if (++s_calls >= 120) {
                        if (s_logs < 6) { ++s_logs; Log("input: reading the pad takes %d.%02d ms per call on average (%d calls)", (int)(s_ms / s_calls), (int)(s_ms / s_calls * 100.0) % 100, s_calls); }
                        s_ms = 0;
                        s_calls = 0;
                    }
                    if (r != JOYERR_NOERROR) { s_failCode = r; s_failTick = nowTick; }
                    else s_failCode = JOYERR_NOERROR;
                }
                if (r == JOYERR_NOERROR && info) ConvertSonyLayout(id, info);
            }
            if (r != JOYERR_NOERROR && id == 0 && g_cfg.xinputFallback && XInputToJoy(info)) r = JOYERR_NOERROR;
        }
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

template <class F>
bool Hook(const char* dll, const char* name, F* real, void* replacement) {
    void** slot = FindImportSlot(GetModuleHandleW(nullptr), dll, name);
    if (!slot) return false;
    *real = (F)*slot;
    return SwapImport(slot, replacement, nullptr);
}

}  // namespace

bool IsOpen() { return g_open; }

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
// Everything that must happen when the menu opens or closes, whatever opened or closed it.
void OpenChanged(bool foreground) {
    g_editing = false;  // half-typed digits or a pending question must not wait for the next opening (and hit item 0)
    g_chipRow = -1;     // the button highlight starts on the first button again
    g_confirm = 0;
    g_dragging = false;
    FlushUnsaved();
    Log("menu: %s", g_open ? "opened" : "closed");
    if (g_open) {
        g_swallowEsc = false;
        return;
    }
    if (g_cfg.hideCursor) SetCursor(nullptr);
    if (Down(VK_ESCAPE)) g_swallowEsc = true;
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
    if (g_open) SyncDifficultyFromGame();
    if (g_open) EnsureCursorVisible();
    if (g_open) HandleInput(foreground);
    if (g_open != wasOpen) OpenChanged(foreground);  // F10, Esc or the photo mode key
    if (g_swallowEsc && !Down(VK_ESCAPE)) g_swallowEsc = false;  // a later Esc press is the game's again
    RememberKeys();
    if (!g_open) return;

    if (!OverlayBegin(device)) {
        Log("menu: could not set up drawing, closing");
        g_open = false;
        OpenChanged(foreground);
        return;
    }
    Draw();
    OverlayEnd();
}

}  // namespace ui
}  // namespace ufx
