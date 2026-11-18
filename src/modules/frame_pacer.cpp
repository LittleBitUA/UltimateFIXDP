// Frame pacing: replaces the game's frame-time call (00700A1A). Mode 1 (the default) keeps the real time, holds the frames
// to 60 per second on average in front of the Present, and hands the game a short delta already rounded up to a whole step,
// so the game's own rounding never adds up. Modes 2 and 3 hand the game a virtual clock that advances in whole steps of
// 1/60 s (30 or 60 frames per second). Mode 0 delegates to the original function.
#include "../base.h"
#include "../sites.h"
#include "../pacer.h"

namespace ufx {

namespace {

typedef double(__cdecl* RealTimeFn)(int);
RealTimeFn const RealTime = (RealTimeFn)(uintptr_t)kRealTimeFn;

PacerState g_state;
bool g_active = false;      // the virtual clock is running
volatile double g_base = 0; // game seconds at the moment the clock started
volatile double g_last = 0; // last value handed to the game (never goes backwards)
HANDLE g_timer = nullptr;
bool g_timerTried = false;
u32 g_hist[5];
u32 g_frames = 0;
const double kStepEpsilon = 1e-8;  // keeps every step a hair above k/60 after the game rounds the delta to float

u64 Qpc() {
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return (u64)v.QuadPart;
}

void WaitUntil(u64 target, u64 freq) {
    if (!g_timerTried) {
        g_timerTried = true;
        g_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, 0x1F0003);
        Log("pacer: %s timer", g_timer ? "high-resolution" : "no high-resolution");
    }
    // A high-resolution timer wakes within about half a millisecond: sleep to 0.7 ms before the target and spin only the rest.
    // Without one, Sleep can be a whole millisecond late, so 1.5 ms are left for spinning.
    const u64 spinUs = g_timer ? 700 : 1500;
    for (;;) {
        u64 now = Qpc();
        if (now >= target) return;
        u64 left = target - now;                     // ticks
        u64 leftUs = left * 1000000 / freq;
        if (leftUs > spinUs + 500) {
            u64 sleepUs = leftUs - spinUs;           // leave the rest for spinning
            if (g_timer) {
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)(sleepUs * 10);
                if (SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE)) {
                    WaitForSingleObject(g_timer, 100);
                    continue;
                }
            }
            Sleep(leftUs > 4000 ? (DWORD)((leftUs - 3000) / 1000) : 1);
        } else {
            YieldProcessor();
        }
    }
}

// base + num / den + num * perStep, computed with the x87 precision control set to 53 bits. Direct3D leaves the FPU in
// 24-bit mode, where every result is cut to 24 bits: after an hour of play a clock value in seconds keeps only half a
// millisecond. The inputs and the result pass through volatile memory so no arithmetic leaves the 53-bit block.
double Seconds53(double base, double num, double den, double perStep) {
    volatile double inBase = base, inNum = num, inDen = den, inStep = perStep;
    volatile double out;
    unsigned short oldCw, newCw;
    asm volatile("fnstcw %0" : "=m"(oldCw)::"memory");
    newCw = (unsigned short)(oldCw | 0x0300);
    asm volatile("fldcw %0" ::"m"(newCw) : "memory");
    double s = inNum;
    out = inBase + s / inDen + s * inStep;
    asm volatile("fldcw %0" ::"m"(oldCw) : "memory");
    return out;
}

double Add53(double a, double b) { return Seconds53(a, b, 1.0, 0.0); }
double Sub53(double a, double b) { return Seconds53(a, -b, 1.0, 0.0); }

// Seconds the game sees in the whole-step modes: base + steps / 60 (+ epsilon per step).
double VirtualSeconds(u64 steps) { return Seconds53(g_base, (double)(long long)steps, 60.0, kStepEpsilon); }

}  // namespace

