// A waypoint, drawn the way the game draws its own "%d Yards" label (FUN_0043C200): the world point is projected with the game's
// view-projection matrix (01481238h) to the HUD space (width 01480974h, height 01480970h), and the text goes through the game's text
// function FUN_0045C540 (CMessage singleton at 00BDA000) in the game's font, centred, with the game's colours. So it looks like the game.
//
//   F7 (or the DualSense touchpad): on the pause map = waypoint at the map centre (again near it = remove); in the world = waypoint at
//   the player (or the free camera). Shift+F7 removes it.
//   In the world: "123 Yards" floats over the place; off the screen it sits on the edge with an arrow (< >), behind the camera at the side.
//   The label is drawn from the entry of the game's text function, at most once per frame and only on frames where the game draws
//   some text itself; like the game's own label, not in cutscenes.
//   While the place is on the screen, the game's own objective beacon stands over it instead of the bare label: the red arrows of
//   GOAL.XPC with "%d Yards" under them, drawn by the game's FUN_005B1120 from the pass of its own target icons (see
//   WaypointBeaconDraw). The label stays for a place off the screen or behind the camera and while the game does not hold the picture.
//   Ctrl+F7 (the objective key): the waypoint goes to the game's current objective, a point of type 8, 9 or 10 of its map point
//   list, so the distance and the edge arrow work for story goals; again = the next objective when there are several.
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
void __cdecl WaypointBeaconDraw();
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
constexpr u32 kSpotManagerVa = 0x0143C078;
constexpr u32 kSpotCountVa = 0x0143C078 + 0x2E838, kSpotListVa = 0x0143C078 + 0x2E83C;
constexpr u32 kSpotEntry = 0xFFFFu | (10u << 16);
// The minimap copies the whole list into a 256-entry stack array (005F7A91..005F7ABD, right below its /GS cookie) and the pause
// map into one of 257 (0065D490), neither checks the count: append only while the list is well below 256.
constexpr int kSpotGuard = 0xF0;
// CGame (pointer at 00BDA004): +834FE0h the area the player is in (0 = the town, the only area with the minimap), +834FF4h the
// player's floor (written only by FUN_005026B0; the pause map shows an indoor point only on its floor).
constexpr u32 kAreaOff = 0x834FE0, kFloorOff = 0x834FF4;

// The navigation table (0x1437A20): the byte at +4 says that it is loaded (FUN_005E1ED0 writes no position otherwise, 005E1ED0),
// the node count is at +8.
constexpr u32 kNavLoadedVa = 0x01437A24, kNavCountVa = 0x01437A28;
// The resource manager (pointer at 00BD9E3Ch, what the getter FUN_004051F0 returns), the event manager CEvent (00BD9E58h, getter
// FUN_00405360) and the 2D renderer FUN_005B1120 draws its quads with (ECX of FUN_006E3360, 005B1470).
constexpr u32 kResourceMgrVa = 0x00BD9E3C, kEventPtrVa = 0x00BD9E58, kQuadRendererVa = 0x00BD7680;
constexpr u32 kGoalPictureId = 0x32;       // UPDATA/BG/00/00/GOAL.XPC in the resource table
constexpr u32 kGameFlagsOff = 0x8C5EC;      // CGame: bit 10000000h = the HUD is off, bit 2 = the mode with the bigger beacons
constexpr u32 kTargetVtable = 0x0077D854;   // CObjectTarget, written by its constructor at 005F438E
// A point this close to the left or right edge of the screen counts as off the screen: the label sits on the edge there.
constexpr float kEdgeMargin = 60.f;

typedef void(__cdecl* TextPrintFn)(void* message, float x, float y, int flags, const float* rgba, const char* format, ...);
typedef void(__attribute__((thiscall)) * IconMoveFn)(void* icon, const float* position);
typedef void(__attribute__((thiscall)) * IconDrawFn)(void* icon, float size);
typedef int(__attribute__((thiscall)) * SpotPosFn)(void* manager, float* out, int index);

