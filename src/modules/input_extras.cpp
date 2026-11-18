// Small input fixes: lower latency (poll before commit).
#include "../base.h"
#include "../sites.h"

namespace ufx {
namespace {

bool g_latencyReady = false;

}  // namespace

// Both calls change together; the two writes happen between ticks (on the game thread, from the menu code that
// runs inside the frame's Present), never in the middle of the tick.
void ApplyInputExtras() {
    if (!g_latencyReady) return;
    const bool on = g_cfg.lowLatencyInput != 0;
    WriteCode(kSiteInputCommit.va, on ? kInputCommitSwapped : kSiteInputCommit.bytes, 5);
    WriteCode(kSiteInputPoll.va, on ? kInputPollSwapped : kSiteInputPoll.bytes, 5);
}

bool InstallInputExtras() {
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
