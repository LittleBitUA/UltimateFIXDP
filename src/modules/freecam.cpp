// Free camera: fly the render camera around with the mouse and keys, hide the HUD for clean screenshots.
//
// The main tick builds the view matrices of the render camera (a context at BD9648h: eye +4, target +14h, up +24h,
// field of view +54h, all floats) with FUN_006B62E0 right after loading ECX. A bridge in front of that call lets us
// change the camera data first; nothing else of the game's camera logic is touched, so switching the camera off puts
// everything back at once. The idea is the one of the FreeCam mod by Behelit09 (MIT); the code is our own.
//
// Keys (F5 camera, F4 HUD by default): W S A D move, Space / E up, C / Q down, Shift faster, Ctrl slower, mouse looks.
// While it flies the game receives no keyboard, mouse or pad input (the same hooks the F10 menu uses).
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {
namespace {

bool g_installed = false;
volatile bool g_active = false;
bool g_init = false;
bool g_hudHidden = false;
u8 g_hudSaved = 1;
float g_pos[3], g_yaw, g_pitch;
LARGE_INTEGER g_last;
bool g_keyPrev[4];

// x87 helpers: the module has no C runtime.
float Sin(float x) { float r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
float Cos(float x) { float r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
float Sqrt(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
float Atan2(float y, float x) { float r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
float Asin(float x) { return Atan2(x, Sqrt(1.0f - x * x)); }

constexpr float kPi = 3.14159265f;

bool Down(int vk) {
    if (!vk) return false;
    HWND w = GameWindowHandle();
    return w && GetForegroundWindow() == w && (GetAsyncKeyState(vk) & 0x8000) != 0;  // only while the game has focus
}

bool Pressed(int index, int vk) {
    bool now = Down(vk);
    bool edge = now && !g_keyPrev[index];
    g_keyPrev[index] = now;
    return edge;
}

void FlyFrame(bool menuOpen);  // the fly cheat, below

// The minimap is a task of its own and does not look at the HUD flag: it draws while its "on" byte 8C29F8h is set (the game
// writes that byte only when a scripted "restricted" mode starts or ends, FUN_004FFC40 / FUN_004FFC70, and at its init).
void SetHud(bool hidden) {
    volatile u8* flag = (volatile u8*)(uintptr_t)kHudFlagVa;
    volatile u8* minimap = (volatile u8*)(uintptr_t)0x8C29F8;
    static u8 minimapSaved = 1;
    if (hidden && !g_hudHidden) {
        g_hudSaved = *flag;
        minimapSaved = *minimap;
        g_hudHidden = true;
    }
    if (hidden) {
        *flag = 0;
        *minimap = 0;
    } else if (g_hudHidden) {
        *flag = g_hudSaved ? g_hudSaved : 1;
        *minimap = minimapSaved;
        g_hudHidden = false;
    }
}

bool CenterOfWindow(POINT* centre) {
    HWND w = GameWindowHandle();
    if (!w || GetForegroundWindow() != w) return false;
    RECT rc;
    if (!GetClientRect(w, &rc)) return false;
    centre->x = (rc.right - rc.left) / 2;
    centre->y = (rc.bottom - rc.top) / 2;
    return ClientToScreen(w, centre) != 0;
}

// Field of view in play. The game's camera update (FUN_00535750) eases the angle at 00BD969Ch towards its target from the
// value that is there (0x536BB8..0x536BD6: fov += (target - fov) * dt / time), so a scaled value must never stay there: it
// is written right before the camera matrices are built and the game's own value goes back after the frame was shown
// (GameFovAfterFrame, from the Present hook). If no Present came in between, the value we wrote is still there and is
// recognised as ours.
float g_fovGame = 0.f, g_fovWritten = 0.f;
bool g_fovSwapped = false;

bool InCutscene() {
    if (*(volatile int*)(uintptr_t)kEventModeVa < 0) return true;  // a scene demo
    u32 ev = *(volatile u32*)(uintptr_t)0x00BD9E58;               // CEvent: a script cutscene (the game's own test, 0042D010)
    if (ev < 0x10000 || ev >= UserAddressEnd()) return false;
    return (*(volatile u32*)(uintptr_t)(ev + 0x44) & 0x40000u) != 0 || *(volatile u32*)(uintptr_t)(ev + 0x34) != 0;
}

// The near plane (+64h): the game writes 1.0 every frame in play (005D361A), event cameras their own. A higher one in play only
// (not in cutscenes), put back after the frame like the angle.
float g_nearGame = 0.f, g_nearWritten = 0.f;
bool g_nearSwapped = false;

void GameNearBeforeBuild() {
    volatile float* nearPlane = (volatile float*)(uintptr_t)(kRenderCameraVa + 0x64);
    float v = *nearPlane;
    if (g_nearSwapped && v == g_nearWritten) v = g_nearGame;
    g_nearSwapped = false;
    float want = (float)g_cfg.nearPlane / 10.f;
    // Never farther than 30 % of the way to the point the camera looks at (York, mostly): close cameras (the save phone, a
    // wall behind York, a tight room) pull the plane in with them, so nothing near the camera is cut; far away the
    // precision stays.
    const float* eye = (const float*)(uintptr_t)(kRenderCameraVa + 0x04);
    const float* tgt = (const float*)(uintptr_t)(kRenderCameraVa + 0x14);
    const float dx = tgt[0] - eye[0], dy = tgt[1] - eye[1], dz = tgt[2] - eye[2];
    const float d2 = dx * dx + dy * dy + dz * dz;
    // 30 %: York's back and shoulders are nearer the camera than the point it looks at (running through a door pulls the
    // camera right behind him)
    const float kShare = 0.3f;
    if (d2 > 0.f && d2 * kShare * kShare < want * want) want = Sqrt(d2) * kShare;
    // the free camera flies close to things on purpose: the game's own plane there
    if (want <= v || v <= 0.f || v > 50.f || InCutscene() || g_active) {
        *nearPlane = v;
        return;
    }
    g_nearGame = v;
    *nearPlane = want;
    g_nearWritten = *nearPlane;
    g_nearSwapped = true;
}

void GameFovBeforeBuild() {
    volatile float* fov = (volatile float*)(uintptr_t)(kRenderCameraVa + 0x54);
    float v = *fov;
    if (g_fovSwapped && v == g_fovWritten) v = g_fovGame;  // the game has not written since: its own value is the saved one
    g_fovSwapped = false;
    const bool use = g_cfg.gameFov != 100 && !g_active && v > 0.05f && v < 3.0f && !InCutscene();
    if (!use) {
        *fov = v;  // the game's own value, in case ours was still there
        return;
    }
    float w = v * (float)g_cfg.gameFov / 100.f;
    if (w > 2.9f) w = 2.9f;  // about 166 degrees: the projection stays finite
    g_fovGame = v;
    *fov = w;
    g_fovWritten = *fov;  // as stored (a float), so the comparison above is exact
    g_fovSwapped = true;
}

// Runs once per frame in the game thread, in front of the camera matrix build.
void __attribute__((cdecl)) CameraWorker() {
    {   // the near plane of the main camera (a float in its context) as the game set it, logged once (NearPlane raises it in play)
        static bool logged = false;
        if (!logged) {
            logged = true;
            float n = *(volatile float*)(uintptr_t)(kRenderCameraVa + 0x64);
            Log("camera: near plane %d.%03d, far plane 200000", (int)n, (int)((n - (float)(int)n) * 1000.f));
        }
    }
    bool menuOpen = ui::IsOpen();
    if (g_cfg.freeCam && g_cfg.freeCamHudKey && Pressed(1, g_cfg.freeCamHudKey) && !menuOpen) g_cfg.photoHud = !g_cfg.photoHud;
    if (g_cfg.freeCam && g_cfg.freeCamKey && Pressed(0, g_cfg.freeCamKey) && !menuOpen) g_cfg.photoActive = !g_cfg.photoActive;
    if (Pressed(2, 0x75 /*F6*/) && !menuOpen) {  // the whole photo mode: camera, frozen time, panel
        bool enter = !(g_cfg.photoActive || g_cfg.photoFreeze);
        g_cfg.photoActive = enter;
        g_cfg.photoFreeze = enter;
        if (!enter) {  // leaving: the interface, the filter, the tilt and the pose go back to normal
            g_cfg.photoHud = 0;
            ui::PhotoLookReset();
            g_cfg.photoRoll = 0;
            g_cfg.photoFovOn = 0;
            g_cfg.photoCostume = 0;
        }
        if (enter) ui::OpenPhotoPage();
        Log("photo mode: %s", enter ? "on" : "off");
    }
    {   // However the photo mode ends (F6, the camera key, the rows of the menu): once neither the camera flies nor the time
        // stands, York gets the game's own motion and face back. The suit stays: it is a thing he wears, not a pose.
        static bool wasPhoto = false;
        const bool photo = g_cfg.photoActive || g_cfg.photoFreeze;
        if (wasPhoto && !photo) {
            if (g_cfg.photoAnim) RequestPose(1);  // state 0: calm
            g_cfg.photoAnim = 0;
            g_cfg.photoBodyNo = g_cfg.photoFaceOn = g_cfg.photoNoBlink = g_cfg.photoPlay = 0;
            g_cfg.photoMoveX = g_cfg.photoMoveY = g_cfg.photoMoveZ = g_cfg.photoTurn = 0;  // FlyFrame puts him back and lands him  // PhotoAnimTick puts York's own motion back
            Log("photo mode: over, York's own motion and face again");
        }
        wasPhoto = photo;
    }
    SetHud(g_cfg.photoHud != 0 || ui::ScreenshotHidesHud());
    if ((g_cfg.photoActive != 0) != g_active) {
        g_active = g_cfg.photoActive != 0;
        g_init = false;
        if (!g_active) {  // the camera went back to the game: the look of the picture goes back too
            ui::PhotoLookReset();
            g_cfg.photoRoll = 0;
            g_cfg.photoFovOn = 0;
        }
        Log("freecam: %s", g_active ? "on" : "off");
    }
    FlyFrame(menuOpen);
    GameFovBeforeBuild();
    GameNearBeforeBuild();
    if (!g_active) return;

    float* eye = (float*)(uintptr_t)(kRenderCameraVa + 0x04);
    float* tgt = (float*)(uintptr_t)(kRenderCameraVa + 0x14);
    float* up = (float*)(uintptr_t)(kRenderCameraVa + 0x24);
    float* fov = (float*)(uintptr_t)(kRenderCameraVa + 0x54);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    float dt = g_last.QuadPart ? (float)((double)(now.QuadPart - g_last.QuadPart) / (double)freq.QuadPart) : 0.f;
    g_last = now;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.f) dt = 0.f;

    if (!g_init) {  // start from where the game's camera is
        float f[3] = {tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2]};
        float len = Sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (len < 1e-4f) {
            f[0] = 0.f; f[1] = 0.f; f[2] = 1.f; len = 1.f;
        }
        for (int i = 0; i < 3; ++i) { f[i] /= len; g_pos[i] = eye[i]; }
        g_yaw = Atan2(f[0], f[2]);
        g_pitch = Asin(f[1]);
        g_init = true;
    }

    POINT centre;
    bool focused = CenterOfWindow(&centre) && !ui::IsOpen();
    if (focused) {
        POINT cur;
        if (GetCursorPos(&cur)) {
            // the game's world is right-handed as seen on the screen: a larger yaw turns the view to the LEFT, so the mouse to the
            // right must lower it (it was the other way round: the mouse turned the camera the wrong way)
            g_yaw -= (float)(cur.x - centre.x) * 0.003f;
            g_pitch -= (float)(cur.y - centre.y) * 0.003f;
        }
        SetCursorPos(centre.x, centre.y);
        const float lim = 89.f * kPi / 180.f;
        if (g_pitch > lim) g_pitch = lim;
        if (g_pitch < -lim) g_pitch = -lim;
        if (g_yaw > kPi) g_yaw -= 2.f * kPi;
        if (g_yaw < -kPi) g_yaw += 2.f * kPi;

        float speed = (float)g_cfg.freeCamSpeed;
        if (Down(VK_SHIFT)) speed *= 3.f;
        if (Down(VK_CONTROL)) speed *= 0.25f;
        float f[3] = {Cos(g_pitch) * Sin(g_yaw), Sin(g_pitch), Cos(g_pitch) * Cos(g_yaw)};
        float r[3] = {-Cos(g_yaw), 0.f, Sin(g_yaw)};  // screen right (right-handed world: the opposite of cross(up, forward))
        float move[3] = {0.f, 0.f, 0.f};
        float fw = (Down('W') ? 1.f : 0.f) - (Down('S') ? 1.f : 0.f);
        float st = (Down('D') ? 1.f : 0.f) - (Down('A') ? 1.f : 0.f);
        float ud = ((Down(VK_SPACE) || Down('E')) ? 1.f : 0.f) - ((Down('C') || Down('Q')) ? 1.f : 0.f);
        for (int i = 0; i < 3; ++i) move[i] = f[i] * fw + r[i] * st;
        move[1] += ud;
        for (int i = 0; i < 3; ++i) g_pos[i] += move[i] * speed * dt;
    }

    float f[3] = {Cos(g_pitch) * Sin(g_yaw), Sin(g_pitch), Cos(g_pitch) * Cos(g_yaw)};
    for (int i = 0; i < 3; ++i) {
        eye[i] = g_pos[i];
        tgt[i] = g_pos[i] + f[i];
    }
    {   // up vector: the pitch derivative of the view direction, turned around the view direction by the roll
        float un[3] = {-Sin(g_pitch) * Sin(g_yaw), Cos(g_pitch), -Sin(g_pitch) * Cos(g_yaw)};
        float rt[3] = {Cos(g_yaw), 0.f, -Sin(g_yaw)};
        float ra = (float)g_cfg.photoRoll * kPi / 180.f;
        float c = Cos(ra), sn = Sin(ra);
        for (int i = 0; i < 3; ++i) up[i] = un[i] * c + rt[i] * sn;
    }
    if (g_cfg.photoFovOn && g_cfg.freeCamFov >= 30) *fov = (float)g_cfg.freeCamFov * kPi / 180.f;
}

// ---- Fly (cheat): York is held where the player steers him; the game's own movement and falling do not apply meanwhile.
// The position (+58h..+60h) is written twice a frame: here, after the game's update and before the camera and the draw, and at
// the start of the player update (FlyHold), so the update starts from it too. The previous-frame position (+3B0h) is kept equal,
// so the frame's movement the game derives from the two is zero.
bool g_flyOn = false;
volatile u8 g_flyBridge = 0;  // read by the physics bridge: York flies (1) or lands this frame (2)
float g_fly[3];
float g_flyYaw = 0.f;  // York's heading while flying (+7Ch, also the target +6Ch): the game's camera follows it
LARGE_INTEGER g_flyLast = {};

// Photo mode: York placed by hand. The rows of the photo page give an offset from where he stood (right, up, forward along
// his own heading at that moment) and a turn; he is held there by the fly cheat's own means (position, physics shapes, no
// ground or fall), and put back and landed by the game's teleport sync when the offsets are zero again or the photo mode ends.
bool g_placing = false;
float g_placeBase[3], g_placeYaw = 0.f;
bool PlaceWanted() { return g_cfg.photoMoveX || g_cfg.photoMoveY || g_cfg.photoMoveZ || g_cfg.photoTurn; }

bool FlyAllowed(u8* p) {
    const u32 state = *(volatile u32*)(p + 0x654);
    return state != 0x87 && state != 0x88 && !InCutscene();  // not behind the wheel, not in a scene
}

void FlyPut(u8* p) {
    for (int i = 0; i < 3; ++i) {
        *(volatile float*)(p + 0x58 + 4 * i) = g_fly[i];
        *(volatile float*)(p + 0x3B0 + 4 * i) = g_fly[i];
    }
}

void FlyFrame(bool menuOpen) {
    if (g_cfg.flyKey && Pressed(3, g_cfg.flyKey) && !menuOpen) {
        g_cfg.cheatFly = !g_cfg.cheatFly;
        ShowNotice(g_cfg.cheatFly ? L"Політ: увімкнено" : L"Політ: вимкнено", g_cfg.cheatFly ? L"Fly: on" : L"Fly: off",
                   g_cfg.cheatFly ? L"WASD, мишка або Q/E поворот, Space вгору, Ctrl вниз, Shift швидше. Вимикайте біля землі."
                                  : L"Йорк упаде звідти, де він зараз.",
                   g_cfg.cheatFly ? L"WASD, the mouse or Q/E turn, Space up, Ctrl down, Shift faster. Turn it off near the ground."
                                  : L"York falls from where he is now.", 3);
    }
    u8* p = (u8*)(uintptr_t)PlayerObjectNow();
    const bool place = !g_cfg.cheatFly && PlaceWanted();
    if (g_placing && !place && g_flyOn && p) {  // the offsets are zero again (or the fly cheat takes over): back where he stood
        for (int i = 0; i < 3; ++i) g_fly[i] = g_placeBase[i];
        g_flyYaw = g_placeYaw;
        *(volatile float*)(p + 0x7C) = g_flyYaw;
        *(volatile float*)(p + 0x6C) = g_flyYaw;
        FlyPut(p);
        Log("photo place: York back where he stood");
    }
    if (!place) g_placing = false;
    if ((!g_cfg.cheatFly && !place) || !p || !FlyAllowed(p)) {
        if (g_flyOn) {
            Log("cheat: fly %s", (g_cfg.cheatFly || place) ? "paused (car or scene)" : "off");
            g_flyBridge = (p && FlyAllowed(p)) ? 2 : 0;  // land through the game's own teleport sync on the next physics call
        }
        g_flyOn = false;
        return;
    }
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (!g_flyOn) {
        for (int i = 0; i < 3; ++i) g_fly[i] = *(volatile float*)(p + 0x58 + 4 * i);
        g_flyOn = true;
        g_flyBridge = 1;
        g_flyLast = now;
        g_flyYaw = *(volatile float*)(p + 0x7C);
        POINT c;
        if (CenterOfWindow(&c)) SetCursorPos(c.x, c.y);
        Log("cheat: fly on at %d %d %d", (int)g_fly[0], (int)g_fly[1], (int)g_fly[2]);
    }
    if (place) {
        if (!g_placing) {
            g_placing = true;
            for (int i = 0; i < 3; ++i) g_placeBase[i] = g_fly[i];
            g_placeYaw = g_flyYaw;
            Log("photo place: York held from %d %d %d, heading %d (thousandths of a radian)", (int)g_fly[0], (int)g_fly[1], (int)g_fly[2],
                (int)(g_flyYaw * 1000.f));
        }
        // forward = where he faced when the placing began (sin, cos of the heading), right = a quarter turn from it
        const float s = Sin(g_placeYaw), c = Cos(g_placeYaw);
        const float right = (float)g_cfg.photoMoveX * 0.1f, fwd = (float)g_cfg.photoMoveZ * 0.1f;
        g_fly[0] = g_placeBase[0] + s * fwd + c * right;
        g_fly[2] = g_placeBase[2] + c * fwd - s * right;
        g_fly[1] = g_placeBase[1] + (float)g_cfg.photoMoveY * 0.1f;
        g_flyYaw = g_placeYaw + (float)g_cfg.photoTurn * (kPi / 180.f);
        *(volatile float*)(p + 0x7C) = g_flyYaw;
        *(volatile float*)(p + 0x6C) = g_flyYaw;
        g_flyLast = now;
        FlyPut(p);
        return;
    }
    float dt = (float)((double)(now.QuadPart - g_flyLast.QuadPart) / (double)freq.QuadPart);
    g_flyLast = now;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.f) dt = 0.f;
    if (!menuOpen && !g_active) {  // with the free camera on, WASD flies the camera, York stays
        // turning: the game sees no input while flying (FreeCamBlocksInput), so the mouse and Q/E turn York here, and the
        // game's camera follows his heading as it always does
        POINT centre, cur;
        if (CenterOfWindow(&centre) && GetCursorPos(&cur)) {
            g_flyYaw -= (float)(cur.x - centre.x) * 0.003f;
            SetCursorPos(centre.x, centre.y);
        }
        g_flyYaw += ((Down('Q') ? 1.f : 0.f) - (Down('E') ? 1.f : 0.f)) * 2.0f * dt;
        if (g_flyYaw > kPi) g_flyYaw -= 2.f * kPi;
        if (g_flyYaw < -kPi) g_flyYaw += 2.f * kPi;
        *(volatile float*)(p + 0x7C) = g_flyYaw;
        *(volatile float*)(p + 0x6C) = g_flyYaw;
        volatile float* eye = (volatile float*)(uintptr_t)(kRenderCameraVa + 0x04);
        volatile float* tgt = (volatile float*)(uintptr_t)(kRenderCameraVa + 0x14);
        float fx = tgt[0] - eye[0], fz = tgt[2] - eye[2];
        float len = Sqrt(fx * fx + fz * fz);
        if (len > 1e-4f) { fx /= len; fz /= len; } else { fx = 0.f; fz = 1.f; }
        const float rx = -fz, rz = fx;  // screen right, as for the free camera
        float fw = (Down('W') ? 1.f : 0.f) - (Down('S') ? 1.f : 0.f);
        float st = (Down('D') ? 1.f : 0.f) - (Down('A') ? 1.f : 0.f);
        float ud = (Down(VK_SPACE) ? 1.f : 0.f) - ((Down(VK_CONTROL) || Down('C')) ? 1.f : 0.f);
        float speed = (float)g_cfg.flySpeed * (Down(VK_SHIFT) ? 4.f : 1.f);
        g_fly[0] += (fx * fw + rx * st) * speed * dt;
        g_fly[2] += (fz * fw + rz * st) * speed * dt;
        g_fly[1] += ud * speed * dt;
    }
    FlyPut(p);
}

}  // namespace

typedef void(__attribute__((thiscall)) * ShapesToFn)(void* player, float lift);
typedef void(__attribute__((thiscall)) * LandFn)(void* player);

// In place of the player's physics (kSitePlayerPhysicsCall) while the bridge byte is set: flying, York and his two physics shapes
// are put where the cheat holds him and the game's ground, fall and collisions are skipped; landing, the game's own teleport sync
// sweeps the shape down onto the ground once.
void __attribute__((cdecl)) FlyPhysicsWorker(void* player) {
    u8* p = (u8*)player;
    if (g_flyBridge == 2) {
        g_flyBridge = 0;
        if (p) {
            ((LandFn)(uintptr_t)kPlayerLandFn)(player);
            Log("cheat: fly landed at %d %d %d", (int)*(volatile float*)(p + 0x58), (int)*(volatile float*)(p + 0x5C), (int)*(volatile float*)(p + 0x60));
        }
        return;
    }
    if (!p || !g_flyOn) { g_flyBridge = 0; return; }
    FlyPut(p);
    ((ShapesToFn)(uintptr_t)kPlayerShapesToFn)(player, 0.f);
}

// At the start of the player update (CheatsTick): the update begins from the held position.
void FlyHold(void* player) {
    if (g_flyOn && player && FlyAllowed((u8*)player)) FlyPut((u8*)player);
}

bool FreeCamBlocksInput() { return (g_active && g_cfg.freeCamFreeze) || g_flyOn; }  // flying: York must not walk or turn by the game's input

void GameFovAfterFrame() {
    if (g_nearSwapped) {
        volatile float* nearPlane = (volatile float*)(uintptr_t)(kRenderCameraVa + 0x64);
        if (*nearPlane == g_nearWritten) *nearPlane = g_nearGame;
        g_nearSwapped = false;
    }
    if (!g_fovSwapped) return;
    volatile float* fov = (volatile float*)(uintptr_t)(kRenderCameraVa + 0x54);
    if (*fov == g_fovWritten) *fov = g_fovGame;  // the game's next camera update eases from its own angle
    g_fovSwapped = false;
}

bool InstallFreeCam() {
    const SiteDef& s = kSiteCameraCall;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("freecam: the camera call at %08X differs from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    u8* stub = AllocStub(32);
    if (!stub) return false;
    BuildCameraBridge(stub, (u32)(uintptr_t)stub, (u32)(uintptr_t)CameraWorker, kCameraBuildFn);
    if (!PatchCall(s.va, s.bytes, s.len, stub)) {
        Log("freecam: could not patch the camera call");
        return false;
    }
    g_installed = true;
    {   // the fly cheat's physics bridge
        const SiteDef& f = kSitePlayerPhysicsCall;
        u8* bridge = BytesMatch(f.va, f.bytes, f.len) ? AllocStub(32) : nullptr;
        if (bridge) {
            BuildFlyPhysicsBridge(bridge, (u32)(uintptr_t)bridge, (u32)(uintptr_t)&g_flyBridge, (u32)(uintptr_t)FlyPhysicsWorker, kPlayerPhysicsFn);
            if (!PatchCall(f.va, f.bytes, f.len, bridge)) Log("freecam: the player physics call could not be patched (the fly cheat is rough)");
        } else {
            Log("freecam: the player physics call at %08X differs from DP.exe 1.01b (the fly cheat is rough)", f.va);
        }
    }
    if (g_cfg.flyKey && (g_cfg.flyKey == g_cfg.freeCamKey || g_cfg.flyKey == g_cfg.freeCamHudKey || g_cfg.flyKey == 0x75))
        Log("freecam: FlyKey %d is also a free camera / photo mode key: the fly cheat will not move York while the camera is free", g_cfg.flyKey);
    Log("freecam: ready (key %d switches the camera, key %d the HUD, speed %d)", g_cfg.freeCamKey, g_cfg.freeCamHudKey,
        g_cfg.freeCamSpeed);
    return true;
}

}  // namespace ufx
