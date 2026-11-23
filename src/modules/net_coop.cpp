// A friend over the network: two running games tell each other where their York is, what he does and what he drives, and each
// shows the other one as a second York (the figure of combat_strafe.cpp) and his car. Nothing else is shared: each game keeps its
// own story, enemies and time.
//
//   [Coop] NetPartner=1        on
//   [Coop] NetPeer=10.0.0.2    the address of the friend's computer (IPv4, digits), or "mirror": no network, the figure repeats this
//                              York a second and a half late, 30 units aside (a test for one player)
//   [Coop] NetPort=47900       the UDP port this game listens on
//   [Coop] NetPeerPort=47900   the port the friend's game listens on (the same number by default; two games on one computer need
//                              two different ones, each naming the other's)
//   [Coop] NetCarCollision=1   the friend's car can be bumped into (0 = it is only a picture, as in the first tests)
//
// One UDP datagram of 96 bytes about 20 times a second, sent and read on the game thread without waiting. Only datagrams from the
// friend's address with our mark and version are taken, and only a newer one than the last; every number is checked before use.
// Nothing is sent anywhere but to NetPeer. Meant for a home network or a virtual one (ZeroTier, Radmin VPN, Hamachi). ws2_32 is
// loaded at run time, as in editor_server.cpp.
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
    u32 flags;    // bit 0: at the wheel
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
#pragma pack(pop)
constexpr u32 kChatMark = 0x43584655u;
constexpr u32 kMark = 0x50584655u;
constexpr u16 kNetVersion = 5;

int g_state = 0;  // 0 not tried, 1 mirror, 2 socket open, -1 failed
SOCKET g_sock = INVALID_SOCKET;
sockaddr_in g_peer;
u32 g_seqOut = 0, g_seqIn = 0;
DWORD g_lastSend = 0, g_lastHeard = 0;
NetPeerState g_last;
bool g_haveLast = false, g_toldHeard = false;

constexpr int kRing = 64;  // mirror: this York's own states, read back a second and a half later
struct Past { DWORD at; NetPeerState s; } g_ring[kRing];
int g_ringAt = 0;

// Chat. Lines to send wait here and go out six times each (UDP loses packets); lines that came wait for the chat window.
// The window runs with the picture, the exchange with the game: a small lock keeps the two apart.
volatile LONG g_chatLock = 0;
struct Lock {
    Lock() { while (InterlockedExchange(&g_chatLock, 1)) Sleep(0); }
    ~Lock() { InterlockedExchange(&g_chatLock, 0); }
};
struct Out { u32 id; int left; DWORD next; wchar_t text[96]; } g_out[4];
wchar_t g_in[8][96];
int g_inHead = 0, g_inCount = 0;
u32 g_chatId = 0, g_seenIds[16];
int g_seenAt = 0;
wchar_t g_peerNick[16];
u32 g_mineZone0 = 0xFFFFFFFFu;

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

