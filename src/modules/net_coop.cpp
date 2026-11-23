// Multiplayer for two: two running games tell each other where their York is, what he does, holds and drives, and each shows the
// other one as a second York (the figure of combat_strafe.cpp) and his car. Nothing else is shared: each game keeps its own story,
// enemies and time.
//
// One game is the server (it waits on a UDP port for whoever joins), the other joins it by address. Both are started from the
// "Multiplayer" page of the F10 menu (ui/net_page.cpp); the ini only keeps what the page remembers:
//   [Coop] NetNick=York        the name the other player sees
//   [Coop] ServerName=...      the name of this game's server in the other players' lists
//   [Coop] NetPort=47900       the UDP port a server listens on (and the port searched in the local network)
//   [Coop] Recent1..4          the addresses joined last
//   [Coop] NetPartner=1        start at once with the game: NetPeer=address joins it, NetPeer=mirror is the test for one player
//                              (the figure repeats this York a second and a half late, 30 units aside), no NetPeer = a server
//   [Coop] NetTest=1           shows the "test for one" button on the page (the mirror); off by default
//   [Coop] NetCarCollision=1, NetWeapons=1   switches for the two riskiest parts (see combat_strafe.cpp)
//
// The list of servers is the local network's: a broadcast question on NetPort, answered by every server there (virtual networks
// such as ZeroTier, Radmin VPN or Hamachi pass it on), plus the addresses joined before. There is no list of the whole internet:
// that needs a computer everybody's game reports to, and the mod has none.
//
// Datagrams: the state (about 20 a second), a chat line (sent six times), the question and the answer of the list. A state or a
// chat line is taken only from the one other player (a server takes its first caller and keeps him until he is silent for 5 s),
// with our mark and version, a newer number than the last, and every value checked before use. ws2_32 is loaded at run time.
// The socket is used from the game's update and from the picture's frame: one small lock around all of it.
#include "../base.h"
#include <winsock2.h>

