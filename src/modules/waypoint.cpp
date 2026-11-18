// A waypoint, drawn the way the game draws its own "%d Yards" label (FUN_0043C200): the world point is projected with the game's
// view-projection matrix (01481238h) to the HUD space (width 01480974h, height 01480970h), and the text goes through the game's text
// function FUN_0045C540 (CMessage singleton at 00BDA000) in the game's font, centred, with the game's colours. So it looks like the game.
//
//   F7 (or the DualSense touchpad): on the pause map = waypoint at the map centre (again near it = remove); in the world = waypoint at
//   the player (or the free camera). Shift+F7 removes it.
//   In the world: "123 Yards" floats over the place; off the screen it sits on the edge with an arrow (< >), behind the camera at the side.
//   The label is drawn from the entry of the game's text function, at most once per frame and only on frames where the game draws
//   some text itself; like the game's own label, not in cutscenes.
//   On the pause map: a red X on the place (or on the edge), a + in the middle of the map where F7 puts it. The X needs the map
//   fix with a fixed orientation (north or south up): on the map that turns with the player only the + is drawn.
//
// The pause map (screen manager object, ECX at the call 0x6541FC of its draw function 00659B40) keeps its centre at +8ED0h (x) and
// +8ED8h (z), the zoom at +8EE0h, in world units; a tile is 10000 units and is drawn  zoom * 1024  pixels wide, north up.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"
#include "../ui/ui.h"

extern "C" {
volatile unsigned g_wpMapObj = 0;    // the screen manager object while the map is drawn
volatile unsigned g_wpMapDraws = 0;  // counts the draws of the map
volatile float g_wpPos[3] = {0.f, 0.f, 0.f};  // the waypoint, for the map point accessor stub
extern volatile int g_mapOrient;
extern volatile int g_mapFixInstalled;
void __cdecl WaypointMapOverlay();
void __cdecl WaypointSpotUpkeep();
}

