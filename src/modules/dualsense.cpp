// Native DualSense input over HID, so that the pad works without Steam Input and without Windows listing it as a
// joystick. A reader thread finds a DualSense (Sony 054C, product 0CE6 / 0DF2), reads its input reports over USB or
// Bluetooth and keeps the latest state; the joyGetPosEx hook hands it to the game in the Xbox 360 layout the game
// expects (see the layout notes in sony_layout.cpp). Report layouts are those of the public documentation of the
// DualSense (see DsParseInput in dsreport.h); the first reports are written to the log in hex to check them on a real pad.
#include "../base.h"
#include "../dsreport.h"
#include <mmsystem.h>

namespace ufx {
void MarkTriggerButtons();  // stick_profile.cpp: this source set buttons 10/11 itself
namespace {

struct HidAttributes {
    ULONG size;
    USHORT vendor, product, version;
};
struct SpDeviceInterfaceData {
    DWORD size;
    GUID guid;
    DWORD flags;
    ULONG_PTR reserved;
};
typedef void(WINAPI* HidGetGuidFn)(GUID*);
typedef BOOL(WINAPI* HidGetAttrFn)(HANDLE, HidAttributes*);
typedef BOOL(WINAPI* HidGetPreparsedFn)(HANDLE, void**);
typedef BOOL(WINAPI* HidFreePreparsedFn)(void*);
typedef BOOL(WINAPI* HidGetFeatureFn)(HANDLE, void*, ULONG);
typedef LONG(WINAPI* HidGetCapsFn)(void*, void*);
typedef HANDLE(WINAPI* SetupGetClassDevsFn)(const GUID*, const wchar_t*, HWND, DWORD);
typedef BOOL(WINAPI* SetupEnumInterfacesFn)(HANDLE, void*, const GUID*, DWORD, SpDeviceInterfaceData*);
typedef BOOL(WINAPI* SetupGetDetailFn)(HANDLE, SpDeviceInterfaceData*, void*, DWORD, DWORD*, void*);
typedef BOOL(WINAPI* SetupDestroyFn)(HANDLE);

HidGetGuidFn hidGetGuid;
HidGetAttrFn hidGetAttr;
HidGetPreparsedFn hidGetPreparsed;
HidFreePreparsedFn hidFreePreparsed;
HidGetFeatureFn hidGetFeature;
HidGetCapsFn hidGetCaps;
SetupGetClassDevsFn setupGetClassDevs;
SetupEnumInterfacesFn setupEnum;
SetupGetDetailFn setupGetDetail;
SetupDestroyFn setupDestroy;

// The latest state, written by the reader thread, read by the game thread (sequence counter around the copy).
typedef DsInput State;
volatile LONG g_seq = 0;
State g_state;
volatile DWORD g_stamp = 0;  // GetTickCount of the last report
volatile LONG g_connected = 0;
volatile LONG g_readerRunning = 0;
int g_reportsLogged = 0;

// A consistent copy of g_state. The compiler barriers keep the copy between the two reads of the counter: without them
// GCC loads the state once before the loop and the loop only waits on g_seq.
State ReadState() {
    State s;
    LONG seq;
    do {
        seq = g_seq;
        __asm__ __volatile__("" ::: "memory");
        s = g_state;
        __asm__ __volatile__("" ::: "memory");
    } while ((seq & 1) || seq != g_seq);
    return s;
}

// "054c" anywhere in the device path, in any case: "vid_054c" over USB, "vid&0002054c" over Bluetooth. Other HID devices
// (keyboards, mice, headsets) are then not opened at all on every search.
bool MayBeSony(const wchar_t* p) {
    for (; p[0]; ++p)
        if (p[0] == L'0' && p[1] == L'5' && p[2] == L'4' && (p[3] | 0x20) == L'c') return true;
    return false;
}

bool LoadApis() {
    HMODULE hid = LoadLibraryW(L"hid.dll");
    HMODULE setup = LoadLibraryW(L"setupapi.dll");
    if (!hid || !setup) return false;
    hidGetGuid = (HidGetGuidFn)(void*)GetProcAddress(hid, "HidD_GetHidGuid");
    hidGetAttr = (HidGetAttrFn)(void*)GetProcAddress(hid, "HidD_GetAttributes");
    hidGetPreparsed = (HidGetPreparsedFn)(void*)GetProcAddress(hid, "HidD_GetPreparsedData");
    hidFreePreparsed = (HidFreePreparsedFn)(void*)GetProcAddress(hid, "HidD_FreePreparsedData");
    hidGetFeature = (HidGetFeatureFn)(void*)GetProcAddress(hid, "HidD_GetFeature");
    hidGetCaps = (HidGetCapsFn)(void*)GetProcAddress(hid, "HidP_GetCaps");
    setupGetClassDevs = (SetupGetClassDevsFn)(void*)GetProcAddress(setup, "SetupDiGetClassDevsW");
    setupEnum = (SetupEnumInterfacesFn)(void*)GetProcAddress(setup, "SetupDiEnumDeviceInterfaces");
    setupGetDetail = (SetupGetDetailFn)(void*)GetProcAddress(setup, "SetupDiGetDeviceInterfaceDetailW");
    setupDestroy = (SetupDestroyFn)(void*)GetProcAddress(setup, "SetupDiDestroyDeviceInfoList");
    return hidGetGuid && hidGetAttr && hidGetPreparsed && hidFreePreparsed && hidGetCaps && setupGetClassDevs &&
           setupEnum && setupGetDetail && setupDestroy;
}

// Why a Sony device was passed over, once per reason: a pad that "does not connect" is otherwise silent in the log.
int g_skipLogs = 0;
void NoteSkipped(const char* what, unsigned value) {
    if (g_skipLogs >= 6) return;
    static unsigned seen[6];
    static int count = 0;
    const unsigned key = (unsigned)what[0] << 24 ^ value;
    for (int i = 0; i < count; ++i)
        if (seen[i] == key) return;
    if (count < 6) seen[count++] = key;
    ++g_skipLogs;
    Log(what, value);
}

// Opens the first DualSense; returns the handle and the input report length (64 = USB, 78 = Bluetooth), or null.
HANDLE OpenDualSense(int* inputLength, USHORT* product, int* outputLength = nullptr, int* featureLength = nullptr) {
    GUID guid;
    hidGetGuid(&guid);
    HANDLE set = setupGetClassDevs(&guid, nullptr, nullptr, 0x2 | 0x10 /*PRESENT | DEVICEINTERFACE*/);
    if (set == INVALID_HANDLE_VALUE) return nullptr;
    HANDLE found = nullptr;
    for (DWORD i = 0; !found; ++i) {
        SpDeviceInterfaceData data;
        data.size = sizeof(data);
        if (!setupEnum(set, nullptr, &guid, i, &data)) break;
        DWORD need = 0;
        setupGetDetail(set, &data, nullptr, 0, &need, nullptr);
        if (need < 8 || need > 1024) continue;
        u8 buf[1024];
        *(DWORD*)buf = 6;  // sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) on x86
        if (!setupGetDetail(set, &data, buf, need, nullptr, nullptr)) continue;
        const wchar_t* path = (const wchar_t*)(buf + 4);
        if (!MayBeSony(path)) continue;
        HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               0, nullptr);
        if (h == INVALID_HANDLE_VALUE) h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            NoteSkipped("dualsense: a Sony device could not be opened (error %u: another program such as DSX, DS4Windows or "
                        "HidHide may hold or hide it)", (unsigned)GetLastError());
            continue;
        }
        HidAttributes a;
        a.size = sizeof(a);
        if (hidGetAttr(h, &a) && a.vendor == 0x054C && (a.product == 0x0CE6 || a.product == 0x0DF2)) {
            void* pre = nullptr;
            u16 caps[32] = {};
            if (hidGetPreparsed(h, &pre) && pre) {
                hidGetCaps(pre, caps);
                hidFreePreparsed(pre);
            }
            *inputLength = caps[2];  // InputReportByteLength
            if (outputLength) *outputLength = caps[3];
            if (featureLength) *featureLength = caps[4];
            *product = a.product;
            found = h;
        } else {
            if (a.vendor == 0x054C) NoteSkipped("dualsense: a Sony device with product %04X is not a DualSense, passed over", a.product);
            CloseHandle(h);
        }
    }
    setupDestroy(set);
    return found;
}

