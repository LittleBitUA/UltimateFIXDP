#include "base.h"

namespace ufx {

Config g_cfg;

static const char kDefaultIni[] =
    "; UltimateFIXDP settings. 1 = on, 0 = off. Delete this file to restore the defaults.\r\n"
    "\r\n"
    "[General]\r\n"
    "; 1 = the mod puts a small quick-start program in place of DPLauncher.exe by itself, so the game starts at once\r\n"
    "; without the launcher window. The first start after installing still shows the stock launcher. The original is kept as\r\n"
    "; DPLauncher_original.exe; hold Shift when starting to open it. 0 = leave the launcher alone (and put the original back).\r\n"
    "; With Borderless=0, or with DPfix next to the game, the stock launcher stays (or comes back): real fullscreen comes only from\r\n"
    "; its Full Screen box.\r\n"
    "ReplaceLauncher=1\r\n"
    "; 1 = the game is exempt from Windows efficiency mode (EcoQoS: slow cores and low clocks for programs Windows does not know).\r\n"
    "NoPowerThrottling=1\r\n"
    "; 1 = on a processor with performance and efficiency cores the game runs only on the performance cores (test for a low frame rate).\r\n"
    "PerformanceCoresOnly=0\r\n"
    "; After this many hours of one session the mod shows a short hint once (top of the screen, 15 seconds): save, and restart the game\r\n"
    "; when convenient. Long sessions of this game are reported to become unstable; no cause was confirmed in its code. The same hint\r\n"
    "; comes earlier when 48 of the game's 64 sound load requests are in use (the game does not check for running out of them; the\r\n"
    "; number is logged every hour). 0 = never. 0..24. Changes at once (F10 -> Fixes -> Diagnostics).\r\n"
    "SessionReminderHours=3\r\n"
    "\r\n"
    "[Time]\r\n"
    "; QueryPerformanceCounter relative to the start of the game. Fixes the slowdown that\r\n"
    "; only a Windows reboot cured (the game keeps time in x87 single precision).\r\n"
    "RelativeQpc=1\r\n"
    "; Ask Windows for a 1 ms timer (smoother frame pacing and Sleep).\r\n"
    "TimerResolution=1\r\n"
    "; 1 = the game's time stands still while the F10 menu is open (the car stops, enemies freeze; can upset a few scripted scenes), 0 = it keeps running (default).\r\n"
    "PauseInMenu=0\r\n"
    "; The game takes 1 to 4 fixed physics steps per frame depending on the frame time, and every extra step lengthens the frame:\r\n"
    "; a spiral that ends near 20 frames/s (a guess, the real cause of the lag was found elsewhere). 1..3 = at most that many steps per frame, 0 = the game's own (default).\r\n"
    "; With 1 the physics (cars, props) runs slower than real time when the frame rate is below 60. Changes at once.\r\n"
    "PhysicsSteps=0\r\n"
    "; Above 120 frames/s (FramePacing=0 and nothing else limits the frames) a frame is shorter than half a physics step, yet the game\r\n"
    "; gives every frame a whole step of 1/60 s: a car drives frames/60 times too fast, burns fuel faster, and its inner timers stand\r\n"
    "; still. 1 = such short frames share whole steps as on the console (their time is gathered, a frame gets a step when a whole one\r\n"
    "; has passed): physics, car control, fuel, odometer, the car's timers and its tyre and engine sound ramps keep real time. Cars\r\n"
    "; and props then move 60 times per second however many frames are drawn. Frames of a whole step or longer (60 frames/s and below,\r\n"
    "; FramePacing=1..3) are not touched at all. 0 = as the game does it. Changes at once. Not tested in the game yet.\r\n"
    "HighFpsPhysics=1\r\n"
    "; 1 = (experimental, not proven to help) when 60 frames in a row are slow (over 30 ms per frame, about 2 seconds; over 60 ms\r\n"
    "; in the 30 frames/s mode FramePacing=2, about 4 seconds), the game's clock and physics are held for 4 frames. It was made for a lag whose cause\r\n"
    "; turned out to be the pad poll (see ThrottleMissingPad). At most once per 20 seconds, written to the log.\r\n"
    "AutoRecover=0\r\n"
    "; Frame pacing. The PC code rounds the frame delta up from 0.5, so at 75-240 Hz the game runs physics and cars too fast.\r\n"
    "; 1 = frame limit (recommended): 60 frames per second on average at any refresh rate, and the game clock stays within half a\r\n"
    "; step of real time (the mod rounds a short frame up to a whole step itself and takes that time off the next one); 2 = 30 per\r\n"
    "; second like the console, 3 = 60 per second with the game time in whole 1/60 s steps (2 and 3 hitch when the game is below 60);\r\n"
    "; 0 = no limit (default): the frame rate follows the screen. The game rounds a delta of 0.5..0.99 steps up to a whole step, so\r\n"
    "; without a limit everything runs too fast at 61..139 Hz (75 Hz: 1.25 times, 120 Hz: twice): on such a screen the mod keeps the\r\n"
    "; limit of 1 by itself and says so in the log. From 140 Hz up the frames are short enough not to be rounded, and HighFpsPhysics\r\n"
    "; keeps the car in real time. Changes at once.\r\n"
    "FramePacing=0"
    "\r\n"
    "\r\n"
    "[Fixes]\r\n"
    "; Muzzle flashes, splashes and hits take a fixed step: at the console's 30 frames/s that is half the speed of the other\r\n"
    "; effects, at the PC's 60 it is full speed, so they play twice as fast as on the console. 1 = console speed. Restart needed.\r\n"
    "HalveFlashEffects=1\r\n"
    "; Text drawn from single-byte strings (credits and a few formats): characters of code 80h and above, which is where a\r\n"
    "; Cyrillic code page lives, are read as signed and land outside the font. 1 = read them as unsigned. For translations that\r\n"
    "; put Cyrillic into those cells. Restart needed.\r\n"
    "ByteTextCyrillic=0\r\n"
    "; Zero frame delta turned movement speed into NaN and froze the game forever.\r\n"
    "ZeroDeltaGuard=1\r\n"
    "; An angle that is not a number (NaN) hung the game for good in the angle normalisation (a deliberate endless loop of the port).\r\n"
    "; 1 = it gives 0 and a line in ufx.log names the caller. Restart needed.\r\n"
    "NanTrapGuard=1\r\n"
    "; Keep XAudio2 and XACT loaded for the whole run: the game closes COM once more than it opens it after every video, and\r\n"
    "; Windows may then unload XAudio2 under a running sound thread (a crash with \"XAudio2_7.dll unloaded\" in the event log).\r\n"
    "PinAudio=1\r\n"
    "; Keep the x87 precision of the main thread at 24 bits, as a plain Direct3D 9 device sets it. The port compares float values\r\n"
    "; for equality (the aim limit), which holds only at 24 bits; DXVK or a Windows compatibility mode can leave 53. Changes at once.\r\n"
    "PinFpu=1\r\n"
    "; 1 = the loader thread waits 1 ms instead of 16 between requests: shorter hitches while the world streams in. Restart needed.\r\n"
    "FasterLoading=1\r\n"
    "; 1 = the mod gives DP.exe the 4 GB flag itself: it prepares the file while the game runs, the next\r\n"
    "; start has 4 GB of address space instead of 2. The original stays as DP.exe.bak. Done again after Steam restores the file.\r\n"
    "AutoLargeAddress=1\r\n"
    "; 1 = the stick-waggle QTE (alternate A/D) counts every swap as at 60 FPS; above 60 the game counted each swap less. Restart needed.\r\n"
    "QteFix=1\r\n"
    "; 1 = the lock-on aims at the chest. The game aims at the waist bone and lowers the point by a tenth of the distance\r\n"
    "; (at 6 m the knees). This removes the drop and aims LockOnHeight tenths of a unit above the waist. Restart needed.\r\n"
    "LockOnAim=1\r\n"
    "LockOnHeight=40\r\n"
    "; Threads of the game that share data without locks (what the Windows 98 compatibility mode hid by one CPU core):\r\n"
    "; SerialSetThread=1 the main thread waits for the world builder at once, RouteFlushWait=1 a level change waits for the\r\n"
    "; resident route thread (at most 250 ms). Restart needed.\r\n"
    "SerialSetThread=1\r\n"
    "RouteFlushWait=1\r\n"
    "; Correct byte order of HOUSE_LIST.NOD (day/night state of buildings).\r\n"
    "HouseListFix=1\r\n"
    "; Drop the per-frame joyGetPosEx call that passes uninitialised memory.\r\n"
    "RemoveExtraJoyPoll=1\r\n"
    "; Skip the publisher logos at start.\r\n"
    "SkipIntro=1\r\n"
    "\r\n"
    "; Windowed Reset() after Alt+Tab no longer fails (the game used to freeze).\r\n"
    "ResetRefreshFix=1\r\n"
    "; A packed game file that does not unpack (damaged, or the disk cannot read it) was read again without end: a silent hang on a\r\n"
    "; loading screen. 1 = it is read 5 times at most, then reported to the game as missing and named in the log; the game may then\r\n"
    "; go on without it or stop with a crash report, but it does not hang silently. Unpacking also stops at the end of its buffer\r\n"
    "; instead of writing past it. Files that unpack are read exactly as before. Off in SafeMode. Restart needed.\r\n"
    "ResourceReadGuard=1\r\n"
    "\r\n"
    "[Display]\r\n"
    "; Borderless window over the whole monitor. The back buffer (1280x720, or OutputWidth) is presented as it is when it has the size\r\n"
    "; of the window, otherwise scaled to it (see ScaleToWindow).\r\n"
    "; Applies to the default windowed mode (start the game without arguments).\r\n"
    "Borderless=1\r\n"
    "HideCursor=1\r\n"
    "; Stop Windows from scaling the window on 125%/150% displays.\r\n"
    "DpiAware=1\r\n"
    "; In the borderless window, scale the back buffer to the window keeping 16:9 (black bars on other monitors), through an extra\r\n"
    "; swap chain of the mod.\r\n"
    "; 0 = the game's own Present fills the whole window without keeping 16:9 (no bars, stretched on a monitor that is not 16:9).\r\n"
    "ScaleToWindow=1\r\n"
    "; 1 = in the borderless window (Borderless=1, no DPfix) log the first frames and every change of the way the picture is\r\n"
    "; presented (with the back buffer size), and (always) the window geometry for about 4 seconds after each Esc press\r\n"
    "; (for finding display problems).\r\n"
    "Debug=0\r\n"
    "\r\n"
    "[Graphics]\r\n"
    "; Real resolution of the picture (back buffer): the game's 2D layer (interface, text, fades) becomes sharp.\r\n"
    "; 0 = off (the game's 1280x720). Set it to the width of your monitor: 1920 (1080p), 2560 (1440p), 3840\r\n"
    "; (2160p, the maximum). The height is always OutputWidth * 9 / 16: OutputHeight is ignored (the log says so when it is set).\r\n"
    "; Never larger than the monitor: a bigger value is cut to the widest 16:9 picture the monitor shows, for that run only\r\n"
    "; (written to the log, this file keeps your value). Needs a game restart. In the borderless window a back buffer of the\r\n"
    "; window's size is presented directly, any other size is scaled through the extra swap chain (ScaleToWindow).\r\n"
    "OutputWidth=0\r\n"
    "OutputHeight=0\r\n"
    "; Internal resolution of the 3D scene in percent. With OutputWidth set it is a percent of the OUTPUT\r\n"
    "; resolution (OutputWidth=1920: 100 = 1920x1080, 200 = 3840x2160 = exact 2x2 supersampling, the best\r\n"
    "; quality for 1080p); with OutputWidth=0 it is a percent of 1280x720. Never above 7680 pixels wide.\r\n"
    "; Needs a game restart. More memory and a faster video card.\r\n"
    "RenderScale=100\r\n"
    "; Shadow map size in percent: 100 = original (512 and 1024), 200 = 1024 and 2048, 400 = 2048 and 4096.\r\n"
    "; Needs a game restart.\r\n"
    "ShadowScale=100\r\n"
    "; With a bigger scene (RenderScale, OutputWidth) the game's blur, bloom and depth-of-field shrink on the screen.\r\n"
    "; 1 = keep their original size, as the 720p picture had it (like DPfix addDOFBlur). Needs a restart.\r\n"
    "KeepBlurLook=0\r\n"
    "; Ambient occlusion (own implementation on the game's depth buffer): darkens creases and contact areas.\r\n"
    "; Strength in percent 0..100 (0 = off), radius in pixels at 720p. Changes at once.\r\n"
    "AmbientOcclusion=0\r\n"
    "AoRadius=10\r\n"
    "; 1 = horizon-based occlusion (8 directions, a depth-aware blur: soft, no stripes; radius up to 40), 0 = the plain pass\r\n"
    "; (12 taps, no blur, radius up to 20). Changes at once.\r\n"
    "AoMode=1\r\n"
    "; Edge smoothing of the final picture, percent 0..100. 0 = off. Changes at once.\r\n"
    "Antialias=0\r\n"
    "; How the edges are smoothed while Antialias is above 0. 0 (or FXAA) = the fast FXAA-like pass. 1 (or SMAA) = SMAA 1x,\r\n"
    "; our own implementation: it finds the jagged borders and straightens only them, so textures and text stay sharper\r\n"
    "; (use Antialias=100 with it); a little heavier for the video card. When SMAA cannot start, the log says why and the\r\n"
    "; FXAA-like pass is used. Changes at once.\r\n"
    "AntialiasMode=0\r\n"
    "; SMAA only: how far along an edge its end is looked for. 0 = low (about 8 pixels), 1 = medium (16), 2 = high (32:\r\n"
    "; the smoothest nearly horizontal and nearly vertical lines). Changes at once.\r\n"
    "SmaaQuality=2\r\n"
    "; Sharpen the final picture (unsharp mask), percent 0..100. 0 = off, 50 by default. Changes at once.\r\n"
    "Sharpen=50\r\n"
    "; PNG screenshot of the picture without the menu into UltimateFix\\screenshots. Virtual key code, 122 = F11, 0 = off.\r\n"
    "ScreenshotKey=122\r\n"
    "; Interface icons. The game loads ONE icon atlas at start and only switches the coordinates between keyboard and\r\n"
    "; pad, so switching modes in the middle of the game shows pieces of the wrong icons. With the pad icons below\r\n"
    "; (UltimateFix\\glyphs\\gamepad\\xbox.png and playstation.png) the atlas is swapped with the mode.\r\n"
    "; 0 = off, 1 = automatic (PlayStation icons for a DualSense, else Xbox), 2 = Xbox, 3 = PlayStation. Restart needed.\r\n"
    "GlyphTheme=1\r\n"
    "; Replace textures from a pack (the DPfix / ZachFix format: <hash>.dds or .png in UltimateFix\\textures\\override,\r\n"
    "; dpfix\\tex_override or ZachFix\\textures\\override). Without a pack nothing is replaced. 0 switches packs off, also while\r\n"
    "; GlyphTheme or DumpTextures keep the texture hook in. Restart needed.\r\n"
    "TextureOverride=1\r\n"
    "; With OutputWidth set, the 2D layer (interface icons, text) is drawn at the real resolution; this keeps its\r\n"
    "; half-pixel offset right so that icons do not show a strip of the next one. Changes at once.\r\n"
    "FixPixelOffset=1\r\n"
    "; Shadow depth in 32-bit float instead of 16-bit: no banding on shadow edges (what DPfix and ZachFix do).\r\n"
    "; Needs a game restart.\r\n"
    "ShadowPrecision=1\r\n"
    "; Enlarge the buffers of the blur (depth of field) and bloom passes together with the scene.\r\n"
    "ScalePostBuffers=0\r\n"
    "; Maximum anisotropic filtering, 1..16 (the game uses 8).\r\n"
    "Anisotropy=16\r\n"
    "; Field of view while playing, in percent of the game's own (about 65 degrees): 100 = as the game, 120 = about 78 degrees.\r\n"
    "; The game keeps its own zoom (aiming, the car); cutscenes and the photo mode keep their own view. Changes at once.\r\n"
    "FieldOfView=100\r\n"
    "; Near plane of the play camera in tenths of a unit (the game: 10 = 1.0, about 10 cm). Higher gives the depth buffer more\r\n"
    "; precision far away (default 200 = 20.0): far surfaces that lie on each other (roads, roofs, decals) stop flickering. Too high cuts what is very\r\n"
    "; close to the camera. 10..200; cutscenes keep their own. Changes at once.\r\n"
    "NearPlane=200\r\n"
    "; 1 = more depth bias on the two far sun shadow cascades: no crawling shadow stripes in the distance. Changes at once.\r\n"
    "ShadowBiasFix=1\r\n"
    "; 1 = world textures stored without mipmaps get them built when they load: far textures stop shimmering. Restart needed.\r\n"
    "GenerateMips=1\r\n"
    "; A photo mode filter applied all the time: 0 none, 1 warm, 2 cold, 3 B&W, 4 cinema, 5 vintage, 6 sepia, 7 noir, 8 film,\r\n"
    "; 9 bleach bypass, 10 dream, 11 VHS, 12 night, 13 fog, 14..17 your presets Mine 1..4 ([PhotoPresets]). Changes at once.\r\n"
    "GameLook=0\r\n"
    "; 1 = fades, loading screens and the item notice cover the screen to its last pixel above 1280x720 (no 1-pixel strip at the\r\n"
    "; right and the bottom). Changes at once.\r\n"
    "FullscreenEdgeFix=1\r\n"
    "; The game's own glow (bloom) in percent (0..300, 100 = as the game) and its exposure in tenths of a stop (-30..30,\r\n"
    "; 0 = as the game; 10 = one stop brighter). The game keeps adapting to the light as before. Changes at once.\r\n"
    "BloomStrength=100\r\n"
    "ExposureComp=0\r\n"
    "; The PC port darkens the picture by 0.85 (and 0.75 more in one cutscene state); the console did not.\r\n"
    "; 1 = remove the factors (about 15% brighter). Changes at once.\r\n"
    "XboxExposure=0\r\n"
    "; Xbox 360 look (experimental, not checked in the game yet; nothing happens in the main menu or with SafeMode=1). The game\r\n"
    "; still computes a contrast, a chroma value and a colour offset for the hour and the weather every frame, but the PC pixel\r\n"
    "; shader of the final picture does not read them. 1 = the mod's own copy of that shader applies them, and the finished\r\n"
    "; picture gets the console's TV gamma. XboxExposure above is separate and works with or without this. Changes at once.\r\n"
    "XboxLook=0\r\n"
    "; Strength of the scene grading in percent, 0..100 (0 = none, the PC colours).\r\n"
    "XboxGrade=100\r\n"
    "; The console's shader is not available, so the exact formulas are not known; these choose between the readings.\r\n"
    "; Contrast k (the game also hands over p = 1 - 2k): 0 = not applied, 1 = (x - k) * p (how ZachFix reads the console shader),\r\n"
    "; 2 = x * p + k (contrast around mid grey), 3 = (x - k) / p (stretch between a black and a white point).\r\n"
    "XboxContrastFormula=1\r\n"
    "; Chroma c: 0 = not applied, 1 = c is the amount of grey mixed in (0 keeps the colour), 2 = c is the saturation (1 keeps it).\r\n"
    "; If the picture turns black and white with one of them, the other one is right.\r\n"
    "XboxChromaFormula=1\r\n"
    "; The console sends the picture to a TV through the BT.709 curve: darker, deeper shadows than a PC monitor shows. Percent\r\n"
    "; 0..100 on the whole frame with the game's interface (0 = off). A general rule of the console, not confirmed for this game.\r\n"
    "XboxGamma=100\r\n"
    "; Soft highlights: bright areas roll off smoothly instead of clipping to white. Percent 0..100, 0 = off. Not a console\r\n"
    "; feature (an improvement that uses the same shader).\r\n"
    "XboxShoulder=0\r\n"
    "; The reflection matrix of the game uses the width twice instead of width and height (a bug of the\r\n"
    "; port): reflections were shifted slightly vertically. Off by default until checked in the game.\r\n"
    "ReflectionBiasFix=0\r\n"
    "\r\n"
    "[Language]\r\n"
    "; Text overlay for translators: put mes_all.json (the translation as the SWERY Localization Tool saves it, DPMsgTool's JSON)\r\n"
    "; and, if you changed it, the tool's glyphmap.json into UltimateFix\\text. The records whose text differs from the loaded\r\n"
    "; MES_ALL.MES are replaced in memory, the game's files stay as they are. Files *.txt with lines \"0x1234 = text\" (same\r\n"
    "; markers) correct single records. What is refused and why is in UltimateFix\\loc.log. With none of these files in the\r\n"
    "; folder nothing is hooked. The guide is docs/TEXT_OVERLAY.md of the mod. Restart needed.\r\n"
    "TextOverlay=1\r\n"
    "; For translators: 1 = the id of every text record is printed over it on the screen (the number the overlay files use).\r\n"
    "; Restart needed to switch it on.\r\n"
    "ShowMessageId=0\r\n"
    "; For translators: 1 = UltimateFix\\loc.log gets every message that uses a cell that is empty in the font atlas or lies beyond\r\n"
    "; it, and every draw that overflows the game's batch of 768 glyph quads (once per message and cell). Restart needed.\r\n"
    "GlyphLog=0\r\n"
    "; For translators: 1 = the MES_ALL.MES the game loaded is written to UltimateFix\\mes_dump.json in the form of the tool's\r\n"
    "; mes_all.json, at every start: switch it off afterwards. Restart needed.\r\n"
    "TextDump=0\r\n"
    "\r\n"
    "[World]\r\n"
    "; Culling distances of the camera, in game units. Objects farther away are not drawn. The game uses\r\n"
    "; 5000, 1000 and 500; the last class holds huts, vending machines and other objects that then vanish\r\n"
    "; at 500. Higher values draw more objects: slightly more load on the processor. Applied at once.\r\n"
    "FarClass3=5000\r\n"
    "FarClass4=1000\r\n"
    "FarClass5=2000\r\n"
    "; Radius in which objects are updated (AI, animation) as a multiple of the game's 1000 units: 1..3.\r\n"
    "ActivationScale=1\r\n"
    "; Detailed models stay visible farther away, as a multiple of the game's distance: 1..4.\r\n"
    "LodScale=1\r\n"
    "; The outer cells of the world also load full detail. Needs a lot of memory (the 4 GB flag, AutoLargeAddress).\r\n"
    "HighDetail=0\r\n"
    "; Props vanish near walls and mirrors indoors (a faulty visibility test of the port). 0 = as the game,\r\n"
    "; 1 = bypass the test at the first place (what ZachFix does), 2 = at both places.\r\n"
    "InteriorVisibility=1\r\n"
    "; Far objects that the game shows as a low detail package get their full model from farther away: 1 = as the game,\r\n"
    "; 2 = one more ring of cells, 3 = the whole streamed area. Costs memory (use ufx_laa.exe). Changes at once.\r\n"
    "Alternate3DScale=1\r\n"
    "\r\n"
    "[Gameplay]\r\n"
    "; The PC port always starts new games on Easy. The console version defaults to Normal.\r\n"
    "; 1 = take control of the difficulty, 0 = leave the game as it is.\r\n"
    "DifficultyControl=1\r\n"
    "; 0 = easy, 1 = normal, 2 = hard. The selection screen of a new game opens on it; the menu changes it at once, also in a running game.\r\n"
    "Difficulty=1\r\n"
    "; Reflection gate of wet surfaces (idea of ZachFix; checked in the game, on by default). Changes at once.\r\n"
    "WetReflections=1\r\n"
    "; Camera angle limits compare with a tolerance. Only needed when the FPU runs in a precision other than\r\n"
    "; the 24 bits Direct3D sets (some wrappers); off by default. Needs a game restart.\r\n"
    "AimTolerance=0\r\n"
    "\r\n"
    "[FreeCam]\r\n"
    "; Free camera (idea of FreeCam by Behelit09, MIT). Enabled switches the F5 / F4 hotkeys (F6 always works). Key switches the camera,\r\n"
    "; HudKey hides the HUD (virtual key codes: 116 = F5, 115 = F4). W S A D move, Space/E up, C/Q down, Shift faster,\r\n"
    "; Ctrl slower, the mouse looks. Speed in world units per second; Fov in degrees (30..120, 0 = the game's).\r\n"
    "Enabled=1\r\n"
    "Key=116\r\n"
    "HudKey=115\r\n"
    "Speed=20\r\n"
    "FreezeGame=1\r\n"
    "Fov=0\r\n"
    "\r\n"
    "[Audio]\r\n"
    "; Surround sound as on the Xbox 360. It acts only when the Windows output device has more than two channels (5.1, 7.1, a\r\n"
    "; spatial sound format for headphones); on stereo nothing is changed. Every tick the game gives each playing sound a\r\n"
    "; volume matrix made for stereo only, which a wider device should not take: a sound then keeps the place and the volume it\r\n"
    "; started with (players report a brake sound that never stops; by the code volume fades are affected too).\r\n"
    "; 1 = automatic: a sound in the world gets the full X3DAudio matrix every tick (it follows its source, left and right as\r\n"
    "; the picture shows them), other sounds the console's stereo pan with the rear speakers at 0.8, a six-channel sound one\r\n"
    "; channel per speaker. 2 = the same with left and right the other way round (try it if the sides are wrong). 0 = off.\r\n"
    "; Not yet checked in the game; the lines starting with audio: in UltimateFix\\ufx.log say what was detected and done.\r\n"
    "; Changes at once.\r\n"
    "Surround=1\r\n"
    "\r\n"
    "[Map]\r\n"
    "; The map in the pause menu: all 64 tiles of the town at once, zoom out down to MinZoom, orientation fixed.\r\n"
    "; Ideas of MapFix by Behelit09 (MIT). Restart needed for Enabled; the rest changes at once from F10.\r\n"
    "Enabled=1\r\n"
    "; 0 = the map turns with the player (the game), 1 = north up, 2 = south up.\r\n"
    "Orientation=1\r\n"
    "; Smallest zoom and the zoom the map opens with, in percent (the game: 100 and 100).\r\n"
    "MinZoom=20\r\n"
    "DefaultZoom=20\r\n"
    "\r\n"
    "[Input]\r\n"
    "; DualSense: 1 = a light engine hum in a car (follows the right trigger) and a kick for every shot (by the kind of weapon).\r\n"
    "; Made from the game state, on the pad's two rumble motors; the game's own vibration (hits, crashes) is added on top. Works with\r\n"
    "; Vibration=0 too, while the game is in front and in pad mode; the car hum reads the triggers through NativeDualSense.\r\n"
    "; It is not the DualSense's fine haptics, which need an audio stream. Changes at once.\r\n"
    "SynthHaptics=1\r\n"
    "; 1 = when there is no pad, Windows is asked about it twice a second instead of every frame (each question costs the game thread most\r\n"
    "; of the frame on some systems). A pad plugged in is noticed within half a second.\r\n"
    "ThrottleMissingPad=1\r\n"
    "; DualSense and DualShock 4 are shown to the game as an Xbox 360 controller (the camera no longer spins\r\n"
    "; on its own, buttons are in the right order).\r\n"
    "SonyLayout=1\r\n"
    "; DualSense adaptive triggers: the fire trigger feels like the weapon in hand (pistol click, machine gun vibration, shotgun, ...).\r\n"
    "; Only with TriggerFire=1 (aim and fire on the triggers); with SwapTriggers=1 the effect moves to the left trigger; no effect in a car.\r\n"
    "; Needs the DualSense to be visible to Windows (Steam Input turned off for the game).\r\n"
    "AdaptiveTriggers=1\r\n"
    "AdaptiveStrength=100\r\n"
    "; A light resistance on the aim trigger while a weapon is drawn. Only with TriggerFire=1; with SwapTriggers=1 it is the right trigger.\r\n"
    "AdaptiveAim=1\r\n"
    "; The game's rumble is never sent to the pad on PC. 1 = send it: to a DualSense directly over HID when the mod has\r\n"
    "; opened one (NativeDualSense, Steam Input off), otherwise through XInput (Xbox pads, or a DualSense shown as XInput\r\n"
    "; by Steam Input / DS4Windows). Percent strength below.\r\n"
    "Vibration=1\r\n"
    "VibrationStrength=100\r\n"
    "; Analog trigger values into the three vehicle functions (a ZachFix idea). The Director's Cut puts the brake and\r\n"
    "; boost of the car on the shoulder buttons, not on the triggers as on the console, so this is off by default.\r\n"
    "AnalogTriggers=0\r\n"
    "; Trigger dead zone out of 255 (the console used 30).\r\n"
    "TriggerDeadzone=30\r\n"
    "; The game commits the input snapshot before polling, so input reaches it one tick late. 1 = poll first.\r\n"
    "LowLatencyInput=0\r\n"
    "; Mouse look: speed in % of the game's (25..400) and inversion of each axis. Changes at once.\r\n"
    "MouseSensitivity=100\r\n"
    "InvertMouseX=0\r\n"
    "InvertMouseY=0\r\n"
    "; Stick curve: 1 = the console's (smooth 24% dead zone, no frame-rate dependent smoothing, camera dead zone\r\n"
    "; 0.25), 0 = the PC port's (a stick counts only past ~40% of its travel, then jumps). Controller mode only.\r\n"
    "StickProfile=1\r\n"
    "; Without configJ.cnf / configJex.cnf the game binds every pad action to button A (Select, cars and more do not\r\n"
    "; work). 1 = use the Xbox 360 layout in that case. Files that exist are never overridden.\r\n"
    "DefaultPadBindings=1\r\n"
    "; Read a DualSense directly over HID (USB or Bluetooth) instead of through Windows. On by default: the mod looks for a\r\n"
    "; DualSense (every 2 seconds) and, when Windows lets it open the device, reads it directly and hands that to the game\r\n"
    "; instead of the Windows pad. That can happen with Steam Input on too, and then Steam Input's mapping does not reach\r\n"
    "; the game: set 0 to use it. Over Bluetooth, rumble and adaptive triggers work only with 1; over USB also with 0.\r\n"
    "; With no DualSense nothing changes. Needs a restart.\r\n"
    "NativeDualSense=1\r\n"
    "; Pads that only XInput can see (Steam Input, DS4Windows, Xbox pads) are given to the game when WinMM has none.\r\n"
    "XInputFallback=1\r\n"
    "; The game polls joystick slots 0..6 every tick but only ever uses slot 0; polling the empty ones can stall\r\n"
    "; for ~60 ms every 20 s. 1 = ask only slot 0.\r\n"
    "OnlyFirstPad=1\r\n"
    "; The game uses a controller only if configJ.cnf says so. 1 = switch between keyboard and gamepad mode\r\n"
    "; automatically by whichever you use.\r\n"
    "AutoSwitch=1\r\n"
    "; 1 = swap L2 and R2: the buttons, the trigger axis and the DualSense effects. Not on a Sony pad read through Windows with\r\n"
    "; SonyLayout=0, nor on 4-axis DirectInput pads whose Z axis is the right stick.\r\n"
    "SwapTriggers=0\r\n"
    "; 1 = aim on L2 and fire on R2 (run moves to L1, hold breath and lock-on to R1); 0 = the game's layout (aim L1, fire R1).\r\n"
    "; Behind the wheel the game's own layout is used, and on a Sony pad read through Windows with SonyLayout=0. Changes at once.\r\n"
    "TriggerFire=1\r\n"
    "; Strafe with LB / RB in combat while locked on (the console had it, the PC code lost it). Both buttons are also bound (LB aim,\r\n"
    "; RB fire; with TriggerFire=1 L1 run, R1 hold breath and lock-on), so pressing either while locked on also sidesteps. Changes at once.\r\n"
    "CombatStrafe=0\r\n"
    "; Quick turn as on the Xbox 360 (the PC code kept the turn itself, player state 0Bh, but on foot without a weapon held ready\r\n"
    "; nothing starts it). On foot, with York's back to the camera: pull the left stick straight back (within 30 degrees) and press\r\n"
    "; Run at the same moment, at the latest about 0.2 s after the stick (later York has already turned by himself), and he turns\r\n"
    "; around on the spot. Run is the button the game runs with (the console's X): L1 with TriggerFire=1, the left trigger with the\r\n"
    "; game's layout; on the keyboard it is S together with the run key (Left Shift). Holding Run before the stick does nothing new:\r\n"
    "; York turns by himself as before. Not while aiming or in a weapon stance, not in a car, a cutscene or with the F10 menu open.\r\n"
    "; Not installed with [Debug] SafeMode=1. Changes at once.\r\n"
    "QuickTurn=1\r\n"
    "\r\n"
    "[Waypoint]\r\n"
    "; A waypoint: F7 on the pause map sets it at the middle of the map (the +), F7 in the world sets it where you stand (or where the\r\n"
    "; free camera is), Shift+F7 removes it. It is drawn like the game's own \"Yards\" label, in the game's font: over the place in the\r\n"
    "; world (on the screen edge with an arrow when it is off the screen; not in cutscenes), and a red X on the map when [Map] Enabled=1\r\n"
    "; and the map does not turn (Orientation 1 or 2). The DualSense touchpad click does the same as F7.\r\n"
    "Enabled=1\r\n"
    "; The distance is in yards with the game's own factor (the one of its \"Yards\" label). Key: virtual key code, 118 = F7.\r\n"
    "Key=118\r\n"
    "; 1 = the waypoint is also put into the game's own list of map points as an objective: the minimap shows the game's red\r\n"
    "; chevrons on its edge towards it (more of them the farther it is), the pause map its icon. Needs a restart.\r\n"
    "Minimap=1\r\n"
    "; 1 = while the place is on the screen, the game's own objective beacon stands over it instead of the bare label: the red arrows\r\n"
    "; (more of them the farther it is) with \"N Yards\" under them, drawn by the game's own code. The label stays for a place off the\r\n"
    "; screen or behind you, and while the game has not loaded the arrow picture. 0 = always the label. Turning it off works at\r\n"
    "; once, turning it on needs a restart.\r\n"
    "Beacon=1\r\n"
    "; Ctrl + this key puts the waypoint on the game's current objective (the story destination first, then the other objective\r\n"
    "; points of its map), so the distance and the arrow on the screen edge work for story goals. Press again for the next one when\r\n"
    "; there are several. Virtual key code: 118 = F7 (so Ctrl+F7), 0 = off. Changes at once.\r\n"
    "ObjectiveKey=118\r\n"
    "\r\n"
    "[Debug]\r\n"
    "; 1 = a run with fewer hooks: no free camera, physics hook, strafe/costume/pose/cheat bridge, alternate 3D, waypoint, icon index fix,\r\n"
    "; effect fix, SSAO, blur look; the frame limit, physics cap, auto recovery, trigger layout, haptics, pause in menu, FXAA, sharpening\r\n"
    "; and the FPS panels are off. Everything else stays on (also the hang watchdog, profiler, map, DualSense reader and output, icon\r\n"
    "; themes, launcher stub, processor settings). This file is not changed. For finding out what crashes after an update: turn it on,\r\n"
    "; and if the crash is gone turn the features back one by one.\r\n"
    "SafeMode=0\r\n"
    "; 1 = show FPS, frame time and the GPU time of the mod's own passes in the corner (also in F10 -> Display).\r\n"
    "PerfOverlay=0\r\n"
    "; Separate frame statistics panel (top right): total, game, physics, Present, worst frame, graph.\r\n"
    "FrameStats=0\r\n"
    "; 1 = write every texture, render target and depth surface the game creates (and the viewports it\r\n"
    "; uses) to the log. Changes nothing in the game. Play a minute, then send UltimateFix\\ufx.log.\r\n"
    "TraceResources=0\r\n"
    "; 1 = after 300 slow frames in a row (over 20 ms each, over 40 ms in the 30 frames/s mode, under 120 ms; about 7 s at 40 frames/s)\r\n"
    "; the sampling profiler runs by itself for 10 seconds (twice at most per start) and\r\n"
    "; writes where the game spends its time to the log (profile: lines). Ctrl+F9 starts it by hand.\r\n"
    "AutoProfile=1\r\n"
    "; Guards against a known crash: an NPC after a cutscene is sent to a navigation node that does not exist and\r\n"
    "; the game reads outside its table. 1 = refuse such lookups instead of crashing.\r\n"
    "CrashGuards=1\r\n"
    "; 1 = on a crash write a report to the log and a minidump (UltimateFix\\crash_*.dmp). Changes nothing otherwise.\r\n"
    "CrashDump=1\r\n"
    "; 1 = write the textures the game loads (DDS, up to 1 MB each, 6000 files) to UltimateFix\\textures\\dump\\<hash>.dds.\r\n"
    "; For finding textures to replace, e.g. the interface icons. Delete the folder afterwards.\r\n"
    "DumpTextures=0\r\n"
    "; 1 = log the player state changes, the game's camera mode changes, scripted camera requests and sudden camera (eye) jumps, each\r\n"
    "; with the cutscene flag, and the car input once a second while driving; at most 600 lines per start. Play to the place where the\r\n"
    "; camera should change, then send UltimateFix\\ufx.log. Off by default (every line is written on the game thread).\r\n"
    "; With it on, Numpad 2 writes a \"camera mark\" line at the moment the camera goes wrong.\r\n"
    "TraceCamera=0\r\n"
    "\r\n"
    "[Menu]\r\n"
    "; 1 = a small card in the top right corner for the first seconds of a start: the mod's settings are on F10.\r\n"
    "StartHint=1\r\n"
    "; 1 = ask GitHub once at the start whether a newer release is out (one anonymous request). F9 on the start card or on the About\r\n"
    "; page then downloads it, replaces X3DAudio1_7.dll and UltimateFix\\glyphs, keeps this file, mods and backups, and restarts the game.\r\n"
    "CheckUpdates=1\r\n"
    "; How opaque the F10 panel is, percent 20..100 (lower: the game shows through). Changes at once.\r\n"
    "Opacity=80\r\n"
    "; Place and size of the F10 panel in the 1280x720 picture. Drag the header to move it, the corner to resize it, double-click\r\n"
    "; the corner to put it back (the mod writes these four values itself).\r\n"
    "X=100\r\n"
    "Y=52\r\n"
    "Width=1080\r\n"
    "Height=616\r\n"
    "; Language of the menu: 0 = Ukrainian, 1 = English (the buttons at the bottom of the menu change it).\r\n"
    "Language=-1\r\n"
    "; Settings menu inside the game, opened with F10.\r\n"
    "Enabled=1\r\n"
    "\r\n"
    "[Save]\r\n"
    "; dp.sav is written to a temporary file and replaced atomically.\r\n"
    "Guard=1\r\n"
    "; How many dated backups of dp.sav to keep (1..200). Each is named by when it was made and where the story stood in it.\r\n"
    "Backups=60\r\n"
    "; 1 = the backup folder is compressed by Windows (NTFS): 60 copies of 8 MB take a fraction of 480 MB.\r\n"
    "CompressBackups=1\r\n"
    "; If dp.sav has the wrong size at load time, restore the newest good backup.\r\n"
    "RestoreDamaged=1\r\n"
    "; Show a message when a save could not be committed or was restored.\r\n"
    "Warnings=1\r\n";

int g_clampedOutput = 0;

// The current mode of the monitor the game window is on. At load time the window does not exist yet: then the primary monitor,
// where the game opens its window. EnumDisplaySettings gives real pixels whatever the DPI awareness is.
static bool MonitorMode(int* width, int* height) {
    const wchar_t* device = nullptr;
    MONITORINFOEXW mi;
    // GameWindowHandle reads a DP.exe 1.01b variable: only on that build (LoadConfig also runs on a renamed, other exe)
    HWND w = IsSupportedBuild(GetModuleHandleW(nullptr)) ? GameWindowHandle() : nullptr;
    if (w && IsWindow(w)) {
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi)) device = mi.szDevice;
    }
    DEVMODEW dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;
    if (!EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &dm) || dm.dmPelsWidth < 640 || dm.dmPelsHeight < 360) return false;
    *width = (int)dm.dmPelsWidth;
    *height = (int)dm.dmPelsHeight;
    return true;
}