namespace ufx {
namespace {

struct Sock {
    int(WSAAPI* startup)(WORD, LPWSADATA);
    SOCKET(WSAAPI* socket)(int, int, int);
    int(WSAAPI* bind)(SOCKET, const sockaddr*, int);
    int(WSAAPI* ioctl)(SOCKET, long, u_long*);
    int(WSAAPI* sendto)(SOCKET, const char*, int, int, const sockaddr*, int);
    int(WSAAPI* recvfrom)(SOCKET, char*, int, int, sockaddr*, int*);
    int(WSAAPI* close)(SOCKET);
    int(WSAAPI* setsockopt)(SOCKET, int, int, const char*, int);
    int(WSAAPI* gethostname)(char*, int);
    hostent*(WSAAPI* gethostbyname)(const char*);
} g_ws;

#pragma pack(push, 1)
struct Packet {
    u32 mark;     // 'UFXP'
    u16 version;
    u16 kind;
    u32 seq;
    u32 zone[3];
    float pos[3];
    float yaw;
    u32 flags;    // bit 0: at the wheel, bit 1: the upper body's motion is on, bit 2: the waist's turn is given
    u32 car;
    float carPos[3];
    float carRot[3];
    int motion;
    int suit;
    int layerMotion, weapon;
    u32 shots, layerSeq;
    wchar_t nick[16];
    float waist[4];
};
struct ChatPacket {
    u32 mark;     // 'UFXC'
    u16 version;
    u16 spare;
    u32 id;       // the same line is sent several times; the id tells them apart
    wchar_t text[96];
};
struct QueryPacket {
    u32 mark;     // 'UFXQ'
    u16 version;
    u16 spare;
};
struct InfoPacket {
    u32 mark;     // 'UFXI'
    u16 version;
    u16 players;  // 1 = only the host, 2 = full
    wchar_t name[24];
    wchar_t nick[16];
};
#pragma pack(pop)
constexpr u32 kMark = 0x50584655u, kChatMark = 0x43584655u, kQueryMark = 0x51584655u, kInfoMark = 0x49584655u;
constexpr u16 kNetVersion = 6;

int g_mode = kNetOff;
bool g_started = false;  // the ini's NetPartner was looked at
bool g_wsUp = false, g_wsFailed = false;
SOCKET g_sock = INVALID_SOCKET;
int g_boundPort = 0;     // the port the socket is bound to, 0 = whichever the system gave
sockaddr_in g_peer;
bool g_havePeer = false;
u32 g_seqOut = 0, g_seqIn = 0;
DWORD g_lastSend = 0, g_lastHeard = 0;
NetPeerState g_last;
bool g_haveLast = false, g_toldHeard = false;

constexpr int kRing = 64;  // mirror: this York's own states, read back a second and a half later
struct Past { DWORD at; NetPeerState s; } g_ring[kRing];
int g_ringAt = 0;

volatile LONG g_netLock = 0;
struct Lock {
    Lock() { while (InterlockedExchange(&g_netLock, 1)) Sleep(0); }
    ~Lock() { InterlockedExchange(&g_netLock, 0); }
};
// Chat. Lines to send wait here and go out six times each (UDP loses packets); lines that came wait for the chat window.
struct Out { u32 id; int left; DWORD next; wchar_t text[96]; } g_out[4];
wchar_t g_in[8][96];
int g_inHead = 0, g_inCount = 0;
u32 g_chatId = 0, g_seenIds[16];
int g_seenAt = 0;
wchar_t g_peerNick[16];
u32 g_mineZone0 = 0xFFFFFFFFu;

NetServer g_servers[16];
int g_serverCount = 0;
wchar_t g_recent[4][24];
bool g_recentRead = false;

void CopyClean(wchar_t* to, const wchar_t* from, int max) {  // no control characters, always ended
    int n = 0;
    for (; n < max - 1 && from[n]; ++n) to[n] = from[n] < 32 ? L' ' : from[n];
    to[n] = 0;
}
void ChatIn(const wchar_t* text) {  // under the lock
    wchar_t* slot = g_in[(g_inHead + g_inCount) % 8];
    if (g_inCount == 8) g_inHead = (g_inHead + 1) % 8; else ++g_inCount;
    CopyClean(slot, text, 96);
}

// a.b.c.d[:port] -> the address in network order; the port only when it is written
u32 ParseAddress(const wchar_t* text, int* port, bool* ok) {
    u32 part[4] = {0, 0, 0, 0};
    int n = 0, digits = 0;
    const wchar_t* c = text;
    while (*c == L' ') ++c;
    for (;; ++c) {
        if (*c >= L'0' && *c <= L'9') {
            part[n] = part[n] * 10 + (u32)(*c - L'0');
            if (++digits > 3 || part[n] > 255) { *ok = false; return 0; }
        } else if (*c == L'.' && digits && n < 3) {
            ++n;
            digits = 0;
        } else if (*c == 0 || *c == L' ' || *c == L':') {
            break;
        } else {
            *ok = false;
            return 0;
        }
    }
    *ok = n == 3 && digits > 0;
    if (*ok && *c == L':') {
        int p = 0;
        for (++c; *c >= L'0' && *c <= L'9' && p < 100000; ++c) p = p * 10 + (*c - L'0');
        if (p >= 1024 && p <= 65535) *port = p; else *ok = false;
    }
    return part[0] | (part[1] << 8) | (part[2] << 16) | (part[3] << 24);
}
inline u_short NetOrder(int port) { return (u_short)(((port & 0xFF) << 8) | ((port >> 8) & 0xFF)); }
inline int HostOrder(u_short port) { return ((port & 0xFF) << 8) | ((port >> 8) & 0xFF); }
inline bool Finite(float v, float limit) { return v > -limit && v < limit; }  // also false for what is not a number
int ServerPort() { return g_cfg.netPort >= 1024 && g_cfg.netPort <= 65535 ? g_cfg.netPort : 47900; }
bool SameAddress(const sockaddr_in& a, const sockaddr_in& b) { return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port; }

// The socket (under the lock). `exact`: it must sit on NetPort (a server); otherwise any port will do when that one is taken
// (a second game on the same computer that only joins).
bool EnsureSocket(bool exact) {
    if (g_wsFailed) return false;
    if (!g_wsUp) {
        HMODULE m = LoadLibraryW(L"ws2_32.dll");
        static const char* const names[10] = {"WSAStartup", "socket", "bind", "ioctlsocket", "sendto", "recvfrom", "closesocket", "setsockopt", "gethostname", "gethostbyname"};
        void** slots = (void**)&g_ws;
        for (int i = 0; m && i < 10; ++i) {
            slots[i] = (void*)GetProcAddress(m, names[i]);
            if (!slots[i]) m = nullptr;
        }
        WSADATA wsa;
        if (!m || g_ws.startup(MAKEWORD(2, 2), &wsa) != 0) {
            g_wsFailed = true;
            Log("multiplayer: Winsock did not start; off");
            return false;
        }
        g_wsUp = true;
    }
    const int port = ServerPort();
    if (g_sock != INVALID_SOCKET && (!exact || g_boundPort == port)) return true;
    if (g_sock != INVALID_SOCKET) g_ws.close(g_sock);
    g_sock = g_ws.socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) return false;
    sockaddr_in me = {};
    me.sin_family = AF_INET;
    me.sin_port = NetOrder(port);
    g_boundPort = port;
    if (g_ws.bind(g_sock, (const sockaddr*)&me, sizeof me) != 0) {
        g_boundPort = 0;
        me.sin_port = 0;
        if (exact || g_ws.bind(g_sock, (const sockaddr*)&me, sizeof me) != 0) {
            Log("multiplayer: UDP port %d could not be opened (taken by another program?)", port);
            g_ws.close(g_sock);
            g_sock = INVALID_SOCKET;
            return false;
        }
    }
    u_long noWait = 1;
    g_ws.ioctl(g_sock, (long)FIONBIO, &noWait);
    const int yes = 1;
    g_ws.setsockopt(g_sock, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof yes);
    return true;
}

void ResetSession() {
    g_seqOut = g_seqIn = 0;
    g_haveLast = g_toldHeard = g_havePeer = false;
    g_lastSend = g_lastHeard = 0;
    g_peerNick[0] = 0;
    for (Past& p : g_ring) p.at = 0;
    for (Out& o : g_out) o.left = 0;
}

void ReadRecent() {
    if (g_recentRead) return;
    g_recentRead = true;
    for (int i = 0; i < 4; ++i) {
        wchar_t key[12];
        wsprintfW(key, L"Recent%d", i + 1);
        GetPrivateProfileStringW(L"Coop", key, L"", g_recent[i], 24, g_paths.iniFile);
    }
}

void SendTo(const void* data, int size, const sockaddr_in& to) { g_ws.sendto(g_sock, (const char*)data, size, 0, (const sockaddr*)&to, sizeof to); }

// Everything that came in, and the chat lines that wait to go out. Under the lock.
void Pump(DWORD now) {
    if (g_sock == INVALID_SOCKET) return;
    if (g_havePeer && g_mode >= kNetHost)
        for (Out& o : g_out)
            if (o.left && (int)(now - o.next) >= 0) {
                --o.left;
                o.next = now + 150;
                ChatPacket c = {};
                c.mark = kChatMark;
                c.version = kNetVersion;
                c.id = o.id;
                CopyClean(c.text, o.text, 96);
                SendTo(&c, sizeof c, g_peer);
            }
    for (int i = 0; i < 48; ++i) {
        union { Packet p; ChatPacket c; QueryPacket q; InfoPacket info; } u;
        Packet& p = u.p;
        sockaddr_in from = {};
        int fromLen = sizeof from;
        const int got = g_ws.recvfrom(g_sock, (char*)&u, sizeof u, 0, (sockaddr*)&from, &fromLen);
        if (got <= 0) break;
        if (got < 8 || u.q.version != kNetVersion) continue;
        if (got == (int)sizeof(QueryPacket) && u.q.mark == kQueryMark) {
            if (g_mode != kNetHost) continue;
            InfoPacket info = {};
            info.mark = kInfoMark;
            info.version = kNetVersion;
            info.players = g_havePeer ? 2 : 1;
            CopyClean(info.name, g_cfg.netServerName, 24);
            CopyClean(info.nick, g_cfg.netNick, 16);
            SendTo(&info, sizeof info, from);
            continue;
        }
        if (got == (int)sizeof(InfoPacket) && u.info.mark == kInfoMark) {
            NetServer* s = nullptr;
            for (int k = 0; k < g_serverCount; ++k)
                if (g_servers[k].ip == from.sin_addr.s_addr && g_servers[k].port == HostOrder(from.sin_port)) s = &g_servers[k];
            if (!s && g_serverCount < 16) s = &g_servers[g_serverCount++];
            if (!s) continue;
            s->ip = from.sin_addr.s_addr;
            s->port = HostOrder(from.sin_port);
            s->players = u.info.players == 2 ? 2 : 1;
            u.info.name[23] = u.info.nick[15] = 0;
            CopyClean(s->name, u.info.name, 24);
            CopyClean(s->nick, u.info.nick, 16);
            s->at = now;
            continue;
        }
        if (g_mode < kNetHost) continue;
        if (got == (int)sizeof(ChatPacket) && u.c.mark == kChatMark) {
            if (!g_havePeer || !SameAddress(from, g_peer)) continue;
            bool seen = false;
            for (u32 id : g_seenIds) seen = seen || id == u.c.id;
            if (!seen && u.c.id) {
                g_seenIds[g_seenAt++ % 16] = u.c.id;
                u.c.text[95] = 0;
                ChatIn(u.c.text);
            }
            continue;
        }
        if (got != (int)sizeof p || p.mark != kMark) continue;
        bool sane = p.kind <= 3 && Finite(p.yaw, 100.f) && p.car < 0x10000 && p.motion >= -1 && p.motion < 0x10000 && p.suit >= 0 && p.suit < 41;
        for (int k = 0; k < 3; ++k)
            if (!Finite(p.pos[k], 1.0e6f) || !Finite(p.carPos[k], 1.0e6f) || !Finite(p.carRot[k], 100.f)) sane = false;
        if (p.weapon < 0 || p.weapon > 0x16D || p.layerMotion < -1 || p.layerMotion > 0xFFFF) sane = false;
        for (int k = 0; k < 4; ++k)
            if (!Finite(p.waist[k], 1.01f)) sane = false;
        if (!sane) continue;
        if (g_mode == kNetHost && !g_havePeer) {  // a server takes its first caller
            g_peer = from;
            g_havePeer = true;
            g_haveLast = false;
            g_seqIn = 0;
        }
        if (!g_havePeer || !SameAddress(from, g_peer)) continue;
        // a newer one than the last; a much older number is the other game started again
        if (g_haveLast && (int)(p.seq - g_seqIn) <= 0 && g_seqIn - p.seq < 2000) continue;
        g_seqIn = p.seq;
        for (int k = 0; k < 3; ++k) {
            g_last.zone[k] = p.zone[k];
            g_last.pos[k] = p.pos[k];
            g_last.carPos[k] = p.carPos[k];
            g_last.carRot[k] = p.carRot[k];
        }
        g_last.kind = p.kind;
        g_last.inCar = (p.flags & 1) != 0;
        g_last.layerOn = (p.flags & 2) != 0;
        g_last.waistOn = (p.flags & 4) != 0;
        g_last.layerMotion = p.layerMotion;
        g_last.weapon = p.weapon;
        g_last.shots = p.shots;
        g_last.layerSeq = p.layerSeq;
        for (int k = 0; k < 4; ++k) g_last.waist[k] = p.waist[k];
        g_last.yaw = p.yaw;
        g_last.car = (int)p.car;
        g_last.motion = p.motion;
        g_last.suit = p.suit;
        p.nick[15] = 0;
        CopyClean(g_peerNick, p.nick, 16);
        g_haveLast = true;
        g_lastHeard = now;
        if (!g_toldHeard) {
            g_toldHeard = true;
            const u32 ip = from.sin_addr.s_addr;
            Log("multiplayer: %S is heard from %u.%u.%u.%u port %d (zone %u %u %u)", g_peerNick, ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
                HostOrder(from.sin_port), p.zone[0], p.zone[1], p.zone[2]);
            wchar_t uk[80], en[80];
            wsprintfW(uk, L"%s у грі", g_peerNick);
            wsprintfW(en, L"%s is in the game", g_peerNick);
            ShowNotice(uk, en, L"Гравця видно, коли ви в одній локації. Чат: клавіша T.", L"The player is seen while you are in the same location. Chat: the T key.", 5);
        }
    }
    if (g_toldHeard && now - g_lastHeard > 5000) {
        g_toldHeard = false;
        g_haveLast = false;
        Log("multiplayer: nothing from the other player for 5 s");
        ShowNotice(L"Гравець зник зі зв'язку", L"The player is gone", L"Від нього нічого не чути вже 5 секунд.", L"Nothing has come from him for 5 seconds.", 4);
        if (g_mode == kNetHost) g_havePeer = false;  // the place is free for the next caller
    }
}

// The ini's NetPartner=1: the session starts with the game, as in the test builds.
void StartFromIni() {
    if (g_started) return;
    g_started = true;
    if (!g_cfg.netPartner) return;
    const wchar_t* peer = g_cfg.netPeer;
    if ((peer[0] == L'm' || peer[0] == L'M') && (peer[1] == L'i' || peer[1] == L'I')) NetMirrorStart();
    else if (peer[0]) NetJoin(peer);
    else NetHostStart();
}

}  // namespace