namespace ufx {
bool WorldActive();
u32 PlayerObjectNow();
bool DsTouchpadDown();

namespace {

constexpr u32 kViewProjVa = 0x01481238, kHudWVa = 0x01480974, kHudHVa = 0x01480970, kMessageVa = 0x00BDA000;
constexpr u32 kTextPrintFn = 0x0045C540;
// The game's own factor for its "Yards" numbers: 0.1 (0076F6F8h) times 1.0936 (007778B0h), in the minimap (005F7C4Ch) and in the
// objective beacon (005B132Bh). So the number matches the game's chevron counts.
constexpr float kYardsPerUnit = 0.1f * 1.0936f;
// The game's map point list (see kSiteSpotAccessor): the waypoint is one more entry, id FFFFh (no game point has it), type 10 (an
// objective: the minimap draws its red chevrons on the edge towards it, the pause map its icon), floor 0 (what the rebuild writes).
constexpr u32 kSpotCountVa = 0x0143C078 + 0x2E838, kSpotListVa = 0x0143C078 + 0x2E83C;
constexpr u32 kSpotEntry = 0xFFFFu | (10u << 16);
// The minimap copies the whole list into a 256-entry stack array (005F7A91..005F7ABD, right below its /GS cookie) and the pause
// map into one of 257 (0065D490), neither checks the count: append only while the list is well below 256.
constexpr int kSpotGuard = 0xF0;
// CGame (pointer at 00BDA004): +834FE0h the area the player is in (0 = the town, the only area with the minimap), +834FF4h the
// player's floor (written only by FUN_005026B0; the pause map shows an indoor point only on its floor).
constexpr u32 kAreaOff = 0x834FE0, kFloorOff = 0x834FF4;

typedef void(__cdecl* TextPrintFn)(void* message, float x, float y, int flags, const float* rgba, const char* format, ...);

bool g_installed = false;
bool g_spotInstalled = false;
volatile bool g_active = false;
volatile float g_x = 0.f, g_y = 0.f, g_z = 0.f;
volatile float g_lastPx = 0.f, g_lastPy = 0.f, g_lastPz = 0.f;  // the player, for the map (the player is not updated in the pause)
volatile bool g_haveLast = false;
volatile int g_area = 0, g_floor = 0;  // where the waypoint was set: it is a point of that area only
volatile unsigned g_frame = 0, g_drawnFrame = 0;
volatile bool g_showWorld = false;
void* volatile g_dev = nullptr;        // the D3D9 device (from the Present hook)
int g_offscreenFrames = 0;            // frames in a row where every 2D call went to another target
bool g_targetCheck = true;            // only draw into the back buffer (the HUD may also draw into textures)
volatile bool g_inWorker = false;

float Sqrt(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
float F(u32 base, u32 off) { return *(volatile float*)(uintptr_t)(base + off); }

void Print(float x, float y, const float* rgba, const char* format, int value) {
    void* msg = *(void**)(uintptr_t)kMessageVa;
    if (!msg) return;
    g_inWorker = true;
    ((TextPrintFn)(uintptr_t)kTextPrintFn)(msg, x, y, 2 /*centred*/, rgba, format, value);
    g_inWorker = false;
}

const float kRed[4] = {1.f, 0.15f, 0.1f, 1.f};
const float kGreen[4] = {0.35f, 1.f, 0.35f, 1.f};
const float kWhite[4] = {1.f, 1.f, 1.f, 1.f};

// like the game (005F7C4Ch): the 3D distance times 0.1 * 1.0936, the fraction dropped
int YardsTo(float x, float y, float z) {
    float dx = g_x - x, dy = g_y - y, dz = g_z - z;
    return (int)(Sqrt(dx * dx + dy * dy + dz * dz) * kYardsPerUnit);
}

u32 GameManager() {
    u32 g = *(volatile u32*)(uintptr_t)kGameManagerPtrVa;
    return g >= 0x10000 && g < 0x7FFE0000u ? g : 0;
}

void RememberArea() {
    u32 g = GameManager();
    g_area = g ? *(volatile int*)(uintptr_t)(g + kAreaOff) : 0;
    g_floor = g ? (int)(signed char)*(volatile int*)(uintptr_t)(g + kFloorOff) : 0;
}

// True when the game is drawing into the back buffer right now (not into one of its textures, such as a map tile or a reflection).
bool DrawingToScreen() {
    void* dev = g_dev;
    if (!dev || !g_targetCheck) return true;
    typedef HRESULT(WINAPI * GetRtFn)(void*, DWORD, void**);
    typedef HRESULT(WINAPI * GetBbFn)(void*, UINT, UINT, UINT, void**);
    typedef ULONG(WINAPI * ReleaseFn)(void*);
    void** vt = *(void***)dev;
    void *rt = nullptr, *bb = nullptr;
    ((GetRtFn)vt[38])(dev, 0, &rt);
    ((GetBbFn)vt[18])(dev, 0, 0, 0, &bb);
    bool same = rt && rt == bb;
    if (rt) ((ReleaseFn)(*(void***)rt)[2])(rt);
    if (bb) ((ReleaseFn)(*(void***)bb)[2])(bb);
    return same;
}

// In the world: the label over the waypoint (game font), on the edge when it is off the screen. Runs at the start of the game's 2D
// sprite and text draws (the HUD draws sprites every frame), at most once per frame, and only while drawing into the back buffer.
void __cdecl TextDrawWorker(void* self, u32 arg) {
    (void)self;
    (void)arg;
    if (g_inWorker || !g_active || !g_showWorld || g_drawnFrame == g_frame) return;
    if (!DrawingToScreen()) return;
    // not in cutscenes: the player update (and so WorldActive) keeps running there, and the game's own label (FUN_0043C200)
    // is drawn only while the event mode is not negative
    if (*(volatile int*)(uintptr_t)kEventModeVa < 0) return;
    // only while the 2D area is the whole screen: FUN_0044AD00 sets it (x 0148097Ch, y 01480978h, width, height) to the minimap's
    // rectangle while the minimap is drawn (sprites go through 00459520 there too) and FUN_005F8430 puts it back. Projected into the
    // minimap's rectangle the label slid around with the minimap. Checked before the frame is marked as drawn, so a later call
    // in the same frame still draws it.
    if (F(0x0148097C, 0) != 0.f || F(0x01480978, 0) != 0.f || F(kHudWVa, 0) < 1000.f || F(kHudHVa, 0) < 560.f) return;
    g_drawnFrame = g_frame;
    u32 player = PlayerObjectNow();
    if (!player) return;
    const float W = F(kHudWVa, 0), H = F(kHudHVa, 0);
    if (W < 100.f || H < 100.f) return;
    const float* m = (const float*)(uintptr_t)kViewProjVa;
    float in[4] = {g_x, g_y + 15.f, g_z, 1.f};  // a little above the ground, like the game's label over a person
    float c[4];
    for (int j = 0; j < 4; ++j) c[j] = in[0] * m[j] + in[1] * m[4 + j] + in[2] * m[8 + j] + in[3] * m[12 + j];
    int yards = YardsTo(F(player, 0x58), F(player, 0x5C), F(player, 0x60));
    const float* colour = yards < 15 ? kGreen : kRed;
    const float margin = 60.f;
    if (c[3] > 0.01f) {
        float sx = (c[0] / c[3]) * W * 0.5f + W * 0.5f, sy = -(c[1] / c[3]) * H * 0.5f + H * 0.5f;
        const char* format = "%d Yards";
        if (sx < margin) { sx = margin; format = "< %d Yards"; }
        else if (sx > W - margin) { sx = W - margin; format = "%d Yards >"; }
        if (sy < 40.f) sy = 40.f;
        else if (sy > H - 70.f) sy = H - 70.f;
        Print(sx, sy, colour, format, yards);
    } else {  // behind the camera: at the side it is on
        float ex = F(kRenderCameraVa, 0x04), ez = F(kRenderCameraVa, 0x0C), tx = F(kRenderCameraVa, 0x14), tz = F(kRenderCameraVa, 0x1C);
        float fx = tx - ex, fz = tz - ez, dx = g_x - ex, dz = g_z - ez;
        float right = dz * fx - dx * fz;  // screen right is (-fz, fx) in this right-handed world (as the free camera shows)
        if (right >= 0.f) Print(W - margin, H * 0.5f, colour, "%d Yards >", yards);
        else Print(margin, H * 0.5f, colour, "< %d Yards", yards);
    }
}

}  // namespace

// Replaces `call 00659B40` (the pause map draw, ECX = the object): the map is drawn, then the waypoint over it, in the same pass.
__attribute__((naked)) void WaypointMapDrawStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov dword ptr [_g_wpMapObj], ecx\n"
        "inc dword ptr [_g_wpMapDraws]\n"
        "mov eax, 0x00659B40\n"
        "call eax\n"
        "pushfd\n"
        "pushad\n"
        "call _WaypointMapOverlay\n"
        "popad\n"
        "popfd\n"
        "ret\n"
        ".att_syntax\n");
}