void Store(const State& s) {
    InterlockedIncrement(&g_seq);  // odd: being written
    g_state = s;
    g_stamp = GetTickCount();
    InterlockedIncrement(&g_seq);
}

DWORD WINAPI Reader(void*) {
    int lines = 0;        // "found" / "disconnected" lines: a pad that opens but cannot be read must not flood the log
    DWORD backOff = 0;    // grows while every read of a freshly opened pad fails
    for (;;) {
        int inLen = 0, featLen = 0;
        USHORT product = 0;
        HANDLE h = OpenDualSense(&inLen, &product, nullptr, &featLen);
        if (!h) {
            Sleep(2000);
            continue;
        }
        if (inLen < 10 || inLen > 600) inLen = 78;  // a read shorter than InputReportByteLength fails, so the buffer follows the device
        const bool bt = inLen >= 70;
        if (lines < 20) {
            ++lines;
            Log("dualsense: found (product %04X), input report %d bytes (%s)", (unsigned)product, inLen, bt ? "Bluetooth" : "USB");
        }
        if (bt) {  // asking for the calibration report (05h) switches a Bluetooth pad from the simple report to the full report 31h
            u8 feat[64] = {0x05};
            bool ok = hidGetFeature && hidGetFeature(h, feat, featLen > 0 && featLen <= 64 ? (ULONG)featLen : 41);
            if (lines < 20) Log("dualsense: Bluetooth full mode request %s", ok ? "sent" : "failed (the simple report is read instead)");
        }
        InterlockedExchange(&g_connected, 1);
        u8 buf[600];
        bool readAny = false;
        DWORD err = 0;
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(h, buf, (DWORD)inLen, &got, nullptr) || got == 0) {
                err = GetLastError();
                break;
            }
            readAny = true;
            if (g_reportsLogged < 4) {
                ++g_reportsLogged;
                char hex[200];
                int n = 0;
                for (DWORD i = 0; i < got && i < 24 && n < 190; ++i) n += wsprintfA(hex + n, "%02X ", buf[i]);
                Log("dualsense: report (%u bytes) %s", (unsigned)got, hex);
            }
            State s;
            if (DsParseInput(buf, (int)got, bt, &s)) Store(s);
        }
        CloseHandle(h);
        InterlockedExchange(&g_connected, 0);
        if (lines < 20) {
            ++lines;
            Log("dualsense: disconnected (read error %u)", (unsigned)err);
        }
        // A pad that went away fails the next open and sleeps there. One that opens but cannot be read would loop here at full speed.
        backOff = readAny ? 0 : (backOff ? (backOff < 8000 ? backOff * 2 : 16000) : 1000);
        if (backOff) Sleep(backOff);
    }
}

}  // namespace

