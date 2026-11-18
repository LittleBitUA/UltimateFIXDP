// Small input fixes: lower latency (poll before commit); mouse look speed and inversion.
#include "../base.h"
#include "../sites.h"
#include "../stubs.h"

namespace ufx {
namespace {

bool g_latencyReady = false;
// read by the mouse stubs every time the game turns the movement into a stick value
volatile float g_mouseScale[2] = {1.0f, 1.0f};

bool PatchMouse(const SiteDef& s, u32 movementVa, volatile float* scale) {
    if (!BytesMatch(s.va, s.bytes, s.len)) return false;
    u8* stub = AllocStub(16);
    if (!stub) return false;
    BuildMouseScaleStub(stub, movementVa, (u32)(uintptr_t)scale);
    return PatchCall(s.va, s.bytes, s.len, stub);
}

}  // namespace

// Both calls change together; the two writes happen between ticks (on the game thread, from the menu code that
// runs inside the frame's Present), never in the middle of the tick.
void ApplyInputExtras() {
    const float speed = (float)g_cfg.mouseSensitivity / 100.0f;
    g_mouseScale[0] = g_cfg.invertMouseX ? -speed : speed;
    g_mouseScale[1] = g_cfg.invertMouseY ? -speed : speed;
    if (!g_latencyReady) return;
    const bool on = g_cfg.lowLatencyInput != 0;
    WriteCode(kSiteInputCommit.va, on ? kInputCommitSwapped : kSiteInputCommit.bytes, 5);
    WriteCode(kSiteInputPoll.va, on ? kInputPollSwapped : kSiteInputPoll.bytes, 5);
}

bool InstallInputExtras() {
    ApplyInputExtras();
    if (PatchMouse(kSiteMouseX, kMouseDxVa, &g_mouseScale[0]) && PatchMouse(kSiteMouseY, kMouseDyVa, &g_mouseScale[1]))
        Log("input: mouse look %d%%%s%s (switchable in the menu)", g_cfg.mouseSensitivity, g_cfg.invertMouseX ? ", x inverted" : "",
            g_cfg.invertMouseY ? ", y inverted" : "");
    else
        Log("input: the mouse readers differ from DP.exe 1.01b, mouse speed and inversion skipped");
    if (BytesMatch(kSiteInputCommit.va, kSiteInputCommit.bytes, 5) && BytesMatch(kSiteInputPoll.va, kSiteInputPoll.bytes, 5)) {
        g_latencyReady = true;
        ApplyInputExtras();
        Log("input: low-latency polling %s (switchable in the menu)", g_cfg.lowLatencyInput ? "on" : "off");
        return true;
    }
    Log("input: tick call sites differ from DP.exe 1.01b, low-latency polling skipped");
    return false;
}

}  // namespace ufx