// Replaces the first 10 bytes of the map point accessor FUN_005E71B0 (thiscall: [esp+4] float* out, [esp+8] index, ret 8). For
// the waypoint's entry (id FFFFh) it writes the waypoint position and returns type 10; any other entry goes on to the game's code
// at 005E71BA (push esi / jge), with the compare redone just before so its flags are the ones the jge reads.
__attribute__((naked)) void WaypointSpotAccessorStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov eax, dword ptr [esp+8]\n"
        "cmp eax, dword ptr [ecx+0x2E838]\n"
        "jge ufx_spot_game\n"
        "cmp word ptr [ecx+eax*4+0x2E83C], 0xFFFF\n"
        "jne ufx_spot_game\n"
        "mov edx, dword ptr [esp+4]\n"
        "mov eax, dword ptr [_g_wpPos]\n"
        "mov dword ptr [edx], eax\n"
        "mov eax, dword ptr [_g_wpPos+4]\n"
        "mov dword ptr [edx+4], eax\n"
        "mov eax, dword ptr [_g_wpPos+8]\n"
        "mov dword ptr [edx+8], eax\n"
        "mov dword ptr [edx+12], 0\n"
        "mov eax, 10\n"
        "ret 8\n"
        "ufx_spot_game:\n"
        "mov eax, dword ptr [esp+8]\n"
        "cmp eax, dword ptr [ecx+0x2E838]\n"
        "push 0x005E71BA\n"  // push and ret leave the flags alone
        "ret\n"
        ".att_syntax\n");
}

