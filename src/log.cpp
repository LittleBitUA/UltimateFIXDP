#include "base.h"

namespace ufx {

static HANDLE g_log = INVALID_HANDLE_VALUE;

void LogInit(const wchar_t* path) {
    g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Log(const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char buf[1100];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = wsprintfA(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    n += wvsprintfA(buf + n, fmt, ap);  // wvsprintfA never writes more than 1024 bytes
    va_end(ap);
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD written;
    WriteFile(g_log, buf, (DWORD)n, &written, nullptr);
}

u64 TickNow() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return (u64)q.QuadPart;
}

u32 TickMicros(u64 from, u64 to) {
    static u64 freq = 0;  // fixed while Windows runs; two threads that read it at once write the same value
    if (!freq) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart > 0 ? (u64)f.QuadPart : 1;
    }
    if (to <= from) return 0;
    const u64 us = (to - from) * 1000000 / freq;
    return us > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)us;
}

void LogShaderTime(const char* name, const char* profile, u64 started) {
    const u32 us = TickMicros(started, TickNow());
    if (profile) Log("shader %s: compiled in %u.%u ms (%s)", name, MsWhole(us), MsTenth(us), profile);
    else Log("shader %s: no profile compiled, %u.%u ms spent", name, MsWhole(us), MsTenth(us));
}

}  // namespace ufx
