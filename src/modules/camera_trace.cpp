// Diagnostic ([Debug] TraceCamera=1): logs the camera and the player state, to see what changes the camera at a
// given place.
// - CCamera mode (CCamera+0x154) changes, which follow the player state (FUN_00534C60 is called only from the
//   player state change);
// - player state changes ([player+0x654], previous [+0x658]) with the player position;
// - scripted camera requests of the game manager;
// - sudden jumps of the render camera (CRdCamera at 00BD9648: eye +4, target +0x14) within one frame;
// - while driving, what the game sees from the pad, once a second;
// - the cutscene flag (bit 31 of 008A6074) with the player, camera and scripted-camera lines;
// - Numpad 2: a mark with the whole camera at that moment (CameraMark).
// Reads memory only; the one game call is the object lookup the camera code itself makes every frame. The whole
// trace writes at most kMaxLines lines per session: every line is a synchronous write on the game thread.
#include "../base.h"

namespace ufx {
namespace {

constexpr u32 kCameraPtrVa = 0x00BE1EA4;   // CSingleton<CCamera>
constexpr u32 kRenderCameraVa = 0x00BD9648;  // CRdCamera object
constexpr u32 kObjectMgrVa = 0x00BD7670;     // object table (ecx of the lookup)
constexpr u32 kPlayerHandleVa = 0x008A9BA4;
constexpr u32 kLookupFn = 0x006C5FD0;        // thiscall(mgr, handle) -> object*
constexpr u32 kEventFlagsVa = 0x008A6074;
constexpr u32 kInputSingletonVa = 0x00BD9E10;  // CInput; slot 0 state at +0xC8
constexpr u32 kGameMgrPtrVa = 0x00BDA004;    // game manager: scripted camera request lives in it
constexpr u32 kReqFlagOff = 0xCEB54;         // request present
constexpr u32 kReqKindOff = 0xCEB5C;         // 7 = fixed camera (read by the follow camera)
constexpr u32 kReqPosOff = 0xCEB24;          // 4 floats: position
constexpr u32 kReqConsumedOff = 0xCFE10;

typedef u8*(__fastcall* LookupFn)(void* mgr, void* edx, u32 handle);

u32 g_lastMode = 0xFFFFFFFF;
u32 g_lastState = 0xFFFFFFFF;
u32 g_lastReq[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
u32 g_frame = 0;
float g_lastEye[3];
bool g_haveEye = false;
DWORD g_lastCarLog = 0;
int g_lines = 0;
constexpr int kMaxLines = 600;

// One more line of the budget; the last one says that the trace stops.
bool Budget() {
    if (g_lines >= kMaxLines) return false;
    if (++g_lines == kMaxLines) {
        Log("camera trace: %d lines written, the trace stops for this session", kMaxLines);
        return false;
    }
    return true;
}

float F(u32 base, u32 off) { return *(volatile const float*)(uintptr_t)(base + off); }
u32 U(u32 base, u32 off) { return *(volatile const u32*)(uintptr_t)(base + off); }
int Milli(float v) { return (int)(v * 1000.f); }

u32 PlayerObject() {
    u32 mgr = U(kObjectMgrVa, 0);
    u32 handle = U(kPlayerHandleVa, 0);
    if (!mgr || !handle) return 0;
    return (u32)(uintptr_t)((LookupFn)(uintptr_t)kLookupFn)((void*)(uintptr_t)mgr, nullptr, handle);
}

}  // namespace

bool PlayerStateNow(u32* state) {
    u32 player = PlayerObject();
    if (!player) return false;
    *state = U(player, 0x654);
    return true;
}

// Numpad 2 (the game in front, the menu closed): "the camera went wrong here". One full line of the camera, outside the
// budget of the trace (at most 200 marks), so the moment can be found in the log.
void CameraMark() {
    static bool was = false;
    static int marks = 0;
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (fg) GetWindowThreadProcessId(fg, &pid);
    const bool now = pid == GetCurrentProcessId() && !ui::IsOpen() && (GetAsyncKeyState(VK_NUMPAD2) & 0x8000);
    const bool press = now && !was;
    was = now;
    if (!press || marks >= 200) return;
    ++marks;
    const u32 cam = U(kCameraPtrVa, 0), player = PlayerObject(), mgr = U(kGameMgrPtrVa, 0);
    Log("camera mark %d: frame %u eye %d %d %d target %d %d %d (x1000); camera mode %u (previous %u); player state %u, "
        "position %d %d %d; scripted request %u kind %u; cutscene mode %u",
        marks, g_frame, Milli(F(kRenderCameraVa, 0x4)), Milli(F(kRenderCameraVa, 0x8)), Milli(F(kRenderCameraVa, 0xC)),
        Milli(F(kRenderCameraVa, 0x14)), Milli(F(kRenderCameraVa, 0x18)), Milli(F(kRenderCameraVa, 0x1C)),
        cam ? U(cam, 0x154) : 0u, cam ? U(cam, 0x158) : 0u, player ? U(player, 0x654) : 0u, player ? Milli(F(player, 0x58)) : 0,
        player ? Milli(F(player, 0x5C)) : 0, player ? Milli(F(player, 0x60)) : 0, mgr ? U(mgr, kReqFlagOff) : 0u,
        mgr ? U(mgr, kReqKindOff) : 0u, U(kEventFlagsVa, 0) >> 31);
    ShowNotice(L"Позначку камери записано", L"Camera mark written", L"Рядок «camera mark» в UltimateFix\\ufx.log",
               L"A \"camera mark\" line in UltimateFix\\ufx.log", 2);
}

// Diagnostic only (rumble and the adaptive triggers have their own calls in the Present hook).
void TraceCameraFrame() {
    if (!g_cfg.traceCamera) return;
    CameraMark();
    if (g_lines >= kMaxLines) return;
    ++g_frame;
    const u32 event = U(kEventFlagsVa, 0) >> 31;

    u32 player = PlayerObject();
    if (player) {
        u32 state = U(player, 0x654);
        if (state != g_lastState && Budget()) {
            Log("player: frame %u state %u -> %u (previous field %u); position %d %d %d (x1000), cutscene mode %u",
                g_frame, g_lastState == 0xFFFFFFFF ? 0u : g_lastState, state, U(player, 0x658), Milli(F(player, 0x58)),
                Milli(F(player, 0x5C)), Milli(F(player, 0x60)), event);
        }
        g_lastState = state;
    }

    // While driving (player state 87h) log what the game itself sees from the pad, once a second by the clock
    // (not by frames: the frame rate is not always 60).
    DWORD now = GetTickCount();
    if (g_lastState == 0x87 && now - g_lastCarLog >= 1000) {
        g_lastCarLog = now;
        u32 in = U(kInputSingletonVa, 0);
        if (in && Budget()) {
            u32 slot = in + 0xC8;
            Log("car input: pad mode %u held %08X edge %08X LT %d RT %d LX %d LY %d RX %d RY %d (x100)",
                (unsigned)*(volatile u8*)(uintptr_t)0x014810F0, U(slot, 0x4), U(slot, 0x8), (int)(F(slot, 0x14) * 100.f),
                (int)(F(slot, 0x18) * 100.f), (int)(F(slot, 0x1C) * 100.f), (int)(F(slot, 0x20) * 100.f),
                (int)(F(slot, 0x24) * 100.f), (int)(F(slot, 0x28) * 100.f));
        }
    }
    u32 mgr = U(kGameMgrPtrVa, 0);
    if (mgr) {
        u32 req[3] = {U(mgr, kReqFlagOff), U(mgr, kReqKindOff), U(mgr, kReqConsumedOff)};
        if ((req[0] != g_lastReq[0] || req[1] != g_lastReq[1] || req[2] != g_lastReq[2]) && Budget()) {
            Log("scripted camera: frame %u request %u kind %u pending %u; position %d %d %d %d (x1000), player state %u, "
                "cutscene mode %u",
                g_frame, req[0], req[1], req[2], Milli(F(mgr, kReqPosOff)), Milli(F(mgr, kReqPosOff + 4)),
                Milli(F(mgr, kReqPosOff + 8)), Milli(F(mgr, kReqPosOff + 12)),
                g_lastState == 0xFFFFFFFF ? 0u : g_lastState, event);
        }
        g_lastReq[0] = req[0]; g_lastReq[1] = req[1]; g_lastReq[2] = req[2];
    }

    u32 cam = U(kCameraPtrVa, 0);
    if (cam) {
        u32 mode = U(cam, 0x154);
        if (mode != g_lastMode && Budget())
            Log("camera: frame %u mode %u -> %u (previous field %u), cutscene mode %u", g_frame,
                g_lastMode == 0xFFFFFFFF ? 0u : g_lastMode, mode, U(cam, 0x158), event);
        g_lastMode = mode;
    }

    float eye[3] = {F(kRenderCameraVa, 0x4), F(kRenderCameraVa, 0x8), F(kRenderCameraVa, 0xC)};
    if (g_haveEye) {
        float dx = eye[0] - g_lastEye[0], dy = eye[1] - g_lastEye[1], dz = eye[2] - g_lastEye[2];
        if (dx * dx + dy * dy + dz * dz > 10.f * 10.f && Budget())
            Log("camera: frame %u eye jumped %d %d %d (x1000) to %d %d %d, target %d %d %d, mode %u, player state %u, "
                "cutscene mode %u",
                g_frame, Milli(dx), Milli(dy), Milli(dz), Milli(eye[0]), Milli(eye[1]), Milli(eye[2]),
                Milli(F(kRenderCameraVa, 0x14)), Milli(F(kRenderCameraVa, 0x18)), Milli(F(kRenderCameraVa, 0x1C)),
                g_lastMode == 0xFFFFFFFF ? 0u : g_lastMode, g_lastState == 0xFFFFFFFF ? 0u : g_lastState, event);
    }
    g_lastEye[0] = eye[0]; g_lastEye[1] = eye[1]; g_lastEye[2] = eye[2];
    g_haveEye = true;
}

}  // namespace ufx