// The game's CObjectTargetIcon as its constructor FUN_0042B010 fills it. The mod keeps one of its own: it is never handed to the
// game's allocator, its vtable is never called by the two functions used here, and it never goes through FUN_005B1050, which may
// load GOAL.XPC synchronously.
struct BeaconIcon {
    u32 vtable;      // +00h  00772124h
    u32 texture;     // +04h  the texture record, 0 = draw nothing (the first test of FUN_005B1120)
    int type;        // +08h  0 = the red arrows with "%d Yards" under them (what a route objective gets, 005B1663)
    float pos[4];    // +0Ch  the place in the world, w = 1 (written by FUN_005B0DD0)
    float rgba[4];   // +1Ch
    float bob;       // +2Ch  the bobbing in pixels, between -16 and 0
    float bobSpeed;  // +30h  -1 at first, FUN_005B0DD0 flips the sign at both ends
    u8 ownColour;    // +34h  1 = rgba, 0 = grey
    u8 unused35;     // +35h  0
    u8 pad[2];
};
static_assert(sizeof(BeaconIcon) == 0x38 && offsetof(BeaconIcon, pos) == 0x0C && offsetof(BeaconIcon, rgba) == 0x1C &&
              offsetof(BeaconIcon, bob) == 0x2C && offsetof(BeaconIcon, ownColour) == 0x34, "CObjectTargetIcon layout");
BeaconIcon g_icon = {0x00772124, 0, 0, {0.f, 0.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}, 0.f, -1.f, 1, 0, {0, 0}};

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
bool g_beaconInstalled = false;
bool g_accessorOk = false;             // the map point accessor is the one of 1.01b (the game's or our stub in front of it)
volatile int g_objNode = -1;           // the node of the objective the waypoint was put on (the objective key), -1 = a free waypoint
volatile bool g_ownBeacon = false;     // the game shows its own red beacon over that objective (TrackOwnBeacon)
volatile unsigned g_beaconPass = 0;    // the frame of the last WaypointBeaconDraw
volatile bool g_beaconPassSeen = false;