// The widest 16:9 picture that fits the monitor (see MonitorMode): 1920 on a 1080p screen, 2560 on 1440p.
// A bigger output than the monitor shows a white strip at the bottom in the loading screens, so this is the largest output allowed.
int MonitorFitWidth() {
    int mw = 0, mh = 0;
    int w = 1920;
    if (MonitorMode(&mw, &mh)) {
        long long byHeight = (long long)mh * 16 / 9;
        w = (int)(mw < byHeight ? mw : byHeight);
    }
    w &= ~7;
    if (w < 1280) w = 1280;
    if (w > 3840) w = 3840;
    return w;
}

// The default ini with OutputWidth set to the monitor (the line is replaced once, in a copy).
const char* DefaultIniText(unsigned long* length) {
    static char text[sizeof(kDefaultIni) + 16];
    static unsigned long len = 0;
    if (!len) {
        const char* key = "OutputWidth=0\r\n";
        int keyLen = 0;
        while (key[keyLen]) ++keyLen;
        unsigned long n = 0, i = 0, total = (unsigned long)(sizeof(kDefaultIni) - 1);
        bool done = false;
        while (i < total) {
            bool match = !done;
            for (int k = 0; match && key[k]; ++k)
                if (i + k >= total || kDefaultIni[i + k] != key[k]) match = false;
            if (match) {
                n += (unsigned long)wsprintfA(text + n, "OutputWidth=%d\r\n", MonitorFitWidth());
                i += (unsigned long)keyLen;
                done = true;
            } else {
                text[n++] = kDefaultIni[i++];
            }
        }
        text[n] = 0;
        len = n;
    }
    *length = len;
    return text;
}

