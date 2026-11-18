// Window and picture presentation, and the per-frame layer of the mod.
//
// The one Present call in the game (mov ecx,[edx+44h] / call ecx) is always replaced by a call to our function:
// the F10 menu, the frame limit, rumble and adaptive triggers, the waypoint, the post filters, the statistics and
// the hang watchdog all run from it, whatever the window settings are. With Borderless=1 it also changes how the
// finished picture reaches the screen:
//   * the window becomes a borderless popup that fills the monitor;
//   * the back buffer (1280x720, or the output resolution when that module is active) is presented as it is when
//     it already has the size of the window. Otherwise the game's own Present() would stretch it over the whole
//     client area without keeping 16:9 (ScaleToWindow=0 leaves it at that), so we scale it ourselves: a child
//     window covers the client area, an additional swap chain is attached to it, and every frame the finished back
//     buffer is StretchRect'ed (bilinear) into that chain's back buffer, keeping 16:9, and the chain is presented
//     instead of the game's;
//   * the mouse cursor is hidden over the window.
// This works on top of any Direct3D 9 implementation (native, DXVK, dgVoodoo). If anything about the extra chain
// fails, the child window is hidden and the game's own Present is used.
#include "../base.h"
#include "../sites.h"
#include "../ui/d3d9.h"

