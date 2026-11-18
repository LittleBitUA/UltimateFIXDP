// Crash diagnostics: an unhandled-exception filter that writes a short report to the log and a minidump next to
// it, then hands over to whatever filter the game installed (or to Windows). Changes no game behaviour.
//
// The report names the module and offset of the faulting instruction, the registers and the top of the stack.
// Addresses of crashes known from other projects are labelled (NavTable / SetAiState class of CrashFix).
#include "../base.h"

namespace ufx {
namespace {

typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, DWORD, void*, void*, void*);
typedef LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI* SetFilterFn)(LPTOP_LEVEL_EXCEPTION_FILTER);

struct DumpExceptionInfo {
    DWORD threadId;
    EXCEPTION_POINTERS* pointers;
    BOOL clientPointers;
};

LPTOP_LEVEL_EXCEPTION_FILTER g_gameFilter = nullptr;
SetFilterFn g_realSetFilter = nullptr;
volatile LONG g_inFilter = 0;
MiniDumpWriteDumpFn g_miniDump = nullptr;  // resolved by the watchdog thread when it starts, not at the moment of a crash or a freeze

// A stack overflow leaves the faulting thread almost no stack, too little for the report and the dump: the watchdog thread makes
// them while the faulting thread waits.
HANDLE g_crashRequest = nullptr, g_crashDone = nullptr;
EXCEPTION_POINTERS* volatile g_crashPointers = nullptr;
volatile DWORD g_crashThread = 0;
volatile DWORD g_watchdogThread = 0;

// .text of DP.exe 1.01b (section table: 0x401000, size 0x36CF83). Pointers into .rdata and .data (strings, vtables, globals)
// are not code.
constexpr u32 kGameCodeBegin = 0x401000, kGameCodeEnd = 0x76DF83;
bool IsGameCode(u32 v) { return v >= kGameCodeBegin && v < kGameCodeEnd; }

// A stack word is taken for a return address only if it points into the game code just after a call instruction: E8 rel32, or FF /2
// (call through a register or memory: 2, 3, 4, 6 or 7 bytes).
bool LooksLikeReturn(u32 v) {
    if (v < kGameCodeBegin + 8 || v >= kGameCodeEnd) return false;
    const u8* b = (const u8*)(uintptr_t)v;
    if (b[-5] == 0xE8) return true;
    if (b[-2] == 0xFF && ((b[-1] >= 0xD0 && b[-1] <= 0xD7) || (b[-1] >= 0x10 && b[-1] <= 0x17 && b[-1] != 0x14 && b[-1] != 0x15))) return true;
    if (b[-3] == 0xFF && ((b[-2] >= 0x50 && b[-2] <= 0x57 && b[-2] != 0x54) || b[-2] == 0x14)) return true;
    if (b[-4] == 0xFF && b[-3] == 0x54) return true;
    if (b[-6] == 0xFF && ((b[-5] >= 0x90 && b[-5] <= 0x97 && b[-5] != 0x94) || b[-5] == 0x15)) return true;
    if (b[-7] == 0xFF && b[-6] == 0x94) return true;
    return false;
}

MiniDumpWriteDumpFn MiniDumpFn() {
    if (!g_miniDump) {
        HMODULE dbg = LoadLibraryW(L"dbghelp.dll");
        g_miniDump = dbg ? (MiniDumpWriteDumpFn)(void*)GetProcAddress(dbg, "MiniDumpWriteDump") : nullptr;
    }
    return g_miniDump;
}

const char* Describe(u32 code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "float divide by zero";
        case 0xE06D7363: return "C++ exception";
        default: return "exception";
    }
}

// Where a faulting address lives: game image, our DLL, or another module.
void ModuleOf(u32 addr, char* out, size_t cap) {
    HMODULE mod = nullptr;
    out[0] = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)addr, &mod) &&
        mod) {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(mod, path, MAX_PATH);
        const char* name = path;
        for (DWORD i = 0; i < n; ++i)
            if (path[i] == '\\' || path[i] == '/') name = path + i + 1;
        wsprintfA(out, "%s+%X", name, (unsigned)(addr - (u32)(uintptr_t)mod));
    } else {
        lstrcpynA(out, "unknown module", (int)cap);
    }
}

