// The chat of the net co-op, the friend's name over his figure and his mark on the pause map. Drawn with the mod's own font (so
// Ukrainian works), once per frame from the Present hook.
//
//   T          opens the line (only with [Coop] NetPartner=1, in the game window, with the F10 menu closed)
//   Enter      sends, Esc closes, Backspace erases, Tab completes a command
//   /nick name the name the friend sees (kept in the ini as [Coop] NetNick)
//   /tp        York goes to the friend (the same location only)
//   /clear     empties the list
//   /help      the commands
//
// While the line is open the game sees no keys (the hooks of ui/menu.cpp ask NetChatBlocksInput). Letters come from the key state
// through ToUnicodeEx with the layout of the game window, so the layout switch of Windows works.
#include "../base.h"
#include "../ui/ui.h"

namespace ufx {
bool WorldActive();
void CheatYorkTo(const float place[4]);

namespace {

constexpr int kLines = 10, kLineLen = 128, kInputMax = 80;
constexpr u32 kViewProjVa = 0x01481238;
struct Line { wchar_t text[kLineLen]; DWORD at; u32 colour; } g_lines[kLines];
int g_lineCount = 0;
wchar_t g_input[kInputMax + 1];
int g_inputLen = 0;
bool g_open = false, g_block = false;
bool g_prev[256];
float g_figure[3];
DWORD g_figureAt = 0;
bool g_figureShown = false;
float g_mark[4];
DWORD g_markAt = 0;

struct Command { const wchar_t* name; const wchar_t* uk; const wchar_t* en; };
const Command kCommands[] = {
    {L"/nick", L"ім'я, яке бачить друг", L"the name the friend sees"},
    {L"/tp", L"перейти до друга", L"go to the friend"},
    {L"/clear", L"очистити чат", L"empty the chat"},
    {L"/help", L"список команд", L"the commands"},
};

bool En() { return g_cfg.language == 1; }
bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
bool Front() {
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    return w && GetWindowThreadProcessId(w, &pid) && pid == GetCurrentProcessId();
}

void AddLine(const wchar_t* text, u32 colour = 0xFFFFFFFF) {
    if (g_lineCount == kLines) {
        for (int i = 1; i < kLines; ++i) g_lines[i - 1] = g_lines[i];
        --g_lineCount;
    }
    Line& l = g_lines[g_lineCount++];
    lstrcpynW(l.text, text, kLineLen);
    l.at = GetTickCount();
    l.colour = colour;
}

bool StartsWith(const wchar_t* text, const wchar_t* head) {
    for (; *head; ++text, ++head)
        if (*text != *head) return false;
    return true;
}

void Submit() {
    g_input[g_inputLen] = 0;
    if (!g_inputLen) return;
    wchar_t line[kLineLen];
    if (g_input[0] != L'/') {
        wsprintfW(line, L"%s: %s", g_cfg.netNick, g_input);
        line[95] = 0;  // what one packet carries
        AddLine(line);
        NetChatSay(line);
        return;
    }
    const u32 grey = 0xFFB8C0CC;
    if (StartsWith(g_input, L"/nick")) {
        const wchar_t* name = g_input + 5;
        while (*name == L' ') ++name;
        if (!*name) {
            wsprintfW(line, En() ? L"* your name is %s; /nick name changes it" : L"* ваше ім'я %s; /nick ім'я змінює його", g_cfg.netNick);
        } else {
            lstrcpynW(g_cfg.netNick, name, 16);
            WritePrivateProfileStringW(L"Coop", L"NetNick", g_cfg.netNick, g_paths.iniFile);
            wsprintfW(line, En() ? L"* your name is now %s" : L"* тепер ваше ім'я %s", g_cfg.netNick);
        }
        AddLine(line, grey);
    } else if (StartsWith(g_input, L"/tp")) {
        NetPeerState s;
        float x, z;
        if (NetCoopPeer(&s) && NetPeerOnMap(&x, &z) && WorldActive()) {
            const float place[4] = {x + 8.f, s.pos[1] + 1.f, z, s.yaw};
            CheatYorkTo(place);
            AddLine(En() ? L"* York goes to the friend" : L"* Йорк переходить до друга", grey);
        } else {
            AddLine(En() ? L"* the friend is not heard or is in another location" : L"* друга не чути або він в іншій локації", grey);
        }
    } else if (StartsWith(g_input, L"/clear")) {
        g_lineCount = 0;
    } else {
        for (const Command& c : kCommands) {
            wsprintfW(line, L"* %s  %s", c.name, En() ? c.en : c.uk);
            AddLine(line, grey);
        }
    }
}

// The keys of the line: each key once per press (Backspace repeats while held).
void Typing() {
    const int key = NetTypeInto(g_input, &g_inputLen, kInputMax, g_prev);
    if (key == 2) { g_open = false; return; }
    if (key == 1) { Submit(); g_open = false; return; }
    if (key == 3) {  // the first command that begins this way
        g_input[g_inputLen] = 0;
        for (const Command& c : kCommands)
            if (g_input[0] == L'/' && StartsWith(c.name, g_input)) {
                lstrcpyW(g_input, c.name);
                g_inputLen = lstrlenW(g_input);
                if (g_inputLen < kInputMax && c.name[1] == L'n') g_input[g_inputLen++] = L' ';
                break;
            }
    }
}

void Shadowed(const wchar_t* text, float x, float y, u32 colour, int px, int weight = ui::kRegular) {
    const u32 a = colour & 0xFF000000u;
    ui::DrawText(text, x + 1, y + 1, a, px, weight);
    ui::DrawText(text, x, y, colour, px, weight);
}

}  // namespace

bool NetChatBlocksInput() { return g_block; }

int NetTypeInto(wchar_t* text, int* length, int max, bool* prev) {
    static DWORD backAt = 0;
    BYTE state[256];
    for (int i = 0; i < 256; ++i) state[i] = 0;
    if (Down(VK_SHIFT)) state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
    if (Down(VK_RMENU) && Down(VK_CONTROL)) state[VK_CONTROL] = state[VK_MENU] = 0x80;  // AltGr
    if (GetKeyState(VK_CAPITAL) & 1) state[VK_CAPITAL] = 1;
    const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
    const DWORD now = GetTickCount();
    int result = 0;
    for (int vk = 8; vk < 255; ++vk) {
        const bool down = Down(vk), was = prev[vk];
        prev[vk] = down;
        if (result) continue;  // the rest only remembered
        if (vk == VK_BACK) {
            if (down && (!was || (int)(now - backAt) >= 0)) {
                backAt = now + (was ? 40 : 400);
                if (*length) --*length;
            }
            continue;
        }
        if (!down || was) continue;
        if (vk == VK_ESCAPE) { result = 2; continue; }
        if (vk == VK_RETURN) { result = 1; continue; }
        if (vk == VK_TAB) { result = 3; continue; }
        if (vk < 0x20 || (vk >= VK_LWIN && vk <= VK_SLEEP) || (vk >= VK_F1 && vk <= VK_F24) || (vk >= VK_SHIFT && vk <= VK_MENU) || (vk >= VK_LSHIFT && vk <= VK_RMENU) ||
            (vk >= VK_PRIOR && vk <= VK_DELETE && vk != VK_SPACE) || Down(VK_CONTROL) != (state[VK_CONTROL] != 0))
            continue;
        wchar_t out[4];
        const int n = ToUnicodeEx((UINT)vk, MapVirtualKeyExW((UINT)vk, 0, layout), state, out, 4, 0, layout);
        if (n == 1 && out[0] >= 32 && *length < max) text[(*length)++] = out[0];
    }
    text[*length] = 0;
    return result;
}

void NetFigureAt(const float* pos, bool shown) {
    for (int i = 0; i < 3; ++i) g_figure[i] = pos[i];
    g_figureShown = shown;
    g_figureAt = GetTickCount();
}

void NetMapMark(float x, float y, float w, float h) {
    g_mark[0] = x;
    g_mark[1] = y;
    g_mark[2] = w;
    g_mark[3] = h;
    g_markAt = GetTickCount();
}

void NetChatFrame(void* device) {
    NetPumpFrame();
    if (!NetCoopOn()) {
        g_open = g_block = false;
        return;
    }
    const DWORD now = GetTickCount();
    wchar_t got[96];
    while (NetChatTake(got, 96)) AddLine(got, 0xFFFFE9A8);
    const bool front = Front();
    if (g_open) {
        if (!front || ui::IsOpen()) g_open = false;
        else Typing();
    } else if (front && !ui::IsOpen()) {
        const bool t = Down('T') && !Down(VK_CONTROL) && !Down(VK_MENU);
        if (t && !g_prev['T'] && WorldActive()) {
            g_open = true;
            g_inputLen = 0;
            for (int vk = 0; vk < 256; ++vk) g_prev[vk] = Down(vk);
        }
        g_prev['T'] = t;
    }
    // the game sees no keys while the line is open, and none of the keys that closed it until they are up
    if (g_open) g_block = true;
    else if (g_block && !Down(VK_ESCAPE) && !Down(VK_RETURN)) g_block = false;

    // what there is to draw
    int visible = 0;
    for (int i = 0; i < g_lineCount; ++i)
        if (g_open || now - g_lines[i].at < 12000) ++visible;
    float nameX = 0, nameY = 0;
    bool name = false;
    const wchar_t* nick = NetPeerNick();
    if (g_figureShown && now - g_figureAt < 250 && nick[0] && !ui::IsOpen()) {
        const float* m = (const float*)(uintptr_t)kViewProjVa;
        const float in[4] = {g_figure[0], g_figure[1] + 21.f, g_figure[2], 1.f};  // over the head
        float c[4];
        for (int j = 0; j < 4; ++j) c[j] = in[0] * m[j] + in[1] * m[4 + j] + in[2] * m[8 + j] + in[3] * m[12 + j];
        if (c[3] > 0.5f && c[3] < 4000.f) {
            nameX = (c[0] / c[3] * 0.5f + 0.5f) * 1280.f;
            nameY = (-c[1] / c[3] * 0.5f + 0.5f) * 720.f;
            name = nameX > -100.f && nameX < 1380.f && nameY > 0.f && nameY < 720.f;
        }
    }
    const bool mark = now - g_markAt < 100 && g_mark[2] > 100.f && g_mark[3] > 100.f;
    if (!visible && !g_open && !name && !mark) return;
    if (!ui::OverlayBegin(device)) return;
    if (name) {
        const float w = ui::TextWidth(nick, 16, ui::kSemibold);
        Shadowed(nick, ui::SnapWhole(nameX - w * 0.5f), ui::SnapWhole(nameY - 18.f), 0xFFFFFFFF, 16, ui::kSemibold);
    }
    if (mark) {
        const float x = ui::SnapWhole(g_mark[0] * 1280.f / g_mark[2]), y = ui::SnapWhole(g_mark[1] * 720.f / g_mark[3]);
        if (x > 20.f && x < 1260.f && y > 20.f && y < 700.f) {
            ui::FillRoundFramed(x - 7, y - 7, 14, 14, 7, 0xFF2F9BFF, 0xFFFFFFFF, 2.f);
            if (nick[0]) Shadowed(nick, x + 11, y - 10, 0xFFFFFFFF, 15, ui::kSemibold);
        }
    }
    const float left = 28.f, top = 96.f, lineH = 21.f;
    float y = top;
    for (int i = 0; i < g_lineCount; ++i) {
        const DWORD age = now - g_lines[i].at;
        if (!g_open && age >= 12000) continue;
        u32 alpha = 255;
        if (!g_open && age > 11000) alpha = 255 * (12000 - age) / 1000;  // the last second: fades
        Shadowed(g_lines[i].text, left, y, (g_lines[i].colour & 0x00FFFFFFu) | (alpha << 24), 16, ui::kSemibold);
        y += lineH;
    }
    if (g_open) {
        const float boxY = top + kLines * lineH + 6.f, boxW = 460.f, boxH = 30.f;
        ui::FillRoundFramed(left - 8, boxY, boxW, boxH, 6, 0xD0101216, 0xFFE8EAEE, 1.f);
        wchar_t shown[kInputMax + 8];
        g_input[g_inputLen] = 0;
        wsprintfW(shown, L"> %s%s", g_input, (now / 500) & 1 ? L"_" : L"");
        ui::DrawFitted(shown, left, boxY + 6, 0xFFFFFFFF, 16, boxW - 16);
        if (g_input[0] == L'/') {  // the commands that begin this way; Tab takes the first
            float rowY = boxY + boxH + 2;
            bool first = true;
            for (const Command& c : kCommands) {
                int head = 0;
                while (g_input[head] && g_input[head] != L' ') ++head;
                wchar_t typed[kInputMax + 1];
                lstrcpynW(typed, g_input, head + 1);
                if (!StartsWith(c.name, typed)) continue;
                ui::FillRect(left - 8, rowY, boxW, 22, first ? 0xE0B4202A : 0xD0101216);
                ui::DrawText(c.name, left, rowY + 3, 0xFFFFFFFF, 14, ui::kSemibold);
                ui::DrawText(En() ? c.en : c.uk, left + 70, rowY + 3, first ? 0xFFFFFFFF : 0xFFB8C0CC, 14);
                rowY += 22;
                first = false;
            }
        }
    }
    ui::OverlayEnd();
}

}  // namespace ufx