// Mode 1: a frame limit (the wait itself is in PacerBeforePresent, right before the picture is shown). The game keeps real time; the
// only change to its delta is the round-up of a short frame to the whole step the game would round to anyway (FrameClock,
// PacerRoundUpSteps), so its own rounding never adds up. The seconds are counted here from the counter with
// 53 bits (the game's own division runs in 24 bits, see Seconds53), starting from the game's value when the mode starts.
static bool g_limActive = false;
static double g_limBase = 0;
static u64 g_limQ0 = 0, g_limFreq = 1;

int DisplayRefreshHz();

// The mode in force. "No limit" (0) is kept only where it is safe: the game rounds a frame of 0.5..0.99 steps up to a whole step
// (0x700B92), so on a screen of 61..139 Hz an unlimited game runs everything too fast (75 Hz: x1.25, 120 Hz: x2). There the limit of
// mode 1 is used instead; from 140 Hz up the frames are below half a step and are not rounded (HighFpsPhysics handles the car).
static int PacingMode() {
    int mode = g_cfg.framePacing;
    if (mode != 0) return mode;
    int hz = DisplayRefreshHz();
    static int said = -1;
    bool keep = hz > 60 && hz < 140;
    if ((int)keep != said) {
        said = (int)keep;
        Log(keep ? "pacer: no frame limit was chosen, but at %d Hz the game would run too fast: the 60 frames/s limit stays on"
                 : "pacer: no frame limit (%d Hz)", hz);
    }
    return keep ? 1 : 0;
}

static double FrameClockInner(int arg) {
    int mode = PacingMode();
    if (arg == 0 && mode == 1) {
        if (!g_limActive) {
            g_limActive = true;
            double t0 = RealTime(0);
            g_limBase = t0 < g_last ? g_last : t0;
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            g_limFreq = f.QuadPart > 0 ? (u64)f.QuadPart : 1;
            g_limQ0 = Qpc();
            Log("pacer: frame limit 60 frames/s, the game keeps real time");
        }
        g_active = false;
        double t = Seconds53(g_limBase, (double)(long long)(Qpc() - g_limQ0), (double)(long long)g_limFreq, 0.0);
        if (t < g_last) t = g_last;
        g_last = t;
        return t;
    }
    g_limActive = false;
    if (arg != 0 || mode <= 0 || mode > 3) {
        double t = RealTime(arg);
        if (arg == 0) {
            if (g_active) {
                // Back to real time: never hand the game an earlier time than the last one.
                g_active = false;
                if (t < g_last) t = g_last;
                Log("pacer: switched off");
            }
            g_last = t;
        }
        return t;
    }
    const int minSteps = mode == 2 ? 2 : 1;  // mode 2 = 30 frames/s like the console, mode 3 = whole steps at 60
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);

    if (!g_active) {
        double t = RealTime(0);
        if (t < g_last) t = g_last;
        g_base = t;
        g_state = PacerState();
        PacerStart(g_state, (u64)f.QuadPart, Qpc());
        g_active = true;
        g_last = t;
        Log("pacer: %s", mode == 2 ? "30 frames/s, whole steps of 1/60 s" : "up to 60 frames/s, whole steps of 1/60 s");
        return t;
    }
    WaitUntil(PacerTarget(g_state, minSteps), g_state.freq);
    int k = PacerAdvance(g_state, Qpc(), minSteps);
    ++g_hist[k];
    if (++g_frames % 3600 == 0)
        Log("pacer: steps per frame over the last %d frames: 1:%u 2:%u 3:%u 4:%u (stalls cut: %u)", 3600, g_hist[1],
            g_hist[2], g_hist[3], g_hist[4], (u32)g_state.stalls);
    if (g_frames % 3600 == 0) g_hist[1] = g_hist[2] = g_hist[3] = g_hist[4] = 0;
    double t = VirtualSeconds(g_state.total);
    if (t < g_last) t = g_last;
    g_last = t;
    return t;
}

// The game's time stands still while the F10 menu is open (when the option is on): the seconds spent in the menu are
// cut out of the clock the game sees, so nothing moves and nothing jumps when the menu closes.
static volatile int g_thaw = 0;  // frames during which the game's time runs even in a pause (loads started from the photo mode)
void PhotoThaw(int frames) { g_thaw = frames; }
static bool g_paused = false;
static double g_pauseStart = 0, g_pauseCut = 0;

