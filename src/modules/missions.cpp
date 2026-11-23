// Quests of the editor and what runs them. A quest is a text file, UltimateFix\missions\missionN.ini (UTF-8 or UTF-16, the
// reader takes both), written by the quest editor (the page the mod serves, editor_server.cpp) or by hand:
//
//   [Mission]  Name, Author, Banner (the start banner; empty = none), EndText (shown when the quest is done), Steps (1..16)
//   [StepN]    Kind      0 a talk with a choice of two answers, 1 a line without a choice, 2 reach a place, 3 wait for an hour
//              Title     the objective shown when the step begins
//              talk / line: Resident (1..49), Placed (0 = in front of York), X Y Z Yaw (thousandths), MotionSet, Motion,
//                           Line, Question, Yes, No, AfterYes, AfterNo
//              reach:       X Y Z, Radius (metres), Zone (-1 = any)
//              wait:        Hour (0..23)
//              when it begins: Teleport (1 = York is put at TX TY TZ TYaw), SetHour (-1 = leave), Weather (0 leave, 1 clear,
//                           2 cloudy, 3 rain), Car (0 none, 1..16 of the spawner's list), CarPlace (0 behind York, 1 well ahead of him,
//                           2 at CarX CarY CarZ CarYaw)
//              when it is done: GiveItem, GiveCount, Money (a talk: for the first answer only)
//              then:        NextYes, NextNo (0 = the following step, -1 = the quest ends, N = step N; "No" only for a talk)
//
// A file of the first version ([Actor] and [Talk]) is read as a quest of one talk. One character stands at a time: the one of
// the running step. Where a quest stopped is kept in missions\progress.ini, never in the game's save.
//
// Running uses what the mission lab proved in the game: the mod's own event script (number 900) with its texts in the game's
// messages, rewritten in place for each step, and the doll for the character (combat_strafe.cpp).
#include "../base.h"
#include "../lab_dsb.h"
#include "../sites.h"

