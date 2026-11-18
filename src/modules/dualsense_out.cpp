// DualSense output over HID: rumble and adaptive trigger effects. A background thread keeps the device open (USB or
// Bluetooth), and sends a report whenever what is wanted changes, and once a second anyway. The report layouts are in
// dsreport.h. The game thread only stores what it wants (cheap, no I/O on the render thread).
#include "../base.h"
#include "../dsreport.h"

namespace ufx {

bool DsApisReady();
void* DsOpen(int* inLen, int* outLen, int* featLen, unsigned short* product);
bool DsGetFeature(void* handle, unsigned char* buf, unsigned long len);
bool DualSenseConnected();
bool DualSenseReaderRunning();

namespace {

CRITICAL_SECTION g_cs;
DsOutput g_wanted;
LONG g_wantedGen = 0;          // increased (under g_cs) whenever g_wanted changes
volatile LONG g_sentGen = 0;   // the generation of the last report written
HANDLE g_event = nullptr;
volatile LONG g_open = 0;
u8 g_seq = 0;
int g_writeErrors = 0;

bool Equal(const DsOutput& a, const DsOutput& b) {
    if (a.rightEffect != b.rightEffect || a.leftEffect != b.leftEffect || a.rumble != b.rumble) return false;
    if (a.motorStrong != b.motorStrong || a.motorWeak != b.motorWeak) return false;
    for (int i = 0; i < 11; ++i)
        if (a.right.data[i] != b.right.data[i] || a.left.data[i] != b.left.data[i]) return false;
    return true;
}

DWORD WINAPI Writer(void*) {
    HANDLE h = nullptr;
    int outLen = 0;
    bool bt = false;
    DsOutput last;
    bool haveLast = false;
    bool saidBt = false;
    for (;;) {
        if (!h) {
            // With the reader running, its search already says whether a DualSense is there: no second search of all HID devices.
            if (DualSenseReaderRunning() && !DualSenseConnected()) {
                Sleep(1000);
                continue;
            }
            int inLen = 0, featLen = 0;
            unsigned short product = 0;
            h = (HANDLE)DsOpen(&inLen, &outLen, &featLen, &product);
            if (!h) {
                Sleep(1000);
                continue;
            }
            bt = inLen >= 70 || outLen >= 70;
            // Output over Bluetooth needs the full mode, and in the full mode the pad sends only report 31h, which WinMM and
            // DirectInput do not understand: the pad would freeze for the game. Only our own reader parses 31h.
            if (bt && !(g_cfg.nativeDualSense && DualSenseReaderRunning())) {
                if (!saidBt) {
                    saidBt = true;
                    Log("dualsense output: Bluetooth pad left alone (rumble and trigger effects over Bluetooth need NativeDualSense=1)");
                }
                CloseHandle(h);
                h = nullptr;
                Sleep(5000);
                continue;
            }
            if (outLen < 48 || outLen > 200) outLen = bt ? 78 : 64;
            if (bt) {  // asks for the calibration report once: this switches a Bluetooth pad to its full mode (the reader asks too)
                u8 feat[64] = {0x05};
                DsGetFeature(h, feat, featLen > 0 && featLen <= 64 ? (unsigned long)featLen : 41);
            }
            InterlockedExchange(&g_open, 1);
            haveLast = false;
            Log("dualsense output: opened (%s, output report %d bytes)", bt ? "Bluetooth" : "USB", outLen);
        }
        WaitForSingleObject(g_event, 1000);
        DsOutput now;
        EnterCriticalSection(&g_cs);
        now = g_wanted;
        const LONG gen = g_wantedGen;
        LeaveCriticalSection(&g_cs);
        u8 buf[256];
        size_t len = bt ? DsBuildBt(buf, g_seq++, now) : DsBuildUsb(buf, (size_t)outLen, now);
        DWORD wrote = 0;
        if (!WriteFile(h, buf, (DWORD)len, &wrote, nullptr)) {
            DWORD e = GetLastError();
            if (g_writeErrors++ < 4) Log("dualsense output: write failed (error %u)", (unsigned)e);
            CloseHandle(h);
            h = nullptr;
            InterlockedExchange(&g_open, 0);
            Sleep(500);
            continue;
        }
        InterlockedExchange(&g_sentGen, gen);
        if (now.rumble && !now.motorStrong && !now.motorWeak) {  // the zeros are out: later reports leave the motors alone
            EnterCriticalSection(&g_cs);
            if (g_wanted.rumble && !g_wanted.motorStrong && !g_wanted.motorWeak) g_wanted.rumble = false;
            LeaveCriticalSection(&g_cs);
        }
        if (!haveLast || !Equal(last, now)) {
            static int logged = 0;
            if (logged++ < 12)
                Log("dualsense output: sent right %s %02X, left %s %02X, rumble %s", now.rightEffect ? "effect" : "-", now.right.data[0],
                    now.leftEffect ? "effect" : "-", now.left.data[0], now.rumble ? "on" : "off");
        }
        last = now;
        haveLast = true;
    }
}

}  // namespace

bool DsOutputReady() { return g_open != 0; }

void DsSetEffects(bool useRight, const TriggerEffect& right, bool useLeft, const TriggerEffect& left) {
    if (!g_event) return;
    DsOutput next;
    EnterCriticalSection(&g_cs);
    next = g_wanted;
    next.rightEffect = useRight;
    next.right = right;
    next.leftEffect = useLeft;
    next.left = left;
    bool changed = !Equal(next, g_wanted);
    g_wanted = next;
    if (changed) ++g_wantedGen;
    LeaveCriticalSection(&g_cs);
    if (changed) SetEvent(g_event);
}

void DsSetRumble(u8 strong, u8 weak) {
    if (!g_event) return;
    EnterCriticalSection(&g_cs);
    bool on = strong || weak;
    bool changed = g_wanted.motorStrong != strong || g_wanted.motorWeak != weak || (on && !g_wanted.rumble);
    // When the motors stop, the flag stays on until the writer has sent one report with zeros; then the writer clears it.
    g_wanted.rumble = on || g_wanted.rumble;
    g_wanted.motorStrong = strong;
    g_wanted.motorWeak = weak;
    if (changed) ++g_wantedGen;
    LeaveCriticalSection(&g_cs);
    if (changed) SetEvent(g_event);
}

// Motors off and trigger effects off, and waits up to `waitMs` until the writer has sent that: on close the process may
// end right after, and the pad would keep the last weapon resistance until it is switched off.
void DsStopAll(unsigned waitMs) {
    if (!g_event || !g_open) return;
    DsSetRumble(0, 0);
    EnterCriticalSection(&g_cs);
    const bool right = g_wanted.rightEffect, left = g_wanted.leftEffect;  // only the triggers we drive: the others stay as they are
    LeaveCriticalSection(&g_cs);
    DsSetEffects(right, EffectOff(), left, EffectOff());
    EnterCriticalSection(&g_cs);
    const LONG target = g_wantedGen;
    LeaveCriticalSection(&g_cs);
    for (unsigned t = 0; t < waitMs && g_open && g_sentGen - target < 0; t += 5) Sleep(5);
}

bool InstallDualSenseOutput() {
    if (!DsApisReady()) {
        Log("dualsense output: hid.dll / setupapi.dll are not available");
        return false;
    }
    InitializeCriticalSection(&g_cs);
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE t = g_event ? CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr) : nullptr;
    if (!t) return false;
    CloseHandle(t);
    Log("dualsense output: writer started (rumble and adaptive triggers for a DualSense over USB or Bluetooth)");
    return true;
}

}  // namespace ufx
