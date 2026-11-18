// The "Residents" page of the F10 menu: where every resident of the town is right now by the game's own timetable, and where they
// go next. The rows come from ReadResidentSchedules (modules/npc_map.cpp: read-only, the game's table choice and lookup done again
// over its data) and are taken again twice a second while the page is shown.
// A row says what the timetable says, not where the person stands: the game can send a resident on the way before the next entry
// begins (FUN_004C6EC0 weighs the travel time), and some entries only pass them on ("next.table", "dummy").
#include "ui.h"

namespace ufx {
namespace ui {
namespace {

constexpr u32 kText = 0xFFF2F2F5, kSoft = 0xFFC4C4CE, kDim = 0xFF9696A4, kAccent = 0xFFD0343C, kLine = 0xFF2A2A34;  // the menu's colours
constexpr float kRowH = 22;
constexpr int kMaxRows = 50;  // NPC_LIST has 50 records

ResidentRow g_rows[kMaxRows];
unsigned char g_order[kMaxRows];  // the rows as shown: the residents that are on the timetable now first
int g_count = kScheduleNoGame;
int g_hour = 0, g_minute = 0, g_rainy = 0;
DWORD g_takenAt = 0;
bool g_taken = false;
int g_scroll = 0;

// The areas: the first two digits of a place. The names follow from the timetables themselves: the room names of the area
// ("georgesroom" and "prison", "autopsyroom", "suiteroom"), the names its building has among the town's outdoor spots
// ("00_06sheriff", "00_15diner", "00_12playhouse"), and who sleeps, works or minds the shop there. They were not checked in the
// game; an area without a clear answer keeps its number. People are named as NPC_LIST names them, like in the first column.
struct AreaName {
    int area;
    const wchar_t* uk;
    const wchar_t* en;
};
const AreaName kAreas[] = {
    {0, L"місто, надворі", L"town, outdoors"},
    {3, L"дім (Becky)", L"Becky's house"},
    {4, L"галерея", L"gallery"},
    {5, L"маєток (Harry)", L"Harry's mansion"},
    {6, L"відділок шерифа", L"sheriff's department"},
    {7, L"дім (Thomas, Carol)", L"Thomas and Carol's house"},
    {8, L"бар зі сценою", L"bar with a stage"},
    {10, L"лікарня", L"hospital"},
    {11, L"готель", L"hotel"},
    {12, L"театр", L"playhouse"},
    {13, L"крамниця Milk Barn", L"Milk Barn store"},
    {14, L"бар (Richard)", L"Richard's bar"},
    {15, L"закусочна", L"diner"},
    {16, L"дім (Jim)", L"Jim's house"},
    {18, L"дім (Keith, Lilly)", L"Keith and Lilly's house"},
    {19, L"дім (Sallie, Richard)", L"Sallie and Richard's house"},  // both sleep there by their timetables
    {20, L"майстерня (Quint)", L"Quint's workshop"},
    {21, L"крамниця (Wesley)", L"Wesley's shop"},
    {22, L"дім (George)", L"George's house"},
    {23, L"дім (Emily)", L"Emily's house"},
    {24, L"дім (Nick, Olivia)", L"Nick and Olivia's house"},
    {25, L"звалище (General)", L"the General's junkyard"},
    {26, L"парк", L"park"},
    {27, L"місце для риболовлі", L"fishing spot"},
    {28, L"заправка", L"gas station"},
    {29, L"кладовище", L"graveyard"},
    {30, L"дім (Fiona)", L"Fiona's house"},
    {31, L"дім (Sigourney)", L"Sigourney's house"},
    {35, L"банк", L"bank"},
};

// The commonest actions by their first word (the rest, such as ".lilly" or a number, is a variant of the animation). Many are
// Japanese words in Latin letters. An action that is not here is shown as the timetable has it.
struct ActionName {
    const char* word;
    const wchar_t* uk;
    const wchar_t* en;
};
const ActionName kActions[] = {
    {"idling", L"без діла", L"idling"},
    {"next", L"іде далі", L"moving on"},           // "next.table"
    {"dummy", L"пропускається", L"skipped"},        // the game goes straight on to the next entry (004D3780)
    {"mijitaku", L"збирається", L"getting ready"},
    {"sleep", L"спить", L"sleeping"},
    {"breakfast", L"сніданок", L"breakfast"},
    {"lunch", L"обід", L"lunch"},
    {"dinner", L"вечеря", L"dinner"},
    {"eating", L"їсть", L"eating"},
    {"tvwatching", L"дивиться телевізор", L"watching TV"},
    {"reading", L"читає", L"reading"},
    {"study", L"вчиться", L"studying"},
    {"work", L"працює", L"working"},
    {"working", L"працює", L"working"},
    {"deskwork", L"працює за столом", L"desk work"},
    {"miseban", L"за прилавком", L"minding the shop"},
    {"sanpo", L"прогулянка", L"a walk"},
    {"walk", L"прогулянка", L"a walk"},
    {"training", L"тренування", L"training"},
    {"kataduke", L"прибирає", L"tidying up"},
    {"katazuke", L"прибирає", L"tidying up"},
    {"kaji", L"хатні справи", L"housework"},
    {"cooking", L"готує", L"cooking"},
    {"jyunbi", L"готується", L"preparing"},
    {"drinking", L"п'є", L"drinking"},
    {"drunk", L"напідпитку", L"drunk"},
    {"sitdown", L"сидить", L"sitting"},
    {"rest", L"відпочиває", L"resting"},
    {"relax", L"відпочиває", L"relaxing"},
    {"singing", L"співає", L"singing"},
    {"fishing", L"рибалить", L"fishing"},
    {"shopping", L"покупки", L"shopping"},
    {"bath", L"у ванній", L"in the bath"},
    {"gohome", L"іде додому", L"going home"},
    {"kigaeru", L"переодягається", L"changing clothes"},
    {"change", L"переодягається", L"changing clothes"},
    {"lihabili", L"реабілітація", L"rehabilitation"},
    {"driving", L"за кермом", L"driving"},
    {"patrol", L"патрулює", L"on patrol"},
    {"front", L"на робочому місці", L"at the front desk"},   // the hotel's and the hospital's front rooms
    {"bring", L"несе обід", L"bringing lunch"},             // "bring.lunch"
    {"fusagikomu", L"сумує", L"brooding"},                  // Japanese: to mope, to be downcast
};

// Room names of the timetables (the word between the room number and the node), as a player would say them.
struct RoomName {
    const char* word;
    const wchar_t* uk;
    const wchar_t* en;
};
const RoomName kRooms[] = {
    {"front", L"приймальня", L"front desk"},
    {"living", L"вітальня", L"living room"},
    {"inside", L"всередині", L"inside"},
    {"property", L"на ділянці", L"on the grounds"},
    {"suiteroom", L"номер люкс", L"suite"},
    {"autopsyroom", L"морг", L"autopsy room"},
    {"annastree", L"біля дерева Анни", L"by Anna's tree"},
    {"kitchen", L"кухня", L"kitchen"},
    {"bedroom", L"спальня", L"bedroom"},
    {"office", L"кабінет", L"office"},
    {"counter", L"прилавок", L"counter"},
    {"garage", L"гараж", L"garage"},
    {"yard", L"двір", L"yard"},
    {"prison", L"камера", L"cell"},
};

bool English() { return g_cfg.language == 1; }
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool OnTimetable(const ResidentRow& r) { return Digit(r.nowPlace[0]); }  // a place with an area, not "none"

// True when `text` begins with `word` and the word ends there: at the end, a dot, a space, or a number before one of those.
bool FirstWordIs(const char* text, const char* word) {
    while (*word) {
        if (*text++ != *word++) return false;
    }
    while (Digit(*text)) ++text;
    return *text == 0 || *text == '.' || *text == ' ';
}

void Widen(const char* from, wchar_t* to, int capacity) {
    int n = 0;
    for (; from[n] && n < capacity - 1; ++n) to[n] = (wchar_t)(unsigned char)from[n];
    to[n] = 0;
}

void ActionText(const char* action, wchar_t* out, int capacity) {
    for (const ActionName& a : kActions) {
        if (!FirstWordIs(action, a.word)) continue;
        lstrcpynW(out, English() ? a.en : a.uk, capacity);
        return;
    }
    Widen(action, out, capacity);
}

// "ZZ_RRroom_NNnode" -> "area name (room)". Returns false for a place that names no area ("none", an empty one).
bool PlaceText(const char* place, wchar_t* out, int capacity) {
    if (!Digit(place[0])) return false;
    int area = place[0] - '0';
    const char* p = place + 1;
    if (Digit(*p)) area = area * 10 + (*p++ - '0');
    if (*p == '_') ++p;
    while (Digit(*p)) ++p;  // the room's number
    char room[32];
    int n = 0;
    for (; p[n] && p[n] != '_' && n < 31; ++n) room[n] = p[n];
    room[n] = 0;
    wchar_t name[40];
    wsprintfW(name, English() ? L"area %d" : L"зона %d", area);
    for (const AreaName& a : kAreas)
        if (a.area == area) lstrcpynW(name, English() ? a.en : a.uk, 40);
    wchar_t text[96];
    const wchar_t* roomName = nullptr;
    for (const RoomName& r : kRooms)
        if (lstrcmpiA(room, r.word) == 0) roomName = English() ? r.en : r.uk;
    if (roomName) wsprintfW(text, L"%s, %s", name, roomName);
    else if (room[0]) wsprintfW(text, L"%s (%S)", name, room);
    else lstrcpyW(text, name);
    lstrcpynW(out, text, capacity);
    return true;
}

// "07:30  shop (counter): working". An hour below 0 = the entry has no time.
void EntryText(int hour, int minute, const char* place, const char* action, wchar_t* out) {
    wchar_t where[96], what[48];
    if (!PlaceText(place, where, 96)) {
        lstrcpyW(out, English() ? L"not on the timetable" : L"поза розкладом");
        return;
    }
    if (hour >= 0) wsprintfW(out, L"%02d:%02d  %s", hour, minute, where);
    else lstrcpyW(out, where);
    ActionText(action, what, 48);
    if (what[0]) {
        lstrcatW(out, L": ");
        lstrcatW(out, what);
    }
}

// Draws `text` cut with "…" when it is wider than maxW.
void Fitted(const wchar_t* text, float x, float y, u32 color, int px, float maxW) {
    wchar_t cut[208];
    int len = lstrlenW(text);
    if (len > 200) len = 200;
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

void Refresh() {
    const DWORD now = GetTickCount();
    if (g_taken && now - g_takenAt < 500) return;
    const int before = g_count;
    g_count = ReadResidentSchedules(g_rows, kMaxRows, &g_hour, &g_minute, &g_rainy);
    g_takenAt = now;
    g_taken = true;
    int n = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < g_count; ++i)
            if (OnTimetable(g_rows[i]) == (pass == 0)) g_order[n++] = (unsigned char)i;
    if (g_count == before) return;
    if (g_count >= 0) Log("residents: the schedule viewer shows %d residents (game time %02d:%02d, %s timetables)", g_count, g_hour, g_minute, g_rainy ? "rain" : "dry weather");
    else Log("residents: the schedule viewer has nothing to show (%s)", g_count == kScheduleNoGame ? "no game world is played" : g_count == kScheduleNotInUse ? "the game runs no timetables here" : "the module is off");
}

}  // namespace

void ResidentsScroll(int rows) { g_scroll += rows; }

void ResidentsPage(float left, float top, float right, float bottom) {
    Refresh();
    const bool en = English();
    DrawText(en ? L"wheel or ↑ ↓ scroll     Tab section     F10 or Esc close" : L"колесо або ↑ ↓ прокрутка     Tab розділ     F10 або Esc закрити", left, bottom + 8, kDim, 13);
    if (g_count < 0) {
        const wchar_t* why = en ? L"Off: SafeMode is on, or this DP.exe is not the supported build." : L"Вимкнено: увімкнено безпечний режим (SafeMode) або це не та версія DP.exe.";
        if (g_count == kScheduleNoGame) why = en ? L"The timetable shows while a game is played: load a save or start a game." : L"Розклад видно під час гри: завантажте збереження або почніть гру.";
        else if (g_count == kScheduleNotInUse) why = en ? L"Right now the game does not lead the residents by a timetable (some chapters and places)." : L"Зараз гра не веде жителів за розкладом (так буває в деяких розділах і місцях).";
        Fitted(why, left, top, kSoft, 15, right - left);
        return;
    }
    wchar_t line[160];
    wsprintfW(line, en ? L"Game time %02d:%02d. Timetables for %s." : L"Час у грі %02d:%02d. Розклади для %s.", g_hour, g_minute,
              g_rainy ? (en ? L"rain" : L"дощу") : (en ? L"dry weather" : L"сухої погоди"));
    Fitted(line, left, top, kText, 15, right - left);
    Fitted(en ? L"By the game's timetable: a person may still be on the way."
              : L"За розкладом гри: людина може бути ще в дорозі.",
           left, top + 22, kDim, 13, right - left);

    const float nameW = 112, width = right - left;
    const float nowX = left + nameW, nextX = nowX + (width - nameW) * 0.54f;
    const float headY = top + 50, listTop = headY + 24;
    DrawText(en ? L"Resident" : L"Житель", left, headY, kAccent, 13);
    DrawText(en ? L"Now (since)" : L"Зараз (від)", nowX, headY, kAccent, 13);
    DrawText(en ? L"Next (from)" : L"Далі (від)", nextX, headY, kAccent, 13);
    FillRect(left, headY + 19, width, 1, kLine);

    int visible = (int)((bottom - listTop) / kRowH);
    if (visible < 1) visible = 1;
    if (g_scroll > g_count - visible) g_scroll = g_count - visible;
    if (g_scroll < 0) g_scroll = 0;
    if (g_count == 0) DrawText(en ? L"The loaded timetables have no residents." : L"У завантажених розкладах немає жителів.", left, listTop, kSoft, 15);
    for (int i = 0; i < visible && g_scroll + i < g_count; ++i) {
        const ResidentRow& r = g_rows[g_order[g_scroll + i]];
        const float y = listTop + (float)i * kRowH;
        wchar_t text[208];
        Widen(r.name, text, 24);
        Fitted(text, left, y, kText, 15, nameW - 8);
        EntryText(r.nowHour, r.nowMinute, r.nowPlace, r.nowAction, text);
        const bool onIt = OnTimetable(r);
        Fitted(text, nowX, y, onIt ? kSoft : kDim, 14, nextX - nowX - 12);
        if (!onIt) continue;
        if (r.nextHour < 0) lstrcpyW(text, en ? L"all day" : L"весь день");
        else EntryText(r.nextHour, r.nextMinute, r.nextPlace, r.nextAction, text);
        Fitted(text, nextX, y, kDim, 14, right - nextX);
    }
    if (g_count > visible) {  // the same slim scroll bar as the settings pages
        const float trackH = (float)visible * kRowH, frac = (float)visible / (float)g_count;
        const float pos = (float)g_scroll / (float)(g_count - visible);
        FillRect(right + 14, listTop, 4, trackH, kLine);
        FillRect(right + 12, listTop + (trackH - trackH * frac) * pos, 8, trackH * frac, kDim);
    }
}

}  // namespace ui
}  // namespace ufx
