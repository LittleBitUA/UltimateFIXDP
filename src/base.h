// UltimateFIXDP: shared declarations. Freestanding build: no C runtime, only kernel32/user32.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

namespace ufx {

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

constexpr const char* kVersion = "0.6.8";

struct Config {
    int relativeQpc;         // QueryPerformanceCounter relative to process start
    int zeroDeltaGuard;      // frameDelta == 0 must not turn movement speed into NaN/INF
    int saveGuard;           // atomic dp.sav writes, backups, restore of a damaged file
    int saveBackups;         // how many backups to keep (1..200)
    int saveRestoreDamaged;  // restore dp.sav from the newest backup if its size is wrong
    int saveWarnings;        // show a message box when a save could not be committed
    int houseListFix;        // correct byte-swap format of HOUSE_LIST.NOD (day/night in buildings)
    int removeExtraJoyPoll;  // drop the per-frame joyGetPosEx call that passes uninitialised memory
    int skipIntro;           // skip publisher logos
    int resetRefreshFix;     // windowed Reset() no longer fails after Alt+Tab
    int borderless;          // borderless window that fills the monitor (the picture is scaled keeping 16:9 with scaleToWindow)
    int hideCursor;          // hide the mouse cursor over the game window
    int dpiAware;            // do not let Windows scale the window (blurry on 125%/150% displays)
    int scaleToWindow;       // scale the back buffer to the borderless window ourselves (bilinear, keeps 16:9); 0 = the game's Present fills the window
    int displayDebug;        // log the first frames and changes of the presenting path, and the window around Esc presses
    int menuOpacity;         // how opaque the F10 panel is, percent 20..100
    int language;            // language of the menu: 0 Ukrainian, 1 English
    int menu;                // F10 settings menu inside the game
    int traceResources;      // log every texture/render target/depth surface the game creates (diagnostic)
    int forceUsSlot;         // language slot US (6) regardless of config.cnf: where translations live
    int farClass3;           // culling distance of visibility class 3 (game: 5000)
    int farClass4;           // class 4 (game: 1000)
    int farClass5;           // class 5, objects without any class bit (game: 500)
    int sonyLayout;          // convert DualSense/DualShock axes and buttons to the Xbox 360 layout
    int adaptiveTriggers;    // DualSense adaptive trigger effects by weapon
    int adaptiveStrength;    // percent
    int adaptiveAim;         // a light resistance on the aim trigger while a weapon is drawn
    int triggerTest;         // menu: play a test effect (not saved)
    int vibration;           // send the game's rumble to the pad: a DualSense over HID when one is open, otherwise XInput
    int vibrationStrength;   // percent
    int analogTriggers;      // analog gas and brake in vehicles (the PC port reduces them to on/off)
    int triggerDeadzone;     // trigger dead zone, 0..254 (console: 30)
    int lowLatencyInput;     // poll the controller before committing the input snapshot (one tick less delay)
    int stickProfile;        // 1 = Xbox 360 stick curve (24% dead zone, no slew), 0 = the PC port's own
    int defaultPadBindings;  // give the game the Xbox layout when configJ.cnf / configJex.cnf are missing
    int nativeDualSense;     // read a DualSense directly over HID (no Steam Input needed)
    int xinputFallback;      // use an XInput pad when WinMM has none
    int onlyFirstPad;        // do not poll joystick slots 1..6 (the game only uses slot 0)
    int timerResolution;     // ask Windows for a 1 ms timer resolution
    int autoSwitchInput;     // switch the game between keyboard and gamepad mode by activity
    int swapTriggers;        // swap L2/R2 after the conversion
    int outputWidth;         // real output resolution of the picture, 0 = off (1280x720 as the game)
    int outputHeight;        // read from the ini but ignored: the output is always outputWidth * 9 / 16
    int renderScale;         // internal resolution of the scene in percent of the output (1280x720 without one; 100 = off)
    int ambientOcclusion;    // strength of the ambient occlusion in percent (0 = off)
    int aoRadius;            // radius in pixels at 720p
    int antialias;           // edge smoothing of the final picture in percent (0 = off)
    int sharpen;             // sharpening of the final picture in percent (0 = off)
    int screenshotKey;       // virtual key code for a PNG screenshot, 0 = off
    int glyphTheme;          // interface icons for the pad: 0 off, 1 auto (PlayStation for a DualSense), 2 Xbox, 3 PlayStation
    int dumpTextures;        // write the original textures to UltimateFix\\textures\\dump (diagnostic)
    int textureOverride;     // replace textures by hash from a pack folder
    int pixelOffsetFix;      // half-pixel constant of the 2D pass follows the output viewport
    int shadowPrecision;     // shadow depth buffers in 32-bit float instead of 16-bit
    int shadowScale;         // shadow map size in percent (100 = original 512/1024)
    int scalePostBuffers;    // also enlarge the blur/bloom storage buffers with the scene
    int anisotropy;          // maximum anisotropic filtering, 1..16 (the game uses 8)
    int xboxExposure;        // no 0.85 / 0.75 exposure factors, as on the console
    int reflectionBiasFix;   // reflection bias matrix uses the height instead of the width twice
    int difficultyEnabled;   // take control of the difficulty byte (the PC port always plays on Easy)
    int difficulty;          // 0 easy, 1 normal, 2 hard: where the new-game selection screen opens, and the menu writes it at any time
    int difficultyDialog;    // show the game's own selection screen before a new game
    int keepSavedDifficulty; // do not reset the difficulty to easy when a slot is loaded
    int wetReflections;      // the wet-surface reflection gate of the renderer
    int aimTolerance;        // camera angle limits compare with a tolerance
    int activationScale;     // object activation radius multiplier 1..3 (game: 1)
    int lodScale;            // mesh LOD distance multiplier 1..4 (game: 1)
    int highDetail;          // 1 = the outer streaming cells also use the full resource set (needs memory)
    int interiorVisibility;  // 0 = off, 1 = bridge the first visibility-volume test, 2 = both tests
    int crashGuards;         // bounds / NULL guards for the NPC navigation table crash
    // photo mode (runtime only, not stored in the ini)
    int photoActive;         // the free camera flies
    int photoFreeze;         // the game's time stands still
    int photoHud;            // the interface is hidden
    int photoFovOn;          // 1 = the photo mode sets the field of view itself (freeCamFov degrees)
    int photoRoll;           // camera tilt in degrees
    int photoFilter;         // 0 none, 1 warm, 2 cold, 3 black and white, 4 cinema, 5 vintage
    int photoBright, photoContrast, photoSat, photoVignette;  // -100..100, vignette 0..100
    int photoSuitSet;        // costume set: York 1..2, Zach 1..3
    int photoCostume;        // costume choice, 0 = untouched
    int photoAnim;           // animation choice, 0 = untouched
    int freeCam;             // the free camera (fly with the mouse and keys, hide the HUD)
    int freeCamKey;          // virtual key that switches it on and off
    int freeCamHudKey;       // virtual key that hides / shows the HUD
    int freeCamSpeed;        // world units per second
    int freeCamFreeze;       // 1 = the game gets no input while the camera flies
    int freeCamFov;          // degrees, 0 = the game's own
    int mapFix;              // the full map: all tiles, zoom out to the whole town, fixed orientation
    int mapOrientation;      // 0 rotating (game), 1 north up, 2 south up
    int mapMinZoom;          // percent, 10..100
    int mapDefaultZoom;      // percent
    int crashDump;           // write a report and a minidump when the game crashes
    int traceCamera;         // log player state changes, camera jumps and the car input once a second (diagnostic, off by default)
    int byteTextCyrillic;    // byte-string text with characters of 80h and above (a single-byte Cyrillic code page) draws right
    int keepBlurLook;        // keep the screen size of the blur / depth-of-field with a bigger scene
    int alt3dScale;          // 1 = as the game, 2..3 = the full model of far objects reaches that much farther
    int halveFlashes;        // muzzle flashes, splashes and hits run at the console's speed
    int cheatGod;            // the current health is brought back to the maximum every frame
    int cheatClockStop;      // the in-game clock stands still
    int cheatClockSpeed;     // clock speed in tenths (10 = as the game)
    int frameStats;          // separate panel: frame time split (game, physics, Present), worst frame, graph
    int perfOverlay;         // FPS, frame time and the GPU time of the mod's passes on the screen
    int triggerFire;         // aim on L2 and fire on R2 (run on L1, hold breath on R1) instead of the shoulder buttons
    int safeMode;            // skips some newer hooks (free camera, physics, player update bridge, alternate 3D, waypoint, icon index, effect step,
                             // SSAO, blur look) and zeroes the newest settings in LoadConfig; the rest stays on (see the ini text)
    int traceStates;         // log every change of the player state
    int combatStrafe;        // LB / RB strafe in combat while locked on, as on the console
    int quickTurn;           // stick back + the run button turns York around on the spot (player state 0Bh), as on the console
    int synthHaptics;        // DualSense: engine hum in a car and a kick for each shot (made from the game state)
    int noPowerThrottling;   // exempt the game from Windows efficiency mode (EcoQoS)
    int performanceCoresOnly; // hybrid CPUs: run the game on the performance cores only
    int autoProfile;         // start the sampling profiler by itself after 300 slow frames in a row (over 20 ms, over 40 ms in the 30 fps mode)
    int throttleMissingPad;  // reuse a failed joyGetPosEx(0) for half a second instead of asking Windows every frame
    int menuX, menuY, menuW, menuH;  // place and size of the F10 panel (moved and resized with the mouse)
    int waypoint;            // F7 waypoint: a cross on the pause map, the distance in yards over the place in the world
    int waypointKey;         // virtual key that sets the waypoint (F7)
    int waypointMinimap;     // the waypoint is also an objective point of the game's map: red chevrons on the minimap, icon on the pause map
    int replaceLauncher;     // put the quick-start stub in place of DPLauncher.exe by itself (the ufx_play.exe job)
    int autoRecover;         // (experimental, not proven to help) after a long run of slow frames, hold the game clock and physics for 4 frames
    int physicsSteps;        // most fixed physics steps per frame: 0 = the game's own (up to 4), 1..3 = a cap
    int pauseInMenu;         // the game's time stands still while the F10 menu is open
    int framePacing;         // 0 off, 1 = frame limit 60 only (game keeps real time), 2 = 30 frames/s in whole steps (console), 3 = 60 in whole steps
};

struct Paths {
    wchar_t gameDir[MAX_PATH];        // folder of DP.exe, with trailing backslash
    wchar_t dataDir[MAX_PATH];        // <gameDir>UltimateFix\ (logs, backups)
    wchar_t iniFile[MAX_PATH];        // <gameDir>UltimateFix.ini
    wchar_t saveBackupDir[MAX_PATH];  // <dataDir>savebackups\ , no trailing backslash
};

extern Config g_cfg;
extern Paths g_paths;

// Where the back buffer's picture sits inside the window client area (kept current by the display module every frame).
struct FitRect {
    int x, y, w, h;
};
extern FitRect g_fit;
HWND GameWindowHandle();

// log.cpp
void LogInit(const wchar_t* path);
void Log(const char* fmt, ...);

// config.cpp
void LoadConfig(const wchar_t* iniFile);
extern int g_clampedOutput;  // the output width that was too big for the monitor (0 = none), for the log; the ini keeps it
int MonitorFitWidth();       // the widest 16:9 picture that fits the monitor of the game window (the primary one before it exists)
const char* DefaultIniText(unsigned long* length);  // the default ini as the DLL writes it

// pe.cpp
bool IsSupportedBuild(HMODULE exe);
bool HasLargeAddressAware(HMODULE exe);
void** FindImportSlot(HMODULE module, const char* dll, const char* func);

// patch.cpp
bool BytesMatch(u32 va, const u8* expected, size_t len);
bool WriteCode(u32 va, const u8* bytes, size_t len);
bool SwapImport(void** slot, void* replacement, void** previousOut);
u8* AllocStub(size_t size);
void PutRel32(u8* at, u32 nextInstrVa, u32 targetVa);
bool PatchJump(u32 va, const u8* expected, size_t len, const void* target);
bool PatchCall(u32 va, const u8* expected, size_t len, const void* target);
u32 UserAddressEnd();  // the top of the user address space (large-address aware), rounded down to 64 KB

// modules (each returns true if it installed)
bool InstallQpcTime();
bool InstallZeroDelta();
bool InstallSaveGuard();
bool InstallHouseList();
bool InstallJoyPoll();
bool InstallSkipIntro();
bool InstallLanguageSlot();
bool InstallResetRefreshFix();
bool InstallDisplay();
bool InstallGraphicsTweaks();
bool InstallD3dTrace();
bool InstallOutputResolution();
bool InstallWorldDistance();
void ApplyWorldDistance();
void ConvertSonyLayout(unsigned id, void* joyInfoEx);
bool InstallStickProfile();
bool XInputToJoy(void* joyInfoEx);
bool InstallDualSense();
bool InstallDualSenseOutput();
bool DsOutputReady();
void DsSetRumble(unsigned char strong, unsigned char weak);
void AdaptiveTriggerFrame();
bool PlayerStateNow(u32* state);
bool DualSenseToJoy(void* joyInfoEx);
bool DualSenseConnected();
bool InstallInputExtras();
bool InstallVehicleTriggers();
void VibrationFrame();   // once per frame from the Present hook
void StopVibration();
void TriggerCapture(const void* joyInfoEx, bool menuOpen);
bool InstallCrashHandler();
bool InstallCrashGuards();
bool InstallMapFix();
void ApplyMapFix();
bool InstallTextureOverride();
void* GlyphAtlasFor(void* device, void* texture);  // SetTexture hook: the pad icons instead of the keyboard atlas
bool SonyPadActive();
bool InstallWorldExtra();
bool InstallFreeCam();
bool InstallAmbientOcclusion();
bool InstallBlurLook();
bool InstallCombatStrafe();
bool InstallQuickTurn();
bool InstallGlyphIndexFix();
void CheatsTick(void* player);
void CheatMoney(int amount);
void CheatSetHour(int hour);
void CheatWeather(int type);
void CheatAmmo();
void CheatYorkToCamera();
int WeaponClassNow();
bool InstallAlternate3D();
bool InstallPhysicsFreeze();
extern bool g_usingDxvk;   // the Direct3D 9 in use is DXVK, recognised by its texts in the d3d9.dll image or its export (d3d/trace.cpp)
extern bool g_d3dWrapper;  // another d3d9.dll that is not the Windows one (DPfix, ReShade, dgVoodoo...)
void HangBeat(bool windowThread);  // every Present: the hang watchdog (crash_handler.cpp)
void PacerBeforePresent();                           // the 60 frames/s limit, in front of the Present
void PacerAfterPresent(u64 returnedAt, double presentMs);
double TakePhysicsMs(int frames);  // PhysX step time per frame since the last call
void SetPhysicsFrozen(bool frozen);  // skip the PhysX step (the F10 menu and the photo mode freeze)
void PhotoThaw(int frames);  // let game time run for a number of frames even while paused
void RequestPose(int stateAndOne);  // photo mode: 0 untouched, n = state n-1 (applied at the next player update)
void RequestSuit(int variant);  // 1..10, applied at the next player update (photo mode)
unsigned PadButtons();  // buttons of the last pad data handed to the game (0 = no pad)
void EnsureLauncherStub(const wchar_t* gameDir);  // in the game: put the quick-start stub in place of DPLauncher.exe
void InstallCpuTweaks();
bool InstallWaypoint();
void WaypointFrame(void* device);  // once per frame from the Present hook: the waypoint keys and the state its drawing needs
bool InstallByteTextFix();
bool InstallEffectStep();
extern int g_sceneWidth;  // width of the 3D scene after the render scale (0 = not scaled)
bool FreeCamBlocksInput();  // true while the camera flies and the game must see no input
void ApplyWorldExtra();  // writes the current settings (also called from the menu)
void ApplyInputExtras();  // writes the current input settings (also called from the menu)
void PadCapture(const void* joyInfoEx);  // pad 0 data as the game is about to receive it, null = none
void AutoInputTick(unsigned id, const void* joyInfoEx, unsigned result, SHORT(WINAPI* getKey)(int));  // called by the joyGetPosEx hook
bool InstallRenderScale();
extern int g_outputW;  // 0 when the output resolution module is not active
extern int g_outputH;
bool InstallShadowScale();
bool InstallGameplay();
bool InstallFramePacer();
void TraceCameraFrame();  // once per frame, from the Present hook
int DisplayRefreshHz();  // refresh rate of the game window's monitor, cached (frame_pacer.cpp)
void ApplyGameplay();
void ApplyDifficulty();
void SyncDifficultyFromGame();  // reads the difficulty the game really has into g_cfg (the menu, while open)
void ApplyGraphicsTweaks();  // writes the current settings (also called from the menu)

namespace ui {
bool Install();                      // input blocking hooks; returns true if the menu is available
void OnFrame(void* device);          // called once per frame right before Present
bool IsOpen();
void SharpenFrame(void* device, int percent);
void AntialiasFrame(void* device, int percent);
void ColorFrame(void* device);
void RequestScreenshot();
void OpenPhotoPage();
void PerfPostBegin(void* device);
void PerfPostEnd(void* device);
void PerfOverlayFrame(void* device, double fps, double frameMs, double presentMs, double physicsMs);
void FrameStatsPush(double frameMs);  // one finished frame for the graph
void FrameStatsPanel(void* device, double totalMs, double gameMs, double physicsMs, double presentMs, double worstMs, double busyMs);
bool PhotoPageOpen();  // the menu is open on the photo mode page
void ScreenshotFrame(void* device);
void ReleasePostResources();
}  // namespace ui

}  // namespace ufx
