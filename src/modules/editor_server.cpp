// The quest editor's page. The editor is a web page (UltimateFix\editor\index.html) that the mod serves to the browser of
// the same computer, with a small interface beside it, so that the editor sees the game live:
//
//   GET  /                     the page
//   GET  /api/state            where York stands, the zone, the clock, what the quest runner does (JSON)
//   GET  /api/lists            the residents, items and cars by name (JSON)
//   GET  /api/maptile?n=0..63  one tile of the game's own map of Greenvale as a DDS file (1024x1024 DXT1), read from the game's
//                              archive on this thread: nothing of the running game is touched
//   GET  /api/mission?slot=N   the file of the quest, as it is on disk (404 = an empty slot)
//   POST /api/mission?slot=N   the file written from the request's body
//   POST /api/cmd?do=...       run, restart, stop (slot=N); tele (x y z yaw, thousandths); hour, weather, car, item (v, n)
//
// It listens on 127.0.0.1 only, and only after "Open the quest editor" was pressed in this run. A request that names another
// site as its origin is refused, so a page on the internet cannot drive the game through the player's browser. The server
// thread never touches the game: it leaves requests that the game thread picks up (MissionRequest and the cheats' own).
#include "../base.h"
#include "../inflate.h"
#include "../item_limits.h"
#include "../item_names.h"
#include "../sites.h"
#include <winsock2.h>

