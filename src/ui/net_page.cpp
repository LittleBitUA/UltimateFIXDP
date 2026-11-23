// The "Multiplayer" page of the F10 menu: your name, your own server, joining by address, and the list of servers (those of the
// local network, asked every few seconds while the page is shown, and the addresses joined before). The session itself is
// modules/net_coop.cpp. Mouse and keyboard; on the pad up/down walk the list and A joins the chosen server.
#include "../base.h"
#include "ui.h"
#include "theme.h"

namespace ufx {
namespace ui {
namespace {

using namespace theme;
constexpr float kFieldH = 30, kButtonH = 30, kRowH = 40;
enum Action { kNone, kFocusNick, kFocusName, kFocusAddress, kHost, kJoinAddress, kLeave, kRefresh, kMirror, kRow };
struct Hit { float x, y, w, h; int action, row; };
Hit g_hits[40];
int g_hitCount = 0;

int g_focus = -1;  // the field that has the keyboard: 0 name of the player, 1 name of the server, 2 address
bool g_hold = false;  // the key that left a field is still down: the menu must not see it
bool g_prev[256];
wchar_t g_address[24];
int g_lenNick = 0, g_lenName = 0, g_lenAddress = 0;
bool g_filled = false;
DWORD g_drawnAt = 0, g_askedAt = 0;
int g_sel = 0;
wchar_t g_error[120];
DWORD g_errorAt = 0;

struct Row { wchar_t address[24]; NetServer server; bool answered; };
Row g_rows[20];
int g_rowCount = 0;

bool English() { return g_cfg.language == 1; }
const wchar_t* T(const wchar_t* uk, const wchar_t* en) { return English() ? en : uk; }
bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

void Error(const wchar_t* uk, const wchar_t* en) {
    lstrcpynW(g_error, T(uk, en), 120);
    g_errorAt = GetTickCount();
}

void Save() {
    if (!g_cfg.netNick[0]) lstrcpyW(g_cfg.netNick, L"York");
    if (!g_cfg.netServerName[0]) lstrcpyW(g_cfg.netServerName, L"Greenvale");
    WritePrivateProfileStringW(L"Coop", L"NetNick", g_cfg.netNick, g_paths.iniFile);
    WritePrivateProfileStringW(L"Coop", L"ServerName", g_cfg.netServerName, g_paths.iniFile);
    g_lenNick = lstrlenW(g_cfg.netNick);
    g_lenName = lstrlenW(g_cfg.netServerName);
}

void Focus(int field) {
    if (g_focus >= 0 && field != g_focus) Save();
    g_focus = field;
    for (int vk = 0; vk < 256; ++vk) g_prev[vk] = KeyDown(vk);
}

void Join(const wchar_t* address) {
    Focus(-1);
    if (!NetJoin(address)) Error(L"Це не адреса. Приклад: 10.0.0.2 або 10.0.0.2:47900", L"Not an address. Example: 10.0.0.2 or 10.0.0.2:47900");
}

void Do(int action, int row) {
    switch (action) {
        case kFocusNick: Focus(0); break;
        case kFocusName: Focus(1); break;
        case kFocusAddress: Focus(2); break;
        case kHost:
            Focus(-1);
            if (!NetHostStart()) Error(L"Порт зайнятий іншою програмою. Змініть [Coop] NetPort в ini.", L"The port is taken by another program. Change [Coop] NetPort in the ini.");
            break;
        case kJoinAddress: Join(g_address); break;
        case kLeave: Focus(-1); NetLeave(); break;
        case kRefresh: NetDiscover(); g_askedAt = GetTickCount(); break;
        case kMirror: Focus(-1); NetMirrorStart(); break;
        case kRow:
            Focus(-1);
            if (row == g_sel && row < g_rowCount) Join(g_rows[row].address);  // a second click on the chosen row joins
            g_sel = row;
            break;
    }
}

void AddHit(float x, float y, float w, float h, int action, int row = 0) {
    if (g_hitCount < 40) g_hits[g_hitCount++] = {x, y, w, h, action, row};
}

float Button(const wchar_t* label, float x, float y, int action, bool primary = false) {
    const float w = TextWidth(label, 14, kSemibold) + 28;
    if (primary) FillRound(x, y, w, kButtonH, 7, kAccent);
    else FillRoundFramed(x, y, w, kButtonH, 7, kSelected, kLine2);
    DrawText(label, x + 14, y + 6, kText, 14, kSemibold);
    AddHit(x, y, w, kButtonH, action);
    return w;
}

void Field(const wchar_t* label, const wchar_t* text, const wchar_t* hint, float x, float y, float w, int field, int action) {
    DrawText(label, x, y, kMuted, 13);
    const float fy = y + 20;
    const bool on = g_focus == field;
    FillRoundFramed(x, fy, w, kFieldH, 7, kRaised, on ? kAccent : kLine2, on ? 2.f : 1.f);
    if (!text[0] && !on) {
        DrawFitted(hint, x + 10, fy + 6, kFaint, 14, w - 20);
    } else {
        const float tw = DrawFitted(text, x + 10, fy + 6, kText, 14, w - 24);
        if (on && (GetTickCount() / 500) & 1) FillRect(x + 11 + tw, fy + 7, 1, 16, kText);
    }
    AddHit(x, fy, w, kFieldH, action);
}

// The rows of the list: the servers that answered, then the recent addresses that did not.
void BuildRows() {
    NetServer servers[16];
    const int n = NetServers(servers, 16);
    g_rowCount = 0;
    for (int i = 0; i < n; ++i) {
        Row& r = g_rows[g_rowCount++];
        r.server = servers[i];
        r.answered = true;
        const u32 ip = servers[i].ip;
        wsprintfW(r.address, L"%u.%u.%u.%u:%d", ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24, servers[i].port);
    }
    for (int i = 0; i < 4; ++i) {
        const wchar_t* recent = NetRecent(i);
        if (!recent[0]) continue;
        bool listed = false;
        for (int k = 0; k < g_rowCount; ++k) listed = listed || lstrcmpW(g_rows[k].address, recent) == 0;
        if (listed) continue;
        Row& r = g_rows[g_rowCount++];
        lstrcpynW(r.address, recent, 24);
        r.answered = false;
    }
    if (g_sel >= g_rowCount) g_sel = g_rowCount ? g_rowCount - 1 : 0;
}

}  // namespace

bool NetPageTyping() {
    if (GetTickCount() - g_drawnAt > 300) {  // the page is not shown any more
        if (g_focus >= 0) Save();
        g_focus = -1;
        g_hold = false;
        return false;
    }
    if (g_hold && !KeyDown(VK_ESCAPE) && !KeyDown(VK_RETURN) && !KeyDown(VK_TAB)) g_hold = false;
    return g_focus >= 0 || g_hold;
}

void NetPageInput(bool up, bool down, bool accept, bool click, float mx, float my) {
    if (g_focus < 0 && !g_hold) {
        if (up && g_sel > 0) --g_sel;
        if (down && g_sel + 1 < g_rowCount) ++g_sel;
        if (accept && g_sel < g_rowCount && NetMode() == kNetOff) Join(g_rows[g_sel].address);
    }
    if (!click) return;
    for (int i = 0; i < g_hitCount; ++i) {
        const Hit& h = g_hits[i];
        if (mx >= h.x && mx < h.x + h.w && my >= h.y && my < h.y + h.h) {
            Do(h.action, h.row);
            return;
        }
    }
    Focus(-1);  // a click beside everything leaves the field
}

void NetPage(float left, float top, float right, float bottom) {
    const DWORD now = GetTickCount();
    g_drawnAt = now;
    g_hitCount = 0;
    if (!g_filled) {
        g_filled = true;
        lstrcpynW(g_address, NetRecent(0), 24);
        g_lenAddress = lstrlenW(g_address);
        g_lenNick = lstrlenW(g_cfg.netNick);
        g_lenName = lstrlenW(g_cfg.netServerName);
    }
    // the field that has the keyboard
    if (g_focus >= 0) {
        wchar_t* text = g_focus == 0 ? g_cfg.netNick : g_focus == 1 ? g_cfg.netServerName : g_address;
        int* length = g_focus == 0 ? &g_lenNick : g_focus == 1 ? &g_lenName : &g_lenAddress;
        const int key = NetTypeInto(text, length, g_focus == 0 ? 15 : g_focus == 1 ? 23 : 21, g_prev);
        if (key) {
            const int was = g_focus;
            g_hold = true;
            if (key == 3) Focus((was + 1) % 3);
            else if (key == 1 && was == 2) Join(g_address);
            else Focus(-1);
        }
    }
    const int mode = NetMode();
    if (mode != kNetHost && now - g_askedAt > 2500) {  // the list keeps itself fresh while the page is shown
        g_askedAt = now;
        NetDiscover();
    }
    BuildRows();

    const float w = right - left;
    float y = top;
    // ---- the state of the session
    {
        wchar_t line[160], peer[24];
        u32 dot = kFaint;
        const wchar_t* nick = NetPeerNick();
        if (mode == kNetOff) {
            lstrcpyW(line, T(L"Не в мережі. Створіть сервер або підключіться до чужого.", L"Offline. Start a server or join one."));
        } else if (mode == kNetMirror) {
            dot = 0xFFE0B040;
            lstrcpyW(line, T(L"Тест наодинці: друга фігура повторює вас із запізненням 1,5 с.", L"Test for one: the second figure repeats you 1.5 s late."));
        } else if (NetPeerHeard()) {
            dot = 0xFF3CC46A;
            NetPeerText(peer);
            wsprintfW(line, T(L"У грі з %s (%s). Чат: клавіша T.", L"Playing with %s (%s). Chat: the T key."), nick[0] ? nick : L"?", peer);
        } else if (mode == kNetHost) {
            dot = 0xFFE0B040;
            wchar_t own[100];
            if (NetOwnAddresses(own, 100))
                wsprintfW(line, T(L"Сервер запущено. Ваша адреса для іншого гравця: %s", L"The server is up. Your address for the other player: %s"), own);
            else
                wsprintfW(line, T(L"Сервер «%s» запущено, порт %d. Чекає на гравця.", L"Server \"%s\" is up, port %d. Waiting for a player."), g_cfg.netServerName, g_cfg.netPort);
        } else {
            dot = 0xFFE0B040;
            lstrcpyW(peer, L"?");
            NetPeerText(peer);
            wsprintfW(line, T(L"Підключення до %s… Відповіді ще немає.", L"Joining %s… No answer yet."), peer);
        }
        FillRoundFramed(left, y, w, 46, 9, kRaised, kLine2);
        FillRound(left + 16, y + 18, 10, 10, 5, dot);
        float bw = 0;
        if (mode != kNetOff) {
            const wchar_t* label = mode == kNetHost ? T(L"Зупинити сервер", L"Stop the server") : T(L"Вийти", L"Leave");
            bw = TextWidth(label, 14, kSemibold) + 28;
            Button(label, right - bw - 8, y + 8, kLeave);
        }
        DrawFitted(line, left + 36, y + 14, kText, 15, w - 52 - bw);
        y += 46 + 18;
    }
    // ---- your name, your server
    const float gap = 14, col = (w - gap * 2) / 3;
    Field(T(L"Ваше ім'я", L"Your name"), g_cfg.netNick, L"York", left, y, col, 0, kFocusNick);
    Field(T(L"Назва сервера", L"Server name"), g_cfg.netServerName, L"Greenvale", left + col + gap, y, col, 1, kFocusName);
    if (mode == kNetOff) Button(T(L"Створити сервер", L"Start a server"), left + (col + gap) * 2, y + 20, kHost, true);
    y += 20 + kFieldH + 16;
    // ---- by address
    Field(T(L"Адреса сервера", L"Server address"), g_address, L"10.0.0.2", left, y, col * 2 + gap, 2, kFocusAddress);
    if (mode == kNetOff) Button(T(L"Підключитись", L"Join"), left + (col + gap) * 2, y + 20, kJoinAddress);
    y += 20 + kFieldH + 8;
    if (g_error[0] && now - g_errorAt < 6000) DrawFitted(g_error, left, y, 0xFFFF8A8A, 13, w);
    y += 22;
    // ---- the list
    DrawText(T(L"Сервери", L"Servers"), left, y, kText, 18, kSemibold);
    {
        const wchar_t* label = T(L"Оновити", L"Refresh");
        Button(label, right - TextWidth(label, 14, kSemibold) - 28, y - 5, kRefresh);
    }
    y += 34;
    FillRect(left, y - 4, w, 1, kLine);
    const float noteH = 58;
    int shown = 0;
    for (int i = 0; i < g_rowCount && y + kRowH <= bottom - noteH; ++i, ++shown) {
        const Row& r = g_rows[i];
        if (i == g_sel) FillRound(left, y, w, kRowH - 4, 7, kSelected);
        wchar_t state[64];
        if (r.answered) {
            DrawFitted(r.server.name, left + 14, y + 9, kText, 15, w * 0.34f, kSemibold);
            DrawFitted(r.server.nick, left + 14 + w * 0.36f, y + 10, kMuted, 14, w * 0.18f);
            wsprintfW(state, T(L"%d з 2", L"%d of 2"), r.server.players);
        } else {
            DrawFitted(T(L"недавній, не відповідає", L"recent, no answer"), left + 14, y + 10, kFaint, 14, w * 0.5f);
            state[0] = 0;
        }
        DrawFitted(r.address, left + 14 + w * 0.56f, y + 10, kMuted, 14, w * 0.26f);
        DrawText(state, right - 14 - TextWidth(state, 14), y + 10, r.answered && r.server.players < 2 ? 0xFF3CC46A : kFaint, 14);
        AddHit(left, y, w, kRowH - 4, kRow, i);
        y += kRowH;
    }
    if (!g_rowCount) {
        DrawText(T(L"У вашій мережі серверів не знайдено.", L"No servers found in your network."), left + 14, y + 8, kFaint, 14);
        y += kRowH;
    } else if (mode == kNetOff && shown) {
        DrawText(T(L"Клацніть по вибраному рядку ще раз (або Enter), щоб підключитись.", L"Click the chosen row again (or Enter) to join."), left + 14, y + 4, kFaint, 12);
    }
    // ---- what the list is, and the test for one
    const float ny = bottom - noteH + 8;
    FillRect(left, ny - 8, w, 1, kLine);
    const wchar_t* mirror = T(L"Тест наодинці", L"Test for one");
    const float mw = mode == kNetOff ? TextWidth(mirror, 14, kSemibold) + 28 : 0;
    DrawLines(T(L"Список показує сервери вашої мережі (також ZeroTier, Radmin VPN, Hamachi) і недавні адреси. Через інтернет: адреса того, хто створив сервер, і відкритий у нього UDP-порт 47900. Грають двоє.",
                L"The list shows the servers of your network (also ZeroTier, Radmin VPN, Hamachi) and recent addresses. Over the internet: the address of whoever runs the server, with UDP port 47900 open there. Two players."),
              left, ny, kFaint, 12, 16, w - mw - 16, 3, true);
    if (mode == kNetOff) Button(mirror, right - mw, ny + 6, kMirror);
}

}  // namespace ui
}  // namespace ufx