// Crash sites reported by CrashFix for the NavTable / SetAiState class (an NPC after a cutscene).
const char* KnownCrash(u32 eip) {
    if (eip == 0x5E1F3D || eip == 0x47F1CB) return "matches the NavTable / SetAiState crash class (CrashFix)";
    return nullptr;
}

void WriteDump(EXCEPTION_POINTERS* ep, DWORD thread) {
    MiniDumpWriteDumpFn write = MiniDumpFn();
    if (!write) {
        Log("crash: dbghelp.dll is not available, no minidump");
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%scrash_%04d%02d%02d_%02d%02d%02d.dmp", g_paths.dataDir, t.wYear, t.wMonth, t.wDay, t.wHour,
              t.wMinute, t.wSecond);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        Log("crash: cannot create the minidump file");
        return;
    }
    DumpExceptionInfo info = {thread, ep, FALSE};  // FALSE: the pointers are in this process
    // MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo
    BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), f, 0x1 | 0x40 | 0x1000, &info, nullptr, nullptr);
    CloseHandle(f);
    Log("crash: minidump %s: %S", ok ? "written" : "FAILED", path);
}

// The report and the minidump of a crash of `thread` (the faulting thread itself, or the watchdog for it).
__attribute__((noinline)) void ReportCrash(EXCEPTION_POINTERS* ep, DWORD thread) {
    {
        const EXCEPTION_RECORD* r = ep->ExceptionRecord;
        const CONTEXT* c = ep->ContextRecord;
        char where[300];
        ModuleOf((u32)(uintptr_t)r->ExceptionAddress, where, sizeof(where));
        Log("crash: %s (%08X) at %08X (%s), thread %u", Describe(r->ExceptionCode), (unsigned)r->ExceptionCode,
            (unsigned)(uintptr_t)r->ExceptionAddress, where, (unsigned)thread);
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
            Log("crash: %s address %08X", r->ExceptionInformation[0] == 0 ? "read of" : r->ExceptionInformation[0] == 1 ? "write to" : "execute at",
                (unsigned)r->ExceptionInformation[1]);
        if (const char* known = KnownCrash((u32)(uintptr_t)r->ExceptionAddress)) Log("crash: %s", known);
        Log("crash: eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X", (unsigned)c->Eax,
            (unsigned)c->Ebx, (unsigned)c->Ecx, (unsigned)c->Edx, (unsigned)c->Esi, (unsigned)c->Edi, (unsigned)c->Ebp,
            (unsigned)c->Esp);
        const u32* sp = (const u32*)(uintptr_t)c->Esp;
        char line[160];
        int n = 0;
        for (int i = 0; i < 16 && n < 150; ++i) {
            if (IsBadReadPtr(sp + i, 4)) break;
            n += wsprintfA(line + n, "%08X ", (unsigned)sp[i]);
        }
        line[n] = 0;
        Log("crash: stack %s", line);
        WriteDump(ep, thread);
    }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    if (InterlockedExchange(&g_inFilter, 1) == 0) {
        DWORD self = GetCurrentThreadId();
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW && g_crashRequest && g_crashDone && g_watchdogThread &&
            self != g_watchdogThread) {
            g_crashPointers = ep;
            g_crashThread = self;
            SetEvent(g_crashRequest);
            WaitForSingleObject(g_crashDone, 60000);
        } else {
            ReportCrash(ep, self);
        }
    }
    if (g_gameFilter) return g_gameFilter(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

// The game (or its C runtime) may install its own filter later; we keep ours first and remember theirs.
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI HookedSetFilter(LPTOP_LEVEL_EXCEPTION_FILTER f) {
    LPTOP_LEVEL_EXCEPTION_FILTER previous = g_gameFilter;
    g_gameFilter = f;
    return previous;
}

// ---- hang watchdog -----------------------------------------------------------------------------------------------------------
// A frozen game writes no crash report. When the game window is in front and no picture has been shown for 8 seconds, the thread that
// shows the pictures is stopped only while its registers and the top of its stack are copied, and runs again before the log is written.
// Then where it stood (instruction, module, return addresses on the stack) goes to the log and a minidump is written, so the place of
// the freeze can be read afterwards. The minidump stops all threads once more for as long as Windows needs to write it
// (MiniDumpWriteDump does that itself). At most three reports per run.
volatile DWORD g_beatTick = 0;
volatile DWORD g_windowThread = 0;
volatile LONG g_wantProfile = 0;

void WriteHangDump(DWORD tid) {
    MiniDumpWriteDumpFn write = MiniDumpFn();
    if (!write) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t path[MAX_PATH + 64];
    wsprintfW(path, L"%shang_%04d%02d%02d_%02d%02d%02d.dmp", g_paths.dataDir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), f, 0x1 | 0x40 | 0x1000, nullptr, nullptr, nullptr);
    CloseHandle(f);
    Log("hang: minidump %s: %S (thread %u)", ok ? "written" : "FAILED", path, (unsigned)tid);
}

