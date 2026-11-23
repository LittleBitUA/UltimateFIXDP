// A friend over the network, first step: two running games tell each other where their York stands, and each shows the other one
// as the partner figure (the doll of combat_strafe.cpp). Nothing else is shared: each game keeps its own story, enemies and time.
//
//   [Coop] NetPartner=1        on
//   [Coop] NetPeer=10.0.0.2    the friend's address (IPv4, digits), or "mirror": no network, the figure repeats this York a second
//                              and a half late, some steps aside (a test for one player)
//   [Coop] NetPort=47900       UDP port, the same on both sides; both listen on it and send to it
//
// One UDP datagram of 40 bytes about 20 times a second, sent and read on the game thread without waiting (a socket that does not
// block). Only datagrams from the friend's address with our mark and version are taken, and only a newer one than the last. No
// address is looked up, nothing is sent anywhere but to NetPeer; it is meant for a home network or a virtual one (ZeroTier,
// Radmin VPN, Hamachi). ws2_32 is loaded at run time, as in editor_server.cpp.
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
    u16 version;  // kNetVersion
    u16 kind;     // 0 stands, 1 walks, 2 runs
    u32 seq;
    u32 zone;     // the location number (CGame+834FE0h): the figure is shown only in the same one
    float pos[3];
    float yaw;
    u32 flags;    // bit 0: in a car
    u32 car;      // the number of the car York drives (the game's numbered cars), 0 = not known
};
#pragma pack(pop)
constexpr u32 kMark = 0x50584655u;
constexpr u16 kNetVersion = 1;

int g_state = 0;  // 0 not tried, 1 mirror, 2 socket open, -1 failed
SOCKET g_sock = INVALID_SOCKET;
sockaddr_in g_peer;
u32 g_seqOut = 0, g_seqIn = 0;
DWORD g_lastSend = 0, g_lastHeard = 0;
NetPeerState g_last;
bool g_haveLast = false, g_toldHeard = false;

// mirror: this York's own states, read back a second and a half later
constexpr int kRing = 64;
struct Past { DWORD at; NetPeerState s; } g_ring[kRing];
int g_ringAt = 0;

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

bool Open() {
    if (g_state) return g_state > 0;
    g_state = -1;
    const wchar_t* peer = g_cfg.netPeer;
    if ((peer[0] == L'm' || peer[0] == L'M') && (peer[1] == L'i' || peer[1] == L'I')) {
        g_state = 1;
        Log("net partner: mirror (no network: the figure repeats York 1.5 s late, 8 units aside)");
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
    int port = g_cfg.netPort;
    if (port < 1024 || port > 65535) port = 47900;
    g_sock = g_ws.socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in me = {};
    me.sin_family = AF_INET;
    me.sin_port = (u_short)(((port & 0xFF) << 8) | (port >> 8));
    u_long noWait = 1;
    if (g_sock == INVALID_SOCKET || g_ws.bind(g_sock, (const sockaddr*)&me, sizeof me) != 0 || g_ws.ioctl(g_sock, (long)FIONBIO, &noWait) != 0) {
        Log("net partner: UDP port %d could not be opened (taken by another program?); off", port);
        if (g_sock != INVALID_SOCKET) g_ws.close(g_sock);
        g_sock = INVALID_SOCKET;
        return false;
    }
    g_peer = me;
    g_peer.sin_addr.s_addr = ip;
    g_state = 2;
    Log("net partner: UDP port %d open, the friend is %u.%u.%u.%u (40 bytes about 20 times a second, to that address only)", port, ip & 255, (ip >> 8) & 255,
        (ip >> 16) & 255, ip >> 24);
    return true;
}

}  // namespace

bool NetCoopOn() { return g_cfg.netPartner != 0; }

// This York's place goes out (20 times a second at most) and what came in is read. Game thread, never waits.
void NetCoopExchange(const float pos[3], float yaw, u32 zone, int kind, bool inCar, int car) {
    if (!Open()) return;
    const DWORD now = GetTickCount();
    if (g_state == 1) {
        if (now - g_lastSend >= 50) {
            g_lastSend = now;
            Past& p = g_ring[g_ringAt++ % kRing];
            p.at = now;
            p.s.zone = zone;
            p.s.kind = kind;
            p.s.inCar = inCar;
            p.s.car = car;
            p.s.yaw = yaw;
            p.s.pos[0] = pos[0] + 8.f;
            p.s.pos[1] = pos[1];
            p.s.pos[2] = pos[2];
        }
        return;
    }
    if (now - g_lastSend >= 50) {
        g_lastSend = now;
        Packet p = {};
        p.mark = kMark;
        p.version = kNetVersion;
        p.kind = (u16)kind;
        p.seq = ++g_seqOut;
        p.zone = zone;
        for (int i = 0; i < 3; ++i) p.pos[i] = pos[i];
        p.yaw = yaw;
        p.flags = inCar ? 1u : 0u;
        p.car = (u32)car;
        g_ws.sendto(g_sock, (const char*)&p, sizeof p, 0, (const sockaddr*)&g_peer, sizeof g_peer);
    }
    for (int i = 0; i < 32; ++i) {
        Packet p;
        sockaddr_in from = {};
        int fromLen = sizeof from;
        const int got = g_ws.recvfrom(g_sock, (char*)&p, sizeof p, 0, (sockaddr*)&from, &fromLen);
        if (got <= 0) break;
        if (got != (int)sizeof p || p.mark != kMark || p.version != kNetVersion || from.sin_addr.s_addr != g_peer.sin_addr.s_addr) continue;
        // a newer one than the last; a much older number is the friend's game started again
        if (g_haveLast && (int)(p.seq - g_seqIn) <= 0 && g_seqIn - p.seq < 2000) continue;
        bool sane = p.kind <= 2;
        for (int k = 0; k < 3; ++k)
            if (!(p.pos[k] > -1.0e6f && p.pos[k] < 1.0e6f)) sane = false;  // also turns down what is not a number
        if (!(p.yaw > -100.f && p.yaw < 100.f) || !sane) continue;
        g_seqIn = p.seq;
        g_last.zone = p.zone;
        g_last.kind = p.kind;
        g_last.inCar = (p.flags & 1) != 0;
        g_last.car = (int)(p.car & 0xFF);
        g_last.yaw = p.yaw;
        for (int k = 0; k < 3; ++k) g_last.pos[k] = p.pos[k];
        g_haveLast = true;
        g_lastHeard = now;
        if (!g_toldHeard) {
            g_toldHeard = true;
            Log("net partner: the friend is heard (zone %u, at %d %d %d)", p.zone, (int)p.pos[0], (int)p.pos[1], (int)p.pos[2]);
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

}  // namespace ufx