namespace ufx {
namespace {

int g_port = 0;
SOCKET g_listen = INVALID_SOCKET;

// Winsock and the shell are loaded only when the editor is first opened: the mod's library keeps the imports it always had,
// so a game that never opens the editor starts exactly as before.
struct Net {
    int(WSAAPI* startup)(WORD, LPWSADATA);
    SOCKET(WSAAPI* socket)(int, int, int);
    int(WSAAPI* bind)(SOCKET, const sockaddr*, int);
    int(WSAAPI* listen)(SOCKET, int);
    SOCKET(WSAAPI* accept)(SOCKET, sockaddr*, int*);
    int(WSAAPI* recv)(SOCKET, char*, int, int);
    int(WSAAPI* send)(SOCKET, const char*, int, int);
    int(WSAAPI* close)(SOCKET);
    int(WSAAPI* shutdown)(SOCKET, int);
    int(WSAAPI* setsockopt)(SOCKET, int, int, const char*, int);
} g_net;
bool LoadNet() {
    HMODULE m = LoadLibraryW(L"ws2_32.dll");
    if (!m) return false;
    static const char* const names[10] = {"WSAStartup", "socket", "bind", "listen", "accept", "recv", "send", "closesocket", "shutdown", "setsockopt"};
    void** slots = (void**)&g_net;
    for (int i = 0; i < 10; ++i) {
        slots[i] = (void*)GetProcAddress(m, names[i]);
        if (!slots[i]) return false;
    }
    return true;
}
inline u_short Swap16(int v) { return (u_short)(((v & 0xFF) << 8) | ((v >> 8) & 0xFF)); }
constexpr int kMaxBody = 400000;

int Find(const char* hay, int n, const char* needle) {
    const int m = lstrlenA(needle);
    for (int i = 0; i + m <= n; ++i) {
        int k = 0;
        while (k < m && (hay[i + k] | 0x20) == (needle[k] | 0x20)) ++k;
        if (k == m) return i;
    }
    return -1;
}

void SendAll(SOCKET s, const char* data, int n) {
    while (n > 0) {
        const int put = g_net.send(s, data, n, 0);
        if (put <= 0) return;
        data += put;
        n -= put;
    }
}

void Reply(SOCKET s, const char* status, const char* type, const char* body, int n) {
    char head[256];
    const int h = wsprintfA(head, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", status, type, n);
    SendAll(s, head, h);
    if (n > 0) SendAll(s, body, n);
}
void ReplyText(SOCKET s, const char* status, const char* text) { Reply(s, status, "text/plain; charset=utf-8", text, lstrlenA(text)); }

// A map tile: resource 937h + n ("UPDATA/BG/SMAP/00/SMAP00_00_0NN.XPC"). Its record in the game's resource index (in the exe
// image, 20h bytes each from 008D8D78h: +0 where the record begins in the archive, the data 104h further; +8 size; +0Ch archive
// number) says where it lies in UPDATA\_FLINK\DPSERIAL.00N; the file is an XPC2 with one zlib-packed DDS.
void MapTile(SOCKET s, int n) {
    const char* why = "no such tile";
    u8 *xpc = nullptr, *dds = nullptr;
    size_t ddsSize = 0;
    if (n >= 0 && n < 64) {
        const u32* e = (const u32*)(uintptr_t)(0x8D8D78u + (0x937u + (u32)n) * 0x20u);
        const u32 off = e[0], size = e[2], arch = e[3];
        why = "the game's index has no such record";
        if (off != 0xFFFFFFFFu && size >= 0x60 && size <= (4u << 20) && arch <= 2) {
            wchar_t path[MAX_PATH + 48];
            wsprintfW(path, L"%sUPDATA\\_FLINK\\DPSERIAL.%03u", g_paths.gameDir, arch + 1);
            HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            why = "the game's archive could not be opened";
            if (f != INVALID_HANDLE_VALUE) {
                LONG high = 0;
                DWORD got = 0;
                xpc = (u8*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                SetFilePointer(f, (LONG)(off + 0x104u), &high, FILE_BEGIN);
                why = "the tile could not be read";
                if (xpc && ReadFile(f, xpc, size, &got, nullptr) && got == size) {
                    size_t need = 0;
                    XpcFirstPicture(xpc, size, nullptr, 0, &need);
                    why = "the tile is not a picture the editor knows";
                    if (need >= 128 && need <= (8u << 20)) {
                        dds = (u8*)VirtualAlloc(nullptr, need, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                        if (dds) ddsSize = XpcFirstPicture(xpc, size, dds, need, nullptr);
                    }
                }
                CloseHandle(f);
            }
        }
    }
    if (ddsSize) {
        char head[256];
        const int h = wsprintfA(head, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\nCache-Control: max-age=86400\r\nConnection: close\r\n\r\n", (int)ddsSize);
        SendAll(s, head, h);
        SendAll(s, (const char*)dds, (int)ddsSize);
    } else {
        static int logged = 0;
        if (logged < 3) {
            ++logged;
            Log("editor: map tile %d not served: %s", n, why);
        }
        Reply(s, "404 Not Found", "text/plain; charset=utf-8", why, lstrlenA(why));
    }
    if (xpc) VirtualFree(xpc, 0, MEM_RELEASE);
    if (dds) VirtualFree(dds, 0, MEM_RELEASE);
}


// A whole file into memory the caller frees; null when it is missing
char* ReadWhole(const wchar_t* path, int* size) {
    *size = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    const DWORD n = GetFileSize(f, nullptr);
    char* buf = n < 4000000 ? (char*)VirtualAlloc(nullptr, n + 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    DWORD got = 0;
    if (buf) ReadFile(f, buf, n, &got, nullptr);
    CloseHandle(f);
    *size = (int)got;
    return buf;
}

// The value of a query parameter as a number; `def` when it is not there
int Query(const char* url, const char* name, int def) {
    char key[24];
    wsprintfA(key, "%s=", name);
    const int n = lstrlenA(url);
    for (int i = 0; i < n; ++i) {
        if ((url[i] != '?' && url[i] != '&') || Find(url + i + 1, lstrlenA(key), key) != 0) continue;
        const char* p = url + i + 1 + lstrlenA(key);
        const bool neg = *p == '-';
        if (neg) ++p;
        int v = 0;
        for (; *p >= '0' && *p <= '9'; ++p) v = v * 10 + (*p - '0');
        return neg ? -v : v;
    }
    return def;
}
bool QueryIs(const char* url, const char* name, const char* value) {
    char key[40];
    wsprintfA(key, "%s=%s", name, value);
    const int at = Find(url, lstrlenA(url), key);
    if (at < 1) return false;
    const char after = url[at + lstrlenA(key)];
    return (url[at - 1] == '?' || url[at - 1] == '&') && (after == 0 || after == '&');
}

int JsonText(char* out, const char* text) {  // the text with quotes and backslashes escaped, ASCII only
    int n = 0;
    for (; *text; ++text) {
        const char c = *text;
        if (c == '"' || c == '\\') out[n++] = '\\';
        out[n++] = (c >= 0x20 && c < 0x7F) ? c : '?';
    }
    return n;
}

void State(SOCKET s) {
    char j[1200], st[160];
    float p[4] = {0, 0, 0, 0};
    const bool have = PlayerPlace(p);
    const u8* game = *(const u8* volatile*)(uintptr_t)kGameManagerPtrVa;
    const int zone = game && !IsBadReadPtr(game + 0x834FE0, 4) ? *(volatile int*)(game + 0x834FE0) : -1;
    const u32 timer = *(volatile u32*)(uintptr_t)0xBE1EA0;
    const bool clock = timer > 0x10000 && !IsBadReadPtr((void*)(uintptr_t)timer, 0x18);
    const bool free = have && *(volatile u32*)(uintptr_t)kGameplayModeVa == 3 && !(*(volatile u32*)(uintptr_t)kEventModeVa & 0x80000000u);
    const int sn = WideCharToMultiByte(CP_UTF8, 0, MissionState(), -1, st, 150, nullptr, nullptr);
    st[sn > 0 ? sn - 1 : 0] = 0;
    for (char* c = st; *c; ++c)
        if (*c == '"' || *c == '\\') *c = '\'';
    const int n = wsprintfA(j,
                            "{\"version\":\"%s\",\"inGame\":%d,\"free\":%d,\"x\":%d,\"y\":%d,\"z\":%d,\"yaw\":%d,\"zone\":%d,\"hour\":%d,\"minute\":%d,"
                            "\"slot\":%d,\"running\":%d,\"step\":%d,\"steps\":%d,\"lang\":%d,\"state\":\"%s\"}",
                            kVersion, have ? 1 : 0, free ? 1 : 0, (int)(p[0] * 1000.f), (int)(p[1] * 1000.f), (int)(p[2] * 1000.f), (int)(p[3] * 1000.f), zone,
                            clock ? *(volatile int*)(timer + 0xC) : -1, clock ? *(volatile int*)(timer + 0x10) : -1, g_mission.slot, MissionActive() ? 1 : 0,
                            MissionStepNow(), g_mission.stepCount, g_cfg.language == 1 ? 1 : 0, st);
    Reply(s, "200 OK", "application/json; charset=utf-8", j, n);
}

void Lists(SOCKET s) {
    char* j = (char*)VirtualAlloc(nullptr, 120000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!j) { ReplyText(s, "500 Error", "no memory"); return; }
    int n = wsprintfA(j, "{\"residents\":[");
    bool first = true;
    for (int id = 1; id <= 49; ++id) {
        char name[16] = "";
        DollName(id, name);
        n += wsprintfA(j + n, "%s{\"id\":%d,\"name\":\"", first ? "" : ",", id);
        n += JsonText(j + n, name);
        n += wsprintfA(j + n, "\"}");
        first = false;
    }
    n += wsprintfA(j + n, "],\"items\":[");
    first = true;
    for (int id = 1; id < kItemNameCount && n < 110000; ++id) {
        if (!kItemNames[id] || !kItemNames[id][0]) continue;
        n += wsprintfA(j + n, "%s{\"id\":%d,\"name\":\"", first ? "" : ",", id);
        n += JsonText(j + n, kItemNames[id]);
        n += wsprintfA(j + n, "\",\"max\":%d}", id <= 66 ? (int)kItemMax[id] : -1);  // weapons: how many the game lets York own (0 = none)
        first = false;
    }
    n += wsprintfA(j + n, "],\"cars\":[");
    for (int c = 1; c <= 16; ++c) {
        char name[64];
        const int k = WideCharToMultiByte(CP_UTF8, 0, CarNameOf(c), -1, name, 60, nullptr, nullptr);
        name[k > 0 ? k - 1 : 0] = 0;
        n += wsprintfA(j + n, "%s{\"id\":%d,\"name\":\"%s\"}", c > 1 ? "," : "", c, name);
    }
    n += wsprintfA(j + n, "]}");
    Reply(s, "200 OK", "application/json; charset=utf-8", j, n);
    VirtualFree(j, 0, MEM_RELEASE);
}

void Command(SOCKET s, const char* url) {
    const int slot = Query(url, "slot", 0), v = Query(url, "v", 0);
    if (QueryIs(url, "do", "run")) MissionRequest(2, slot);
    else if (QueryIs(url, "do", "restart")) MissionRequest(4, slot);
    else if (QueryIs(url, "do", "stop")) MissionRequest(3, slot);
    else if (QueryIs(url, "do", "tele")) {
        const float to[4] = {(float)Query(url, "x", 0) / 1000.f, (float)Query(url, "y", 0) / 1000.f, (float)Query(url, "z", 0) / 1000.f, (float)Query(url, "yaw", 0) / 1000.f};
        CheatYorkTo(to);
    } else if (QueryIs(url, "do", "hour") && v >= 0 && v <= 23) CheatSetHour(v);
    else if (QueryIs(url, "do", "weather") && v >= 1 && v <= 4) CheatWeather(v);
    else if (QueryIs(url, "do", "car") && v >= 1 && v <= 16) CheatSpawnCar(CarNumberOf(v));
    else if (QueryIs(url, "do", "item") && v >= 1 && v <= 0x16D) CheatGiveItem(v, Query(url, "n", 1) < 1 ? 1 : Query(url, "n", 1));
    else { ReplyText(s, "400 Bad Request", "unknown command"); return; }
    Log("editor: command %s", url);
    ReplyText(s, "200 OK", "ok");
}

void Handle(SOCKET s) {
    char* buf = (char*)VirtualAlloc(nullptr, kMaxBody + 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) return;
    int n = 0, headEnd = -1, bodyLen = 0;
    DWORD ms = 3000;
    g_net.setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof ms);
    while (n < kMaxBody + 8000) {
        const int got = g_net.recv(s, buf + n, kMaxBody + 8000 - n, 0);
        if (got <= 0) break;
        n += got;
        buf[n] = 0;
        if (headEnd < 0) {
            headEnd = Find(buf, n, "\r\n\r\n");
            if (headEnd >= 0) {
                headEnd += 4;
                const int cl = Find(buf, headEnd, "\r\nContent-Length:");
                if (cl >= 0) {
                    const char* p = buf + cl + 17;
                    while (*p == ' ') ++p;
                    for (; *p >= '0' && *p <= '9' && bodyLen <= kMaxBody; ++p) bodyLen = bodyLen * 10 + (*p - '0');
                }
            }
        }
        if (headEnd >= 0 && n - headEnd >= bodyLen) break;
    }
    if (headEnd < 0 || bodyLen > kMaxBody || n - headEnd < bodyLen) {
        if (n > 0) ReplyText(s, "400 Bad Request", "bad request");
        VirtualFree(buf, 0, MEM_RELEASE);
        return;
    }
    // "GET /path HTTP/1.1"
    const bool post = Find(buf, 5, "POST ") == 0, get = Find(buf, 4, "GET ") == 0;
    char url[400];
    int u = 0;
    for (const char* p = buf + (post ? 5 : 4); *p && *p != ' ' && *p != '\r' && u < 398; ++p) url[u++] = *p;
    url[u] = 0;
    // another site's page asking through the player's browser: refused
    const int origin = Find(buf, headEnd, "\r\nOrigin:");
    if (origin >= 0) {
        int e = origin + 2;
        while (buf[e] && buf[e] != '\r') ++e;
        const int len = e - origin;
        if (Find(buf + origin, len, "//127.0.0.1:") < 0 && Find(buf + origin, len, "//localhost:") < 0) {
            ReplyText(s, "403 Forbidden", "another origin");
            VirtualFree(buf, 0, MEM_RELEASE);
            return;
        }
    }
    wchar_t path[MAX_PATH + 60];
    if (get && (lstrcmpA(url, "/") == 0 || Find(url, 2, "/?") == 0)) {
        lstrcpyW(path, g_paths.dataDir);
        lstrcatW(path, L"editor\\index.html");
        int size = 0;
        char* file = ReadWhole(path, &size);
        if (file) {
            Reply(s, "200 OK", "text/html; charset=utf-8", file, size);
            VirtualFree(file, 0, MEM_RELEASE);
        } else {
            ReplyText(s, "404 Not Found", "UltimateFix\\editor\\index.html is missing: copy the editor folder from the mod's archive.");
        }
    } else if (get && Find(url, 10, "/api/state") == 0) {
        State(s);
    } else if (get && Find(url, 10, "/api/lists") == 0) {
        Lists(s);
    } else if (get && Find(url, 12, "/api/maptile") == 0) {
        MapTile(s, Query(url, "n", -1));
    } else if (Find(url, 12, "/api/mission") == 0) {
        int slot = Query(url, "slot", 1);
        if (slot < 1 || slot > 9) slot = 1;
        MissionPath(slot, path);
        if (get) {
            int size = 0;
            char* file = ReadWhole(path, &size);
            if (file) {
                Reply(s, "200 OK", "application/octet-stream", file, size);
                VirtualFree(file, 0, MEM_RELEASE);
            } else {
                ReplyText(s, "404 Not Found", "empty slot");
            }
        } else if (post) {
            HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD put = 0;
            if (f != INVALID_HANDLE_VALUE) {
                WriteFile(f, buf + headEnd, (DWORD)bodyLen, &put, nullptr);
                CloseHandle(f);
            }
            Log("editor: quest %d saved from the editor (%u bytes)", slot, (u32)put);
            ReplyText(s, (int)put == bodyLen ? "200 OK" : "500 Error", (int)put == bodyLen ? "saved" : "could not write the file");
        } else {
            ReplyText(s, "405 Method Not Allowed", "GET or POST");
        }
    } else if (post && Find(url, 8, "/api/cmd") == 0) {
        Command(s, url);
    } else {
        ReplyText(s, "404 Not Found", "not found");
    }
    VirtualFree(buf, 0, MEM_RELEASE);
}

DWORD WINAPI ServerThread(LPVOID) {
    for (;;) {
        SOCKET c = g_net.accept(g_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET) {
            Sleep(50);
            continue;
        }
        Handle(c);
        g_net.shutdown(c, SD_SEND);
        g_net.close(c);
    }
}

}  // namespace

int EditorServerPort() { return g_port; }

bool EditorServerStart() {
    if (g_port) return true;
    WSADATA wsa;
    if (!LoadNet() || g_net.startup(MAKEWORD(2, 2), &wsa) != 0) {
        Log("editor: Winsock did not start");
        return false;
    }
    SOCKET s = g_net.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = 0x0100007F;  // 127.0.0.1 in network order: this computer only
    int port = 0;
    for (int p = 47800; p < 47810 && !port; ++p) {
        a.sin_port = Swap16(p);
        if (g_net.bind(s, (const sockaddr*)&a, sizeof a) == 0) port = p;
    }
    if (!port || g_net.listen(s, 8) != 0) {
        Log("editor: no free port in 47800..47809");
        g_net.close(s);
        return false;
    }
    g_listen = s;
    HANDLE t = CreateThread(nullptr, 0, ServerThread, nullptr, 0, nullptr);
    if (!t) {
        g_net.close(s);
        return false;
    }
    CloseHandle(t);
    g_port = port;
    Log("editor: the quest editor's page is served at http://127.0.0.1:%d/ (this computer only)", port);
    return true;
}

void EditorOpenBrowser() {
    if (!EditorServerStart()) {
        ShowNotice(L"Редактор не відкрився", L"The editor did not open", L"Мод не зміг відкрити локальний порт 47800..47809. Деталі в ufx.log.",
                   L"The mod could not open a local port in 47800..47809. See ufx.log.", 8, kNoticeWarning);
        return;
    }
    wchar_t url[64];
    wsprintfW(url, L"http://127.0.0.1:%d/", g_port);
    typedef HINSTANCE(WINAPI * ShellFn)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
    HMODULE shell = LoadLibraryW(L"shell32.dll");
    ShellFn open = shell ? (ShellFn)(void*)GetProcAddress(shell, "ShellExecuteW") : nullptr;
    const INT_PTR r = open ? (INT_PTR)open(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL) : 0;
    Log("editor: the browser was asked to open the page (ShellExecute answered %d)", (int)r);
    ShowNotice(L"Редактор квестів у браузері", L"The quest editor is in the browser", L"Згорніть гру (Alt+Tab): сторінка редактора відкрилась у вашому браузері.",
               L"Switch away from the game (Alt+Tab): the editor's page has opened in your browser.", 8);
}

}  // namespace ufx