void ReportHang(DWORD tid) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!th) { Log("hang: no picture for 8 s, but the thread %u cannot be opened", (unsigned)tid); return; }
    if (SuspendThread(th) == (DWORD)-1) { CloseHandle(th); return; }
    // While the thread stands, only copies are made: no call that could wait for a lock the frozen thread may hold (the loader,
    // the heap), so a long but passing stall can never become a lasting one here.
    CONTEXT c;
    ZeroMemory(&c, sizeof(c));
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    static u32 stack[512];  // the watchdog thread is the only user
    int words = 0;
    bool haveContext = GetThreadContext(th, &c) != 0;
    if (haveContext) {
        const u32* sp = (const u32*)(uintptr_t)c.Esp;
        for (; words < 512; ++words) {
            if (IsBadReadPtr(sp + words, 4)) break;
            stack[words] = sp[words];
        }
    }
    ResumeThread(th);
    CloseHandle(th);
    if (haveContext) {
        char where[300];
        ModuleOf((u32)c.Eip, where, sizeof(where));
        Log("hang: no picture for 8 s; the picture thread %u stands at %08X (%s)", (unsigned)tid, (unsigned)c.Eip, where);
        Log("hang: eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X", (unsigned)c.Eax, (unsigned)c.Ebx, (unsigned)c.Ecx,
            (unsigned)c.Edx, (unsigned)c.Esi, (unsigned)c.Edi, (unsigned)c.Ebp, (unsigned)c.Esp);
        // return addresses: stack words that point just after a call in the game code, most recent first
        char line[200];
        int n = 0, found = 0;
        for (int i = 0; i < words && found < 24; ++i) {
            u32 v = stack[i];
            if (LooksLikeReturn(v)) {
                n += wsprintfA(line + n, "%08X ", (unsigned)v);
                ++found;
                if (n > 170) { line[n] = 0; Log("hang: game addresses on the stack %s", line); n = 0; }
            }
        }
        if (n) { line[n] = 0; Log("hang: game addresses on the stack %s", line); }
        // other modules on the stack (driver, PhysX) tell what the game is waiting for
        n = 0;
        found = 0;
        for (int i = 0; i < words && i < 256 && found < 10; ++i) {
            u32 v = stack[i];
            if (v < 0x1000000 || v >= UserAddressEnd()) continue;  // with the large address flag modules can lie above 2 GB
            char m[200];
            ModuleOf(v, m, sizeof(m));
            if (m[0] == 'u' && m[1] == 'n') continue;  // "unknown module"
            n += wsprintfA(line + n, "%s ", m);
            ++found;
            if (n > 150) break;
        }
        line[n] = 0;
        if (n) Log("hang: other modules on the stack %s", line);
    }
    WriteHangDump(tid);  // MiniDumpWriteDump stops the threads itself for as long as it needs them
}

// ---- sampling profiler (F9) ---------------------------------------------------------------------------------------------------
// Press F9 in the game: for 10 seconds the picture thread is stopped for a moment about every 2 ms and where it stands is noted. Then the log
// gets the share per module (game, video driver, PhysX, Windows) and the busiest places of the game code (blocks of 128 bytes of DP.exe;
// for time spent in a driver, the game code that called it). Reading the addresses against the engine map tells what costs the CPU time.
struct Sample {
    u32 eip;
    u32 caller;  // the first game address found on the stack (for samples outside the game)
};
Sample g_samples[6000];

void SortU32(u32* a, int n) {  // shell sort: no C runtime
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; ++i) {
            u32 v = a[i];
            int j = i;
            while (j >= gap && a[j - gap] > v) { a[j] = a[j - gap]; j -= gap; }
            a[j] = v;
        }
}