namespace ufx {

FitRect g_fit = {0, 0, 1280, 720};
HWND GameWindowHandle() { return *(HWND*)(uintptr_t)kGameWindowVa; }
extern bool g_dpfixPresent;  // main.cpp: DPfix manages the window, so the borderless switch stays off
void DetectD3dLibrary();     // d3d/trace.cpp: names the Direct3D 9 library in the log and the stats panel

namespace {

// Size of the picture the game hands us: 1280x720, or the output resolution when that module is active.
int LogicalW() { return g_outputW ? g_outputW : 1280; }
int LogicalH() { return g_outputH ? g_outputH : 720; }
constexpr u32 kTexfPoint = 1, kTexfLinear = 2;

LRESULT(WINAPI* r_DefWindowProcA)(HWND, UINT, WPARAM, LPARAM) = nullptr;
bool g_switched = false;
int g_frames = 0;

// --- the extra swap chain ---------------------------------------------------------------------------
HWND g_view = nullptr;       // child window that shows the chain
void* g_chain = nullptr;     // IDirect3DSwapChain9*
void* g_chainBB = nullptr;   // its back buffer, IDirect3DSurface9*
int g_chainW = 0, g_chainH = 0;
int g_chainFailures = 0;     // failures in a row; a successful scaled Present or a device Reset starts again from 0
bool g_chainGaveUpLogged = false;  // the "failed 3 times" line was written since the last device Reset
u32 g_filter = kTexfLinear;
bool g_classRegistered = false;
// The loading screen thread presents too (FUN_0040A860 calls the same Present). The chain is created, released and
// used under this lock, so a Reset or a failure on one thread never frees it while the other is presenting through
// it. Only the window thread creates the chain or shows and hides the child window. Initialised in InstallDisplay,
// which runs before the first Present.
CRITICAL_SECTION g_chainLock;
struct ChainGuard {
    ChainGuard() { EnterCriticalSection(&g_chainLock); }
    ~ChainGuard() { LeaveCriticalSection(&g_chainLock); }
};
LARGE_INTEGER g_qpf = {};  // QueryPerformanceFrequency, fixed while Windows runs: read once in InstallDisplay

double Ms(long long ticks) { return (double)ticks * 1000.0 / (double)g_qpf.QuadPart; }

LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;  // the mouse belongs to the game window underneath
    if (m == WM_ERASEBKGND) return 1;             // never paint over the presented picture
    return DefWindowProcW(h, m, w, l);
}

typedef ULONG(WINAPI* ReleaseFn)(void*);

void ReleaseChain() {
    ChainGuard lock;
    if (g_chainBB) {
        d3d::Method<ReleaseFn>(g_chainBB, d3d::kRelease)(g_chainBB);
        g_chainBB = nullptr;
    }
    if (g_chain) {
        d3d::Method<ReleaseFn>(g_chain, d3d::kRelease)(g_chain);
        g_chain = nullptr;
    }
    g_chainW = g_chainH = 0;
}

// Window thread only: this frame goes out through the game's own Present, so the child window must not cover the
// game window any more, and the extra chain is no longer needed.
void DropChain(const char* why) {
    bool had = g_chain || (g_view && IsWindowVisible(g_view));
    ReleaseChain();
    if (g_view && IsWindowVisible(g_view)) ShowWindow(g_view, SW_HIDE);
    if (had && why) Log("display: %s", why);
}

bool EnsureView(HWND game, int cw, int ch) {
    if (!g_classRegistered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ViewProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"UltimateFixView";
        if (!RegisterClassExW(&wc)) return false;
        g_classRegistered = true;
    }
    if (!g_view) {
        g_view = CreateWindowExW(0, L"UltimateFixView", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, cw, ch,
                                 game, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_view) return false;
    } else {
        SetWindowPos(g_view, HWND_TOP, 0, 0, cw, ch, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    return true;
}

bool EnsureChain(void* dev, HWND game, int cw, int ch) {
    ChainGuard lock;
    if (g_chain && g_chainW == cw && g_chainH == ch) return true;
    if (g_chainFailures >= 3) {
        if (!g_chainGaveUpLogged) {
            g_chainGaveUpLogged = true;
            Log("display: the extra swap chain failed 3 times in a row, the game's own Present is used until the next device reset");
        }
        return false;
    }
    ReleaseChain();
    if (!EnsureView(game, cw, ch)) {
        ++g_chainFailures;
        Log("display: could not create the view window");
        return false;
    }
    u32 pp[14];
    ZeroMemory(pp, sizeof(pp));
    pp[0] = (u32)cw;            // BackBufferWidth
    pp[1] = (u32)ch;            // BackBufferHeight
    pp[2] = 0;                  // BackBufferFormat: same as the desktop
    pp[3] = 1;                  // BackBufferCount
    // SwapEffect = D3DSWAPEFFECT_COPY (DISCARD = 1, FLIP = 2, COPY = 3): the back buffer keeps its content after a
    // Present, so the black bars filled once below stay black. Needs BackBufferCount 1, as set above.
    pp[6] = 3;
    pp[7] = (u32)(uintptr_t)g_view;  // hDeviceWindow
    pp[8] = 1;                  // Windowed
    pp[13] = 0;                 // PresentationInterval = default (vsync on, like the game)
    typedef HRESULT(WINAPI * CreateChainFn)(void*, void*, void**);
    HRESULT hr = d3d::Method<CreateChainFn>(dev, 13)(dev, pp, &g_chain);
    if (FAILED(hr) || !g_chain) {
        g_chain = nullptr;
        ++g_chainFailures;
        Log("display: CreateAdditionalSwapChain failed (%08X)", (u32)hr);
        return false;
    }
    typedef HRESULT(WINAPI * ChainGetBackBufferFn)(void*, UINT, UINT, void**);
    hr = d3d::Method<ChainGetBackBufferFn>(g_chain, 5)(g_chain, 0, 0, &g_chainBB);
    if (FAILED(hr) || !g_chainBB) {
        Log("display: chain GetBackBuffer failed (%08X)", (u32)hr);
        ReleaseChain();
        ++g_chainFailures;
        return false;
    }
    typedef HRESULT(WINAPI * ColorFillFn)(void*, void*, const RECT*, u32);
    d3d::Method<ColorFillFn>(dev, 35)(dev, g_chainBB, nullptr, 0xFF000000);
    g_chainW = cw;
    g_chainH = ch;
    Log("display: presenting through an extra swap chain %dx%d", cw, ch);
    return true;
}

// Called by the game right before it releases its default-pool resources and calls Reset().
void __cdecl PreResetHook() {
    ReleaseChain();
    {
        ChainGuard lock;
        g_chainFailures = 0;     // a new device: the chain gets new tries, and bilinear filtering too
        g_chainGaveUpLogged = false;
        g_filter = kTexfLinear;
    }
    ui::ReleasePostResources();
    ((void(__cdecl*)())(uintptr_t)kPreResetTarget)();
}

// --- window -----------------------------------------------------------------------------------------
bool IsWindowed() { return *(volatile u32*)(uintptr_t)kFullscreenFlagVa == 0; }

// Keeps Reset() from sizing the back buffer after the (now much bigger) window.
void PinBackBufferSize() {
    volatile u32* pp = (volatile u32*)(uintptr_t)kResetParamsVa;
    if (pp[0] == 0 && pp[1] == 0) {
        pp[0] = (u32)LogicalW();
        pp[1] = (u32)LogicalH();
        Log("display: Reset parameters pinned to %dx%d", LogicalW(), LogicalH());
    }
}

void GoBorderless(HWND w) {
    HMONITOR mon = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) {
        Log("display: GetMonitorInfo failed, window left as it is");
        return;
    }
    int x = mi.rcMonitor.left, y = mi.rcMonitor.top;
    int cx = mi.rcMonitor.right - x, cy = mi.rcMonitor.bottom - y;
    SetWindowLongW(w, GWL_STYLE, (LONG)(WS_POPUP | WS_VISIBLE));
    SetClassLongW(w, GCL_HBRBACKGROUND, (LONG)(LONG_PTR)GetStockObject(BLACK_BRUSH));
    SetWindowPos(w, HWND_TOP, x, y, cx, cy, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    InvalidateRect(w, nullptr, TRUE);
    Log("display: borderless window %dx%d at %d,%d", cx, cy, x, y);
}

// Back to the original 1280x720 window with a frame, centred on the monitor.
void RestoreWindow(HWND w) {
    DropChain(nullptr);
    const DWORD style = 0x02CA0000;  // what the game itself uses
    SetWindowLongW(w, GWL_STYLE, (LONG)(style | WS_VISIBLE));
    RECT r = {0, 0, 1280, 720};
    AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    HMONITOR mon = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    int x = mi.rcMonitor.left + ((mi.rcMonitor.right - mi.rcMonitor.left) - ww) / 2;
    int y = mi.rcMonitor.top + ((mi.rcMonitor.bottom - mi.rcMonitor.top) - wh) / 2;
    SetWindowPos(w, HWND_NOTOPMOST, x, y, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    Log("display: window restored to 1280x720");
}

void ComputeFit(int cw, int ch) {
    int fw, fh;
    long long lw = LogicalW(), lh = LogicalH();
    if ((long long)cw * lh > (long long)ch * lw) {  // wider than 16:9: bars left and right
        fh = ch;
        fw = (int)((long long)ch * lw / lh);
    } else {  // taller than 16:9: bars above and below
        fw = cw;
        fh = (int)((long long)cw * lh / lw);
    }
    g_fit.x = (cw - fw) / 2;
    g_fit.y = (ch - fh) / 2;
    g_fit.w = fw;
    g_fit.h = fh;
}

typedef HRESULT(WINAPI* PresentFn)(void*, const RECT*, const RECT*, HWND, const void*);
typedef HRESULT(WINAPI* GetBackBufferFn)(void*, UINT, UINT, UINT, void**);
typedef HRESULT(WINAPI* StretchRectFn)(void*, void*, const RECT*, void*, const RECT*, u32);
typedef HRESULT(WINAPI* ChainPresentFn)(void*, const RECT*, const RECT*, HWND, const void*, DWORD);

// Stretches the game's finished back buffer into the extra chain and presents that chain. Any thread: the chain
// is checked again under the lock, it may have been released since the caller looked at it.
bool PresentScaled(void* dev, int cw, int ch, HRESULT* result) {
    ChainGuard lock;
    if (!g_chain || !g_chainBB || g_chainW != cw || g_chainH != ch) return false;
    void* src = nullptr;
    if (FAILED(d3d::Method<GetBackBufferFn>(dev, d3d::kGetBackBuffer)(dev, 0, 0, 0, &src)) || !src) {
        ++g_chainFailures;  // counted like a failed copy: the caller drops the chain, and after 3 in a row it is not recreated
        return false;
    }
    RECT dst = {g_fit.x, g_fit.y, g_fit.x + g_fit.w, g_fit.y + g_fit.h};
    HRESULT hr = d3d::Method<StretchRectFn>(dev, 34)(dev, src, nullptr, g_chainBB, &dst, g_filter);
    if (FAILED(hr) && g_filter == kTexfLinear) {
        g_filter = kTexfPoint;  // the driver refused bilinear for this copy
        Log("display: bilinear StretchRect failed (%08X), using point sampling", (u32)hr);
        hr = d3d::Method<StretchRectFn>(dev, 34)(dev, src, nullptr, g_chainBB, &dst, g_filter);
    }
    d3d::Method<ReleaseFn>(src, d3d::kRelease)(src);
    if (FAILED(hr)) {
        Log("display: StretchRect into the chain failed (%08X)", (u32)hr);
        ++g_chainFailures;
        ReleaseChain();
        return false;
    }
    *result = d3d::Method<ChainPresentFn>(g_chain, 3)(g_chain, nullptr, nullptr, nullptr, nullptr, 0);
    g_chainFailures = 0;
    return true;
}

int g_geometryTick = 0;
LARGE_INTEGER g_lastPresentEnd = {};  // when the previous Present returned: the game's own work is what lies between
double* g_presentAccum = nullptr;

HRESULT WINAPI OurPresent(void* dev, const RECT* src, const RECT* dst, HWND wnd, const void* dirty) {
    HRESULT hr = 0;
    bool done = false;
    HWND w = GameWindowHandle();
    bool windowThread = w && GetWindowThreadProcessId(w, nullptr) == GetCurrentThreadId();
    if (g_switched && (!windowThread || src || dst)) {
        static int odd = 0;
        if (++odd <= 6)
            Log("display: Present from %s%s%s", windowThread ? "the window thread" : "another thread (loading screen?)",
                src ? ", with a source rectangle" : "", dst ? ", with a destination rectangle" : "");
    }
    HangBeat(windowThread);
    if (windowThread && !src && !dst) {
        static bool named = false;
        if (!named) {  // once, now that the device exists: which Direct3D 9 library is in use
            named = true;
            DetectD3dLibrary();
        }
        VibrationFrame();
        AdaptiveTriggerFrame();
        TraceCameraFrame();  // diagnostic, does nothing unless [Debug] TraceCamera=1
    }
    if (windowThread && !src && !dst) {
        ui::PerfPostBegin(dev);
        if (g_cfg.antialias > 0) ui::AntialiasFrame(dev, g_cfg.antialias);
        ui::ColorFrame(dev);
        if (g_cfg.sharpen > 0) ui::SharpenFrame(dev, g_cfg.sharpen);
        ui::XboxGammaFrame(dev);  // last: it stands for the screen of the console, behind everything the game and the filters drew
        ui::PerfPostEnd(dev);
        ui::ScreenshotFrame(dev);
    }
    // Frame statistics over a window of 30 frames. While both panels are off nothing is measured; when one is
    // switched on again everything starts from zero, so the time it was off does not show up as one huge frame.
    static bool statsFresh = true;
    if (!g_cfg.perfOverlay && !g_cfg.frameStats) {
        g_presentAccum = nullptr;
        statsFresh = true;
    } else if (windowThread && !src && !dst) {
        static LARGE_INTEGER lastFrame = {};
        static double sumFrame = 0, sumPresent = 0, sumGame = 0, worstFrame = 0, sumBusy = 0;
        static unsigned long long lastCycles = 0;
        static double cyclesPerMs = 0;
        static int count = 0;
        static double shownFps = 0, shownFrame = 0, shownPresent = 0, shownPhys = 0, shownGame = 0, shownWorst = 0, shownBusy = 0;
        if (statsFresh) {
            statsFresh = false;
            lastFrame.QuadPart = 0;
            lastCycles = 0;
            count = 0;
            sumFrame = sumPresent = sumGame = worstFrame = sumBusy = 0;
            shownFps = shownFrame = shownPresent = shownPhys = shownGame = shownWorst = shownBusy = 0;
            TakePhysicsMs(1);  // drop the physics time gathered while the panels were off
        }
        LARGE_INTEGER nowT;
        QueryPerformanceCounter(&nowT);
        const LARGE_INTEGER fq = g_qpf;
        unsigned long long cyc = 0;
        QueryThreadCycleTime(GetCurrentThread(), &cyc);
        if (cyclesPerMs == 0) {  // once: how many cycles this thread counts in a millisecond (a short busy loop)
            LARGE_INTEGER a, b;
            unsigned long long c0 = 0, c1 = 0;
            QueryPerformanceCounter(&a);
            QueryThreadCycleTime(GetCurrentThread(), &c0);
            do { QueryPerformanceCounter(&b); } while ((b.QuadPart - a.QuadPart) * 1000 < fq.QuadPart * 4);  // 4 ms
            QueryThreadCycleTime(GetCurrentThread(), &c1);
            double ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)fq.QuadPart;
            cyclesPerMs = ms > 0.0 && c1 > c0 ? (double)(c1 - c0) / ms : 1.0;
            lastCycles = 0;
            QueryThreadCycleTime(GetCurrentThread(), &cyc);
        }
        if (lastCycles) sumBusy += (double)(cyc - lastCycles) / cyclesPerMs;
        lastCycles = cyc;
        if (lastFrame.QuadPart) {
            double fr = (double)(nowT.QuadPart - lastFrame.QuadPart) * 1000.0 / (double)fq.QuadPart;
            sumFrame += fr;
            if (fr > worstFrame) worstFrame = fr;
            ui::FrameStatsPush(fr);
            if (g_lastPresentEnd.QuadPart) sumGame += (double)(nowT.QuadPart - g_lastPresentEnd.QuadPart) * 1000.0 / (double)fq.QuadPart;
            if (++count >= 30) {
                shownFrame = sumFrame / count;
                shownFps = 1000.0 / shownFrame;
                shownPresent = sumPresent / count;
                shownGame = sumGame / count;
                shownWorst = worstFrame;
                shownBusy = sumBusy / count;
                shownPhys = TakePhysicsMs(count);
                sumFrame = sumPresent = sumGame = worstFrame = sumBusy = 0;
                count = 0;
            }
        }
        lastFrame = nowT;
        if (shownFrame > 0.0) {
            ui::PerfOverlayFrame(dev, shownFps, shownFrame, shownPresent, shownPhys);
            ui::FrameStatsPanel(dev, shownFrame, shownGame, shownPhys, shownPresent, shownWorst, shownBusy);
        }
        g_presentAccum = &sumPresent;
    }
    if (g_cfg.displayDebug && windowThread && !src && !dst) {  // [Display] Debug=1: "a strip appears when Esc is pressed", the window and the picture around that moment
        static bool prevEsc = false;
        static int countdown = 0;
        bool esc = w && GetForegroundWindow() == w && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        if (esc && !prevEsc) countdown = 241;
        prevEsc = esc;
        if (countdown > 0 && (countdown == 241 || countdown % 60 == 0)) {
            RECT wr = {0, 0, 0, 0}, cr = {0, 0, 0, 0};
            GetWindowRect(w, &wr);
            GetClientRect(w, &cr);
            Log("display: Esc +%d frames: window %d,%d..%d,%d (%dx%d), client %dx%d, picture %d,%d %dx%d, logical %dx%d, chain %dx%d, style %08X, %s", 241 - countdown,
                (int)wr.left, (int)wr.top, (int)wr.right, (int)wr.bottom, (int)(wr.right - wr.left), (int)(wr.bottom - wr.top), (int)cr.right, (int)cr.bottom,
                g_fit.x, g_fit.y, g_fit.w, g_fit.h, LogicalW(), LogicalH(), g_chainW, g_chainH, (u32)GetWindowLongW(w, GWL_STYLE),
                IsZoomed(w) ? "maximised" : IsIconic(w) ? "minimised" : "normal");
        }
        if (countdown > 0) --countdown;
    }
    if (windowThread && !src && !dst) WaypointFrame(dev);
    if (windowThread && !src && !dst) TextToolsFrame();
    if (g_cfg.menu && windowThread && !src && !dst) ui::OnFrame(dev);  // laid out in 1280x720 units, scaled to the back buffer
    if (windowThread && !src && !dst) SessionFrame(dev);  // after the menu: its notice box is drawn on top
    if (windowThread && !src && !dst) PacerBeforePresent();  // the 60 frames/s limit (mode 1), then the Present time is measured
    // The Present time must cover the scaled path too (the extra swap chain does the waiting for the video card and the screen).
    LARGE_INTEGER pt0, pt1;
    QueryPerformanceCounter(&pt0);
    if (!src && !dst && w && IsWindowed()) {
        if (windowThread) {
            if (g_cfg.borderless && !g_switched && !g_dpfixPresent) {  // with DPfix the window is its business
                PinBackBufferSize();
                GoBorderless(w);
                g_switched = true;
            } else if (!g_cfg.borderless && g_switched) {
                RestoreWindow(w);
                g_switched = false;
            } else if (g_switched && (++g_geometryTick % 20) == 0) {
                // The game resizes and re-frames its window now and then (loading screens, device resets). Put the
                // borderless window back over the whole monitor when that happens.
                RECT wr;
                HMONITOR mon = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi;
                mi.cbSize = sizeof(mi);
                if (GetWindowRect(w, &wr) && GetMonitorInfoW(mon, &mi) &&
                    (wr.left != mi.rcMonitor.left || wr.top != mi.rcMonitor.top || wr.right != mi.rcMonitor.right ||
                     wr.bottom != mi.rcMonitor.bottom || (GetWindowLongW(w, GWL_STYLE) & WS_CAPTION))) {
                    Log("display: the game changed the window to %d,%d %dx%d, putting the borderless window back", (int)wr.left,
                        (int)wr.top, (int)(wr.right - wr.left), (int)(wr.bottom - wr.top));
                    GoBorderless(w);
                }
            }
        }
        if (g_cfg.borderless && g_switched && g_cfg.scaleToWindow) {
            RECT c;
            if (GetClientRect(w, &c) && c.right > 0 && c.bottom > 0) {
                ComputeFit(c.right, c.bottom);
                // The back buffer is already the size of the window: present it as it is, no second resampling.
                // The chain and the child window are released and hidden on the window thread only: ShowWindow from
                // the loading thread would wait for the main thread, which may itself be waiting for that thread.
                if (g_outputW && c.right == g_outputW && c.bottom == g_outputH) {
                    if (windowThread) DropChain("the back buffer matches the window, presenting directly");
                    goto direct;
                }
                bool ready = g_chain && g_chainW == c.right && g_chainH == c.bottom;
                // Creating or resizing the chain touches the window: main thread only.
                if (!ready && windowThread) ready = EnsureChain(dev, w, c.right, c.bottom);
                if (ready) done = PresentScaled(dev, c.right, c.bottom, &hr);
                // The game's own Present goes to the game window: the child window must not stay over it.
                if (!done && windowThread) DropChain("the extra swap chain is not available, presenting directly");
            }
        } else if (windowThread && (g_chain || (g_view && IsWindowVisible(g_view)))) {
            DropChain("scaling to the window is off, presenting directly");  // ScaleToWindow switched off in the menu
        }
    }
direct:
    // Where the picture sits in the client area, for the menu's mouse: ComputeFit above only when this frame went out
    // through the extra swap chain; otherwise the game's own Present stretches it over the whole client area
    // (borderless with ScaleToWindow=0, the back buffer already the size of the window, the chain not available, the
    // normal window, a window left to DPfix).
    if (windowThread && !src && !dst && w && !done) {
        RECT c;
        if (GetClientRect(w, &c) && c.right > 0 && c.bottom > 0) {
            g_fit.x = 0;
            g_fit.y = 0;
            g_fit.w = c.right;
            g_fit.h = c.bottom;
        }
    }
    if (!done) {
        PresentFn present = d3d::Method<PresentFn>(dev, d3d::kPresent);
        hr = present(dev, src, dst, wnd, dirty);
    }
    QueryPerformanceCounter(&pt1);
    if (windowThread && !src && !dst) g_lastPresentEnd = pt1;
    if (windowThread && !src && !dst) PacerAfterPresent((u64)pt1.QuadPart, Ms(pt1.QuadPart - pt0.QuadPart));
    if (g_presentAccum && windowThread && !src && !dst) *g_presentAccum += Ms(pt1.QuadPart - pt0.QuadPart);
    if (g_cfg.displayDebug && g_switched && windowThread) {
        // The first three frames, then only when the way of presenting changes (scaled or plain) or the Present fails.
        static int lastWay = -1;
        static HRESULT lastHr = 0;
        int way = done ? 1 : 0;
        ++g_frames;
        if (g_frames <= 3 || way != lastWay || (hr != lastHr && hr < 0))
            Log("display: frame %d, Present %08X, %s, back buffer %dx%d, picture %d,%d %dx%d", g_frames, (u32)hr,
                done ? "scaled" : "plain", LogicalW(), LogicalH(), g_fit.x, g_fit.y, g_fit.w, g_fit.h);
        lastWay = way;
        lastHr = hr;
    }
    return hr;
}

LRESULT WINAPI H_DefWindowProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    // F10 must not open the system menu loop of the window: it belongs to our settings menu.
    if (g_cfg.menu && msg == WM_SYSKEYDOWN && wp == VK_F10) return 0;
    if (msg == WM_CLOSE || msg == WM_DESTROY || (msg == WM_ACTIVATEAPP && !wp)) StopVibration();  // no stuck rumble
    if (msg == WM_SETCURSOR && g_cfg.hideCursor && LOWORD(lp) == HTCLIENT && h == GameWindowHandle() &&
        !ui::IsOpen()) {
        SetCursor(nullptr);
        return TRUE;
    }
    return r_DefWindowProcA(h, msg, wp, lp);
}

}  // namespace

bool InstallDisplay() {
    bool any = false;
    InitializeCriticalSection(&g_chainLock);
    QueryPerformanceFrequency(&g_qpf);
    if (g_qpf.QuadPart <= 0) g_qpf.QuadPart = 1;
    if (g_cfg.dpiAware) SetProcessDPIAware();

    if (g_cfg.borderless) {
        // The stock launcher starts the game with an argument that asks for real fullscreen; windowed is what our
        // borderless mode, the output resolution and the scaling all need. The exact argument "-full1280" also sets
        // the 1280x1024 flag at 0148BBC4h; that store is cleared too, so every argument gives the normal windowed path.
        bool ok = true;
        for (const SiteDef& f : kSiteFullscreenArg) ok = ok && BytesMatch(f.va, f.bytes, f.len);
        if (ok) {
            const u8 zero = 0;
            for (const SiteDef& f : kSiteFullscreenArg) WriteCode(f.va + 6, &zero, 1);
            Log("display: a launcher argument no longer switches the game to real fullscreen");
        }
    }
    // The Present and Reset hooks are installed whatever the window settings: the menu, the frame limit, the
    // controller output, the waypoint, the filters and the hang watchdog all run from the Present hook.
    const SiteDef& s = kSitePresentCall;
    const SiteDef& r = kSitePreResetCall;
    if (!BytesMatch(s.va, s.bytes, s.len) || !BytesMatch(r.va, r.bytes, r.len)) {
        Log("display: bytes at %08X or %08X differ from DP.exe 1.01b, the Present hook (menu, frame limit, rumble, "
            "borderless window) is skipped", s.va, r.va);
    } else if (!PatchCall(r.va, r.bytes, r.len, (const void*)PreResetHook) ||
               !PatchCall(s.va, s.bytes, s.len, (const void*)OurPresent)) {
        Log("display: could not patch the Present/Reset call sites");
    } else {
        Log("display: Present hook installed at %08X, Reset hook at %08X (borderless window %s)", s.va, r.va,
            g_cfg.borderless ? "on" : (g_dpfixPresent ? "left to DPfix" : "off"));
        any = true;
    }
    if (g_cfg.hideCursor || g_cfg.menu) {
        void** slot = FindImportSlot(GetModuleHandleW(nullptr), "USER32.dll", "DefWindowProcA");
        if (slot) {
            r_DefWindowProcA = (decltype(r_DefWindowProcA))*slot;
            if (SwapImport(slot, (void*)H_DefWindowProcA, nullptr)) {
                Log("display: window message hook installed (cursor hiding, F10)");
                any = true;
            }
        } else {
            Log("display: DefWindowProcA import not found, cursor left visible");
        }
    }
    return any;
}

}  // namespace ufx
