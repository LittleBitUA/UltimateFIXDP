// The mission editor's data and what runs a mission (first version: one character, one talk with a yes / no choice, an item
// on "yes", a start banner). A mission is a text file, UltimateFix\missions\missionN.ini (UTF-16 with a byte order mark, so
// Notepad and the Windows profile functions both read Ukrainian in it):
//
//   [Mission]  Name, Author, Banner (the text of the start banner; empty = none)
//   [Actor]    Resident (1..49 of the game's NPC list), Placed (0 = in front of York when the mission starts),
//              X, Y, Z, Yaw (thousandths), MotionSet, Motion (0 = stands calm)
//   [Talk]     Line, Question, Yes, No, AfterYes, AfterNo, GiveItem (0 = nothing), GiveCount
//
// Running it uses what the mission lab proved in the game: the mod's own event script (number 900) with its texts in the
// game's messages, rewritten in place for the mission, and the doll for the character (combat_strafe.cpp).
#include "../base.h"
#include "../lab_dsb.h"
#include "../sites.h"

namespace ufx {

Mission g_mission;

namespace {

volatile int g_request = 0;  // 1 = remember York's place, 2 = run, 3 = stop
bool g_active = false;
wchar_t g_state[2][40] = {L"не запущена", L"not running"};

// The row shows a few words; the reason goes to a notice
void SetState(const wchar_t* uk, const wchar_t* en, const wchar_t* whyUk = nullptr, const wchar_t* whyEn = nullptr) {
    lstrcpynW(g_state[0], uk, 38);
    lstrcpynW(g_state[1], en, 38);
    if (whyUk) ShowNotice(uk, en, whyUk, whyEn, 6);
}

void MissionPath(int slot, wchar_t* out) {
    lstrcpyW(out, g_paths.dataDir);
    lstrcatW(out, L"missions");
    CreateDirectoryW(out, nullptr);
    wsprintfW(out + lstrlenW(out), L"\\mission%d.ini", slot);
}

// A new file: UTF-16 with a byte order mark and an example the author can change
void CreateFileFor(const wchar_t* path) {
    static const wchar_t kText[] =
        L"\xFEFF; Місія редактора UltimateFIXDP. Змініть тексти й збережіть файл (кодування лишіть як є: UTF-16).\r\n"
        L"; У меню F10 -> Редактор місій натисніть «Перечитати файл».\r\n"
        L"[Mission]\r\nName=Вишневий пиріг\r\nAuthor=\r\nBanner=Вишневий пиріг\r\n\r\n"
        L"[Actor]\r\nResident=6\r\nPlaced=0\r\nX=0\r\nY=0\r\nZ=0\r\nYaw=0\r\nMotionSet=1\r\nMotion=0\r\n\r\n"
        L"[Talk]\r\nLine=Доброго дня, агенте Морган.\r\nQuestion=Хочете вишневого пирога?\r\nYes=Так\r\nNo=Ні\r\n"
        L"AfterYes=Смачного!\r\nAfterNo=Тоді іншим разом.\r\nGiveItem=100\r\nGiveCount=1\r\n";
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD put;
    WriteFile(f, kText, (DWORD)(sizeof(kText) - sizeof(wchar_t)), &put, nullptr);
    CloseHandle(f);
}

void PutInt(const wchar_t* path, const wchar_t* section, const wchar_t* key, int v) {
    wchar_t num[16];
    wsprintfW(num, L"%d", v);
    WritePrivateProfileStringW(section, key, num, path);
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

void Run() {
    Mission& m = g_mission;
    float place[4];
    if (!LabTextsIn()) {
        SetState(L"Місія: немає текстів", L"Mission: no texts", L"Тексти місій не додано до повідомлень гри. Деталі в ufx.log.", L"The mission texts are not in the game's messages. See ufx.log.");
        Log("mission: the lab texts are not in the game's messages, nothing started");
        return;
    }
    if (*(volatile u32*)(uintptr_t)kGameplayModeVa != 3 || (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u) || !PlayerPlace(place)) {
        SetState(L"Місія: не зараз", L"Mission: not now", L"Потрібна вільна гра, пішки, не сцена.", L"It needs free play on foot, no scene.");
        return;
    }
    bool ok = SetMessage(kMsMsgLine, m.line, "", "{NEXT_SEGMENT}") && SetMessage(kMsMsgQuestion, m.question, "", "{NEXT_SEGMENT}") &&
              SetMessage(kMsMsgYes, m.yes, "", "") && SetMessage(kMsMsgNo, m.no, "", "") &&
              SetMessage(kMsMsgAfterYes, m.afterYes, "", "{NEXT_SEGMENT}") && SetMessage(kMsMsgAfterNo, m.afterNo, "", "{NEXT_SEGMENT}");
    if (m.banner[0])
        ok = ok && SetMessage(kMsMsgBanner, m.banner, "{SET_COLOR color=135, 240, 120}", "{NEXT_SEGMENT}                                 --Start{RESET_COLOR}");
    if (!ok) {
        SetState(L"Місія: текст не закодовано", L"Mission: a text failed", L"Якийсь текст не вдалося закодувати: літера без клітинки у шрифті або задовгий рядок. Деталі в ufx.log.",
                 L"A text could not be encoded: a letter without a cell in the font, or a line too long. See ufx.log.");
        return;
    }
    const bool gift = m.giveItem >= 1 && m.giveItem <= 0x16D;
    if (gift) LabSetGiveItem(m.giveItem);
    if (m.placed) {
        place[0] = (float)m.x / 1000.f;
        place[1] = (float)m.y / 1000.f;
        place[2] = (float)m.z / 1000.f;
        place[3] = (float)m.yaw / 1000.f;
    } else {  // in front of York, facing him (the doll's own rule, written out)
        const float yaw = place[3];
        float s, c;
        __asm__("fsincos" : "=t"(c), "=u"(s) : "0"(yaw));
        place[0] += s * 8.f;
        place[2] += c * 8.f;
        place[3] = yaw + 3.14159265f;
    }
    DollPutAt(m.resident, place, m.motionSet, m.motion, gift ? kMsEvTalkGift : kMsEvTalkPlain);
    g_active = true;
    if (m.banner[0]) LabRunEvent(kMsEvBanner);
    SetState(L"Місія запущена", L"Mission running", L"Підійдіть до персонажа й натисніть E.", L"Walk up to the character and press E.");
    Log("mission: slot %d started (resident %d, %s, item %d, banner %s)", m.slot, m.resident, m.placed ? "placed" : "in front of York", gift ? m.giveItem : 0,
        m.banner[0] ? "yes" : "no");
}

}  // namespace

void MissionLoad(int slot) {
    if (slot < 1) slot = 1;
    if (slot > 9) slot = 9;
    Mission& m = g_mission;
    wchar_t path[MAX_PATH + 40];
    MissionPath(slot, path);
    m.slot = slot;
    m.exists = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    auto text = [&](const wchar_t* section, const wchar_t* key, wchar_t* out, int cap) { GetPrivateProfileStringW(section, key, L"", out, cap, path); };
    text(L"Mission", L"Name", m.name, 64);
    text(L"Mission", L"Author", m.author, 64);
    text(L"Mission", L"Banner", m.banner, 96);
    text(L"Talk", L"Line", m.line, 200);
    text(L"Talk", L"Question", m.question, 200);
    text(L"Talk", L"Yes", m.yes, 40);
    text(L"Talk", L"No", m.no, 40);
    text(L"Talk", L"AfterYes", m.afterYes, 200);
    text(L"Talk", L"AfterNo", m.afterNo, 200);
    m.resident = (int)GetPrivateProfileIntW(L"Actor", L"Resident", 6, path);
    m.placed = (int)GetPrivateProfileIntW(L"Actor", L"Placed", 0, path);
    m.x = (int)GetPrivateProfileIntW(L"Actor", L"X", 0, path);
    m.y = (int)GetPrivateProfileIntW(L"Actor", L"Y", 0, path);
    m.z = (int)GetPrivateProfileIntW(L"Actor", L"Z", 0, path);
    m.yaw = (int)GetPrivateProfileIntW(L"Actor", L"Yaw", 0, path);
    m.motionSet = (int)GetPrivateProfileIntW(L"Actor", L"MotionSet", 1, path) & 3;
    m.motion = (int)GetPrivateProfileIntW(L"Actor", L"Motion", 0, path);
    m.giveItem = (int)GetPrivateProfileIntW(L"Talk", L"GiveItem", 0, path);
    m.giveCount = (int)GetPrivateProfileIntW(L"Talk", L"GiveCount", 1, path);
    if (m.resident < 1 || m.resident > 49) m.resident = 6;
    if (m.giveItem < 0 || m.giveItem > 0x16D) m.giveItem = 0;
    if (!m.yes[0]) lstrcpyW(m.yes, L"Так");
    if (!m.no[0]) lstrcpyW(m.no, L"Ні");
}

// The numbers the editor's rows change go back into the file (the texts are the file's own)
void MissionSave() {
    Mission& m = g_mission;
    wchar_t path[MAX_PATH + 40];
    MissionPath(m.slot, path);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) CreateFileFor(path);
    PutInt(path, L"Actor", L"Resident", m.resident);
    PutInt(path, L"Actor", L"Placed", m.placed);
    PutInt(path, L"Actor", L"X", m.x);
    PutInt(path, L"Actor", L"Y", m.y);
    PutInt(path, L"Actor", L"Z", m.z);
    PutInt(path, L"Actor", L"Yaw", m.yaw);
    PutInt(path, L"Actor", L"MotionSet", m.motionSet);
    PutInt(path, L"Actor", L"Motion", m.motion);
    PutInt(path, L"Talk", L"GiveItem", m.giveItem);
    PutInt(path, L"Talk", L"GiveCount", m.giveCount);
    m.exists = true;
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

void MissionRequest(int what) {
    g_request = what;
    PhotoThaw(180);  // the game thread does it, and the model loads from disk
}
bool MissionActive() { return g_active; }
const wchar_t* MissionState() { return g_state[g_cfg.language == 1 ? 1 : 0]; }

void MissionTick(void* player) {
    (void)player;
    if (g_active && (*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u)) {  // a scene began: the doll is gone with it
        g_active = false;
        DollTalkDefault();
        SetState(L"зупинена (сцена гри)", L"stopped (a game scene)");
    }
    const int req = g_request;
    if (!req) return;
    g_request = 0;
    if (req == 1) {
        float place[4];
        if (!PlayerPlace(place)) return;
        g_mission.x = (int)(place[0] * 1000.f);
        g_mission.y = (int)(place[1] * 1000.f);
        g_mission.z = (int)(place[2] * 1000.f);
        g_mission.yaw = (int)(place[3] * 1000.f);
        g_mission.placed = 1;
        MissionSave();
        Log("mission: slot %d: the character's place is %d %d %d (thousandths)", g_mission.slot, g_mission.x, g_mission.y, g_mission.z);
    } else if (req == 2) {
        Run();
    } else if (req == 3) {
        g_active = false;
        DollTalkDefault();
        RequestDoll(2);
        SetState(L"зупинена", L"stopped");
        Log("mission: slot %d stopped", g_mission.slot);
    }
}

}  // namespace ufx