bool NetCoopOn() {
    StartFromIni();
    return g_mode != kNetOff;
}
int NetMode() {
    StartFromIni();
    return g_mode;
}

bool NetHostStart() {
    Lock lock;
    g_started = true;
    if (!EnsureSocket(true)) return false;
    ResetSession();
    g_mode = kNetHost;
    Log("multiplayer: server \"%S\" on UDP port %d, waiting for a player", g_cfg.netServerName, g_boundPort);
    return true;
}

bool NetJoin(const wchar_t* address) {
    bool ok = false;
    int port = ServerPort();
    const u32 ip = ParseAddress(address, &port, &ok);
    if (!ok || !ip) return false;
    {
        Lock lock;
        g_started = true;
        if (!EnsureSocket(false)) return false;
        ResetSession();
        g_peer = {};
        g_peer.sin_family = AF_INET;
        g_peer.sin_port = NetOrder(port);
        g_peer.sin_addr.s_addr = ip;
        g_havePeer = true;
        g_mode = kNetClient;
        Log("multiplayer: joining %u.%u.%u.%u port %d", ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24, port);
        // the addresses joined last, this one first
        ReadRecent();
        wchar_t text[24];
        wsprintfW(text, L"%u.%u.%u.%u:%d", ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24, port);
        int at = 3;
        for (int i = 0; i < 4; ++i)
            if (lstrcmpW(g_recent[i], text) == 0) { at = i; break; }
        for (int i = at; i > 0; --i) lstrcpyW(g_recent[i], g_recent[i - 1]);
        lstrcpyW(g_recent[0], text);
    }
    for (int i = 0; i < 4; ++i) {
        wchar_t key[12];
        wsprintfW(key, L"Recent%d", i + 1);
        WritePrivateProfileStringW(L"Coop", key, g_recent[i], g_paths.iniFile);
    }
    return true;
}