// An experiment (AutoRecover, off by default): when the frames have been slow for a couple of seconds, the game clock and the PhysX
// step are held for a few frames, as the F10 menu does with PauseInMenu. It is not proven to help: the frame rate that came back
// with the F10 menu was traced to the pad poll, which is skipped while the menu is open (ThrottleMissingPad deals with that).
static int g_slowFrames = 0, g_recoverFrames = 0;
static u64 g_lastFrameQ = 0, g_lastRecoverQ = 0;

static bool RecoverTick() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    u64 q = Qpc();
    double ms = g_lastFrameQ ? (double)(q - g_lastFrameQ) * 1000.0 / (double)f.QuadPart : 0.0;
    g_lastFrameQ = q;
    if (g_recoverFrames > 0) { --g_recoverFrames; return true; }
    if (!g_cfg.autoRecover || ui::IsOpen() || g_cfg.photoFreeze) { g_slowFrames = 0; return false; }
    // Slow = 1.8 frames of the mode's own rate (30 ms at 60, 60 ms in the 30 frames/s mode, where every frame lasts 33 ms by design).
    double slowMs = PacingMode() == 2 ? 60.0 : 30.0;
    if (ms > slowMs && ms < 250.0) ++g_slowFrames; else g_slowFrames = 0;   // longer gaps are loading screens
    if (g_slowFrames >= 60 && (g_lastRecoverQ == 0 || q - g_lastRecoverQ > 20ull * (u64)f.QuadPart)) {
        g_slowFrames = 0;
        g_lastRecoverQ = q;
        g_recoverFrames = 4;
        Log("pacer: recover: %d ms per frame for a while, holding the game clock and physics for 4 frames", (int)ms);
        return true;
    }
    return false;
}

// The refresh rate of the monitor the game window is on (not the primary one). The driver is asked only when the window has moved to
// another monitor, or once a minute (a mode change); otherwise this is a cached value, checked against the window once a second.
static int g_hzCached = 60;
static HMONITOR g_hzMonitor = nullptr;
static DWORD g_hzChecked = 0, g_hzQueried = 0;

int DisplayRefreshHz() {
    DWORD now = GetTickCount();
    if (g_hzChecked && now - g_hzChecked < 1000) return g_hzCached;
    g_hzChecked = now | 1;
    HMONITOR mon = MonitorFromWindow(GameWindowHandle(), MONITOR_DEFAULTTOPRIMARY);
    if (mon == g_hzMonitor && g_hzQueried && now - g_hzQueried < 60000) return g_hzCached;
    g_hzMonitor = mon;
    g_hzQueried = now | 1;
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    const wchar_t* device = mon && GetMonitorInfoW(mon, &mi) ? mi.szDevice : nullptr;
    DEVMODEW dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;
    if (EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency >= 30 && dm.dmDisplayFrequency <= 500)
        g_hzCached = (int)dm.dmDisplayFrequency;
    return g_hzCached;
}

// The 60 frames/s limit of mode 1, placed in front of the Present (the arithmetic is LimiterState in pacer.h, tested on the host).
// Without vsync the Presents follow a fixed grid of 1/60 s; when the Present is seen to wait for the screen refresh, each frame is held
// for a whole number of refreshes (hz/60 on average), so the average is 60 frames/s on any refresh rate.
static LimiterState g_lim;
static bool g_limOn = false;

void PacerBeforePresent() {
    if (PacingMode() != 1) { g_limOn = false; return; }
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    u64 freq = (u64)f.QuadPart;
    if (!g_limOn) {
        LimiterStart(g_lim, freq);
        g_limOn = true;
    }
    LimiterSetHz(g_lim, (u32)DisplayRefreshHz());
    u64 target = LimiterTarget(g_lim);
    if (target) WaitUntil(target, freq);
    LimiterIssue(g_lim, Qpc());
}