bool DualSenseConnected() { return g_connected != 0; }
bool DualSenseReaderRunning() { return g_readerRunning != 0; }

// The touchpad of the DualSense is pressed (the click, bit 1 of the special byte).
bool DsTouchpadDown() {
    if (!g_connected) return false;
    return (ReadState().special & 2) != 0;
}

// The two trigger axes (0..255) of the latest report (physical L2 and R2, SwapTriggers is not applied).
void DsTriggerAxes(unsigned char* l2, unsigned char* r2) {
    State s = ReadState();
    *l2 = s.l2;
    *r2 = s.r2;
}

// For the output module: the same device opening, on demand.
bool DsApisReady() {
    static bool tried = false, ok = false;
    if (!tried) {
        tried = true;
        ok = LoadApis();
    }
    return ok;
}
void* DsOpen(int* inLen, int* outLen, int* featLen, unsigned short* product) {
    USHORT pid = 0;
    HANDLE h = OpenDualSense(inLen, &pid, outLen, featLen);
    if (product) *product = pid;
    return h;
}
bool DsGetFeature(void* handle, unsigned char* buf, unsigned long len) {
    return hidGetFeature && hidGetFeature((HANDLE)handle, buf, len);
}

// Fills the JOYINFOEX of pad 0 from the DualSense in the Xbox 360 layout (Z: LT above the centre, RT below).
bool DualSenseToJoy(void* raw) {
    JOYINFOEX* j = (JOYINFOEX*)raw;
    if (!j || j->dwSize != sizeof(JOYINFOEX) || !g_connected) return false;
    State s = ReadState();
    if (GetTickCount() - g_stamp > 500) return false;  // no report for half a second
    if (g_cfg.swapTriggers) {  // the same swap as on the WinMM path (sony_layout.cpp): analog values and the click bits together
        u8 t = s.l2;
        s.l2 = s.r2;
        s.r2 = t;
        s.shoulder = (u8)((s.shoulder & ~0x0C) | ((s.shoulder & 0x04) << 1) | ((s.shoulder & 0x08) >> 1));
    }
    j->dwXpos = (u32)s.lx * 257;
    j->dwYpos = (u32)s.ly * 257;
    j->dwUpos = (u32)s.rx * 257;
    j->dwRpos = (u32)s.ry * 257;
    j->dwZpos = (u32)(32767 + ((int)s.l2 - (int)s.r2) * 128);
    j->dwVpos = 32767;
    // face: square cross circle triangle -> X A B Y ; Xbox button numbers: A 0, B 1, X 2, Y 3, LB 4, RB 5, Back 6, Start 7, LS 8, RS 9
    DWORD b = 0;
    if (s.face & 2) b |= 1u << 0;   // cross -> A
    if (s.face & 4) b |= 1u << 1;   // circle -> B
    if (s.face & 1) b |= 1u << 2;   // square -> X
    if (s.face & 8) b |= 1u << 3;   // triangle -> Y
    if (s.shoulder & 0x01) b |= 1u << 4;   // L1 -> LB
    if (s.shoulder & 0x02) b |= 1u << 5;   // R1 -> RB
    if (s.shoulder & 0x10) b |= 1u << 6;   // Create -> Back (Select)
    if (s.shoulder & 0x20) b |= 1u << 7;   // Options -> Start
    if (s.shoulder & 0x40) b |= 1u << 8;   // L3
    if (s.shoulder & 0x80) b |= 1u << 9;   // R3
    if (s.l2 > 64 || (s.shoulder & 0x04)) b |= 1u << 10;   // L2 as its own button: aim and fire on the triggers can be held together
    if (s.r2 > 64 || (s.shoulder & 0x08)) b |= 1u << 11;   // R2
    j->dwButtons = b;
    int count = 0;
    for (int i = 0; i < 12; ++i) count += (b >> i) & 1;
    j->dwButtonNumber = (DWORD)count;
    j->dwPOV = s.hat >= 8 ? 0xFFFF : (DWORD)s.hat * 4500;
    MarkTriggerButtons();
    return true;
}

bool InstallDualSense() {
    if (!LoadApis()) {
        Log("dualsense: hid.dll / setupapi.dll are not available, native DualSense input is off");
        return false;
    }
    HANDLE t = CreateThread(nullptr, 0, Reader, nullptr, 0, nullptr);
    if (!t) return false;
    CloseHandle(t);
    InterlockedExchange(&g_readerRunning, 1);
    Log("dualsense: reader started (looking for a DualSense over USB or Bluetooth)");
    return true;
}

}  // namespace ufx