// Replaces a `call 005EFF20` (the map point list rebuild, ECX = the manager, plain ret): the list is rebuilt, then the waypoint
// goes back into it at once, so the pause map, which rebuilds it when it opens, has it from its first frame.
__attribute__((naked)) void WaypointSpotRebuildStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov eax, 0x005EFF20\n"
        "call eax\n"
        "pushfd\n"
        "pushad\n"
        "call _WaypointSpotUpkeep\n"
        "popad\n"
        "popfd\n"
        "ret\n"
        ".att_syntax\n");
}

// The minimap chevrons and the pause map icon: the accessor hook first (an entry must never be read by the game's own code), then
// the rebuild calls. Without the accessor nothing is ever added to the list.
void InstallWaypointSpot() {
    const SiteDef& a = kSiteSpotAccessor;
    if (!BytesMatch(a.va, a.bytes, a.len)) {
        Log("waypoint: the map point accessor differs from DP.exe 1.01b, no minimap arrows (the label still works)");
        return;
    }
    if (!PatchJump(a.va, a.bytes, a.len, (const void*)WaypointSpotAccessorStub)) return;
    g_spotInstalled = true;
    int wrapped = 0;
    for (const SiteDef& r : kSiteSpotRebuild)
        if (PatchCall(r.va, r.bytes, r.len, (const void*)WaypointSpotRebuildStub)) ++wrapped;
    Log("waypoint: minimap arrows ready (the waypoint is an objective point of the game's map, %d of 3 rebuild calls wrapped)", wrapped);
}

bool InstallWaypoint() {
    const SiteDef& s = kSiteMapDrawCall;
    const SiteDef& t = kSiteTextDrawEntry;
    if (!BytesMatch(s.va, s.bytes, s.len) || !BytesMatch(t.va, t.bytes, t.len)) {
        Log("waypoint: the map draw call or the text function differs from DP.exe 1.01b, skipped");
        return false;
    }
    u8* hook = AllocStub(64);
    if (!hook) return false;
    BuildEntryHook(hook, (u32)(uintptr_t)hook, (u32)(uintptr_t)TextDrawWorker, t.bytes, t.len, t.va + t.len);
    if (!PatchJump(t.va, t.bytes, t.len, hook)) return false;
    const SiteDef& p = kSiteSpriteDrawEntry;  // the HUD draws sprites every frame: the label no longer needs a text draw in the frame
    if (BytesMatch(p.va, p.bytes, p.len)) {
        u8* hook2 = AllocStub(64);
        if (hook2) {
            BuildEntryHook(hook2, (u32)(uintptr_t)hook2, (u32)(uintptr_t)TextDrawWorker, p.bytes, p.len, p.va + p.len);
            if (!PatchJump(p.va, p.bytes, p.len, hook2)) Log("waypoint: could not hook the sprite draw, the label follows text draws only");
        }
    } else {
        Log("waypoint: the sprite draw at %08X differs from DP.exe 1.01b, the label follows text draws only", p.va);
    }
    if (!PatchCall(s.va, s.bytes, s.len, (const void*)WaypointMapDrawStub)) return false;
    g_installed = true;
    if (g_cfg.waypointMinimap) InstallWaypointSpot();
    Log("waypoint: ready (F7 sets a waypoint on the map or in the world, Shift+F7 removes it; drawn in the game's font)");
    return true;
}

