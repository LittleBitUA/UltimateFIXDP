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
bool g_keyPrev[3];

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

void SetHud(bool hidden) {
    volatile u8* flag = (volatile u8*)(uintptr_t)kHudFlagVa;
    if (hidden && !g_hudHidden) {
        g_hudSaved = *flag;
        g_hudHidden = true;
    }
    if (hidden) *flag = 0;
    else if (g_hudHidden) {
        *flag = g_hudSaved ? g_hudSaved : 1;
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
    {   // the near plane of the main camera (a float in its context), logged once for diagnostics; it is never changed
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
            g_cfg.photoFilter = g_cfg.photoBright = g_cfg.photoContrast = g_cfg.photoSat = g_cfg.photoVignette = 0;
            g_cfg.photoRoll = 0;
            g_cfg.photoFovOn = 0;
            if (g_cfg.photoAnim) RequestPose(1);  // state 0: calm
            g_cfg.photoAnim = 0;
            g_cfg.photoCostume = 0;
        }
        if (enter) ui::OpenPhotoPage();
        Log("photo mode: %s", enter ? "on" : "off");
    }
    SetHud(g_cfg.photoHud != 0);
    if ((g_cfg.photoActive != 0) != g_active) {
        g_active = g_cfg.photoActive != 0;
        g_init = false;
        if (!g_active) {  // the camera went back to the game: the look of the picture goes back too
            g_cfg.photoFilter = g_cfg.photoBright = g_cfg.photoContrast = g_cfg.photoSat = g_cfg.photoVignette = 0;
            g_cfg.photoRoll = 0;
            g_cfg.photoFovOn = 0;
        }
        Log("freecam: %s", g_active ? "on" : "off");
    }
    GameFovBeforeBuild();
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

}  // namespace

bool FreeCamBlocksInput() { return g_active && g_cfg.freeCamFreeze; }

void GameFovAfterFrame() {
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
    Log("freecam: ready (key %d switches the camera, key %d the HUD, speed %d)", g_cfg.freeCamKey, g_cfg.freeCamHudKey,
        g_cfg.freeCamSpeed);
    return true;
}

}  // namespace ufx