namespace ufx {

Mission g_mission;

namespace {

volatile int g_request = 0;  // 2 = run (from the saved step), 3 = stop, 4 = run from the first step
volatile int g_requestSlot = 0;  // the slot the request is for (0 = the loaded one)
bool g_active = false;
int g_step = 0;              // the running step, 0-based
int g_phase = 0;             // 0 = the step has to begin, 1 = it waits for its condition
volatile int g_talkDone = 0; // 1 = the talk ended with the second answer (or had none), 2 = with the first
int g_bannerWait = 0;        // player updates to wait for the start banner before the first step begins
wchar_t g_state[2][40] = {L"не запущено", L"not running"};

// The row shows a few words; the reason goes to a notice
void SetState(const wchar_t* uk, const wchar_t* en, const wchar_t* whyUk = nullptr, const wchar_t* whyEn = nullptr) {
    lstrcpynW(g_state[0], uk, 38);
    lstrcpynW(g_state[1], en, 38);
    if (whyUk) ShowNotice(uk, en, whyUk, whyEn, 6);
}

void ProgressPath(wchar_t* out) {
    lstrcpyW(out, g_paths.dataDir);
    lstrcatW(out, L"missions\\progress.ini");
}
void SaveProgress(int stepOneBased, int done) {
    wchar_t path[MAX_PATH + 40], sec[16], v[16];
    ProgressPath(path);
    wsprintfW(sec, L"mission%d", g_mission.slot);
    wsprintfW(v, L"%d", stepOneBased);
    WritePrivateProfileStringW(sec, L"Step", v, path);
    if (done) {
        wsprintfW(v, L"%d", (int)GetPrivateProfileIntW(sec, L"Done", 0, path) + 1);
        WritePrivateProfileStringW(sec, L"Done", v, path);
    }
}
int SavedStep() {
    wchar_t path[MAX_PATH + 40], sec[16];
    ProgressPath(path);
    wsprintfW(sec, L"mission%d", g_mission.slot);
    return (int)GetPrivateProfileIntW(sec, L"Step", 0, path);
}

// A new file: an example of three steps the author can change
void CreateFileFor(const wchar_t* path) {
    static const wchar_t kText[] =
        L"\xFEFF; Квест редактора UltimateFIXDP. Зручніше редагувати в редакторі: F10 -> Редактор місій -> «Відкрити редактор».\r\n"
        L"[Mission]\r\nName=Вишневий пиріг\r\nAuthor=\r\nBanner=Вишневий пиріг\r\nEndText=Дякую за допомогу, агенте.\r\nSteps=2\r\n\r\n"
        L"[Step1]\r\nKind=0\r\nTitle=Поговоріть із персонажем\r\nResident=6\r\nPlaced=0\r\nMotionSet=1\r\n"
        L"Line=Доброго дня, агенте Морган.\r\nQuestion=Хочете вишневого пирога?\r\nYes=Так\r\nNo=Ні\r\n"
        L"AfterYes=Смачного!\r\nAfterNo=Тоді іншим разом.\r\nGiveItem=100\r\nGiveCount=1\r\nNextNo=-1\r\n\r\n"
        L"[Step2]\r\nKind=1\r\nTitle=Поговоріть ще раз\r\nResident=6\r\nPlaced=0\r\nMotionSet=1\r\nLine=Заходьте ще, агенте.\r\nMoney=50\r\n";
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD put;
    WriteFile(f, kText, (DWORD)(sizeof(kText) - sizeof(wchar_t)), &put, nullptr);
    CloseHandle(f);
}

// A text of the file as the message language wants it: UTF-8, braces taken out (they would be read as markers), one line
// with the empty line the game's windows have after a single one
bool SetMessage(u32 id, const wchar_t* text, const char* before, const char* after) {
    wchar_t clean[220];
    int n = 0;
    for (const wchar_t* p = text; *p && n < 200; ++p)
        if (*p != L'{' && *p != L'}' && *p >= 0x20) clean[n++] = *p;
    clean[n] = 0;
    char utf8[900];
    int at = lstrlenA(before);
    lstrcpyA(utf8, before);
    at += WideCharToMultiByte(CP_UTF8, 0, clean, n, utf8 + at, 700, nullptr, nullptr);
    utf8[at] = 0;
    lstrcatA(utf8, after);
    return LabSetText(id, utf8);
}

void Stop(const wchar_t* uk, const wchar_t* en) {
    g_active = false;
    g_talkDone = 0;
    DollTalkDefault();
    RequestDoll(2);
    SetState(uk, en);
}

void Finish() {
    Mission& m = g_mission;
    Stop(L"квест завершено", L"quest done");
    SaveProgress(0, 1);
    ShowNotice(L"Квест завершено", L"Quest done", m.endText[0] ? m.endText : m.name, m.endText[0] ? m.endText : m.name, 8);
    Log("mission: slot %d done", m.slot);
}

// The step begins: what it changes in the world, its character, its objective
bool BeginStep(int index) {
    Mission& m = g_mission;
    MissionStep& s = m.steps[index];
    float place[4];
    if (!PlayerPlace(place)) return false;
    if (s.teleport) {
        float to[4] = {(float)s.tx / 1000.f, (float)s.ty / 1000.f, (float)s.tz / 1000.f, (float)s.tyaw / 1000.f};
        CheatYorkTo(to);
        for (int i = 0; i < 4; ++i) place[i] = to[i];
    }
    if (s.setHour >= 0 && s.setHour <= 23) CheatSetHour(s.setHour);
    if (s.weather >= 1 && s.weather <= 3) CheatWeather(s.weather);
    if (s.car >= 1 && s.car <= 16) {
        const float at[4] = {(float)s.carX / 1000.f, (float)s.carY / 1000.f, (float)s.carZ / 1000.f, (float)s.carYaw / 1000.f};
        CheatSpawnCarAt(CarNumberOf(s.car), s.carPlace == 2 ? 3 : s.carPlace == 1 ? 2 : 1, at);
    }
    if (s.kind <= 1) {
        bool ok = SetMessage(kMsMsgLine, s.line, "", "{NEXT_SEGMENT}");
        if (s.kind == 0)
            ok = ok && SetMessage(kMsMsgQuestion, s.question, "", "{NEXT_SEGMENT}") && SetMessage(kMsMsgYes, s.yes[0] ? s.yes : L"Так", "", "") &&
                 SetMessage(kMsMsgNo, s.no[0] ? s.no : L"Ні", "", "") && SetMessage(kMsMsgAfterYes, s.afterYes, "", "{NEXT_SEGMENT}") &&
                 SetMessage(kMsMsgAfterNo, s.afterNo, "", "{NEXT_SEGMENT}");
        if (!ok) {
            Stop(L"текст не закодовано", L"a text failed");
            ShowNotice(L"Квест: текст не закодовано", L"Quest: a text failed", L"Якийсь текст кроку не вдалося закодувати: літера без клітинки у шрифті або задовгий рядок. Деталі в ufx.log.",
                       L"A text of the step could not be encoded: a letter without a cell in the font, or a line too long. See ufx.log.", 8);
            Log("mission: step %d: a text could not be encoded", index + 1);
            return false;
        }
        LabSetGiveItem(0, 1);  // the runner gives the reward itself (MissionTalkOver)
        if (s.placed) {
            place[0] = (float)s.x / 1000.f;
            place[1] = (float)s.y / 1000.f;
            place[2] = (float)s.z / 1000.f;
            place[3] = (float)s.yaw / 1000.f;
        } else {  // in front of York, facing him
            const float yaw = place[3];
            float sn, cs;
            __asm__("fsincos" : "=t"(cs), "=u"(sn) : "0"(yaw));
            place[0] += sn * 8.f;
            place[2] += cs * 8.f;
            place[3] = yaw + 3.14159265f;
        }
        DollPutAt(s.resident, place, s.motionSet, s.motion, s.kind == 0 ? kMsEvTalkGift : kMsEvSay);
    } else {
        DollTalkDefault();
        RequestDoll(2);  // no character in this step
    }
    g_talkDone = 0;
    g_phase = 1;
    SaveProgress(index + 1, 0);
    wchar_t st[2][40];
    wsprintfW(st[0], L"крок %d із %d", index + 1, m.stepCount);
    wsprintfW(st[1], L"step %d of %d", index + 1, m.stepCount);
    SetState(st[0], st[1]);
    static const wchar_t* const kDefUk[4] = {L"Підійдіть до персонажа й натисніть E.", L"Підійдіть до персонажа й натисніть E.", L"Дійдіть до позначеного місця.", L"Дочекайтеся потрібної години."};
    static const wchar_t* const kDefEn[4] = {L"Walk up to the character and press E.", L"Walk up to the character and press E.", L"Reach the place.", L"Wait for the hour."};
    wchar_t head[2][48];
    wsprintfW(head[0], L"Ціль %d / %d", index + 1, m.stepCount);
    wsprintfW(head[1], L"Objective %d / %d", index + 1, m.stepCount);
    ShowNotice(head[0], head[1], s.title[0] ? s.title : kDefUk[s.kind & 3], s.title[0] ? s.title : kDefEn[s.kind & 3], 7);
    Log("mission: slot %d step %d of %d begins (kind %d, resident %d, teleport %d, hour %d, weather %d, car %d)", m.slot, index + 1, m.stepCount, s.kind,
        s.kind <= 1 ? s.resident : 0, s.teleport, s.setHour, s.weather, s.car);
    return true;
}

// The step is done: its reward (for a talk only with the first answer), then the step it leads to
void CompleteStep(bool first) {
    Mission& m = g_mission;
    const MissionStep& s = m.steps[g_step];
    if (first) {
        if (s.giveItem >= 1 && s.giveItem <= 0x16D) {
            const int r = GiveItemNow(s.giveItem, s.giveCount < 1 ? 1 : s.giveCount);
            Log("mission: step %d: item %d x %d given (the game answered %d)", g_step + 1, s.giveItem, s.giveCount, r);
            if (r == -2) ShowNotice(L"Гра не дала предмет", L"The game gave no item", L"Він уже є або для нього немає місця.", L"York has it already or there is no room.", 4);
            else ShowNotice(r == -3 ? L"Предмет пішов у сховище" : L"Отримано предмет", r == -3 ? L"The item went to the storage" : L"Item obtained", L"Дивіться інвентар.",
                            L"See the inventory.", 4);
        }
        if (s.money > 0) CheatMoney(s.money > 9999 ? 9999 : s.money);
    }
    int next = first ? s.nextYes : s.nextNo;
    Log("mission: step %d done (%s), next %d", g_step + 1, first ? "first answer / reached" : "second answer", next);
    if (next < 0) { Finish(); return; }
    next = next == 0 ? g_step + 1 : next - 1;
    if (next >= m.stepCount) { Finish(); return; }
    g_step = next;
    g_phase = 0;
}

void Run(bool fromStart) {
    Mission& m = g_mission;
    float place[4];
    if (!m.stepCount) {
        SetState(L"немає кроків", L"no steps", L"У файлі квесту немає жодного кроку.", L"The quest's file has no steps.");
        return;
    }
    if (!LabTextsIn()) {
        SetState(L"немає текстів", L"no texts", L"Тексти квестів не додано до повідомлень гри. Деталі в ufx.log.", L"The quest texts are not in the game's messages. See ufx.log.");
        Log("mission: the lab texts are not in the game's messages, nothing started");
        return;
    }
    if (*(volatile u32*)(uintptr_t)kGameplayModeVa != 3 || (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u) || !PlayerPlace(place)) {
        SetState(L"не зараз", L"not now", L"Потрібна вільна гра, пішки, не сцена.", L"It needs free play on foot, no scene.");
        return;
    }
    int from = fromStart ? 0 : SavedStep() - 1;
    if (from < 0 || from >= m.stepCount) from = 0;
    g_step = from;
    g_phase = 0;
    g_talkDone = 0;
    g_bannerWait = 0;
    g_active = true;
    if (m.banner[0] && from == 0 &&
        SetMessage(kMsMsgBanner, m.banner, "{SET_COLOR color=135, 240, 120}", u8"{NEXT_SEGMENT}                                 --Початок{RESET_COLOR}") &&
        LabRunEvent(kMsEvBanner) != -1)
        g_bannerWait = 30;
    Log("mission: slot %d started at step %d of %d (banner %s)", m.slot, from + 1, m.stepCount, g_bannerWait ? "yes" : "no");
}

// The file's text as UTF-16, whatever it was saved as: UTF-16 with a mark, UTF-8 with or without one. The caller frees it.
wchar_t* ReadFileText(const wchar_t* path, int* chars) {
    *chars = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    const DWORD size = GetFileSize(f, nullptr);
    u8* raw = size && size < 400000 ? (u8*)VirtualAlloc(nullptr, size + 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    DWORD got = 0;
    if (raw) ReadFile(f, raw, size, &got, nullptr);
    CloseHandle(f);
    if (!raw) return nullptr;
    wchar_t* out = (wchar_t*)VirtualAlloc(nullptr, (got + 4) * sizeof(wchar_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (out) {
        if (got >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
            *chars = (int)(got - 2) / 2;
            for (int i = 0; i < *chars; ++i) out[i] = (wchar_t)(raw[2 + i * 2] | raw[3 + i * 2] << 8);
        } else {
            const int skip = got >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF ? 3 : 0;
            *chars = MultiByteToWideChar(CP_UTF8, 0, (const char*)raw + skip, (int)got - skip, out, (int)got + 2);
        }
        out[*chars] = 0;
    }
    VirtualFree(raw, 0, MEM_RELEASE);
    return out;
}

bool SameText(const wchar_t* a, int n, const wchar_t* b) {
    for (int i = 0; i < n; ++i)
        if (!b[i] || (a[i] | 0x20) != (b[i] | 0x20)) return false;
    return !b[n];
}

int ToInt(const wchar_t* p) {
    bool neg = *p == L'-';
    if (neg) ++p;
    int v = 0;
    for (; *p >= L'0' && *p <= L'9'; ++p) v = v * 10 + (*p - L'0');
    return neg ? -v : v;
}

// One key of a step: where its value goes
void StepField(MissionStep& s, const wchar_t* key, int keyLen, const wchar_t* value) {
    struct Num { const wchar_t* key; int MissionStep::*at; };
    struct Txt { const wchar_t* key; wchar_t* at; int cap; };
    static const Num nums[] = {
        {L"Kind", &MissionStep::kind}, {L"Resident", &MissionStep::resident}, {L"Placed", &MissionStep::placed}, {L"X", &MissionStep::x},
        {L"Y", &MissionStep::y}, {L"Z", &MissionStep::z}, {L"Yaw", &MissionStep::yaw}, {L"MotionSet", &MissionStep::motionSet},
        {L"Motion", &MissionStep::motion}, {L"Radius", &MissionStep::radius}, {L"Zone", &MissionStep::zone}, {L"Hour", &MissionStep::hour},
        {L"NextYes", &MissionStep::nextYes}, {L"NextNo", &MissionStep::nextNo}, {L"GiveItem", &MissionStep::giveItem},
        {L"GiveCount", &MissionStep::giveCount}, {L"Money", &MissionStep::money}, {L"Car", &MissionStep::car}, {L"SetHour", &MissionStep::setHour},
        {L"Weather", &MissionStep::weather}, {L"Teleport", &MissionStep::teleport}, {L"TX", &MissionStep::tx}, {L"TY", &MissionStep::ty},
        {L"TZ", &MissionStep::tz}, {L"TYaw", &MissionStep::tyaw}, {L"CarPlace", &MissionStep::carPlace}, {L"CarX", &MissionStep::carX},
        {L"CarY", &MissionStep::carY}, {L"CarZ", &MissionStep::carZ}, {L"CarYaw", &MissionStep::carYaw}};
    for (const Num& n : nums)
        if (SameText(key, keyLen, n.key)) { s.*(n.at) = ToInt(value); return; }
    const Txt txts[] = {{L"Title", s.title, 96}, {L"Line", s.line, 200}, {L"Question", s.question, 200}, {L"Yes", s.yes, 40}, {L"No", s.no, 40},
                        {L"AfterYes", s.afterYes, 200}, {L"AfterNo", s.afterNo, 200}};
    for (const Txt& t : txts)
        if (SameText(key, keyLen, t.key)) { lstrcpynW(t.at, value, t.cap); return; }
}

}  // namespace

void MissionPath(int slot, wchar_t* out) {
    lstrcpyW(out, g_paths.dataDir);
    lstrcatW(out, L"missions");
    CreateDirectoryW(out, nullptr);
    wsprintfW(out + lstrlenW(out), L"\\mission%d.ini", slot);
}

void MissionLoad(int slot) {
    if (slot < 1) slot = 1;
    if (slot > 9) slot = 9;
    Mission& m = g_mission;
    wchar_t path[MAX_PATH + 40];
    MissionPath(slot, path);
    for (u32 i = 0; i < sizeof(Mission); ++i) ((volatile u8*)&m)[i] = 0;  // (no memset in this build)
    m.slot = slot;
    for (MissionStep& s : m.steps) {
        s.resident = 6;
        s.motionSet = 1;
        s.giveCount = 1;
        s.radius = 15;
        s.zone = -1;
        s.setHour = -1;
    }
    int chars = 0, steps = -1;
    bool legacy = false;
    wchar_t* text = ReadFileText(path, &chars);
    m.exists = text != nullptr;
    wchar_t section[32] = L"";
    for (int i = 0; text && i < chars;) {
        int end = i;
        while (end < chars && text[end] != L'\r' && text[end] != L'\n') ++end;
        int a = i, b = end;
        while (a < b && (text[a] == L' ' || text[a] == L'\t' || text[a] == 0xFEFF)) ++a;
        while (b > a && (text[b - 1] == L' ' || text[b - 1] == L'\t')) --b;
        if (a < b && text[a] == L'[' && text[b - 1] == L']') {
            lstrcpynW(section, text + a + 1, b - a - 1 < 31 ? b - a - 1 : 31);
        } else if (a < b && text[a] != L';') {
            int eq = a;
            while (eq < b && text[eq] != L'=') ++eq;
            int ke = eq, vs = eq + 1;
            while (ke > a && (text[ke - 1] == L' ' || text[ke - 1] == L'\t')) --ke;
            while (vs < b && (text[vs] == L' ' || text[vs] == L'\t')) ++vs;
            if (eq < b) {
                wchar_t value[220];
                lstrcpynW(value, text + vs, b - vs + 1 < 220 ? b - vs + 1 : 220);
                if (vs >= b) value[0] = 0;
                const wchar_t* key = text + a;
                const int kl = ke - a, sl = lstrlenW(section);
                if (SameText(section, sl, L"Mission")) {
                    if (SameText(key, kl, L"Name")) lstrcpynW(m.name, value, 64);
                    else if (SameText(key, kl, L"Author")) lstrcpynW(m.author, value, 64);
                    else if (SameText(key, kl, L"Banner")) lstrcpynW(m.banner, value, 96);
                    else if (SameText(key, kl, L"EndText")) lstrcpynW(m.endText, value, 200);
                    else if (SameText(key, kl, L"Steps")) steps = ToInt(value);
                } else if (sl > 4 && SameText(section, 4, L"Step")) {
                    const int n = ToInt(section + 4);
                    if (n >= 1 && n <= kMissionSteps) StepField(m.steps[n - 1], key, kl, value);
                } else if (SameText(section, sl, L"Actor") || SameText(section, sl, L"Talk")) {  // a file of the first version: one talk
                    legacy = true;
                    StepField(m.steps[0], key, kl, value);
                }
            }
        }
        i = end;
        while (i < chars && (text[i] == L'\r' || text[i] == L'\n')) ++i;
    }
    if (text) VirtualFree(text, 0, MEM_RELEASE);
    if (steps < 0) steps = legacy ? 1 : 0;
    m.stepCount = steps > kMissionSteps ? kMissionSteps : steps;
    for (MissionStep& s : m.steps) {
        s.kind &= 3;
        s.motionSet &= 3;
        if (s.resident < 1 || s.resident > 49) s.resident = 6;
        if (s.giveItem < 0 || s.giveItem > 0x16D) s.giveItem = 0;
        if (s.radius < 2) s.radius = 2;
        if (s.hour < 0 || s.hour > 23) s.hour = 0;
        if (s.nextYes > kMissionSteps) s.nextYes = 0;
        if (s.nextNo > kMissionSteps) s.nextNo = 0;
    }
}

// The file in Notepad (made first when the slot is empty)
void MissionOpenFile() {
    wchar_t path[MAX_PATH + 40], cmd[MAX_PATH + 80];
    MissionPath(g_mission.slot, path);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        CreateFileFor(path);
        MissionLoad(g_mission.slot);
    }
    wsprintfW(cmd, L"notepad.exe \"%s\"", path);
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    const bool ok = CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) != 0;
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    Log("mission: the file of slot %d %s", g_mission.slot, ok ? "was opened in Notepad" : "could not be opened (Notepad did not start)");
}

void MissionRequest(int what, int slot) {
    g_requestSlot = slot;
    g_request = what;
    PhotoThaw(180);  // the game thread does it, and the model loads from disk
}
bool MissionActive() { return g_active; }
int MissionStepNow() { return g_active ? g_step + 1 : 0; }
const wchar_t* MissionState() { return g_state[g_cfg.language == 1 ? 1 : 0]; }
void MissionTalkOver(bool firstAnswer) { g_talkDone = firstAnswer ? 2 : 1; }

void MissionTick(void* player) {
    const int req = g_request;
    if (req) {
        g_request = 0;
        if (req == 2 || req == 4) {
            MissionLoad(g_requestSlot ? g_requestSlot : g_mission.slot ? g_mission.slot : 1);
            Run(req == 4);
        } else if (req == 3) {
            Stop(L"зупинено", L"stopped");
            Log("mission: slot %d stopped", g_mission.slot);
        }
    }
    if (!g_active) return;
    Mission& m = g_mission;
    if (g_bannerWait > 0) {  // the start banner is an event of ours: the first step begins when it has had its time
        --g_bannerWait;
        return;
    }
    if (g_phase == 0) {
        // a step with a character begins in free play only (the doll goes with a scene)
        if (*(volatile u32*)(uintptr_t)kGameplayModeVa != 3 || (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u)) return;
        // behind the wheel (player state 87h, 88h while getting out) a character would be put under the car: the step waits
        const u32 state = player ? *(volatile u32*)((u8*)player + 0x654) : 0;
        if (m.steps[g_step].kind <= 1 && (state == 0x87 || state == 0x88)) {
            static int told = -1;
            if (told != g_step) {
                told = g_step;
                Log("mission: step %d waits: York is in a car", g_step + 1);
                ShowNotice(L"Квест чекає", L"The quest waits", L"Вийдіть з машини: персонаж з'явиться, коли Йорк стоятиме на землі.",
                           L"Get out of the car: the character appears once York stands on the ground.", 5);
            }
            return;
        }
        BeginStep(g_step);
        return;
    }
    const MissionStep& s = m.steps[g_step];
    if (s.kind <= 1) {
        const int done = g_talkDone;
        if (done) {
            g_talkDone = 0;
            CompleteStep(s.kind == 1 || done == 2);
        } else if ((*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u) && !DollStands()) {
            g_phase = 0;  // a scene of the game took the character away: the step begins again after it
        }
        return;
    }
    if (s.kind == 2) {
        float p[4];
        if (!PlayerPlace(p)) return;
        const u8* game = *(const u8* volatile*)(uintptr_t)kGameManagerPtrVa;
        if (s.zone >= 0 && game && *(volatile int*)(game + 0x834FE0) != s.zone) return;
        const float dx = p[0] - (float)s.x / 1000.f, dz = p[2] - (float)s.z / 1000.f, r = (float)s.radius * 4.f;  // about 4 units to a metre
        if (dx * dx + dz * dz <= r * r) CompleteStep(true);
        return;
    }
    const u32 timer = *(volatile u32*)(uintptr_t)0xBE1EA0;
    if (timer > 0x10000 && !IsBadReadPtr((void*)(uintptr_t)timer, 0x18) && *(volatile int*)(timer + 0xC) == s.hour) CompleteStep(true);
}

}  // namespace ufx