// Once per frame from the Present hook: the keys, the frame counter and the state the drawing (in the game's passes) needs.
void WaypointFrame(void* dev) {
    WaypointSpotUpkeep();  // between frames: the game is not walking the list now (same thread)
    if (!g_installed || !g_cfg.waypoint) { g_showWorld = false; return; }
    g_dev = dev;
    if (g_active && g_showWorld && g_drawnFrame != g_frame) {  // the last frame had no 2D call into the back buffer
        if (g_targetCheck && ++g_offscreenFrames > 180) {
            g_targetCheck = false;  // the HUD never draws into the back buffer here: draw on its first 2D call instead
            Log("waypoint: the HUD draws into another target, the label now follows the first 2D call of each frame");
        }
    } else {
        g_offscreenFrames = 0;
    }
    ++g_frame;
    static unsigned lastDraws = 0;
    static int sinceDraw = 100;
    static bool prevKey = false, prevPad = false;
    if (g_wpMapDraws != lastDraws) { lastDraws = g_wpMapDraws; sinceDraw = 0; } else if (sinceDraw < 100) ++sinceDraw;
    const bool mapOpen = sinceDraw < 4 && g_wpMapObj;
    const bool inWorld = WorldActive() && !mapOpen && !ui::IsOpen();
    g_showWorld = inWorld;
    u32 player = PlayerObjectNow();
    if (player) {
        g_lastPx = F(player, 0x58);
        g_lastPy = F(player, 0x5C);
        g_lastPz = F(player, 0x60);
        g_haveLast = true;
    }

    HWND w = GameWindowHandle();
    const bool front = w && GetForegroundWindow() == w;
    const bool key = front && g_cfg.waypointKey && (GetAsyncKeyState(g_cfg.waypointKey) & 0x8000) != 0;
    const bool pad = DsTouchpadDown();
    const bool shift = front && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool press = (key && !prevKey) || (pad && !prevPad);
    prevKey = key;
    prevPad = pad;
    if (!press || ui::IsOpen()) return;
    if (shift) {
        g_active = false;
        Log("waypoint: removed");
    } else if (mapOpen) {
        float cx = F(g_wpMapObj, 0x8ED0), cz = F(g_wpMapObj, 0x8ED8);
        float dx = g_x - cx, dz = g_z - cz;
        if (g_active && dx * dx + dz * dz < 400.f * 400.f) {
            g_active = false;
            Log("waypoint: removed (map)");
        } else {
            g_x = cx;
            g_z = cz;
            g_y = g_haveLast ? g_lastPy : 0.f;
            RememberArea();
            g_active = true;
            Log("waypoint: set on the map at %d %d (area %d, floor %d)", (int)g_x, (int)g_z, g_area, g_floor);
        }
    } else if (inWorld && player) {
        if (g_cfg.photoActive) {
            g_x = F(kRenderCameraVa, 0x04);
            g_y = F(kRenderCameraVa, 0x08);
            g_z = F(kRenderCameraVa, 0x0C);
        } else {
            g_x = F(player, 0x58);
            g_y = F(player, 0x5C);
            g_z = F(player, 0x60);
        }
        RememberArea();
        g_active = true;
        Log("waypoint: set here at %d %d (area %d, floor %d)", (int)g_x, (int)g_z, g_area, g_floor);
    }
}

}  // namespace ufx

