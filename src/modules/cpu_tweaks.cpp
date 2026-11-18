// Processor scheduling of the game. Logs what the CPU looks like (hybrid P/E cores or not) and, when asked, takes away what Windows can
// do to a game thread without asking:
//   NoPowerThrottling=1 (default): the process and the main thread are exempt from "efficiency mode" (EcoQoS), which keeps a thread
//       on slow cores / low clocks (Windows 11 does it to programs it does not know).
//   PerformanceCoresOnly=1 (off by default): on a CPU with performance and efficiency cores the game runs only on the performance ones.
// DP.exe is a 2013 program with one heavy main thread: if that thread lands on an efficiency core the frame rate halves.
#include "../base.h"

namespace ufx {
namespace {

struct ThrottleState {
    ULONG version;
    ULONG controlMask;
    ULONG stateMask;
};

typedef BOOL(WINAPI* SetProcInfoFn)(HANDLE, int, void*, DWORD);
typedef BOOL(WINAPI* SetThreadInfoFn)(HANDLE, int, void*, DWORD);
typedef BOOL(WINAPI* GetCpuSetsFn)(void*, ULONG, PULONG, HANDLE, ULONG);
typedef BOOL(WINAPI* SetProcCpuSetsFn)(HANDLE, const ULONG*, ULONG);

}  // namespace

void InstallCpuTweaks() {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;

    // what the processor looks like: efficiency classes of its logical processors
    static unsigned char buf[16384];
    ULONG length = 0;
    GetCpuSetsFn getSets = (GetCpuSetsFn)(void*)GetProcAddress(k32, "GetSystemCpuSetInformation");
    int classCount[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int highest = 0, logical = 0;
    ULONG ids[256];
    unsigned char idClass[256];
    int idCount = 0;
    if (getSets && getSets(buf, sizeof(buf), &length, GetCurrentProcess(), 0)) {
        for (ULONG off = 0; off + 24 <= length;) {
            ULONG size = *(ULONG*)(buf + off);
            ULONG type = *(ULONG*)(buf + off + 4);
            if (size < 24) break;
            if (type == 0) {  // CpuSetInformation
                ULONG id = *(ULONG*)(buf + off + 8);
                unsigned char eff = buf[off + 18];
                if (eff > 7) eff = 7;
                ++classCount[eff];
                if (eff > highest) highest = eff;
                if (idCount < 256) { ids[idCount] = id; idClass[idCount] = eff; ++idCount; }
                ++logical;
            }
            off += size;
        }
    }
    char line[160];
    int n = 0;
    for (int c = 0; c < 8; ++c)
        if (classCount[c]) n += wsprintfA(line + n, "class %d: %d  ", c, classCount[c]);
    line[n] = 0;
    bool hybrid = false;
    int used = 0;
    for (int c = 0; c < 8; ++c)
        if (classCount[c]) ++used;
    hybrid = used > 1;
    Log("cpu: %d logical processors; efficiency classes %s(%s)", logical, line, hybrid ? "hybrid: performance and efficiency cores" : "all alike");

    if (g_cfg.noPowerThrottling) {
        ThrottleState st = {1, 1 /*EXECUTION_SPEED*/, 0 /*off*/};
        SetProcInfoFn setProc = (SetProcInfoFn)(void*)GetProcAddress(k32, "SetProcessInformation");
        SetThreadInfoFn setThread = (SetThreadInfoFn)(void*)GetProcAddress(k32, "SetThreadInformation");
        bool p = setProc && setProc(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, &st, sizeof(st));
        bool t = setThread && setThread(GetCurrentThread(), 3 /*ThreadPowerThrottling*/, &st, sizeof(st));
        Log("cpu: power throttling (efficiency mode) switched off: process %s, main thread %s", p ? "yes" : "not available", t ? "yes" : "not available");
    }
    if (g_cfg.performanceCoresOnly) {
        SetProcCpuSetsFn setSets = (SetProcCpuSetsFn)(void*)GetProcAddress(k32, "SetProcessDefaultCpuSets");
        if (!hybrid) {
            Log("cpu: PerformanceCoresOnly asked for, but the processor has one kind of cores: nothing to do");
        } else if (setSets) {
            ULONG pick[256];
            int pc = 0;
            for (int i = 0; i < idCount; ++i)
                if (idClass[i] == highest) pick[pc++] = ids[i];
            bool ok = setSets(GetCurrentProcess(), pick, (ULONG)pc);
            Log("cpu: the game is limited to the %d performance cores: %s", pc, ok ? "ok" : "failed");
        } else {
            Log("cpu: SetProcessDefaultCpuSets is not available (Windows 10 or newer is needed)");
        }
    }
}

}  // namespace ufx
