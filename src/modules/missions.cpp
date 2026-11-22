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
        ok = ok && SetMessage(kMsMsgBanner, m.banner, "{SET_COLOR color=135, 240, 120}", u8"{NEXT_SEGMENT}                                 --Початок{RESET_COLOR}");
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

// The file's text as UTF-16, whatever it was saved as: UTF-16 with a mark, UTF-8 with or without one (Notepad's "Save as"
// offers all of them, and the Windows profile functions read only the first). The caller frees it with VirtualFree.
wchar_t* ReadFileText(const wchar_t* path, int* chars) {
    *chars = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    const DWORD size = GetFileSize(f, nullptr);
    u8* raw = size && size < 200000 ? (u8*)VirtualAlloc(nullptr, size + 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
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

}  // namespace

void MissionLoad(int slot) {
    if (slot < 1) slot = 1;
    if (slot > 9) slot = 9;
    Mission& m = g_mission;
    wchar_t path[MAX_PATH + 40];
    MissionPath(slot, path);
    m = Mission{};
    m.slot = slot;
    m.resident = 6;
    m.motionSet = 1;
    m.giveCount = 1;
    int chars = 0;
    wchar_t* text = ReadFileText(path, &chars);
    m.exists = text != nullptr;
    struct Field {
        const wchar_t* section;
        const wchar_t* key;
        wchar_t* text;
        int cap;
        int* number;
    };
    const Field fields[] = {
        {L"Mission", L"Name", m.name, 64, nullptr}, {L"Mission", L"Author", m.author, 64, nullptr}, {L"Mission", L"Banner", m.banner, 96, nullptr},
        {L"Talk", L"Line", m.line, 200, nullptr}, {L"Talk", L"Question", m.question, 200, nullptr}, {L"Talk", L"Yes", m.yes, 40, nullptr},
        {L"Talk", L"No", m.no, 40, nullptr}, {L"Talk", L"AfterYes", m.afterYes, 200, nullptr}, {L"Talk", L"AfterNo", m.afterNo, 200, nullptr},
        {L"Actor", L"Resident", nullptr, 0, &m.resident}, {L"Actor", L"Placed", nullptr, 0, &m.placed}, {L"Actor", L"X", nullptr, 0, &m.x},
        {L"Actor", L"Y", nullptr, 0, &m.y}, {L"Actor", L"Z", nullptr, 0, &m.z}, {L"Actor", L"Yaw", nullptr, 0, &m.yaw},
        {L"Actor", L"MotionSet", nullptr, 0, &m.motionSet}, {L"Actor", L"Motion", nullptr, 0, &m.motion},
        {L"Talk", L"GiveItem", nullptr, 0, &m.giveItem}, {L"Talk", L"GiveCount", nullptr, 0, &m.giveCount},
    };
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
            if (eq < b)
                for (const Field& f : fields) {
                    if (!SameText(section, lstrlenW(section), f.section) || !SameText(text + a, ke - a, f.key)) continue;
                    wchar_t value[220];
                    lstrcpynW(value, text + vs, b - vs + 1 < 220 ? b - vs + 1 : 220);
                    if (vs >= b) value[0] = 0;
                    if (f.text) lstrcpynW(f.text, value, f.cap);
                    else *f.number = ToInt(value);
                }
        }
        i = end;
        while (i < chars && (text[i] == L'\r' || text[i] == L'\n')) ++i;
    }
    if (text) VirtualFree(text, 0, MEM_RELEASE);
    m.motionSet &= 3;
    if (m.resident < 1 || m.resident > 49) m.resident = 6;
    if (m.giveItem < 0 || m.giveItem > 0x16D) m.giveItem = 0;
    if (!m.yes[0]) lstrcpyW(m.yes, L"Так");
    if (!m.no[0]) lstrcpyW(m.no, L"Ні");
}

// The whole mission written back as UTF-16 with a byte order mark (the texts as they were read, the numbers as the editor has them)
void MissionSave() {
    Mission& m = g_mission;
    wchar_t path[MAX_PATH + 40];
    MissionPath(m.slot, path);
    if (!m.exists && !m.name[0]) {  // an empty slot: the example first
        CreateFileFor(path);
        const Mission keep = m;
        MissionLoad(m.slot);
        m.resident = keep.resident; m.placed = keep.placed; m.x = keep.x; m.y = keep.y; m.z = keep.z; m.yaw = keep.yaw;
        m.motionSet = keep.motionSet; m.motion = keep.motion;
        if (keep.giveItem) m.giveItem = keep.giveItem;
    }
    wchar_t* out = (wchar_t*)VirtualAlloc(nullptr, 8192 * sizeof(wchar_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!out) return;
    int n = wsprintfW(out, L"\xFEFF; Місія редактора UltimateFIXDP. Змініть тексти, збережіть файл і в меню F10 -> Редактор місій натисніть «Перечитати файл».\r\n");
    n += wsprintfW(out + n, L"[Mission]\r\nName=%s\r\nAuthor=%s\r\nBanner=%s\r\n\r\n", m.name, m.author, m.banner);
    n += wsprintfW(out + n, L"[Actor]\r\nResident=%d\r\nPlaced=%d\r\nX=%d\r\nY=%d\r\nZ=%d\r\nYaw=%d\r\nMotionSet=%d\r\nMotion=%d\r\n\r\n", m.resident, m.placed, m.x,
                   m.y, m.z, m.yaw, m.motionSet, m.motion);
    n += wsprintfW(out + n, L"[Talk]\r\nLine=%s\r\nQuestion=%s\r\n", m.line, m.question);
    n += wsprintfW(out + n, L"Yes=%s\r\nNo=%s\r\nAfterYes=%s\r\n", m.yes, m.no, m.afterYes);
    n += wsprintfW(out + n, L"AfterNo=%s\r\nGiveItem=%d\r\nGiveCount=%d\r\n", m.afterNo, m.giveItem, m.giveCount);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD put;
        WriteFile(f, out, (DWORD)n * sizeof(wchar_t), &put, nullptr);
        CloseHandle(f);
        m.exists = true;
    }
    VirtualFree(out, 0, MEM_RELEASE);
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