u32 ParseIp(const wchar_t* text, bool* ok) {  // a.b.c.d -> network order
    u32 part[4] = {0, 0, 0, 0};
    int n = 0, digits = 0;
    for (const wchar_t* c = text;; ++c) {
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
    return part[0] | (part[1] << 8) | (part[2] << 16) | (part[3] << 24);
}
inline u_short NetOrder(int port) { return (u_short)(((port & 0xFF) << 8) | ((port >> 8) & 0xFF)); }
inline bool Finite(float v, float limit) { return v > -limit && v < limit; }  // also false for what is not a number

bool Open() {
    if (g_state) return g_state > 0;
    g_state = -1;
    const wchar_t* peer = g_cfg.netPeer;
    if ((peer[0] == L'm' || peer[0] == L'M') && (peer[1] == L'i' || peer[1] == L'I')) {
        g_state = 1;
        Log("net partner: mirror (no network: the figure repeats York 1.5 s late, 30 units aside)");
        return true;
    }
    bool ok = false;
    const u32 ip = ParseIp(peer, &ok);
    if (!ok || !ip) {
        Log("net partner: [Coop] NetPeer \"%S\" is not an address like 10.0.0.2 (or \"mirror\"); off", peer);
        return false;
    }
    HMODULE m = LoadLibraryW(L"ws2_32.dll");
    static const char* const names[7] = {"WSAStartup", "socket", "bind", "ioctlsocket", "sendto", "recvfrom", "closesocket"};
    void** slots = (void**)&g_ws;
    for (int i = 0; m && i < 7; ++i) {
        slots[i] = (void*)GetProcAddress(m, names[i]);
        if (!slots[i]) m = nullptr;
    }
    WSADATA wsa;
    if (!m || g_ws.startup(MAKEWORD(2, 2), &wsa) != 0) {
        Log("net partner: Winsock did not start; off");
        return false;
    }
    int port = g_cfg.netPort, peerPort = g_cfg.netPeerPort;
    if (port < 1024 || port > 65535) port = 47900;
    if (peerPort < 1024 || peerPort > 65535) peerPort = port;
    g_sock = g_ws.socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in me = {};
    me.sin_family = AF_INET;
    me.sin_port = NetOrder(port);
    u_long noWait = 1;
    if (g_sock == INVALID_SOCKET || g_ws.bind(g_sock, (const sockaddr*)&me, sizeof me) != 0 || g_ws.ioctl(g_sock, (long)FIONBIO, &noWait) != 0) {
        Log("net partner: UDP port %d could not be opened (taken by another program?); off", port);
        if (g_sock != INVALID_SOCKET) g_ws.close(g_sock);
        g_sock = INVALID_SOCKET;
        return false;
    }
    g_peer = me;
    g_peer.sin_port = NetOrder(peerPort);
    g_peer.sin_addr.s_addr = ip;
    g_state = 2;
    Log("net partner: listening on UDP port %d, the friend is %u.%u.%u.%u port %d (96 bytes about 20 times a second, to that address only)", port, ip & 255,
        (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24, peerPort);
    return true;
}

}  // namespace

bool NetCoopOn() { return g_cfg.netPartner != 0; }

// This York's state goes out (20 times a second at most) and what came in is read. Game thread, never waits.
void NetCoopExchange(const NetPeerState& mine) {
    g_mineZone0 = mine.zone[0];
    if (!Open()) return;
    const DWORD now = GetTickCount();
    if (g_state == 1) {
        if (now - g_lastSend >= 50) {
            g_lastSend = now;
            Past& p = g_ring[g_ringAt++ % kRing];
            p.at = now;
            p.s = mine;
            p.s.pos[0] += 30.f;
            p.s.carPos[0] += 30.f;
            CopyClean(g_peerNick, g_cfg.netNick, 16);
        }
        Lock lock;
        for (Out& o : g_out)  // the mirror says the line back a second and a half later
            if (o.left && (int)(now - o.next) >= 0) { o.left = 0; ChatIn(o.text); }
        return;
    }
    if (now - g_lastSend >= 50) {
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
        g_ws.sendto(g_sock, (const char*)&p, sizeof p, 0, (const sockaddr*)&g_peer, sizeof g_peer);
    }
    {
        Lock lock;
        for (Out& o : g_out)
            if (o.left && (int)(now - o.next) >= 0) {
                --o.left;
                o.next = now + 150;
                ChatPacket c = {};
                c.mark = kChatMark;
                c.version = kNetVersion;
                c.id = o.id;
                CopyClean(c.text, o.text, 96);
                g_ws.sendto(g_sock, (const char*)&c, sizeof c, 0, (const sockaddr*)&g_peer, sizeof g_peer);
            }
    }
    for (int i = 0; i < 32; ++i) {
        union { Packet p; ChatPacket c; } u;
        Packet& p = u.p;
        sockaddr_in from = {};
        int fromLen = sizeof from;
        const int got = g_ws.recvfrom(g_sock, (char*)&u, sizeof u, 0, (sockaddr*)&from, &fromLen);
        if (got <= 0) break;
        if (got == (int)sizeof(ChatPacket) && u.c.mark == kChatMark && u.c.version == kNetVersion && from.sin_addr.s_addr == g_peer.sin_addr.s_addr) {
            bool seen = false;
            for (u32 id : g_seenIds) seen = seen || id == u.c.id;
            if (!seen && u.c.id) {
                g_seenIds[g_seenAt++ % 16] = u.c.id;
                u.c.text[95] = 0;
                Lock lock;
                ChatIn(u.c.text);
            }
            continue;
        }
        if (got != (int)sizeof p || p.mark != kMark || p.version != kNetVersion || from.sin_addr.s_addr != g_peer.sin_addr.s_addr) continue;
        // a newer one than the last; a much older number is the friend's game started again
        if (g_haveLast && (int)(p.seq - g_seqIn) <= 0 && g_seqIn - p.seq < 2000) continue;
        bool sane = p.kind <= 3 && Finite(p.yaw, 100.f) && p.car < 0x10000 && p.motion >= -1 && p.motion < 0x10000 && p.suit >= 0 && p.suit < 41;
        for (int k = 0; k < 3; ++k)
            if (!Finite(p.pos[k], 1.0e6f) || !Finite(p.carPos[k], 1.0e6f) || !Finite(p.carRot[k], 100.f)) sane = false;
        if (p.weapon < 0 || p.weapon > 0x16D || p.layerMotion < -1 || p.layerMotion > 0xFFFF) sane = false;
        for (int k = 0; k < 4; ++k)
            if (!Finite(p.waist[k], 1.01f)) sane = false;
        if (!sane) continue;
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
            Log("net partner: the friend is heard (zone %u %u %u, at %d %d %d)", p.zone[0], p.zone[1], p.zone[2], (int)p.pos[0], (int)p.pos[1], (int)p.pos[2]);
            ShowNotice(L"Напарник на зв'язку", L"The partner is connected", L"Друга видно, коли ви в одній локації.", L"The friend is seen while you are in the same location.", 4);
        }
    }
    if (g_toldHeard && now - g_lastHeard > 5000) {
        g_toldHeard = false;
        Log("net partner: nothing from the friend for 5 s");
        ShowNotice(L"Напарник зник зі зв'язку", L"The partner is gone", L"Від друга нічого не чути вже 5 секунд.", L"Nothing has come from the friend for 5 seconds.", 4);
    }
}

// Where the friend is now; false while nothing fresh is known.
bool NetCoopPeer(NetPeerState* out) {
    const DWORD now = GetTickCount();
    if (g_state == 1) {
        const Past* best = nullptr;
        for (int i = 0; i < kRing; ++i) {
            const Past& p = g_ring[i];
            if (p.at && now - p.at >= 1500 && (!best || p.at > best->at)) best = &p;
        }
        if (!best || now - best->at > 4000) return false;
        *out = best->s;
        return true;
    }
    if (g_state != 2 || !g_haveLast || now - g_lastHeard > 3000) return false;
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
    if (g_state <= 0 || !text[0]) return;
    Lock lock;
    Out* slot = &g_out[0];
    for (Out& o : g_out)
        if (!o.left) { slot = &o; break; }
    slot->id = (GetTickCount() << 8) ^ ++g_chatId;
    if (!slot->id) slot->id = 1;
    slot->left = g_state == 1 ? 1 : 6;
    slot->next = GetTickCount() + (g_state == 1 ? 1500 : 0);
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
