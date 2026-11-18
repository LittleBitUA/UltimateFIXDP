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

}  // namespace ufx
