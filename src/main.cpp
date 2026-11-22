// Entry point. This DLL is installed as X3DAudio1_7.dll next to DP.exe: the game imports two
// functions from it, so Windows loads our copy first. It forwards both functions to the real DLL
// (proxy.cpp) and applies the fixes before any game code runs.
#include "base.h"

namespace ufx {

Paths g_paths;
bool g_dpfixPresent = false;  // DPfix.ini next to the game: the window and the resolution are left to DPfix (display.cpp reads it)

static bool HostIsDpExe() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/') base = p + 1;
    return lstrcmpiW(base, L"DP.exe") == 0;
}

static bool BuildPaths() {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    wchar_t* slash = nullptr;
    for (wchar_t* p = exe; *p; ++p)
        if (*p == L'\\') slash = p;
    if (!slash) return false;
    slash[1] = 0;  // keep the trailing backslash
    // Every module builds its names from these paths into MAX_PATH buffers. The longest is the save backup name
    // (saves.cpp BackupName): <game>\UltimateFix\savebackups\uncommitted_YYYYMMDD_HHMMSS_mmm.sav, 59 characters after
    // the folder; the texture override and pad icon paths need up to 43. A game folder too deep for that, with some
    // room to spare, is refused instead of overflowing a buffer later.
    if (lstrlenW(exe) + 64 >= MAX_PATH) return false;
    lstrcpyW(g_paths.gameDir, exe);

    lstrcpyW(g_paths.dataDir, g_paths.gameDir);
    lstrcatW(g_paths.dataDir, L"UltimateFix\\");
    CreateDirectoryW(g_paths.dataDir, nullptr);

    lstrcpyW(g_paths.saveBackupDir, g_paths.dataDir);
    lstrcatW(g_paths.saveBackupDir, L"savebackups");
    CreateDirectoryW(g_paths.saveBackupDir, nullptr);

    // The game never creates its own save folder: when savedata\ is missing (a fresh or cleaned install) every save ends
    // with "Save failed". Creating it is harmless when it exists.
    wchar_t saveDir[MAX_PATH];
    lstrcpyW(saveDir, g_paths.gameDir);
    lstrcatW(saveDir, L"savedata");
    CreateDirectoryW(saveDir, nullptr);

    lstrcpyW(g_paths.iniFile, g_paths.gameDir);
    lstrcatW(g_paths.iniFile, L"UltimateFix.ini");
    return true;
}

static bool HostIsLauncher() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/') base = p + 1;
    return lstrcmpiW(base, L"DPLauncher.exe") == 0;
}