void RunProfile(DWORD tid) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!th) { Log("profile: the picture thread cannot be opened"); return; }
    Log("profile: sampling for 10 seconds (F9)");
    int n = 0;
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 10000 && n < 6000) {
        Sleep(2);
        if (SuspendThread(th) == (DWORD)-1) break;
        CONTEXT c;
        ZeroMemory(&c, sizeof(c));
        c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(th, &c)) {
            Sample& s = g_samples[n++];
            s.eip = (u32)c.Eip;
            s.caller = 0;
            if (!IsGameCode(s.eip)) {
                const u32* sp = (const u32*)(uintptr_t)c.Esp;
                for (int i = 0; i < 128; ++i) {
                    if (IsBadReadPtr(sp + i, 4)) break;
                    if (LooksLikeReturn(sp[i])) { s.caller = sp[i]; break; }
                }
            }
        }
        ResumeThread(th);
    }
    CloseHandle(th);
    if (n < 100) { Log("profile: only %d samples, nothing to report", n); return; }
    // share per module
    struct Mod { char name[48]; int count; };
    static Mod mods[24];
    static u32 pageOf[256];
    static char pageName[256][48];
    int modCount = 0, pages = 0, inGame = 0;
    for (int i = 0; i < n; ++i) {
        u32 e = g_samples[i].eip;
        int pi = -1;
        for (int k = 0; k < pages; ++k)
            if (pageOf[k] == (e >> 12)) { pi = k; break; }
        if (pi < 0 && pages < 256) {
            pi = pages++;
            pageOf[pi] = e >> 12;
            char where[300];
            ModuleOf(e, where, sizeof(where));
            int len = 0;
            while (where[len] && where[len] != '+' && len < 40) { pageName[pi][len] = where[len]; ++len; }
            pageName[pi][len] = 0;
        }
        const char* name = pi >= 0 ? pageName[pi] : "other";
        if (IsGameCode(e)) ++inGame;
        int m = -1;
        for (int k = 0; k < modCount; ++k) {
            int j = 0;
            while (mods[k].name[j] && mods[k].name[j] == name[j]) ++j;
            if (!mods[k].name[j] && !name[j]) { m = k; break; }
        }
        if (m < 0 && modCount < 24) {
            m = modCount++;
            lstrcpynA(mods[m].name, name, 48);
            mods[m].count = 0;
        }
        if (m >= 0) ++mods[m].count;
    }
    for (int r = 0; r < modCount && r < 8; ++r) {  // the biggest first
        int best = -1;
        for (int k = 0; k < modCount; ++k)
            if (mods[k].count > 0 && (best < 0 || mods[k].count > mods[best].count)) best = k;
        if (best < 0) break;
        int per = mods[best].count * 1000 / n;
        Log("profile: module %s %d.%d%%", mods[best].name, per / 10, per % 10);
        mods[best].count = -1;
    }
    // the busiest 128-byte blocks of the game code: own samples, and the callers of the samples spent in other modules
    static u32 own[6000], via[6000];
    int ownN = 0, viaN = 0;
    for (int i = 0; i < n; ++i) {
        u32 e = g_samples[i].eip;
        if (IsGameCode(e)) own[ownN++] = e >> 7;
        else if (g_samples[i].caller) via[viaN++] = g_samples[i].caller >> 7;
    }
    for (int pass = 0; pass < 2; ++pass) {
        u32* a = pass == 0 ? own : via;
        int cnt = pass == 0 ? ownN : viaN;
        if (cnt == 0) continue;
        SortU32(a, cnt);
        static u32 keys[1200];
        static int counts[1200];
        int kn = 0;
        for (int i = 0; i < cnt && kn < 1200;) {
            int j = i;
            while (j < cnt && a[j] == a[i]) ++j;
            keys[kn] = a[i];
            counts[kn++] = j - i;
            i = j;
        }
        Log("profile: %s (%d samples)", pass == 0 ? "game code where the time is spent" : "game code that called into other modules (driver, PhysX, Windows)", cnt);
        for (int r = 0; r < 20; ++r) {
            int best = -1;
            for (int k = 0; k < kn; ++k)
                if (counts[k] > 0 && (best < 0 || counts[k] > counts[best])) best = k;
            if (best < 0) break;
            int per = counts[best] * 1000 / n;
            Log("profile:   %08X..%08X %d.%d%%", (unsigned)(keys[best] << 7), (unsigned)((keys[best] << 7) + 127), per / 10, per % 10);
            counts[best] = -1;
        }
    }
    Log("profile: %d samples, %d%% of them in the game code itself", n, inGame * 100 / n);
}