void NetMirrorStart() {
    Lock lock;
    g_started = true;
    ResetSession();
    g_mode = kNetMirror;
    Log("multiplayer: mirror (no network: the figure repeats York 1.5 s late, 30 units aside)");
}

void NetLeave() {
    Lock lock;
    g_started = true;
    if (g_mode != kNetOff) Log("multiplayer: off");
    ResetSession();
    g_mode = kNetOff;
}

bool NetPeerText(wchar_t* out) {  // the other player's address, false while there is none
    Lock lock;
    if (g_mode < kNetHost || !g_havePeer) return false;
    const u32 ip = g_peer.sin_addr.s_addr;
    wsprintfW(out, L"%u.%u.%u.%u:%d", ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24, HostOrder(g_peer.sin_port));
    return true;
}
// This computer's own IPv4 addresses, for the player to tell the other one ("192.168.1.5, 10.0.0.2"); false = none found.
bool NetOwnAddresses(wchar_t* out, int max) {
    Lock lock;
    out[0] = 0;
    char name[256];
    if (!g_wsUp || g_ws.gethostname(name, sizeof name) != 0) return false;
    const hostent* h = g_ws.gethostbyname(name);
    if (!h || h->h_addrtype != AF_INET || !h->h_addr_list) return false;
    int len = 0;
    for (int i = 0; h->h_addr_list[i] && i < 4; ++i) {
        const u8* a = (const u8*)h->h_addr_list[i];
        if (a[0] == 127 || (a[0] == 169 && a[1] == 254)) continue;  // this computer only, or no network
        if (len + 20 >= max) break;
        len += wsprintfW(out + len, len ? L", %u.%u.%u.%u" : L"%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    }
    return len > 0;
}
bool NetPeerHeard() { return g_mode == kNetMirror || (g_mode >= kNetHost && g_haveLast); }

// The picture's frame: what came in is read also while the game's own update does not run (the pause, the menu).
void NetPumpFrame() {
    if (g_sock == INVALID_SOCKET) return;
    Lock lock;
    Pump(GetTickCount());
}

// The question of the server list goes out: to the whole local network and to the addresses joined before.
void NetDiscover() {
    Lock lock;
    if (!EnsureSocket(false)) return;
    ReadRecent();
    QueryPacket q = {kQueryMark, kNetVersion, 0};
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = NetOrder(ServerPort());
    to.sin_addr.s_addr = 0xFFFFFFFFu;
    SendTo(&q, sizeof q, to);
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        int port = ServerPort();
        const u32 ip = ParseAddress(g_recent[i], &port, &ok);
        if (!ok || !ip) continue;
        to.sin_port = NetOrder(port);
        to.sin_addr.s_addr = ip;
        SendTo(&q, sizeof q, to);
    }
    // a server that has not answered the last few questions is not there any more
    const DWORD now = GetTickCount();
    int kept = 0;
    for (int i = 0; i < g_serverCount; ++i)
        if (now - g_servers[i].at < 9000) g_servers[kept++] = g_servers[i];
    g_serverCount = kept;
}

int NetServers(NetServer* out, int max) {
    Lock lock;
    int n = 0;
    for (; n < g_serverCount && n < max; ++n) out[n] = g_servers[n];
    return n;
}

const wchar_t* NetRecent(int i) {
    ReadRecent();
    return i >= 0 && i < 4 ? g_recent[i] : L"";
}

// This York's state goes out (20 times a second at most) and what came in is read. Game thread, never waits.
void NetCoopExchange(const NetPeerState& mine) {
    g_mineZone0 = mine.zone[0];
    if (g_mode == kNetOff) return;
    const DWORD now = GetTickCount();
    Lock lock;
    if (g_mode == kNetMirror) {
        if (now - g_lastSend >= 50) {
            g_lastSend = now;
            Past& p = g_ring[g_ringAt++ % kRing];
            p.at = now;
            p.s = mine;
            p.s.pos[0] += 30.f;
            p.s.carPos[0] += 30.f;
            CopyClean(g_peerNick, g_cfg.netNick, 16);
        }
        for (Out& o : g_out)  // the mirror says the line back a second and a half later
            if (o.left && (int)(now - o.next) >= 0) { o.left = 0; ChatIn(o.text); }
        return;
    }
    if (g_sock == INVALID_SOCKET) return;
    if (g_havePeer && now - g_lastSend >= 50) {
        g_lastSend = now;
        Packet p = {};
        p.mark = kMark;
        p.version = kNetVersion;
        p.kind = (u16)mine.kind;
        p.seq = ++g_seqOut;
        for (int i = 0; i < 3; ++i) {
            p.zone[i] = mine.zone[i];
            p.pos[i] = mine.pos[i];
            p.carPos[i] = mine.carPos[i];
            p.carRot[i] = mine.carRot[i];
        }
        p.yaw = mine.yaw;
        p.flags = (mine.inCar ? 1u : 0u) | (mine.layerOn ? 2u : 0u) | (mine.waistOn ? 4u : 0u);
        p.layerMotion = mine.layerMotion;
        p.weapon = mine.weapon;
        p.shots = mine.shots;
        p.layerSeq = mine.layerSeq;
        for (int i = 0; i < 4; ++i) p.waist[i] = mine.waist[i];
        p.car = (u32)mine.car;
        p.motion = mine.motion;
        p.suit = mine.suit;
        CopyClean(p.nick, g_cfg.netNick, 16);
        SendTo(&p, sizeof p, g_peer);
    }
    Pump(now);
}

// Where the other player is now; false while nothing fresh is known.
bool NetCoopPeer(NetPeerState* out) {
    const DWORD now = GetTickCount();
    Lock lock;
    if (g_mode == kNetMirror) {
        const Past* best = nullptr;
        for (int i = 0; i < kRing; ++i) {
            const Past& p = g_ring[i];
            if (p.at && now - p.at >= 1500 && (!best || p.at > best->at)) best = &p;
        }
        if (!best || now - best->at > 4000) return false;
        *out = best->s;
        return true;
    }
    if (g_mode < kNetHost || !g_haveLast || now - g_lastHeard > 3000) return false;
    *out = g_last;
    return true;
}

const wchar_t* NetPeerNick() { return g_peerNick; }

bool NetPeerOnMap(float* x, float* z) {
    NetPeerState s;
    if (!NetCoopOn() || !NetCoopPeer(&s) || s.zone[0] != g_mineZone0) return false;
    *x = s.inCar ? s.carPos[0] : s.pos[0];
    *z = s.inCar ? s.carPos[2] : s.pos[2];
    return true;
}

void NetChatSay(const wchar_t* text) {
    if (g_mode == kNetOff || !text[0]) return;
    Lock lock;
    Out* slot = &g_out[0];
    for (Out& o : g_out)
        if (!o.left) { slot = &o; break; }
    slot->id = (GetTickCount() << 8) ^ ++g_chatId;
    if (!slot->id) slot->id = 1;
    slot->left = g_mode == kNetMirror ? 1 : 6;
    slot->next = GetTickCount() + (g_mode == kNetMirror ? 1500 : 0);
    CopyClean(slot->text, text, 96);
}

bool NetChatTake(wchar_t* text, int max) {
    Lock lock;
    if (!g_inCount) return false;
    CopyClean(text, g_in[g_inHead], max < 96 ? max : 96);
    g_inHead = (g_inHead + 1) % 8;
    --g_inCount;
    return true;
}

}  // namespace ufx