void PacerAfterPresent(u64 returnedAt, double presentMs) {
    if (PacingMode() != 1 || !g_limOn) return;
    bool was = g_lim.vsync;
    u64 presentTicks = presentMs > 0 ? (u64)(long long)(presentMs * (double)(long long)g_lim.freq / 1000.0) : 0;
    LimiterReturned(g_lim, returnedAt, presentTicks);
    if (g_lim.vsync != was)
        Log(g_lim.vsync ? "pacer: the Present waits for the refresh (%d Hz): frames are held for whole refreshes"
                        : "pacer: the Present does not wait for the refresh as assumed (%d Hz): back to the 1/60 s grid",
            (int)g_lim.hz);
}

// The clock the game sees must never jump when the mode is changed in the menu. The whole-step clock (modes 2 and 3) cuts long stalls
// (loading screens) out of its time, so after a while it runs behind the real time by seconds or minutes; switching to the real time
// (modes 0 and 1) would hand the game that whole difference as one frame step and the game would spin through it ("not responding").
// So at a change the value last handed out is the new starting point.
static double g_lastOut = 0;
static bool g_haveOut = false;
static int g_prevMode = -1;

static double FrameClockOut(int arg) {
    double raw = FrameClockInner(arg);
    if (arg != 0) return raw;
    int mode = PacingMode();
    if (g_prevMode >= 0 && mode != g_prevMode && g_haveOut) {
        double basis = g_paused ? g_pauseStart : raw;
        double jump = Sub53(Sub53(basis, g_pauseCut), g_lastOut);
        if (jump > 0.05 || jump < -0.05) Log("pacer: mode %d -> %d, the two clocks differ by %d ms: the game clock continues from where it was", g_prevMode, mode, (int)(jump * 1000.0));
        g_pauseCut = Sub53(raw, g_lastOut);
        if (g_paused) g_pauseStart = raw;
    }
    g_prevMode = mode;
    bool pause = (g_cfg.pauseInMenu && ui::IsOpen()) || g_cfg.photoFreeze;
    if (RecoverTick()) pause = true;
    if (g_thaw > 0) {  // a costume or pose request is being carried out: the loaders and the game need time to move
        --g_thaw;
        pause = false;
    }
    SetPhysicsFrozen(pause);  // the PhysX step stands still with the game clock
    if (pause) {
        if (!g_paused) {
            g_paused = true;
            g_pauseStart = raw;
        }
        return Sub53(g_pauseStart, g_pauseCut);
    }
    if (g_paused) {
        g_paused = false;
        g_pauseCut = Add53(g_pauseCut, Sub53(raw, g_pauseStart));
    }
    return Sub53(raw, g_pauseCut);
}

double __cdecl FrameClock(int arg) {
    double out = FrameClockOut(arg);
    if (arg == 0) {
        // Mode 1 keeps the real time, but the game rounds a delta of 0.5..0.99 steps up to a whole step (pacer.h, PacerRoundUpSteps):
        // hand it the rounded value at once and measure the next delta from there, so the rounding never adds up.
        if (PacingMode() == 1 && g_haveOut) {
            int k = PacerRoundUpSteps(Sub53(out, g_lastOut) * 60.0);
            if (k >= 0) out = Seconds53(g_lastOut, (double)k, 60.0, kStepEpsilon);  // a hair above k/60, as in the whole-step modes
        }
        g_lastOut = out;
        g_haveOut = true;
    }
    return out;
}

bool InstallFramePacer() {
    const SiteDef& s = kSiteFrameClockCall;
    if (!BytesMatch(s.va, s.bytes, s.len)) {
        Log("pacer: bytes at %08X differ from DP.exe 1.01b, skipped", s.va);
        return false;
    }
    if (!PatchCall(s.va, s.bytes, s.len, (const void*)FrameClock)) return false;
    Log("pacer: frame clock hooked, mode %d", PacingMode());
    return true;
}

}  // namespace ufx