DWORD WINAPI WatchdogThread(void*) {
    g_watchdogThread = GetCurrentThreadId();
    MiniDumpFn();  // load dbghelp now, while nothing is stuck, not inside a freeze or a crash
    int reports = 0;
    bool armed = true;
    int tick = 0;
    for (;;) {
        if (!g_crashRequest) {
            Sleep(100);
        } else if (WaitForSingleObject(g_crashRequest, 100) == WAIT_OBJECT_0) {  // a stack overflow: report it for the faulting thread
            ReportCrash(g_crashPointers, g_crashThread);
            SetEvent(g_crashDone);
            continue;
        }
        {   // F9 (the game in front): the sampling profiler
            HWND gw = GameWindowHandle();
            static bool prevF9 = false;
            bool f9 = gw && GetForegroundWindow() == gw && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
            if ((f9 && !prevF9 && g_windowThread) || (g_wantProfile && g_windowThread)) {
                g_wantProfile = 0;
                RunProfile(g_windowThread);
            }
            prevF9 = f9;
        }
        if (++tick % 5) continue;  // the freeze check runs every 500 ms
        DWORD beat = g_beatTick, tid = g_windowThread;
        if (!beat || !tid) continue;
        DWORD silent = GetTickCount() - beat;
        if (silent < 2000) { armed = true; continue; }
        if (silent > 8000 && armed && reports < 3) {
            HWND w = GameWindowHandle();
            if (!w || GetForegroundWindow() != w) continue;  // the game is not in front (Alt+Tab, minimised): not a freeze to report
            armed = false;
            ++reports;
            ReportHang(tid);
        }
    }
}

}  // namespace

// Called with every Present: the game is alive while pictures keep coming. `windowThread` = the thread that owns the game window.
void HangBeat(bool windowThread) {
    DWORD now = GetTickCount();
    if (windowThread) {
        // 300 slow frames in a row (about 7 s at 40 frames/s) start the sampling profiler by itself, twice at most per run. Slow is over
        // 20 ms (over 40 ms in the 30 frames/s mode, where 33 ms is the normal frame) and under 120 ms. Measured with the counter:
        // GetTickCount moves in steps of about 15.6 ms, too coarse for this.
        static u64 last = 0;
        static int slow = 0, runs = 0;
        LARGE_INTEGER q, f;
        QueryPerformanceCounter(&q);
        QueryPerformanceFrequency(&f);
        u64 qn = (u64)q.QuadPart;
        u64 dtUs = last && f.QuadPart > 0 ? (qn - last) * 1000000 / (u64)f.QuadPart : 0;
        last = qn;
        u64 slowUs = g_cfg.framePacing == 2 ? 40000 : 20000;
        if (dtUs > slowUs && dtUs < 120000) ++slow; else slow = 0;
        if (g_cfg.autoProfile && slow >= 300 && runs < 2 && !g_wantProfile) {
            g_wantProfile = 1;
            ++runs;
            slow = 0;
        }
        g_windowThread = GetCurrentThreadId();
    }
    g_beatTick = now;
}

bool InstallCrashHandler() {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    g_realSetFilter = k32 ? (SetFilterFn)(void*)GetProcAddress(k32, "SetUnhandledExceptionFilter") : nullptr;
    if (!g_realSetFilter) return false;
    UserAddressEnd();  // read now, not first in a report
    g_gameFilter = g_realSetFilter(CrashFilter);
    void** slot = FindImportSlot(GetModuleHandleW(nullptr), "KERNEL32.dll", "SetUnhandledExceptionFilter");
    if (slot) SwapImport(slot, (void*)HookedSetFilter, nullptr);
    Log("crash: diagnostic handler installed (minidump into UltimateFix\\)%s", g_gameFilter ? ", game filter chained" : "");
    g_crashRequest = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_crashDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE wt = CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr);
    if (wt) CloseHandle(wt);
    return true;
}

}  // namespace ufx
