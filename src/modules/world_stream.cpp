// The queue of world objects waiting to be built.
//
// SetThread builds the objects of the open world (buildings among them) from a queue of 4096 requests at 00894570 (count at
// 008A4578), a millisecond per frame, and only while its "go" flag (00894560) is set. A queue that only grows means the
// thread is starved: the town stays without buildings, and when all 4096 cells are taken the main thread spins for ever
// looking for a free one (the mod's own SerialSetThread did exactly that from 0.7.1 to 0.7.13, see stability.cpp). This
// writes a line to the log when 256 or more requests wait, so a user's log shows it.
#include "../base.h"

namespace ufx {

void WorldStreamFrame() {
    const u32 waiting = *(volatile u32*)(uintptr_t)0x8A4578;
    static u32 worst = 0, said = 0;
    static DWORD last = 0;
    const DWORD t = GetTickCount();
    if (waiting > 4096) return;
    if (waiting > worst) worst = waiting;
    if (waiting >= 256 && said < 20 && t - last > 5000) {
        last = t;
        ++said;
        Log("world build: %u of 4096 object requests wait for SetThread (most so far %u, go flag %u)", waiting, worst,
            *(volatile u32*)(uintptr_t)0x894560);
    }
}

}  // namespace ufx