// Keeps the waypoint's entry in the game's map point list: one entry while a waypoint is set, none otherwise. Runs once per frame
// from the Present hook (also in the pause, where the player update stops) and right after every rebuild of the list.
extern "C" void __cdecl WaypointSpotUpkeep() {
    using namespace ufx;
    if (!g_spotInstalled) return;
    g_wpPos[0] = g_x;
    g_wpPos[1] = g_y;
    g_wpPos[2] = g_z;
    volatile int* count = (volatile int*)(uintptr_t)kSpotCountVa;
    volatile u32* list = (volatile u32*)(uintptr_t)kSpotListVa;
    const int n = *count;
    if (n < 0 || n > 0x1000) return;  // not a list (yet)
    int found = -1;
    for (int i = 0; i < n; ++i)
        if ((list[i] & 0xFFFFu) == 0xFFFFu) { found = i; break; }
    // g_haveLast: a game world has been played (the list belongs to a stage)
    u32 g = GameManager();
    const bool sameArea = g && *(volatile int*)(uintptr_t)(g + kAreaOff) == g_area;
    const bool want = g_active && g_cfg.waypoint && g_cfg.waypointMinimap && g_haveLast && sameArea;
    if (want && found < 0 && n < kSpotGuard) {
        list[n] = kSpotEntry | ((u32)(u8)g_floor << 24);
        *count = n + 1;
    } else if (!want && found >= 0) {
        for (int i = found; i + 1 < n; ++i) list[i] = list[i + 1];
        *count = n - 1;
    }
}

// After the pause map is drawn (same pass, game font): a + in the middle (where F7 puts the waypoint) and an X on the waypoint.
extern "C" void __cdecl WaypointMapOverlay() {
    using namespace ufx;
    if (!g_installed || !g_cfg.waypoint || !g_wpMapObj) return;
    const float W = F(kHudWVa, 0), H = F(kHudHVa, 0);
    if (W < 100.f || H < 100.f) return;
    // the map is centred on its own frame, not on the screen: the middle of points 0 and 1 of its layout (object + 19D0h, a table
    // of 16-byte points at +88h, count at +84h), exactly as the map draw computes it before placing the tiles
    float cx = W * 0.5f, cy = H * 0.5f;
    {
        u32 layout = g_wpMapObj + 0x19D0;
        int count = *(volatile int*)(uintptr_t)(layout + 0x84);
        u32 table = *(volatile u32*)(uintptr_t)(layout + 0x88);
        if (count >= 2 && table >= 0x10000 && table < 0x7FFE0000u) {
            cx = (F(table, 0x04) + F(table, 0x14)) * 0.5f;
            cy = (F(table, 0x08) + F(table, 0x18)) * 0.5f;
        }
    }
    Print(cx, cy - 8.f, kWhite, "+", 0);
    // the X is placed north up (orientation 1) or south up (2). Without the map fix the stock map turns with the player, and
    // orientation 0 keeps that; the turn is not reproduced here, so no X rather than one in the wrong place
    if (!g_active || !g_mapFixInstalled || (g_mapOrient != 1 && g_mapOrient != 2)) return;
    const float zoom = F(g_wpMapObj, 0x8EE0);
    if (zoom <= 0.f) return;
    const float sign = g_mapOrient == 2 ? -1.f : 1.f;
    const float k = zoom * 1024.f / 10000.f * sign;
    float sx = cx + (g_x - F(g_wpMapObj, 0x8ED0)) * k, sy = cy + (g_z - F(g_wpMapObj, 0x8ED8)) * k;
    int yards = g_haveLast ? YardsTo(g_lastPx, g_lastPy, g_lastPz) : 0;
    const float margin = 50.f;
    if (sx >= margin && sx <= W - margin && sy >= margin && sy <= H - margin) {
        Print(sx, sy - 8.f, kRed, "X", 0);
        if (g_haveLast) Print(sx, sy + 12.f, kRed, "%d Yards", yards);
    } else {
        const char* format = "%d Yards";
        if (sx < margin) { sx = margin; format = "< %d Yards"; }
        else if (sx > W - margin) { sx = W - margin; format = "%d Yards >"; }
        if (sy < margin) { sy = margin; if (format[0] == '%' && format[8] == 0) format = "^ %d Yards"; }
        else if (sy > H - margin) { sy = H - margin; if (format[0] == '%' && format[8] == 0) format = "v %d Yards"; }
        Print(sx, sy, kRed, format, yards);
    }
}
