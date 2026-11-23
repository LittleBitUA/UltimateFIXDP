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

constexpr const char* kVersion = "0.7.16";

struct Config {
    int relativeQpc;         // QueryPerformanceCounter relative to process start
    int zeroDeltaGuard;      // frameDelta == 0 must not turn movement speed into NaN/INF
    int nanTrapGuard;        // the angle normalisation returns 0 for a NaN instead of hanging (stability.cpp)
    int pinAudio;            // XAudio2 and XACT stay loaded for the whole run
    int pinFpu;              // the main thread's x87 precision stays at 24 bits, as a Direct3D 9 device sets it
    int saveGuard;           // atomic dp.sav writes, backups, restore of a damaged file
    int saveBackups;         // how many backups to keep (1..200)
    int saveRestoreDamaged;  // restore dp.sav from the newest backup if its size is wrong
    int serialSetThread;     // the main thread waits for SetThread at once (no world objects built during the update)
    int routeFlushWait;      // a level change lets RouteThread finish its request before the queue is cleared
    int lockOnAim;           // the lock-on aims at the chest: no drop with distance, LockOnHeight above the waist
    int lockOnHeight;        // tenths of a game unit above the target's waist bone (40 = 4.0)
    int smokeFix;            // smoke drawn with a black DXT3 texture gets its colour back from the alpha (the prologue doorways)
    int fogFix;              // smoke volumes are kept out of the G-buffer (the black hole in the prologue doorways)
    int qteFix;              // the stick-waggle QTE counts a swap as at 60 FPS (restart needed)
    int checkUpdates;        // ask GitHub for a newer release at the start (F9 on the start card / About page updates)
    int autoLargeAddress;    // the mod gives DP.exe the 4 GB flag itself (from the next start)
    int fasterLoading;       // the loader thread waits 1 ms instead of 16 between requests
    int compressBackups;     // the backup folder is NTFS-compressed (60 copies of 8 MB would be about 480 MB)
    int saveWarnings;        // show a message box when a save could not be committed
    int houseListFix;        // correct byte-swap format of HOUSE_LIST.NOD (day/night in buildings)
    int removeExtraJoyPoll;  // drop the per-frame joyGetPosEx call that passes uninitialised memory
    int skipIntro;           // skip publisher logos
    int resetRefreshFix;     // windowed Reset() no longer fails after Alt+Tab
    int zeroFrameMenus;      // a zero-length frame no longer closes the toolbox, side missions list or map mission view (restart needed)
    int forceZeroFrame;      // [Debug] every Nth frame the game gets the time of the frame before (dt == 0), to reproduce the menu hangs; 0 = off
    int borderless;          // borderless window that fills the monitor (the picture is scaled keeping 16:9 with scaleToWindow)
    int hideCursor;          // hide the mouse cursor over the game window
    int dpiAware;            // do not let Windows scale the window (blurry on 125%/150% displays)
    int scaleToWindow;       // scale the back buffer to the borderless window ourselves (bilinear, keeps 16:9); 0 = the game's Present fills the window
    int displayDebug;        // log the first frames and changes of the presenting path, and the window around Esc presses
    int menuOpacity;         // how opaque the F10 panel is, percent 20..100
    int menuAnimations;      // [Menu] Animations: the panel fades in, the selection glides, a switch knob slides
    int menuPadControl;      // [Menu] PadControl: the F10 menu on a gamepad (View + Menu opens it), the game does not see the pad meanwhile
    int language;            // language of the menu: 0 Ukrainian, 1 English
    int menu;                // F10 settings menu inside the game
    int missionLab;          // [Debug] MissionLab: experiments for the mission creator (Numpad 7 runs the mod's own event script)
    int frameCapture;        // [Debug] FrameCapture: Numpad 0 writes one frame, draw by draw, into a capture folder under UltimateFix
    int traceWater;
    int waterTest;           // experiment: 1 = the river's own texture shows at least 60 % (c10.z) on water draws          // log what every distinct water draw has bound (textures, c10/c13) (diagnostic)
    int traceResources;      // log every texture/render target/depth surface the game creates (diagnostic)
    int forceUsSlot;         // language slot US (6) regardless of config.cnf: where translations live
    int textOverlay;         // replace records of MES_ALL.MES in memory from UltimateFix\\text (mes_all.json of the SWERY tool, *.txt); inert without files
    int showMessageId;       // for translators: print the id of every drawn text record over it
    int glyphLog;            // for translators: log empty / out-of-atlas font cells and full glyph batches to loc.log
    int textDump;            // for translators: write the loaded MES_ALL.MES in the form of the tool's mes_all.json to mes_dump.json
    int farClass3;           // culling distance of visibility class 3 (game: 5000)
    int farClass4;           // class 4 (game: 1000)
    int farClass5;           // class 5, objects without any class bit (game: 500)
    int sonyLayout;          // convert DualSense/DualShock axes and buttons to the Xbox 360 layout
    int adaptiveTriggers;    // DualSense adaptive trigger effects by weapon
    int adaptiveStrength;    // percent
    int carGasEffect;        // behind the wheel, the gas trigger: 0 off, 1 resistance, 2 a click (a wall that gives way), 3 vibration
    int carGasStrength;      // percent
    int carGasStart;         // where along the travel it begins, 0..8
    int carBrakeEffect;      // the same three for the brake trigger
    int carBrakeStrength;
    int carBrakeStart;
    int adaptiveAim;         // a light resistance on the aim trigger while a weapon is drawn
    int triggerTest;         // menu: play a test effect (not saved)
    int vibration;           // send the game's rumble to the pad: a DualSense over HID when one is open, otherwise XInput
    int vibrationStrength;   // percent
    int analogTriggers;      // analog gas and brake in vehicles (the PC port reduces them to on/off)
    int triggerDeadzone;     // trigger dead zone, 0..254 (console: 30)
    int mouseSensitivity;    // %, 25..400: the mouse look speed over the game's own setting
    int invertMouseX, invertMouseY;
    int startHint;           // the card "settings on F10" for the first seconds of a start
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
    int aoView;              // diagnostic: the scene shows only the occlusion (white = none), session only
    int aoMode;              // 0 = the plain pass (12 taps, no blur), 1 = horizon-based with a depth-aware blur
    int antialias;           // edge smoothing of the final picture in percent (0 = off)
    int antialiasMode;       // how the edges are smoothed: 0 the FXAA-like pass, 1 SMAA 1x (falls back to 0 when it cannot run)
    int smaaQuality;         // SMAA: 0 low, 1 medium, 2 high (how far along an edge its end is looked for)
    int sharpen;             // sharpening of the final picture in percent (0 = off)
    int sharpenMode;         // how it sharpens: 0 the plain unsharp mask, 1 contrast-adaptive (AMD FidelityFX RCAS; falls back to 0)
    int filtersUnderHud;     // smoothing, sharpening and the in-game look run after the game's composite, under its 2D layer
    int gameAlias;           // the game's own unused 4-tap filter in the composite: 0 off (as on PC), 1 the game's offset, 2 a quarter of an output pixel
    int screenshotKey;       // virtual key code for a PNG screenshot, 0 = off
    int glyphTheme;          // interface icons for the pad: 0 off, 1 auto (PlayStation for a DualSense), 2 Xbox, 3 PlayStation
    int dumpTextures;        // write the original textures to UltimateFix\\textures\\dump (diagnostic)
    int textureOverride;     // replace textures by hash from a pack folder
    int pixelOffsetFix;      // half-pixel constant of the 2D pass follows the output viewport
    int shadowPrecision;     // shadow depth buffers in 32-bit float instead of 16-bit
    int shadowScale;         // shadow map size in percent (100 = original 512/1024)
    int scalePostBuffers;    // also enlarge the blur/bloom storage buffers with the scene
    int anisotropy;          // maximum anisotropic filtering, 1..16 (the game uses 8)
    int nearPlane;           // near plane of the play camera in tenths (game 10 = 1.0), 10..60: depth precision far away
    int bloomStrength;       // percent of the game's own bloom (100 = as the game)
    int smoothBloom;         // bloom and depth of field blur read with a cubic B-spline in the composite (the mod's copy of it)
    int dither;              // fixed +-0.5/255 noise before the 8-bit write of the composite and the TV gamma pass (banding)
    int exposureComp;        // exposure compensation in tenths of a stop (-30..30)
    int fullscreenEdgeFix;   // full-screen 2D quads (fades, item notice) cover the last pixel row and column above 720p
    int gameLook;            // a photo filter applied all the time (0 = none), [Graphics] GameLook
    int generateMips;        // build the mipmaps of world textures stored without them (far shimmer), at load
    int shadowBiasFix;       // more depth bias on the two far sun shadow cascades (crawling stripes far away)
    int riverWater;          // the river gets the values of its own "_river" water record and a wave map 16 times larger (as on the Xbox)
    int farShadowFilter;     // the far sun shadow cascade gets the game's own 4-tap filter of the near two (patched shader copies)
    int gameFov;             // field of view in play, percent of the game's own (100 = unchanged), 70..150
    int xboxExposure;        // no 0.85 / 0.75 exposure factors, as on the console
    int xboxLook;            // master switch of the Xbox 360 look: scene grading in the final composite and the display gamma
    int xboxGrade;           // strength of the scene grading (the game's contrast, chroma and colour offset), percent
    int xboxContrast;        // how the contrast pair is applied: 0 not, 1 (x - k) * p, 2 x * p + k, 3 (x - k) / p
    int xboxChroma;          // how the chroma value is read: 0 not, 1 = 0 keeps the colour, 2 = 1 keeps the colour
    int xboxGamma;           // HDTV display gamma on the final picture (sRGB decoded, BT.709 encoded), percent
    int xboxShoulder;        // soft roll-off of the highlights instead of the hard clip, percent (not a console feature)
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
    int photoTemp, photoTint;              // -100..100: cold..warm, green..magenta
    int photoSepia, photoFade, photoGrain, photoAberration, photoBars;  // 0..100
    int photoSuitSet;        // costume set: York 1..2, Zach 1..3
    int photoCostume;        // costume choice, 0 = untouched
    int photoAnim;           // animation choice, 0 = untouched
    int photoBodySet;        // which set of body motions: 0 York, 1 residents (day), 2 cutscene extras, 3 Raincoat Killer
    int photoBodyNo;         // 0 = untouched, n = the n-th motion of the set
    int photoFrame;          // the held frame, percent of the motion's length
    int photoMoveX, photoMoveY, photoMoveZ, photoTurn;  // photo mode: York moved right / up / forward (tenths of a unit) and turned (degrees) from where he stood
    int photoPlay;           // 1 = the motion runs instead of holding the frame
    int photoFaceOn;         // 1 = the face page drives York's face
    int coopPartner;         // 1 = a second character (the doll) walks by the gamepad while York keeps the keyboard and mouse
    int playAsOn;            // 1 = the Cheats row asked for the kind in photoPlayAs (cleared once the game plays it)
    int photoPlayAs;         // photo mode: which of the game's playable characters York's place shows (0 York, 1 Emily, 2 York as a child (CNC01), 3 CBS02, 4 Zach)
    int photoDollId;         // photo mode: which resident (1..49 of the game's NPC list) the doll is
    int photoFaceTarget;     // whose face the photo page's expression goes to: 0 York, 1 the doll
    int dollBodySet, dollBodyNo, dollFrame, dollPlay;  // the doll's body motion: set, number (0 = its idle), held frame in %, 1 = runs
    int dollMoveX, dollMoveY, dollMoveZ, dollTurn;  // the doll moved right / up / forward (tenths of a unit) and turned (degrees) from where it was put
    int photoFaceAdd;        // 1 = over the game's own face of the frame instead of in its place
    int photoFaceW[58];      // weight of face channel i+2 in percent (-100..150; channels 0 and 1 scale the blink and the darts)
    int photoNoBlink;        // 1 = no blinking and no eye darts
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
    int cheatFly;            // York flies: held where WASD/Space/C steer him (not saved)
    int flyKey;              // [Cheats] FlyKey, default F5
    int flySpeed;            // [Cheats] FlySpeed, world units per second
    int cheatGod;            // the current health is brought back to the maximum every frame
    int cheatClockStop;      // the in-game clock stands still
    int cheatClockSpeed;     // clock speed in tenths (10 = as the game)
    int frameStats;          // separate panel: frame time split (game, physics, Present), worst frame, graph
    int perfOverlay;         // FPS, frame time and the GPU time of the mod's passes on the screen
    int triggerDrive;        // behind the wheel: brake on L2, gas on R2 (the car reads the aim and attack bindings)
    int triggerFire;         // aim on L2 and fire on R2 (run on L1, hold breath on R1) instead of the shoulder buttons
    int bisect;              // [Debug] Bisect: a bit mask of hook groups left out for this run, to find which one breaks something (see main.cpp)
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
    int padPollThread;       // a missing pad is asked about on a worker thread; the hook answers from its last result
    int menuX, menuY, menuW, menuH;  // place and size of the F10 panel (moved and resized with the mouse)
    int waypoint;            // F7 waypoint: a cross on the pause map, the distance in yards over the place in the world
    int waypointKey;         // virtual key that sets the waypoint (F7)
    int waypointMinimap;     // the waypoint is also an objective point of the game's map: red chevrons on the minimap, icon on the pause map
    int waypointBeacon;      // the game's own objective beacon (red arrows with the yards) over the waypoint instead of the bare label
    int waypointObjectiveKey;  // Ctrl + this virtual key puts the waypoint on the game's current objective (F7), 0 = off
    int replaceLauncher;     // put the quick-start stub in place of DPLauncher.exe by itself (the ufx_play.exe job)
    int autoRecover;         // (experimental, not proven to help) after a long run of slow frames, hold the game clock and physics for 4 frames
    int physicsSteps;        // most fixed physics steps per frame: 0 = the game's own (up to 4), 1..3 = a cap
    int highFpsPhysics;      // frames shorter than one step of 1/60 s (above 120 frames/s) share whole physics steps instead of taking one each
    int pauseInMenu;         // the game's time stands still while the F10 menu is open
    int framePacing;         // 0 off, 1 = frame limit 60 only (game keeps real time), 2 = 30 frames/s in whole steps (console), 3 = 60 in whole steps
    int surround;            // 5.1 / 7.1 as on the console: 0 off, 1 = automatic (only when the mix has more than 2 channels), 2 = the same without the left/right mirror
    int resourceReadGuard;   // a packed resource that does not unpack is read 5 times, not forever; unpacking stops at the end of its buffer
    int sessionReminderHours; // one hint to save and restart after this many hours of a session, 0 = never
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
// Timing for the log only (no behaviour depends on it). The ticks are the real QueryPerformanceCounter of kernel32: the hook
// of RelativeQpc sits in DP.exe's import table, not in ours. wsprintf has no %f, so a time is printed as "%u.%u ms" from
// MsWhole and MsTenth of a span in microseconds.
u64 TickNow();
u32 TickMicros(u64 from, u64 to);  // saturates at about 71 minutes
inline u32 MsWhole(u32 micros) { return micros / 1000; }
inline u32 MsTenth(u32 micros) { return micros / 100 % 10; }
// "shader <name>: compiled in N ms (<profile>)", or that no profile compiled when `profile` is null. Every shader of the
// mod is compiled at its first use on the render thread and kept for the run, so each name is logged once.
void LogShaderTime(const char* name, const char* profile, u64 started);

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
bool SaveWriteInFlight();  // saves.cpp: the game is writing dp.sav right now (only seen while [Save] Guard is on)
bool InstallHouseList();
bool InstallJoyPoll();
bool InstallSkipIntro();
bool InstallLanguageSlot();
bool InstallResetRefreshFix();
bool InstallZeroFrameMenus();
bool InstallDisplay();
bool InstallGraphicsTweaks();
bool InstallD3dTrace(bool full);
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
u32 PlayerObjectNow();     // combat_strafe.cpp: the player object of the last player update, 0 outside the world
void FlyHold(void* player);  // freecam.cpp: the fly cheat holds York at the start of the player update
bool DualSenseToJoy(void* joyInfoEx);
bool DualSenseConnected();
bool InstallInputExtras();
bool InstallVehicleTriggers();
void VibrationFrame();   // once per frame from the Present hook
void StopVibration();
void TriggerCapture(const void* joyInfoEx, bool menuOpen);
bool InstallCrashHandler();
bool InstallCrashGuards();
bool InstallStability();   // stability.cpp: the NaN trap guard and the x87 precision of the main thread
bool MemoryIsLow();
void InstallAutoLargeAddress();  // laa_self.cpp: prepares DP.exe with the 4 GB flag for the next start
void InstallUpdater();           // updater.cpp: the check for a newer release (a thread), removes the DLL the last update replaced
int UpdateState();               // 0 none, 1 a newer release, 2 updating, 3 updated (restarting), 4 failed
const char* UpdateVersion();     // the newest release, "0.7.2"
void UpdateStart();              // download and install it (a thread)
void UpdateFrame();              // main thread: its notices and the restart
void RestartGameNow();           // updater.cpp: start the game again and end this process (the menu's restart button)
void LargeAddressFrame();        // its one notice, main thread  // stability.cpp: the address space is nearly used up (the once-a-minute check)
void StabilityFrame();     // once per frame from the Present hook: pins the sound libraries once, keeps the x87 word
bool InstallAudioGuard();  // proxy.cpp: the X3DAudio guard, and the game's calls go through it from then on
void ApplySurround();      // follows [Audio] Surround (also called from the menu)
bool InstallMapFix();
void ApplyMapFix();
bool InstallTextureOverride();
// texture_override.cpp: what the texture loads cost (log only). Per kind of load: 0 = mipmaps made by D3DX (GenerateMips),
// 1 = as the game gave it, 2 = a file of a pack or a mod. False when the texture hook is not in (then nothing is counted).
constexpr int kLoadKinds = 3;
bool TextureLoadTotals(u32* loads, u32* micros);  // both kLoadKinds long, counted since the start (wrapping)
void TextureLoadFrame();                          // once per frame from the Present hook: ends a run of loads in the log
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
void CheatSpawnCarAt(int number, int where, const float place[4]);  // where: 0 close in front of York, 1 behind him, 2 well ahead of him, 3 at place (x y z yaw)
void CheatSpawnCar(int number);          // one of the game's numbered cars put in front of York (open Greenvale only)
void CheatGiveItem(int id, int count);   // the game's own give-item call
void CheatYorkToCamera();
void CheatYorkTo(const float place[4]);   // York put at a place of the same zone (position and heading written)
int WeaponClassNow();
bool InstallAlternate3D();
bool InstallPhysicsFreeze();
bool InstallHighFpsPhysics();  // needs the physics step hook of InstallPhysicsFreeze
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
// mods.cpp: the mod manager (UltimateFix\mods\<folder>\ with mod.ini or mod.txt, text\ and textures\; order and switches in mods.txt)
bool IsModDescription(const wchar_t* name);  // mod.ini, mod.txt, info.txt, readme.txt, description.txt, the cover: never game files
constexpr int kMaxMods = 32;
struct ModInfo {
    wchar_t folder[64];
    wchar_t name[64];
    wchar_t author[48];
    wchar_t version[24];
    wchar_t description[1024];  // '\n' breaks a line (written as \n in mod.txt, or the lines of description.txt)
    wchar_t cover[MAX_PATH];    // the full path of cover.png (or .jpg, preview.*, image.png) in the mod folder, empty = none
    bool enabled;
    bool textJson;   // text\mes_all.json
    int textFiles;   // text\*.txt
    int textures;    // textures\*.dds and *.png
    int files;       // files\ (game files by name, game_files.cpp), counted in all subfolders
};
void ScanMods();                     // once at start, before the text and texture overlays
int ModCount();
ModInfo* ModAt(int index);           // in order: 0 wins over the ones after it
int ModTextSource();                 // the mod the text overlay reads, -1 = UltimateFix\text\ (as before mods)
void ModToggle(int index);           // saved at once, used from the next start
void ModMove(int index, int direction);
bool ModsChanged();                  // differs from what this run started with: a restart is needed
void ModTextDir(wchar_t* out);       // the text folder the overlay reads, with a trailing backslash (MAX_PATH)
int ModTextureDirs(wchar_t (*out)[MAX_PATH], int max);  // texture folders of the mods that are on, first wins
void OpenModsFolder();
void ModOpenFolder(int index);       // that mod's own folder in Explorer
int ModFileDirs(wchar_t (*out)[MAX_PATH], int max);     // files\ folders of the mods that are on, first wins
bool InstallGameFiles();                // game_files.cpp: game files from the mods' files\ folders, after ScanMods
bool GameFileTextures();                // a mod replaces textures of an XPC by name (the texture hook is needed)
bool GameFileNextTexture(wchar_t* path);  // the file for the next texture the loader creates, if a mod has one (MAX_PATH)
void GameFovAfterFrame();  // freecam.cpp: gives the game its own field of view back after a frame was shown
void WaypointFrame(void* device);  // once per frame from the Present hook: the waypoint keys and the state its drawing needs
bool WaypointToObjective();        // the waypoint goes to the game's current objective (the next one when it stands on one already)
int WaypointObjectives(int* yards);  // objective points on the game's map now and the yards to the first one; -1 = not known
// npc_map.cpp: the town's residents, read-only (no patches, no calls into the game)
bool InstallNpcMap();  // checks the facts the module relies on (kNpcFacts); false = the schedule viewer stays off
// One row of the schedule viewer: where a resident is now by the game's own timetable, and where next.
struct ResidentRow {
    char name[20];
    signed char nowHour, nowMinute;    // when the current entry began; -1 = it has no time (it lasts the whole day)
    signed char nextHour, nextMinute;  // when the next one begins; -1 = there is no other entry
    char nowPlace[32], nowAction[32];  // as the timetable has them: "ZZ_RRroom_NNnode" (ZZ = area), "idling"...; "none" = not on it
    char nextPlace[32], nextAction[32];
};
constexpr int kScheduleNoGame = -1;   // no game world is being played (title screen, loading)
constexpr int kScheduleNotInUse = -2;  // the game runs no timetables now (FUN_004B19F0 loads none in some chapters and places)
constexpr int kScheduleOff = -3;      // the module is off (another DP.exe, SafeMode)
// Fills rows for the residents that have a block in a loaded timetable; returns their number or one of the codes above.
// hour, minute = the game clock, rainy = 1 when the rain timetables apply. Main thread only (the Present hook).
int ReadResidentSchedules(ResidentRow* rows, int max, int* hour, int* minute, int* rainy);
bool InstallByteTextFix();
bool InstallTextTools();   // translator tools: text overlay and dump, message ids, glyph log (text_overlay.cpp)
void TextToolsFrame();     // once per frame from the Present hook: writes what the text hooks noted to loc.log
bool InstallEffectStep();
extern int g_sceneWidth;  // width of the 3D scene after the render scale (0 = not scaled)
bool FreeCamBlocksInput();  // true while the camera flies and the game must see no input
void ApplyWorldExtra();  // writes the current settings (also called from the menu)
float MouseLookSpeed();   // input_extras.cpp: the mouse look speed, 1 = the game's
void ApplyInputExtras();
void ApplyShadowBias();
void ApplyEdgeFix();      // graphics_tweaks.cpp: follows FullscreenEdgeFix   // graphics_tweaks.cpp: writes the cascade bias table (also from the menu)  // writes the current input settings (also called from the menu)
void PadCapture(const void* joyInfoEx);  // pad 0 data as the game is about to receive it, null = none
void AutoInputTick(unsigned id, const void* joyInfoEx, unsigned result, SHORT(WINAPI* getKey)(int));  // called by the joyGetPosEx hook
bool InstallRenderScale();
extern int g_outputW;  // 0 when the output resolution module is not active
extern int g_outputH;
bool InstallShadowScale();
bool InstallGameplay();
bool InstallFramePacer();
void TraceCameraFrame();  // once per frame, from the Present hook
void WaterFrame();        // trace.cpp: Numpad 1 switches [Debug] WaterTest while the water hooks are on
int DisplayRefreshHz();  // refresh rate of the game window's monitor, cached (frame_pacer.cpp)
void ApplyGameplay();
void ApplyDifficulty();
void SyncDifficultyFromGame();  // reads the difficulty the game really has into g_cfg (the menu, while open)
void ApplyGraphicsTweaks();  // writes the current settings (also called from the menu)
bool InstallResourceGuard();
bool ResourceGuardTakeFailure(char* name, int cap);  // main thread: the name of a resource the loading thread gave up on, once
void ResourceGuardCounts(u32* retries, u32* givenUp, u32* overflows);
void SessionFrame(void* device);  // once per frame from the Present hook: the long-session hint and the notice box
// The notice box (diagnostics.cpp, main thread). `kind` is what the mark at its left says: grey for information and a
// finished job, amber for a warning, coral for a failure. Up to three wait their turn; one whose English title belongs to
// the same family as a shown or waiting one (the same text before a colon, or one title the first words of the other:
// "Fly: on" and "Fly: off", "Scan" and "Scan finished") takes its place instead of queueing.
constexpr int kNoticeInfo = 0, kNoticeWarning = 1, kNoticeError = 2;
bool PartnerPad(float* x, float* y, u32* buttons);  // menu.cpp: the pad's left stick (-1..1, up positive) and buttons for the partner
void DollTick(void* player);   // combat_strafe.cpp, from the player update
void RequestDoll(int what);    // photo mode: 1 = put the chosen resident next to York as a doll, 2 = remove it (at the next player update)
bool DollName(int id, char* out16);  // the resident's internal name from the game's NPC list, false when it cannot be read
// the mission editor (missions.cpp) and what it uses of the lab and the doll (combat_strafe.cpp)
bool PlayerPlace(float out[4]);       // York's position and heading, false when there is no player
void DollPutAt(int resident, const float place[4], int motionSet, int motion, u32 talkEvent);
void DollTalkDefault();
bool DollStands();
void LabSetGiveItem(int id, int count);
int GiveItemNow(int id, int count);   // cheats.cpp: the game's give-item call, on the game thread; its answer (-3 = went to the storage)
int LabRunEvent(u32 event);
void WorldStreamFrame();              // world_stream.cpp, once a frame: the queue of world objects to build, watched
void LabWatchFrame();                 // once a frame: which answer the running talk took
// A quest of the editor: UltimateFix\missions\missionN.ini, a list of steps done one after another (missions.cpp)
struct MissionStep {
    int kind;  // 0 talk with a choice of two answers, 1 a line without a choice, 2 reach a place, 3 wait for an hour of the game's clock
    int resident, placed, x, y, z, yaw, motionSet, motion;  // the character of a talk (placed 0 = in front of York)
    int radius, zone, hour;                                  // reach: metres around x y z, zone -1 = any; wait: the hour
    int nextYes, nextNo;                                     // the step after it: 0 = the following one, -1 = the quest ends
    int giveItem, giveCount, money;                          // the reward when the step is done (a talk: for the first answer)
    int carPlace, carX, carY, carZ, carYaw;                  // where the car goes: 0 behind York, 1 well ahead of him, 2 at carX.. (thousandths)
    int car, setHour, weather, teleport, tx, ty, tz, tyaw;   // done when the step begins: car 1..16, hour 0..23 or -1, weather 0..3 (0 none)
    wchar_t title[96], line[200], question[200], yes[40], no[40], afterYes[200], afterNo[200];
};
constexpr int kMissionSteps = 16;
struct Mission {
    int slot, exists, stepCount;
    wchar_t name[64], author[64], banner[96], endText[200];
    MissionStep steps[kMissionSteps];
};
extern Mission g_mission;
void MissionLoad(int slot);           // reads the file of slot 1..9 into g_mission
void MissionOpenFile();               // the file in Notepad
void MissionRequest(int what, int slot = 0);  // 2 = run (from the saved step), 3 = stop, 4 = run from the first step; slot 0 = the loaded one
const wchar_t* MissionState();
void MissionTick(void* player);       // from the player update
bool MissionActive();
void MissionTalkOver(bool firstAnswer);  // combat_strafe.cpp: the talk of the running step is over
int MissionStepNow();                 // 1..N while a quest runs, 0 otherwise
void MissionPath(int slot, wchar_t* out);
bool EditorServerStart();             // editor_server.cpp: the local page of the quest editor; false = could not listen
int EditorServerPort();
void EditorOpenBrowser();
int CarNumberOf(int pick);            // menu.cpp: the game's number of the car 1..16 of the spawner's list
const wchar_t* CarNameOf(int pick);
bool LabSetText(u32 id, const char* text);  // text_overlay.cpp: one of the lab's message records rewritten in place
bool LabTextsIn();  // text_overlay.cpp: the mission lab's texts are in the game's messages
void ShowNotice(const wchar_t* titleUk, const wchar_t* titleEn, const wchar_t* textUk, const wchar_t* textEn, int seconds,
                int kind = kNoticeInfo);
// While the F10 menu is open it draws the notice itself (between OverlayBegin and OverlayEnd, inside its own fade), so the
// box never covers the panel's title; the floating box stays away in that frame. InBox: the text and the mark inside a box
// the menu has drawn (the description box), x,y,w,h being that box; `context` names the selected row, and a notice that has
// been in the box for 1.5 s gives way when another row is selected. Below: a floating box centred on centreX, its top at
// `top`, its text at most maxW wide, for the pages without a description box. Both return false when there is no notice.
bool DrawNoticeInBox(float x, float y, float w, float h, int context);
bool DrawNoticeBelow(float centreX, float top, float maxW);
void OpenLogFolder();          // the F10 menu: Explorer on the UltimateFix folder
void WriteDiagnosticReport();  // the F10 menu: UltimateFix\ufx_report.txt

// 2 to the power of tenths: 2^(t/10) for t in -30..30 without a C runtime (exposure in ssao.cpp, sharpness in overlay.cpp).
inline float Pow2Tenths(int t) {
    const float step = 1.0717735f;  // 2^0.1
    float v = 1.0f;
    if (t > 0) for (int i = 0; i < t; ++i) v *= step;
    else for (int i = 0; i < -t; ++i) v /= step;
    return v;
}

namespace ui {
bool Install();                      // input blocking hooks; returns true if the menu is available
void OnFrame(void* device);          // called once per frame right before Present
bool IsOpen();
void SharpenFrame(void* device, int percent);
void AntialiasFrame(void* device, int percent);
void ColorFrame(void* device);
void XboxGammaFrame(void* device);
void RequestScreenshot();
void RequestScreenshotNamed(const wchar_t* suffix);  // the next shot with a suffix in its file name
bool ScreenshotHidesHud();  // a shot without the HUD is being prepared (freecam.cpp hides the HUD)
void OpenPhotoPage();
void PerfPostBegin(void* device);
void PerfPostEnd(void* device);
// FiltersUnderHud: right after the game's final composite (ssao.cpp) the smoothing, sharpening and in-game look go onto the
// 3D picture before the game draws its 2D layer; at Present the second says what already ran this frame (and forgets it).
void PostFiltersAfterComposite(void* device);
bool TakePostFiltersUnderHud(bool* lookDone);
void PerfOverlayFrame(void* device, double fps, double frameMs, double presentMs, double physicsMs);
void FrameStatsPush(double frameMs);  // one finished frame for the graph
void FrameStatsPanel(void* device, double totalMs, double gameMs, double physicsMs, double presentMs, double worstMs, double busyMs);
bool PhotoPageOpen();  // the menu is open on the photo mode page
void PhotoLookReset();
bool GameLookValues(int* out);  // menu.cpp: the in-game look in the order of the photo sliders (11 values), false = none  // menu.cpp: the filter and every slider of the look back to none (own presets stay in the ini)
void ScreenshotFrame(void* device);
void ReleasePostResources();
}  // namespace ui

}  // namespace ufx