static void WriteDefaultIniIfMissing(const wchar_t* iniFile) {
    HANDLE h = CreateFileW(iniFile, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;  // exists already (or cannot be created)
    DWORD w;
    unsigned long iniLen = 0;
    const char* iniText = DefaultIniText(&iniLen);
    WriteFile(h, iniText, iniLen, &w, nullptr);
    CloseHandle(h);
}

// Up to 0.5.124 the default ini put these five keys under [Gameplay], while the mod and the menu read and write them in [World]:
// the [Gameplay] copies were never read (the defaults or the menu's [World] values were used). They are removed and NOT moved to
// [World]: an old template could hold other values there (HighDetail=1 once), and moving them would change what an existing
// install runs. Whatever [World] says, or the default, stays in effect. Once removed there is nothing left to do.
static void DropStaleWorldKeysFromGameplay(const wchar_t* ini) {
    static const wchar_t* const kKeys[5] = {L"ActivationScale", L"LodScale", L"HighDetail", L"InteriorVisibility", L"Alternate3DScale"};
    int removed = 0;
    for (int i = 0; i < 5; ++i) {
        wchar_t old[32];
        if (GetPrivateProfileStringW(L"Gameplay", kKeys[i], L"", old, 32, ini) == 0) continue;
        WritePrivateProfileStringW(L"Gameplay", kKeys[i], nullptr, ini);
        ++removed;
    }
    if (removed)
        Log("config: %d unused [Gameplay] copies of the [World] keys removed (they never had an effect; [World] or the defaults apply)", removed);
}

// Keys of older templates that nothing reads any more ([General] LogLevel and SkipLauncher) or that were never used ([Waypoint]
// UnitsPerMeter, HudX, HudY, MapCenterX, MapCenterY). Their comments said they had an effect, so they are removed; nothing
// changes, since none of them was read. The old comment lines above them stay in the file.
static void DropUnreadKeys(const wchar_t* ini) {
    struct Key {
        const wchar_t* section;
        const wchar_t* key;
    };
    static const Key kKeys[7] = {{L"General", L"LogLevel"},   {L"General", L"SkipLauncher"}, {L"Waypoint", L"UnitsPerMeter"},
                                 {L"Waypoint", L"HudX"},      {L"Waypoint", L"HudY"},        {L"Waypoint", L"MapCenterX"},
                                 {L"Waypoint", L"MapCenterY"}};
    int removed = 0;
    for (int i = 0; i < 7; ++i) {
        wchar_t old[32];
        if (GetPrivateProfileStringW(kKeys[i].section, kKeys[i].key, L"", old, 32, ini) == 0) continue;
        WritePrivateProfileStringW(kKeys[i].section, kKeys[i].key, nullptr, ini);
        ++removed;
    }
    if (removed) Log("config: %d keys that are no longer read removed (LogLevel, SkipLauncher, old [Waypoint] keys)", removed);
}

void LoadConfig(const wchar_t* ini) {
    WriteDefaultIniIfMissing(ini);
    DropStaleWorldKeysFromGameplay(ini);
    DropUnreadKeys(ini);
    g_cfg.relativeQpc = (int)GetPrivateProfileIntW(L"Time", L"RelativeQpc", 1, ini);
    g_cfg.halveFlashes = (int)GetPrivateProfileIntW(L"Fixes", L"HalveFlashEffects", 1, ini);
    g_cfg.byteTextCyrillic = (int)GetPrivateProfileIntW(L"Fixes", L"ByteTextCyrillic", 0, ini);
    g_cfg.keepBlurLook = (int)GetPrivateProfileIntW(L"Graphics", L"KeepBlurLook", 0, ini);
    g_cfg.alt3dScale = (int)GetPrivateProfileIntW(L"World", L"Alternate3DScale", 1, ini);
    if (g_cfg.alt3dScale < 1) g_cfg.alt3dScale = 1;
    if (g_cfg.alt3dScale > 3) g_cfg.alt3dScale = 3;
    g_cfg.perfOverlay = (int)GetPrivateProfileIntW(L"Debug", L"PerfOverlay", 0, ini);
    g_cfg.frameStats = (int)GetPrivateProfileIntW(L"Debug", L"FrameStats", 0, ini);
    g_cfg.triggerFire = (int)GetPrivateProfileIntW(L"Input", L"TriggerFire", 1, ini);
    g_cfg.safeMode = (int)GetPrivateProfileIntW(L"Debug", L"SafeMode", 0, ini);
    g_cfg.traceStates = (int)GetPrivateProfileIntW(L"Debug", L"TraceStates", 0, ini);
    g_cfg.combatStrafe = (int)GetPrivateProfileIntW(L"Input", L"CombatStrafe", 0, ini);
    g_cfg.quickTurn = (int)GetPrivateProfileIntW(L"Input", L"QuickTurn", 1, ini);
    g_cfg.synthHaptics = (int)GetPrivateProfileIntW(L"Input", L"SynthHaptics", 1, ini);
    g_cfg.noPowerThrottling = (int)GetPrivateProfileIntW(L"General", L"NoPowerThrottling", 1, ini);
    g_cfg.performanceCoresOnly = (int)GetPrivateProfileIntW(L"General", L"PerformanceCoresOnly", 0, ini);
    g_cfg.autoProfile = (int)GetPrivateProfileIntW(L"Debug", L"AutoProfile", 1, ini);
    g_cfg.throttleMissingPad = (int)GetPrivateProfileIntW(L"Input", L"ThrottleMissingPad", 1, ini);
    g_cfg.waypoint = (int)GetPrivateProfileIntW(L"Waypoint", L"Enabled", 1, ini);
    g_cfg.waypointKey = (int)GetPrivateProfileIntW(L"Waypoint", L"Key", 0x76, ini);
    g_cfg.waypointMinimap = (int)GetPrivateProfileIntW(L"Waypoint", L"Minimap", 1, ini);
    g_cfg.waypointBeacon = (int)GetPrivateProfileIntW(L"Waypoint", L"Beacon", 1, ini);
    g_cfg.waypointObjectiveKey = (int)GetPrivateProfileIntW(L"Waypoint", L"ObjectiveKey", 0x76, ini);
    if (g_cfg.waypointObjectiveKey < 0 || g_cfg.waypointObjectiveKey > 254) g_cfg.waypointObjectiveKey = 0;
    g_cfg.replaceLauncher = (int)GetPrivateProfileIntW(L"General", L"ReplaceLauncher", 1, ini);
    g_cfg.autoRecover = (int)GetPrivateProfileIntW(L"Time", L"AutoRecover", 0, ini);
    g_cfg.physicsSteps = (int)GetPrivateProfileIntW(L"Time", L"PhysicsSteps", 0, ini);
    if (g_cfg.physicsSteps < 0 || g_cfg.physicsSteps > 3) g_cfg.physicsSteps = 0;
    g_cfg.highFpsPhysics = (int)GetPrivateProfileIntW(L"Time", L"HighFpsPhysics", 1, ini);
    g_cfg.pauseInMenu = (int)GetPrivateProfileIntW(L"Time", L"PauseInMenu", 0, ini);
    g_cfg.surround = (int)GetPrivateProfileIntW(L"Audio", L"Surround", 1, ini);
    if (g_cfg.surround < 0 || g_cfg.surround > 2) g_cfg.surround = 1;
    g_cfg.framePacing = (int)GetPrivateProfileIntW(L"Time", L"FramePacing", 0, ini);
    if (g_cfg.framePacing < 0 || g_cfg.framePacing > 3) g_cfg.framePacing = 0;
    // Migration 2 of 0.5.x turned a 0 into 1 once. Since 0.7.0 no limit is the default again and is safe (PacingMode keeps the
    // limit on screens of 61..139 Hz), so nothing is migrated any more; the mark stays for files that still lack it.
    if (GetPrivateProfileIntW(L"Time", L"PacingMigrated", 0, ini) < 2) WritePrivateProfileStringW(L"Time", L"PacingMigrated", L"2", ini);
    g_cfg.zeroDeltaGuard = (int)GetPrivateProfileIntW(L"Fixes", L"ZeroDeltaGuard", 1, ini);
    g_cfg.nanTrapGuard = (int)GetPrivateProfileIntW(L"Fixes", L"NanTrapGuard", 1, ini);
    g_cfg.pinAudio = (int)GetPrivateProfileIntW(L"Fixes", L"PinAudio", 1, ini);
    g_cfg.pinFpu = (int)GetPrivateProfileIntW(L"Fixes", L"PinFpu", 1, ini);
    g_cfg.houseListFix = (int)GetPrivateProfileIntW(L"Fixes", L"HouseListFix", 1, ini);
    g_cfg.removeExtraJoyPoll = (int)GetPrivateProfileIntW(L"Fixes", L"RemoveExtraJoyPoll", 1, ini);
    g_cfg.skipIntro = (int)GetPrivateProfileIntW(L"Fixes", L"SkipIntro", 1, ini);
    g_cfg.resetRefreshFix = (int)GetPrivateProfileIntW(L"Fixes", L"ResetRefreshFix", 1, ini);
    g_cfg.borderless = (int)GetPrivateProfileIntW(L"Display", L"Borderless", 1, ini);
    g_cfg.hideCursor = (int)GetPrivateProfileIntW(L"Display", L"HideCursor", 1, ini);
    g_cfg.dpiAware = (int)GetPrivateProfileIntW(L"Display", L"DpiAware", 1, ini);
    g_cfg.scaleToWindow = (int)GetPrivateProfileIntW(L"Display", L"ScaleToWindow", 1, ini);
    g_cfg.displayDebug = (int)GetPrivateProfileIntW(L"Display", L"Debug", 0, ini);
    g_cfg.forceUsSlot = 0;  // removed: the game keeps the language of config.cnf (mods replace files whatever the slot); old ini keys are ignored
    g_cfg.textOverlay = (int)GetPrivateProfileIntW(L"Language", L"TextOverlay", 1, ini);
    g_cfg.showMessageId = (int)GetPrivateProfileIntW(L"Language", L"ShowMessageId", 0, ini);
    g_cfg.glyphLog = (int)GetPrivateProfileIntW(L"Language", L"GlyphLog", 0, ini);
    g_cfg.textDump = (int)GetPrivateProfileIntW(L"Language", L"TextDump", 0, ini);
    g_cfg.farClass3 = (int)GetPrivateProfileIntW(L"World", L"FarClass3", 5000, ini);
    g_cfg.farClass4 = (int)GetPrivateProfileIntW(L"World", L"FarClass4", 1000, ini);
    g_cfg.farClass5 = (int)GetPrivateProfileIntW(L"World", L"FarClass5", 2000, ini);
    // Never below what the game itself uses: a lower value only makes the world vanish sooner.
    if (g_cfg.farClass3 < 5000) g_cfg.farClass3 = 5000;
    if (g_cfg.farClass4 < 1000) g_cfg.farClass4 = 1000;
    if (g_cfg.farClass5 < 500) g_cfg.farClass5 = 500;
    // Nor above the loaded area (about 10000 around the player): more changes nothing.
    if (g_cfg.farClass3 > 10000) g_cfg.farClass3 = 10000;
    if (g_cfg.farClass4 > 5000) g_cfg.farClass4 = 5000;
    if (g_cfg.farClass5 > 10000) g_cfg.farClass5 = 10000;
    g_cfg.sonyLayout = (int)GetPrivateProfileIntW(L"Input", L"SonyLayout", 1, ini);
    g_cfg.adaptiveTriggers = (int)GetPrivateProfileIntW(L"Input", L"AdaptiveTriggers", 1, ini);
    g_cfg.adaptiveStrength = (int)GetPrivateProfileIntW(L"Input", L"AdaptiveStrength", 100, ini);
    if (g_cfg.adaptiveStrength < 0) g_cfg.adaptiveStrength = 0;
    if (g_cfg.adaptiveStrength > 100) g_cfg.adaptiveStrength = 100;
    g_cfg.adaptiveAim = (int)GetPrivateProfileIntW(L"Input", L"AdaptiveAim", 1, ini);
    g_cfg.triggerTest = 0;
    g_cfg.vibration = (int)GetPrivateProfileIntW(L"Input", L"Vibration", 1, ini);
    g_cfg.vibrationStrength = (int)GetPrivateProfileIntW(L"Input", L"VibrationStrength", 100, ini);
    if (g_cfg.vibrationStrength < 0) g_cfg.vibrationStrength = 0;
    if (g_cfg.vibrationStrength > 100) g_cfg.vibrationStrength = 100;
    g_cfg.analogTriggers = (int)GetPrivateProfileIntW(L"Input", L"AnalogTriggers", 0, ini);
    g_cfg.triggerDeadzone = (int)GetPrivateProfileIntW(L"Input", L"TriggerDeadzone", 30, ini);
    if (g_cfg.triggerDeadzone < 0) g_cfg.triggerDeadzone = 0;
    if (g_cfg.triggerDeadzone > 254) g_cfg.triggerDeadzone = 254;
    g_cfg.lowLatencyInput = (int)GetPrivateProfileIntW(L"Input", L"LowLatencyInput", 0, ini);
    g_cfg.mouseSensitivity = (int)GetPrivateProfileIntW(L"Input", L"MouseSensitivity", 100, ini);
    if (g_cfg.mouseSensitivity < 25) g_cfg.mouseSensitivity = 25;
    if (g_cfg.mouseSensitivity > 400) g_cfg.mouseSensitivity = 400;
    g_cfg.invertMouseX = GetPrivateProfileIntW(L"Input", L"InvertMouseX", 0, ini) ? 1 : 0;
    g_cfg.invertMouseY = GetPrivateProfileIntW(L"Input", L"InvertMouseY", 0, ini) ? 1 : 0;
    g_cfg.stickProfile = (int)GetPrivateProfileIntW(L"Input", L"StickProfile", 1, ini);
    g_cfg.defaultPadBindings = (int)GetPrivateProfileIntW(L"Input", L"DefaultPadBindings", 1, ini);
    g_cfg.nativeDualSense = (int)GetPrivateProfileIntW(L"Input", L"NativeDualSense", 1, ini);
    if (GetPrivateProfileIntW(L"Input", L"NativeDualSenseMigrated", 0, ini) == 0) {  // the old file says 0 only because that used to be the default
        WritePrivateProfileStringW(L"Input", L"NativeDualSenseMigrated", L"1", ini);
        if (!g_cfg.nativeDualSense) {
            g_cfg.nativeDualSense = 1;
            WritePrivateProfileStringW(L"Input", L"NativeDualSense", L"1", ini);
        }
    }
    g_cfg.xinputFallback = (int)GetPrivateProfileIntW(L"Input", L"XInputFallback", 1, ini);
    g_cfg.onlyFirstPad = (int)GetPrivateProfileIntW(L"Input", L"OnlyFirstPad", 1, ini);
    g_cfg.timerResolution = (int)GetPrivateProfileIntW(L"Time", L"TimerResolution", 1, ini);
    g_cfg.autoSwitchInput = (int)GetPrivateProfileIntW(L"Input", L"AutoSwitch", 1, ini);
    g_cfg.swapTriggers = (int)GetPrivateProfileIntW(L"Input", L"SwapTriggers", 0, ini);
    g_cfg.outputWidth = (int)GetPrivateProfileIntW(L"Graphics", L"OutputWidth", 0, ini);
    if (g_cfg.outputWidth < 0) g_cfg.outputWidth = 0;
    if (g_cfg.outputWidth > 3840) g_cfg.outputWidth = 3840;  // 4K is the limit: larger buffers do not fit in video memory
    g_cfg.outputHeight = (int)GetPrivateProfileIntW(L"Graphics", L"OutputHeight", 0, ini);
    if (g_cfg.outputHeight < 0) g_cfg.outputHeight = 0;
    if (g_cfg.outputHeight > 2160) g_cfg.outputHeight = 2160;
    {   // an output bigger than the monitor shows a white strip at the bottom in loading screens: the monitor is the limit, the upscale
        // is chosen with RenderScale (supersampling) instead. Only this run is limited: the ini keeps the player's value, so one start
        // with a smaller monitor mode (a TV, a laptop panel) does not destroy it. main.cpp logs a cut width (g_clampedOutput), after
        // the DPfix check. The height is always 9/16 of the width (output_resolution.cpp logs an OutputHeight it ignores).
        int fit = MonitorFitWidth();
        if (g_cfg.outputWidth > fit) {
            g_clampedOutput = g_cfg.outputWidth;
            g_cfg.outputWidth = fit;
            g_cfg.outputHeight = 0;  // a height set for the cut width means nothing now
        }
    }
    g_cfg.renderScale = (int)GetPrivateProfileIntW(L"Graphics", L"RenderScale", 100, ini);
    if (g_cfg.renderScale < 100) g_cfg.renderScale = 100;
    if (g_cfg.renderScale > 400) g_cfg.renderScale = 400;
    g_cfg.shadowScale = (int)GetPrivateProfileIntW(L"Graphics", L"ShadowScale", 100, ini);
    if (g_cfg.shadowScale < 100) g_cfg.shadowScale = 100;
    if (g_cfg.shadowScale > 400) g_cfg.shadowScale = 400;
    g_cfg.ambientOcclusion = (int)GetPrivateProfileIntW(L"Graphics", L"AmbientOcclusion", 0, ini);
    if (g_cfg.ambientOcclusion < 0) g_cfg.ambientOcclusion = 0;
    if (g_cfg.ambientOcclusion > 100) g_cfg.ambientOcclusion = 100;
    g_cfg.aoRadius = (int)GetPrivateProfileIntW(L"Graphics", L"AoRadius", 10, ini);
    if (g_cfg.aoRadius < 2) g_cfg.aoRadius = 2;
    if (g_cfg.aoRadius > 40) g_cfg.aoRadius = 40;  // the plain pass stops at 20 by itself (overlay.cpp)
    g_cfg.aoMode = (int)GetPrivateProfileIntW(L"Graphics", L"AoMode", 1, ini);
    if (g_cfg.aoMode < 0 || g_cfg.aoMode > 1) g_cfg.aoMode = 1;
    g_cfg.antialias = (int)GetPrivateProfileIntW(L"Graphics", L"Antialias", 0, ini);
    if (g_cfg.antialias < 0) g_cfg.antialias = 0;
    if (g_cfg.antialias > 100) g_cfg.antialias = 100;
    {   // a number as the menu writes it (read like every other number here, so text after it does no harm), or the
        // name of the method
        wchar_t mode[16];
        GetPrivateProfileStringW(L"Graphics", L"AntialiasMode", L"", mode, 16, ini);
        mode[4] = 0;  // "SMAA" and whatever follows it on the line
        g_cfg.antialiasMode =
            (GetPrivateProfileIntW(L"Graphics", L"AntialiasMode", 0, ini) == 1 || lstrcmpiW(mode, L"SMAA") == 0) ? 1 : 0;
    }
    g_cfg.smaaQuality = (int)GetPrivateProfileIntW(L"Graphics", L"SmaaQuality", 2, ini);
    if (g_cfg.smaaQuality < 0 || g_cfg.smaaQuality > 2) g_cfg.smaaQuality = 2;
    g_cfg.sharpen = (int)GetPrivateProfileIntW(L"Graphics", L"Sharpen", 50, ini);
    if (g_cfg.sharpen < 0) g_cfg.sharpen = 0;
    if (g_cfg.sharpen > 100) g_cfg.sharpen = 100;
    g_cfg.screenshotKey = (int)GetPrivateProfileIntW(L"Graphics", L"ScreenshotKey", 122, ini);
    g_cfg.glyphTheme = (int)GetPrivateProfileIntW(L"Graphics", L"GlyphTheme", 1, ini);
    if (g_cfg.glyphTheme < 0 || g_cfg.glyphTheme > 3) g_cfg.glyphTheme = 1;
    g_cfg.textureOverride = (int)GetPrivateProfileIntW(L"Graphics", L"TextureOverride", 1, ini);
    g_cfg.pixelOffsetFix = (int)GetPrivateProfileIntW(L"Graphics", L"FixPixelOffset", 1, ini);
    g_cfg.shadowPrecision = (int)GetPrivateProfileIntW(L"Graphics", L"ShadowPrecision", 1, ini);
    g_cfg.scalePostBuffers = (int)GetPrivateProfileIntW(L"Graphics", L"ScalePostBuffers", 0, ini);
    g_cfg.anisotropy = (int)GetPrivateProfileIntW(L"Graphics", L"Anisotropy", 16, ini);
    g_cfg.gameFov = (int)GetPrivateProfileIntW(L"Graphics", L"FieldOfView", 100, ini);
    g_cfg.nearPlane = (int)GetPrivateProfileIntW(L"Graphics", L"NearPlane", 200, ini);
    if (GetPrivateProfileIntW(L"Graphics", L"NearPlaneMigrated", 0, ini) == 0) {  // 30 was the default of 0.7.0-test20..24
        WritePrivateProfileStringW(L"Graphics", L"NearPlaneMigrated", L"1", ini);
        if (g_cfg.nearPlane == 30) {
            g_cfg.nearPlane = 200;
            WritePrivateProfileStringW(L"Graphics", L"NearPlane", L"200", ini);
        }
    }
    if (g_cfg.nearPlane < 10) g_cfg.nearPlane = 10;
    if (g_cfg.nearPlane > 200) g_cfg.nearPlane = 200;
    g_cfg.shadowBiasFix = GetPrivateProfileIntW(L"Graphics", L"ShadowBiasFix", 1, ini) ? 1 : 0;
    g_cfg.generateMips = GetPrivateProfileIntW(L"Graphics", L"GenerateMips", 1, ini) ? 1 : 0;
    g_cfg.gameLook = (int)GetPrivateProfileIntW(L"Graphics", L"GameLook", 0, ini);
    g_cfg.fullscreenEdgeFix = GetPrivateProfileIntW(L"Graphics", L"FullscreenEdgeFix", 1, ini) ? 1 : 0;
    g_cfg.bloomStrength = (int)GetPrivateProfileIntW(L"Graphics", L"BloomStrength", 100, ini);
    if (g_cfg.bloomStrength < 0) g_cfg.bloomStrength = 0;
    if (g_cfg.bloomStrength > 300) g_cfg.bloomStrength = 300;
    g_cfg.exposureComp = (int)GetPrivateProfileIntW(L"Graphics", L"ExposureComp", 0, ini);
    if (g_cfg.exposureComp < -30) g_cfg.exposureComp = -30;
    if (g_cfg.exposureComp > 30) g_cfg.exposureComp = 30;
    if (g_cfg.gameLook < 0 || g_cfg.gameLook > 17) g_cfg.gameLook = 0;
    if (g_cfg.gameFov < 70) g_cfg.gameFov = 70;
    if (g_cfg.gameFov > 150) g_cfg.gameFov = 150;
    if (g_cfg.anisotropy < 1) g_cfg.anisotropy = 1;
    if (g_cfg.anisotropy > 16) g_cfg.anisotropy = 16;
    g_cfg.xboxExposure = (int)GetPrivateProfileIntW(L"Graphics", L"XboxExposure", 0, ini);
    g_cfg.xboxLook = (int)GetPrivateProfileIntW(L"Graphics", L"XboxLook", 0, ini);
    g_cfg.xboxGrade = (int)GetPrivateProfileIntW(L"Graphics", L"XboxGrade", 100, ini);
    if (g_cfg.xboxGrade < 0) g_cfg.xboxGrade = 0;
    if (g_cfg.xboxGrade > 100) g_cfg.xboxGrade = 100;
    g_cfg.xboxContrast = (int)GetPrivateProfileIntW(L"Graphics", L"XboxContrastFormula", 1, ini);
    if (g_cfg.xboxContrast < 0 || g_cfg.xboxContrast > 3) g_cfg.xboxContrast = 1;
    g_cfg.xboxChroma = (int)GetPrivateProfileIntW(L"Graphics", L"XboxChromaFormula", 1, ini);
    if (g_cfg.xboxChroma < 0 || g_cfg.xboxChroma > 2) g_cfg.xboxChroma = 1;
    g_cfg.xboxGamma = (int)GetPrivateProfileIntW(L"Graphics", L"XboxGamma", 100, ini);
    if (g_cfg.xboxGamma < 0) g_cfg.xboxGamma = 0;
    if (g_cfg.xboxGamma > 100) g_cfg.xboxGamma = 100;
    g_cfg.xboxShoulder = (int)GetPrivateProfileIntW(L"Graphics", L"XboxShoulder", 0, ini);
    if (g_cfg.xboxShoulder < 0) g_cfg.xboxShoulder = 0;
    if (g_cfg.xboxShoulder > 100) g_cfg.xboxShoulder = 100;
    g_cfg.reflectionBiasFix = (int)GetPrivateProfileIntW(L"Graphics", L"ReflectionBiasFix", 0, ini);
    g_cfg.difficultyEnabled = (int)GetPrivateProfileIntW(L"Gameplay", L"DifficultyControl", 1, ini);
    g_cfg.difficulty = (int)GetPrivateProfileIntW(L"Gameplay", L"Difficulty", 1, ini);
    if (g_cfg.difficulty < 0) g_cfg.difficulty = 0;
    if (g_cfg.difficulty > 2) g_cfg.difficulty = 2;
    g_cfg.difficultyDialog = 1;  // always: the game's own difficulty screen before a new game
    g_cfg.keepSavedDifficulty = 1;  // always: a loaded slot keeps its difficulty
    g_cfg.wetReflections = (int)GetPrivateProfileIntW(L"Gameplay", L"WetReflections", 1, ini);
    g_cfg.aimTolerance = (int)GetPrivateProfileIntW(L"Gameplay", L"AimTolerance", 0, ini);
    g_cfg.activationScale = (int)GetPrivateProfileIntW(L"World", L"ActivationScale", 1, ini);
    if (g_cfg.activationScale < 1) g_cfg.activationScale = 1;
    if (g_cfg.activationScale > 3) g_cfg.activationScale = 3;
    g_cfg.lodScale = (int)GetPrivateProfileIntW(L"World", L"LodScale", 1, ini);
    if (g_cfg.lodScale < 1) g_cfg.lodScale = 1;
    if (g_cfg.lodScale > 4) g_cfg.lodScale = 4;
    g_cfg.highDetail = (int)GetPrivateProfileIntW(L"World", L"HighDetail", 0, ini);
    g_cfg.interiorVisibility = (int)GetPrivateProfileIntW(L"World", L"InteriorVisibility", 1, ini);
    if (g_cfg.interiorVisibility < 0) g_cfg.interiorVisibility = 0;
    if (g_cfg.interiorVisibility > 2) g_cfg.interiorVisibility = 2;
    g_cfg.crashGuards = (int)GetPrivateProfileIntW(L"Debug", L"CrashGuards", 1, ini);
    g_cfg.resourceReadGuard = (int)GetPrivateProfileIntW(L"Fixes", L"ResourceReadGuard", 1, ini);
    g_cfg.sessionReminderHours = (int)GetPrivateProfileIntW(L"General", L"SessionReminderHours", 3, ini);
    if (g_cfg.sessionReminderHours < 0) g_cfg.sessionReminderHours = 0;
    if (g_cfg.sessionReminderHours > 24) g_cfg.sessionReminderHours = 24;
    g_cfg.photoSuitSet = 1;
    g_cfg.cheatClockSpeed = 10;
    if (g_cfg.safeMode) {  // one switch to find what broke the picture or the camera: everything added lately is off (the file is not changed)
        g_cfg.framePacing = 0;
        g_cfg.physicsSteps = 0;
        g_cfg.autoRecover = 0;
        g_cfg.triggerFire = 0;
        g_cfg.synthHaptics = 0;
        g_cfg.pauseInMenu = 0;
        g_cfg.halveFlashes = 0;
        g_cfg.keepBlurLook = 0;
        g_cfg.antialias = 0;
        g_cfg.ambientOcclusion = 0;
        g_cfg.xboxLook = 0;  // its composite hook (the SSAO one) and its world test are not installed in this mode
        g_cfg.sharpen = 0;
        g_cfg.frameStats = 0;
        g_cfg.perfOverlay = 0;
        Log("config: SafeMode: frame limit, physics cap, auto recovery, trigger layout, haptics, pause in menu, effect speed, blur look, FXAA, SSAO, sharpening and the FPS panels are off for this run");
    }
    g_cfg.freeCam = (int)GetPrivateProfileIntW(L"FreeCam", L"Enabled", 1, ini);
    g_cfg.freeCamKey = (int)GetPrivateProfileIntW(L"FreeCam", L"Key", 0x74, ini);
    g_cfg.freeCamHudKey = (int)GetPrivateProfileIntW(L"FreeCam", L"HudKey", 0x73, ini);
    g_cfg.flyKey = (int)GetPrivateProfileIntW(L"Cheats", L"FlyKey", 0x72, ini);  // F3 (F4..F7 are the free camera, the HUD, the photo mode, the waypoint)
    g_cfg.flySpeed = (int)GetPrivateProfileIntW(L"Cheats", L"FlySpeed", 40, ini);
    if (g_cfg.flySpeed < 5) g_cfg.flySpeed = 5;
    if (g_cfg.flySpeed > 500) g_cfg.flySpeed = 500;
    g_cfg.freeCamSpeed = (int)GetPrivateProfileIntW(L"FreeCam", L"Speed", 20, ini);
    if (g_cfg.freeCamSpeed < 1) g_cfg.freeCamSpeed = 1;
    if (g_cfg.freeCamSpeed > 500) g_cfg.freeCamSpeed = 500;
    g_cfg.freeCamFreeze = (int)GetPrivateProfileIntW(L"FreeCam", L"FreezeGame", 1, ini);
    g_cfg.freeCamFov = (int)GetPrivateProfileIntW(L"FreeCam", L"Fov", 0, ini);
    if (g_cfg.freeCamFov != 0 && (g_cfg.freeCamFov < 30 || g_cfg.freeCamFov > 120)) g_cfg.freeCamFov = 0;
    g_cfg.photoFovOn = g_cfg.freeCamFov >= 30;
    if (!g_cfg.photoFovOn) g_cfg.freeCamFov = 65;  // the game's own field of view is about 65 degrees
    g_cfg.mapFix = (int)GetPrivateProfileIntW(L"Map", L"Enabled", 1, ini);
    g_cfg.mapOrientation = (int)GetPrivateProfileIntW(L"Map", L"Orientation", 1, ini);
    if (g_cfg.mapOrientation < 0 || g_cfg.mapOrientation > 2) g_cfg.mapOrientation = 1;
    g_cfg.mapMinZoom = (int)GetPrivateProfileIntW(L"Map", L"MinZoom", 20, ini);
    if (g_cfg.mapMinZoom < 10) g_cfg.mapMinZoom = 10;
    if (g_cfg.mapMinZoom > 100) g_cfg.mapMinZoom = 100;
    g_cfg.mapDefaultZoom = (int)GetPrivateProfileIntW(L"Map", L"DefaultZoom", 20, ini);
    if (g_cfg.mapDefaultZoom < 10) g_cfg.mapDefaultZoom = 10;
    if (g_cfg.mapDefaultZoom > 350) g_cfg.mapDefaultZoom = 350;
    g_cfg.crashDump = (int)GetPrivateProfileIntW(L"Debug", L"CrashDump", 1, ini);
    g_cfg.dumpTextures = (int)GetPrivateProfileIntW(L"Debug", L"DumpTextures", 0, ini);
    g_cfg.traceCamera = (int)GetPrivateProfileIntW(L"Debug", L"TraceCamera", 0, ini);
    if (GetPrivateProfileIntW(L"Debug", L"TraceCameraMigrated", 0, ini) == 0) {  // the old file says 1 only because that used to be the default
        WritePrivateProfileStringW(L"Debug", L"TraceCameraMigrated", L"1", ini);
        if (g_cfg.traceCamera) {
            g_cfg.traceCamera = 0;
            WritePrivateProfileStringW(L"Debug", L"TraceCamera", L"0", ini);
            Log("config: TraceCamera=1 changed to 0 (the old default; set it to 1 again to trace the camera)");
        }
    }
    g_cfg.traceResources = (int)GetPrivateProfileIntW(L"Debug", L"TraceResources", 0, ini);
    g_cfg.traceWater = (int)GetPrivateProfileIntW(L"Debug", L"TraceWater", 0, ini);
    g_cfg.waterTest = (int)GetPrivateProfileIntW(L"Debug", L"WaterTest", 0, ini);
    g_cfg.menu = (int)GetPrivateProfileIntW(L"Menu", L"Enabled", 1, ini);
    g_cfg.startHint = (int)GetPrivateProfileIntW(L"Menu", L"StartHint", 1, ini);
    g_cfg.checkUpdates = GetPrivateProfileIntW(L"Menu", L"CheckUpdates", 1, ini) ? 1 : 0;
    g_cfg.menuX = (int)GetPrivateProfileIntW(L"Menu", L"X", 100, ini);
    g_cfg.menuY = (int)GetPrivateProfileIntW(L"Menu", L"Y", 52, ini);
    g_cfg.menuW = (int)GetPrivateProfileIntW(L"Menu", L"Width", 1080, ini);
    g_cfg.menuH = (int)GetPrivateProfileIntW(L"Menu", L"Height", 616, ini);
    g_cfg.menuOpacity = (int)GetPrivateProfileIntW(L"Menu", L"Opacity", 80, ini);
    if (g_cfg.menuOpacity < 20) g_cfg.menuOpacity = 20;
    if (g_cfg.menuOpacity > 100) g_cfg.menuOpacity = 100;
    g_cfg.language = (int)GetPrivateProfileIntW(L"Menu", L"Language", -1, ini);
    if (g_cfg.language < 0) {  // not chosen yet: Ukrainian or Russian Windows -> Ukrainian, anything else -> English
        WORD lang = PRIMARYLANGID(GetUserDefaultUILanguage());
        g_cfg.language = (lang == LANG_UKRAINIAN || lang == LANG_RUSSIAN) ? 0 : 1;
    }
    g_cfg.saveGuard = (int)GetPrivateProfileIntW(L"Save", L"Guard", 1, ini);
    g_cfg.saveBackups = (int)GetPrivateProfileIntW(L"Save", L"Backups", 60, ini);
    if (GetPrivateProfileIntW(L"Save", L"BackupsMigrated", 0, ini) == 0) {  // 20 was the old default: an untouched file gets the new one
        WritePrivateProfileStringW(L"Save", L"BackupsMigrated", L"1", ini);
        if (g_cfg.saveBackups == 20) {
            g_cfg.saveBackups = 60;
            WritePrivateProfileStringW(L"Save", L"Backups", L"60", ini);
        }
    }
    g_cfg.saveRestoreDamaged = (int)GetPrivateProfileIntW(L"Save", L"RestoreDamaged", 1, ini);
    g_cfg.saveWarnings = (int)GetPrivateProfileIntW(L"Save", L"Warnings", 1, ini);
    g_cfg.compressBackups = GetPrivateProfileIntW(L"Save", L"CompressBackups", 1, ini) ? 1 : 0;
    g_cfg.fasterLoading = GetPrivateProfileIntW(L"Fixes", L"FasterLoading", 1, ini) ? 1 : 0;
    g_cfg.qteFix = GetPrivateProfileIntW(L"Fixes", L"QteFix", 1, ini) ? 1 : 0;
    g_cfg.lockOnAim = GetPrivateProfileIntW(L"Fixes", L"LockOnAim", 1, ini) ? 1 : 0;
    g_cfg.lockOnHeight = (int)GetPrivateProfileIntW(L"Fixes", L"LockOnHeight", 40, ini);
    if (g_cfg.lockOnHeight < -100) g_cfg.lockOnHeight = -100;
    if (g_cfg.lockOnHeight > 150) g_cfg.lockOnHeight = 150;
    g_cfg.autoLargeAddress = GetPrivateProfileIntW(L"Fixes", L"AutoLargeAddress", 1, ini) ? 1 : 0;
    g_cfg.serialSetThread = GetPrivateProfileIntW(L"Fixes", L"SerialSetThread", 1, ini) ? 1 : 0;
    g_cfg.routeFlushWait = GetPrivateProfileIntW(L"Fixes", L"RouteFlushWait", 1, ini) ? 1 : 0;
    if (g_cfg.saveBackups < 1) g_cfg.saveBackups = 1;
    if (g_cfg.saveBackups > 200) g_cfg.saveBackups = 200;
}

}  // namespace ufx