static void Startup() {
    // DPLauncher.exe does not import X3DAudio1_7.dll, so this only happens when something loads the DLL into it by hand: stay a
    // plain proxy there. The launcher is skipped by the quick-start stub (ReplaceLauncher, ufx_play.exe), not from here.
    if (HostIsLauncher()) return;
    // Loaded by some other program: stay a plain proxy. The game is recognised by its name or, when the exe was
    // renamed (some launchers do), by the build identity in its PE header.
    if (!HostIsDpExe() && !IsSupportedBuild(GetModuleHandleW(nullptr))) return;
    if (!BuildPaths()) return;

    // The log is opened before the settings are read, so that what LoadConfig reports (SafeMode, one-time
    // migrations of old values) reaches ufx.log too.
    wchar_t logPath[MAX_PATH], prevPath[MAX_PATH];
    lstrcpyW(logPath, g_paths.dataDir);
    lstrcatW(logPath, L"ufx.log");
    lstrcpyW(prevPath, g_paths.dataDir);
    lstrcatW(prevPath, L"ufx-prev.log");
    MoveFileExW(logPath, prevPath, MOVEFILE_REPLACE_EXISTING);
    LogInit(logPath);
    Log("UltimateFIXDP %s", kVersion);
    LoadConfig(g_paths.iniFile);

    HMODULE exe = GetModuleHandleW(nullptr);
    if (!IsSupportedBuild(exe)) {
        Log("DP.exe is not the Steam 1.01b build this version supports: every fix is switched off");
        return;
    }
    Log("DP.exe 1.01b recognised; large address aware: %s", HasLargeAddressAware(exe) ? "yes" : "no");
    EnsureLauncherStub(g_paths.gameDir);
    InstallCpuTweaks();

    // DPfix (d3d9.dll wrapper) manages the window, the resolution and the buffers itself: leave those to it so
    // the two do not fight over the same things. Everything else stays, including the per-frame layer of the
    // Present hook (F10 menu, frame limit, rumble and triggers, waypoint, post filters, hang watchdog).
    wchar_t dpfix[MAX_PATH];
    lstrcpyW(dpfix, g_paths.gameDir);
    lstrcatW(dpfix, L"DPfix.ini");
    if (GetFileAttributesW(dpfix) != INVALID_FILE_ATTRIBUTES) {
        Log("DPfix found next to the game: the borderless window, output resolution, render scale, shadow size and "
            "post buffer size are left to it; the frame limit, controller and other fixes stay, and the F10 menu and the "
            "panels are laid out for a 1280x720 back buffer");
        g_dpfixPresent = true;
        g_cfg.borderless = 0;
        g_cfg.outputWidth = 0;
        g_cfg.outputHeight = 0;
        g_cfg.renderScale = 100;
        g_cfg.shadowScale = 100;
        g_cfg.scalePostBuffers = 0;
    } else if (g_clampedOutput) {  // the limit LoadConfig applied (only for this run: the ini keeps the player's value)
        Log("display: OutputWidth=%d is larger than the monitor allows, %d is used for this run, the ini keeps it (the upscale is RenderScale)",
            g_clampedOutput, g_cfg.outputWidth);
    }

    int on = 0;
    if (g_cfg.timerResolution) {
        HMODULE winmm = LoadLibraryW(L"winmm.dll");
        typedef UINT(WINAPI * TimeBeginFn)(UINT);
        TimeBeginFn begin = winmm ? (TimeBeginFn)(void*)GetProcAddress(winmm, "timeBeginPeriod") : nullptr;
        if (begin && begin(1) == 0) {
            Log("timer: Windows timer resolution set to 1 ms");
            ++on;
        }
    }
    if (g_cfg.crashDump && InstallCrashHandler()) ++on;
    if (InstallAudioGuard()) ++on;  // always: it only acts on values that would break the sound
    if (g_cfg.relativeQpc && InstallQpcTime()) ++on;
    if (InstallFramePacer()) ++on;
    // The mod's own pause (F10 with PauseInMenu, the photo mode, AutoRecover) hands the game frames of zero length: the guard
    // stays in for those even when ZeroDeltaGuard=0.
    if (!g_cfg.zeroDeltaGuard) Log("zero-delta: ZeroDeltaGuard=0 is not followed: the mod's own pause makes frames of zero length, the guard stays");
    if (InstallZeroDelta()) ++on;
    if (InstallStability()) ++on;
    InstallAutoLargeAddress();
    InstallUpdater();
    if (g_cfg.saveGuard && InstallSaveGuard()) ++on;
    if (g_cfg.houseListFix && InstallHouseList()) ++on;
    if (g_cfg.removeExtraJoyPoll && InstallJoyPoll()) ++on;
    if (g_cfg.skipIntro && InstallSkipIntro()) ++on;
    if ((g_cfg.forceUsSlot || g_cfg.defaultPadBindings) && InstallLanguageSlot()) ++on;
    if (g_cfg.resetRefreshFix && InstallResetRefreshFix()) ++on;
    if (g_cfg.zeroFrameMenus && InstallZeroFrameMenus()) ++on;
    // Always: the Present hook carries the whole per-frame layer, not only the borderless window.
    if (InstallDisplay()) ++on;
    if (InstallWorldDistance()) ++on;
    if (InstallOutputResolution()) ++on;
    if (InstallRenderScale()) ++on;
    if (InstallShadowScale()) ++on;
    if (InstallGraphicsTweaks()) ++on;
    if (InstallGameplay()) ++on;
    if (InstallStickProfile()) ++on;
    if (g_cfg.crashGuards && InstallCrashGuards()) ++on;
    if (g_cfg.resourceReadGuard && !g_cfg.safeMode && InstallResourceGuard()) ++on;
    if (g_cfg.mapFix && InstallMapFix()) ++on;
    ScanMods();  // before the overlays that read the mods' folders
    if (InstallGameFiles()) ++on;
    if ((g_cfg.textureOverride || g_cfg.dumpTextures || g_cfg.glyphTheme || GameFileTextures() || g_cfg.generateMips) && InstallTextureOverride()) ++on;
    if (g_cfg.nativeDualSense && InstallDualSense()) ++on;
    // The writer also carries SynthHaptics and the menu's trigger test, which work with AdaptiveTriggers and Vibration off.
    if ((g_cfg.adaptiveTriggers || g_cfg.vibration || g_cfg.synthHaptics || g_cfg.menu) && InstallDualSenseOutput()) ++on;
    if (InstallInputExtras()) ++on;
    if (InstallVehicleTriggers()) ++on;
    if (InstallWorldExtra()) ++on;
    if (!g_cfg.safeMode && InstallFreeCam()) ++on;
    if (!g_cfg.safeMode && InstallAlternate3D()) ++on;
    if (!g_cfg.safeMode && InstallPhysicsFreeze()) ++on;
    if (!g_cfg.safeMode && InstallHighFpsPhysics()) ++on;
    if (!g_cfg.safeMode && InstallCombatStrafe()) ++on;
    if (!g_cfg.safeMode && InstallQuickTurn()) ++on;
    if (!g_cfg.safeMode && g_cfg.waypoint && InstallWaypoint()) ++on;
    if (!g_cfg.safeMode && InstallNpcMap()) ++on;  // after the waypoint: its map pass draws the residents
    if (!g_cfg.safeMode && InstallGlyphIndexFix()) ++on;
    if (g_cfg.byteTextCyrillic && InstallByteTextFix()) ++on;
    if (InstallTextTools()) ++on;
    if (!g_cfg.safeMode && g_cfg.halveFlashes && InstallEffectStep()) ++on;
    if (!g_cfg.safeMode && InstallAmbientOcclusion()) ++on;
    if (!g_cfg.safeMode && g_cfg.keepBlurLook && InstallBlurLook()) ++on;
    // The half-pixel fix is hooked whenever the output resolution is active, so that switching it on in the menu works at once.
    // Always: with none of these the layer only watches CreateDevice (a failure is reported instead of the game's crash).
    if (InstallD3dTrace(g_cfg.traceResources || g_cfg.shadowScale > 100 || g_cfg.shadowPrecision || g_cfg.glyphTheme || g_outputW || g_cfg.traceWater ||
                        g_cfg.waterTest || g_cfg.fogFix || g_cfg.smokeFix || g_cfg.frameCapture || g_cfg.farShadowFilter || g_cfg.riverWater)) ++on;
    // Always: besides the menu's own input blocking, the joyGetPosEx hook carries the missing-pad worker and throttle, the XInput
    // fallback, OnlyFirstPad, the native DualSense and the automatic input switch.
    if (ui::Install()) ++on;
    Log("startup finished: %d fixes active", on);
}

}  // namespace ufx

extern "C" BOOL WINAPI DllEntry(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        ufx::Startup();
    }
    return TRUE;
}