float Sqrt(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
float F(u32 base, u32 off) { return *(volatile float*)(uintptr_t)(base + off); }
u32 U(u32 va) { return *(volatile u32*)(uintptr_t)va; }
bool Ptr(u32 p) { return p >= 0x10000 && p < UserAddressEnd(); }

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
const float kBeaconRed[4] = {1.f, 0.f, 0.f, 1.f};  // what FUN_005B1050 gives the type 0 icon (005B10C8)

// like the game (005F7C4Ch): the 3D distance times 0.1 * 1.0936, the fraction dropped
int YardsTo(float x, float y, float z) {
    float dx = g_x - x, dy = g_y - y, dz = g_z - z;
    return (int)(Sqrt(dx * dx + dy * dy + dz * dz) * kYardsPerUnit);
}

u32 GameManager() {
    u32 g = U(kGameManagerPtrVa);
    return Ptr(g) ? g : 0;
}

void RememberArea() {
    u32 g = GameManager();
    g_area = g ? *(volatile int*)(uintptr_t)(g + kAreaOff) : 0;
    g_floor = g ? (int)(signed char)*(volatile int*)(uintptr_t)(g + kFloorOff) : 0;
}

// The game's own "free play" test FUN_0042D010 (with ECX = CEvent, [00BD9E58h]): no script cutscene (CEvent+44h bit 40000h,
// CEvent+34h a running P-demo) and no SCENE demo ([008A6074h] < 0). The last alone misses script cutscenes and videos.
bool GameIdle() {
    if (*(volatile int*)(uintptr_t)kEventModeVa < 0) return false;
    u32 ev = U(kEventPtrVa);
    if (!Ptr(ev)) return true;  // not created yet: no event can run
    if (*(volatile u32*)(uintptr_t)(ev + 0x44) & 0x40000u) return false;
    return *(volatile u32*)(uintptr_t)(ev + 0x34) == 0;
}

bool g_spriteHooked = false;

// The texture record of GOAL.XPC, fetched the way FUN_005B1050 does it (005B10D8..005B10F7), or 0 while the game does not hold the
// picture. FUN_006B2C70(manager, 32h, 0) gives the picture object (+1Ch of the 30h-byte entry; the table is at manager+0Ch, the
// number of entries at manager+4) only while the entry's reference count (u16 at +2Ch) is not 0, so a fresh look every frame
// never keeps a pointer to a freed picture. FUN_006B5660(picture, 0, 0) gives the first texture record (at [picture+8]) when the
// header [picture+4] has a row and a column (u16 at +0Ah, u8 at +0Ch) and the record has a Direct3D texture (+14h, +18h or +1Ch,
// FUN_006B60E0). Read here instead of called: the getter FUN_004051F0 would create the manager, and FUN_006B5660 reads through
// the header pointer without a check.
u32 GoalTexture() {
    u32 mgr = U(kResourceMgrVa);
    if (!Ptr(mgr) || *(volatile int*)(uintptr_t)(mgr + 4) <= (int)kGoalPictureId) return 0;
    u32 table = U(mgr + 0x0C);
    if (!Ptr(table)) return 0;
    u32 entry = table + kGoalPictureId * 0x30;
    if (*(volatile u16*)(uintptr_t)(entry + 0x2C) == 0) return 0;
    u32 picture = U(entry + 0x1C);
    if (!Ptr(picture)) return 0;
    u32 header = U(picture + 4), record = U(picture + 8);
    if (!Ptr(header) || !Ptr(record)) return 0;
    if (*(volatile u16*)(uintptr_t)(header + 0x0A) == 0 || *(volatile u8*)(uintptr_t)(header + 0x0C) == 0) return 0;
    if (!U(record + 0x14) && !U(record + 0x18) && !U(record + 0x1C)) return 0;
    return record;
}

// True when the game's beacon function would show an icon at the waypoint on the screen. FUN_005B1120 projects the place itself
// (D3DXVec4Transform with 01481238h, then FUN_006B6870: x / w and -y / w scaled to the HUD size) and draws nothing when the clip z
// is not above 0 (005B1186). The arrows stand 48 pixels above that point and reach about 140 pixels up (168 with the bigger
// size), the text is under them: outside this box the icon would hang half off the screen, and the label on the edge says more.
bool BeaconInView() {
    const float W = F(kHudWVa, 0), H = F(kHudHVa, 0);
    const float* m = (const float*)(uintptr_t)kViewProjVa;
    const float in[4] = {g_x, g_y, g_z, 1.f};
    float c[4];
    for (int j = 0; j < 4; ++j) c[j] = in[0] * m[j] + in[1] * m[4 + j] + in[2] * m[8 + j] + in[3] * m[12 + j];
    if (!(c[2] > 0.f) || !(c[3] > 0.01f)) return false;
    const float sx = (c[0] / c[3]) * W * 0.5f + W * 0.5f, sy = -(c[1] / c[3]) * H * 0.5f + H * 0.5f;
    return sx >= kEdgeMargin && sx <= W - kEdgeMargin && sy >= 160.f && sy <= H + 18.f;
}

// Everything FUN_005B1120 relies on without a check, and the game's own HUD gate (the HUD function FUN_005FFE80 at
// 005FFEAB..005FFED1: the HUD flag byte, the free play test that GameIdle repeats, bit 10000000h of CGame+8C5ECh):
//   - CGame, CEvent and the text object exist: the function fetches them through getters that would create them (0040A320 at
//     005B1130, 00405360 at 005B14A8, the allocation at 005B14F3);
//   - the player object exists: its position is read at 005B11AF right after the lookup, with no test of the result;
//   - GOAL.XPC is held by the game and has a texture.
bool BeaconReady(u32* texture) {
    if (!g_beaconInstalled || !g_cfg.waypointBeacon) return false;
    if (!*(volatile u8*)(uintptr_t)kHudFlagVa) return false;
    u32 g = GameManager();
    if (!g || (U(g + kGameFlagsOff) & 0x10000000u)) return false;
    if (!Ptr(U(kEventPtrVa)) || !Ptr(U(kMessageVa)) || !Ptr(U(kQuadRendererVa))) return false;
    u32 state;
    if (!PlayerStateNow(&state)) return false;  // the lookup FUN_005B1120 itself makes (FUN_006C5FD0 with the handle at 008A9BA4h)
    *texture = GoalTexture();
    return *texture != 0;
}

// True when arrows stand over the place on the screen in this frame, so the text label is left out: the game's own beacon of the
// objective the waypoint was put on, or the mod's. WaypointBeaconDraw makes the same tests in the same frame; it must have run
// in this frame or the one before, so the label comes back by itself if the game ever stops sending the pass. With the beacon
// switched off the label is always drawn.
bool BeaconStands() {
    if (!g_cfg.waypointBeacon || !BeaconInView()) return false;
    if (g_ownBeacon) return true;
    u32 texture;
    return g_beaconPassSeen && g_frame - g_beaconPass <= 1 && BeaconReady(&texture);
}

// The waypoint was put on one of the game's objectives. A route objective (map point type 8 or 9) is a CObjectTarget that shows
// its own red arrows with the yards (icon type 0 at object+460h, alpha at +480h, texture at +45Ch) while its "show" byte +3Bh
// is set: then the mod draws nothing over the place, or there would be two beacons in one spot. The byte is written only by
// the object's update (FUN_005B1CB0), which also moves the icon's bobbing (+484h) every frame through FUN_005B0DD0: a bobbing
// that stands still means the object is not being updated and the byte may be stale. Once per frame, from the Present hook.
void TrackOwnBeacon() {
    static u32 lastBob = 0;
    static int lastNode = -1, still = 100;
    bool shown = false;
    const int node = g_objNode;
    if (node != lastNode) {
        lastNode = node;
        still = 100;
    }
    const u32 g = GameManager();
    // the manager holds A000h object pointers of the stage in play, indexed by the node: only in the area the objective is in
    if (g_active && node >= 0 && node < 0xA000 && g && *(volatile int*)(uintptr_t)(g + kAreaOff) == g_area) {
        u32 obj = U(kSpotManagerVa + (u32)node * 4);
        if (Ptr(obj) && U(obj) == kTargetVtable && *(volatile u8*)(uintptr_t)(obj + 0x3B) && U(obj + 0x460) == 0 && U(obj + 0x45C) &&
            F(obj, 0x480) > 0.f) {
            u32 bob = U(obj + 0x484);
            if (bob != lastBob) {
                lastBob = bob;
                still = 0;
            } else if (still < 100) {
                ++still;
            }
            shown = still < 4;
        }
    }
    g_ownBeacon = shown;
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
    if (!GameIdle()) return;
    // only while the 2D area is the whole screen: FUN_0044AD00 sets it (x 0148097Ch, y 01480978h, width, height) to the minimap's
    // rectangle while the minimap is drawn (sprites go through 00459520 there too) and FUN_005F8430 puts it back. Projected into the
    // minimap's rectangle the label slid around with the minimap. Checked before the frame is marked as drawn, so a later call
    // in the same frame still draws it.
    if (F(0x0148097C, 0) != 0.f || F(0x01480978, 0) != 0.f || F(kHudWVa, 0) < 1000.f || F(kHudHVa, 0) < 560.f) return;
    g_drawnFrame = g_frame;
    u32 player = PlayerObjectNow();
    if (!player) return;
    if (BeaconStands()) return;
    const float W = F(kHudWVa, 0), H = F(kHudHVa, 0);
    if (W < 100.f || H < 100.f) return;
    const float* m = (const float*)(uintptr_t)kViewProjVa;
    float in[4] = {g_x, g_y + 15.f, g_z, 1.f};  // a little above the ground, like the game's label over a person
    float c[4];
    for (int j = 0; j < 4; ++j) c[j] = in[0] * m[j] + in[1] * m[4 + j] + in[2] * m[8 + j] + in[3] * m[12 + j];
    int yards = YardsTo(F(player, 0x58), F(player, 0x5C), F(player, 0x60));
    const float* colour = yards < 15 ? kGreen : kRed;
    const float margin = kEdgeMargin;
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

// The entry of the text draw FUN_0045B820: only a fallback when the sprite draw could not be hooked. Mes_Draw calls 0045B820
// in the middle of its own glyph batch (a "**" number: 0045D77C -> 0045C720 -> 0045B820), and a print from there would reset that
// batch's counter ([CMessage+12070h], 0045B90C) and drop the glyphs before the number for that frame.
void __cdecl TextEntryWorker(void* self, u32 arg) {
    if (g_spriteHooked) return;
    TextDrawWorker(self, arg);
}

// One of the game's objective points in the map point list. FUN_005EFF20 gives an active target object (CObjectTarget) type 8
// (a route objective inside its time window), 9 (outside it) or 10 (an ordinary objective); the minimap draws its chevrons
// for exactly these.
struct Objective {
    float pos[3];
    int type, node;
    float distance2;  // from the player, squared
};

// The objectives of the list: route objectives first (8, then 9), then the ordinary ones, each kind nearest first. The positions
// come from the game's accessor FUN_005E71B0 (thiscall: ECX = the manager, float out[4], index; ret 8, the type in AL): for these
// types it is the node's position from the navigation table. Main thread only, between frames (the Present hook), like the
// upkeep of the list.
int CollectObjectives(Objective* out, int max) {
    if (!g_accessorOk || !*(volatile u8*)(uintptr_t)kNavLoadedVa) return 0;
    volatile u32* list = (volatile u32*)(uintptr_t)kSpotListVa;
    const int n = *(volatile int*)(uintptr_t)kSpotCountVa;
    if (n <= 0 || n > 0x1000) return 0;  // not a list (yet)
    const u32 nodes = U(kNavCountVa);
    float px = g_lastPx, py = g_lastPy, pz = g_lastPz;
    if (u32 player = PlayerObjectNow()) {
        px = F(player, 0x58);
        py = F(player, 0x5C);
        pz = F(player, 0x60);
    }
    int found = 0;
    for (int i = 0; i < n && found < max; ++i) {
        const u32 entry = list[i];
        const u32 node = entry & 0xFFFFu;
        const int type = (signed char)(entry >> 16);
        if (node == 0xFFFFu || node > nodes) continue;  // the waypoint's own entry (also type 10), or no node of this stage
        if (type != 8 && type != 9 && type != 10) continue;
        float p[4] = {0.f, 0.f, 0.f, 0.f};
        const int got = ((SpotPosFn)(uintptr_t)kSiteSpotAccessor.va)((void*)(uintptr_t)kSpotManagerVa, p, i) & 0xFF;
        if (got != type) continue;
        const float dx = p[0] - px, dy = p[1] - py, dz = p[2] - pz;
        Objective o = {{p[0], p[1], p[2]}, type, (int)node, dx * dx + dy * dy + dz * dz};
        int at = found++;
        for (; at > 0 && (out[at - 1].type > o.type || (out[at - 1].type == o.type && out[at - 1].distance2 > o.distance2)); --at)
            out[at] = out[at - 1];
        out[at] = o;
    }
    return found;
}

constexpr int kMaxObjectives = 16;

}  // namespace

// The waypoint goes to the game's current objective; when it already stands on one, to the next one. False when the map has none.
bool WaypointToObjective() {
    if (!g_installed || !g_cfg.waypoint) return false;
    Objective list[kMaxObjectives];
    const int n = CollectObjectives(list, kMaxObjectives);
    if (n == 0) {
        Log("waypoint: the game's map has no objective point now (types 8, 9, 10), nothing changed");
        return false;
    }
    int pick = 0;
    if (g_active && g_objNode >= 0)
        for (int i = 0; i < n; ++i)
            if (list[i].node == g_objNode) pick = (i + 1) % n;
    const Objective& o = list[pick];
    g_x = o.pos[0];
    g_y = o.pos[1];
    g_z = o.pos[2];
    RememberArea();
    g_objNode = o.node;
    g_active = true;
    Log("waypoint: set on the game's objective %d of %d (map point type %d, node %d) at %d %d, %d yards away (area %d)", pick + 1, n,
        o.type, o.node, (int)g_x, (int)g_z, (int)(Sqrt(o.distance2) * kYardsPerUnit), g_area);
    return true;
}

// For the Story page of the menu: how many objective points the game's map has now and the yards to the first one (the one the
// objective key takes first). -1 = not known (the waypoint is off, or the accessor is not the one of 1.01b).
int WaypointObjectives(int* yards) {
    *yards = 0;
    if (!g_installed || !g_accessorOk) return -1;
    Objective list[kMaxObjectives];
    const int n = CollectObjectives(list, kMaxObjectives);
    if (n) *yards = (int)(Sqrt(list[0].distance2) * kYardsPerUnit);
    return n;
}

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

// Replaces `call 005EE3F0` at 005D4A74, the whole 12h (2D draw) branch of the stage manager's handler: mov ecx,014375F4h / call /
// pop edi / pop ebx / pop esi / mov ecx,[esp+18Ch] (the stack cookie). FUN_005EE3F0 takes ECX alone and ends in a plain ret
// (005EE45D). The game's own target icons are drawn first, then the waypoint's beacon, in the same pass and render state. After
// the call the game uses neither EAX, ECX, EDX nor the flags; everything but EAX (dead before the call too: it holds the index
// of the handler's jump table, and FUN_005EE3F0 overwrites it at once) is handed back as the game's function left it.
__attribute__((naked)) void WaypointBeaconStub() {
    asm volatile(
        ".intel_syntax noprefix\n"
        "mov eax, 0x005EE3F0\n"
        "call eax\n"
        "pushfd\n"
        "pushad\n"
        "call _WaypointBeaconDraw\n"
        "popad\n"
        "popfd\n"
        "ret\n"
        ".att_syntax\n");
}

// The game's objective beacon over the waypoint: only when the call and the two icon functions are the ones of 1.01b.
void InstallWaypointBeacon() {
    const SiteDef& c = kSiteBeaconCall;
    if (!BytesMatch(c.va, c.bytes, c.len) || !BytesMatch(kSiteBeaconDraw.va, kSiteBeaconDraw.bytes, kSiteBeaconDraw.len) ||
        !BytesMatch(kSiteBeaconMove.va, kSiteBeaconMove.bytes, kSiteBeaconMove.len)) {
        Log("waypoint: the target icon code differs from DP.exe 1.01b, no beacon (the text label stays)");
        return;
    }
    if (!PatchCall(c.va + 5, c.bytes + 5, 5, (const void*)WaypointBeaconStub)) {
        Log("waypoint: could not patch the target icon draw call at %08X, no beacon (the text label stays)", c.va + 5);
        return;
    }
    g_beaconInstalled = true;
    Log("waypoint: beacon ready (the game's objective arrows over the waypoint, drawn after the game's own target icons at %08X)", c.va + 5);
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
    BuildEntryHook(hook, (u32)(uintptr_t)hook, (u32)(uintptr_t)TextEntryWorker, t.bytes, t.len, t.va + t.len);
    if (!PatchJump(t.va, t.bytes, t.len, hook)) return false;
    const SiteDef& p = kSiteSpriteDrawEntry;  // the HUD draws sprites every frame: the label no longer needs a text draw in the frame
    if (BytesMatch(p.va, p.bytes, p.len)) {
        u8* hook2 = AllocStub(64);
        if (hook2) {
            BuildEntryHook(hook2, (u32)(uintptr_t)hook2, (u32)(uintptr_t)TextDrawWorker, p.bytes, p.len, p.va + p.len);
            if (PatchJump(p.va, p.bytes, p.len, hook2)) g_spriteHooked = true;
            else Log("waypoint: could not hook the sprite draw, the label follows text draws only");
        }
    } else {
        Log("waypoint: the sprite draw at %08X differs from DP.exe 1.01b, the label follows text draws only", p.va);
    }
    if (!PatchCall(s.va, s.bytes, s.len, (const void*)WaypointMapDrawStub)) return false;
    g_installed = true;
    // checked before our own jump goes in: the objective key calls the accessor, patched or not
    g_accessorOk = BytesMatch(kSiteSpotAccessor.va, kSiteSpotAccessor.bytes, kSiteSpotAccessor.len);
    if (!g_accessorOk) Log("waypoint: the map point accessor differs from DP.exe 1.01b, the objective key is off");
    if (g_cfg.waypointMinimap) InstallWaypointSpot();
    if (g_cfg.waypointBeacon) InstallWaypointBeacon();
    else Log("waypoint: beacon off (Beacon=0), the text label is used");
    Log("waypoint: ready (F7 sets a waypoint on the map or in the world, Shift+F7 removes it, Ctrl + key %d puts it on the game's objective; drawn in the game's font)",
        g_cfg.waypointObjectiveKey);
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
    TrackOwnBeacon();
    static unsigned lastDraws = 0;
    static int sinceDraw = 100;
    static bool prevKey = false, prevPad = false, prevObjective = false;
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
    // The objective key counts only with Ctrl held at the moment it goes down. By default it is the waypoint key itself: that
    // press is then not a waypoint press, whatever is released first afterwards (the edges are taken from the keys alone).
    const bool objectiveKey = front && g_cfg.waypointObjectiveKey && (GetAsyncKeyState(g_cfg.waypointObjectiveKey) & 0x8000) != 0;
    const bool toObjective = objectiveKey && !prevObjective && (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool press = (key && !prevKey && !(toObjective && g_cfg.waypointObjectiveKey == g_cfg.waypointKey)) || (pad && !prevPad);
    prevKey = key;
    prevPad = pad;
    prevObjective = objectiveKey;
    if (ui::IsOpen()) return;
    if (toObjective) {
        if (mapOpen || inWorld) WaypointToObjective();
        return;
    }
    if (!press) return;
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
            g_objNode = -1;
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
        g_objNode = -1;
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
    // The waypoint's own entry, and whether the game's list already has the place: a waypoint put on an objective (the objective
    // key) needs no second point while the game's own point of that node is in the list.
    int found = -1;
    bool gameHasIt = false;
    const int objNode = g_objNode;
    for (int i = 0; i < n; ++i) {
        const u32 id = list[i] & 0xFFFFu;
        const int type = (signed char)(list[i] >> 16);
        if (id == 0xFFFFu) {
            if (found < 0) found = i;
        } else if ((int)id == objNode && type >= 8 && type <= 10) {
            gameHasIt = true;
        }
    }
    // g_haveLast: a game world has been played (the list belongs to a stage)
    u32 g = GameManager();
    const bool sameArea = g && *(volatile int*)(uintptr_t)(g + kAreaOff) == g_area;
    const bool want = g_active && g_cfg.waypoint && g_cfg.waypointMinimap && g_haveLast && sameArea && !gameHasIt;
    if (want && found < 0 && n < kSpotGuard) {
        list[n] = kSpotEntry | ((u32)(u8)g_floor << 24);
        *count = n + 1;
    } else if (!want && found >= 0) {
        for (int i = found; i + 1 < n; ++i) list[i] = list[i + 1];
        *count = n - 1;
    }
}

// The game's objective beacon over the waypoint. Called once per frame from the game's 2D pass (WaypointBeaconStub), on the game's
// thread, right after the game drew its own target icons with the same function. The icon is the game's type 0: the red arrows of
// GOAL.XPC, one to four by the distance, bobbing, with "%d Yards" under them in the game's font (green arrows within 15 yards, as
// the label turns green there).
extern "C" void __cdecl WaypointBeaconDraw() {
    using namespace ufx;
    const bool again = g_beaconPassSeen && g_beaconPass == g_frame;  // a second pass in one frame: the bobbing runs once per frame
    g_beaconPass = g_frame;
    g_beaconPassSeen = true;
    if (again || g_inWorker || !g_active || !g_showWorld || !g_cfg.waypoint) return;
    if (!GameIdle()) return;  // the test FUN_005EE3F0 makes before the game's own icons (005EE3F3..005EE40D)
    if (g_ownBeacon || !BeaconInView()) return;
    u32 texture;
    const u32 g = GameManager();
    if (!g || !BeaconReady(&texture)) return;
    // the size FUN_005B19A0 gives an objective's icon: 128 while bit 2 of CGame+8C5ECh is set (FUN_004508D0), else 102
    const float size = (U(g + kGameFlagsOff) & 2u) ? 128.f : 102.f;
    const u32 player = PlayerObjectNow();
    const float* colour = player && YardsTo(F(player, 0x58), F(player, 0x5C), F(player, 0x60)) < 15 ? kGreen : kBeaconRed;
    for (int i = 0; i < 4; ++i) g_icon.rgba[i] = colour[i];
    const float pos[3] = {g_x, g_y, g_z};
    g_icon.texture = texture;  // looked up in this frame, and taken away again below: the icon never keeps the pointer
    g_inWorker = true;         // FUN_005B1120 prints through FUN_0045C540: the hooked text entry must not draw the label from inside
    ((IconMoveFn)(uintptr_t)kIconMoveFn)(&g_icon, pos);
    ((IconDrawFn)(uintptr_t)kIconDrawFn)(&g_icon, size);
    g_inWorker = false;
    g_icon.texture = 0;
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
