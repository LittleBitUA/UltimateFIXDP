# UltimateFIXDP: what was new in earlier versions

The two newest versions are in the [README](README.md).

## What's new in 0.7.13

A small update after 0.7.12.
- **The partner on the gamepad** (still experimental): its standing, walking and running motions are learnt once each from a steady York, so it no longer copies York's turns, aiming and door motions (it looked as if it answered the keyboard, and its motions fell apart); it comes along when York changes location or teleports instead of staying behind. Still to do: the camera does not follow it, it walks through walls and doors, it does not find the ground.
- **The update card** no longer cuts its second line with "…" when a gamepad is in use.
- **No spoilers in the menu:** the playable characters past York and Emily are not named in the "Play as" row.
- **The talk with a placed character** (mission editor, `[Debug] MissionLab=1`) runs in the game's own talk mode: York turns to the character and the camera frames it.

## What's new in 0.7.12

**Photo mode: a whole scene, not only York.** The second picture at the top of the README was made entirely in the mod's photo mode.
- **A character next to York.** Any of Greenvale's 49 residents can be put in front of York as a still figure for the picture: no behaviour, no schedule, never in a save. It has its own rows to move it left / right, back / forward, lower / higher and to turn it, its own body motion (set, number, frame, playback), and the page's face rows work on it too ("The face of: York / The character"). One stands at a time; it goes when the photo mode ends or a cutscene begins.
- **York placed by hand.** Four rows move York from where he stood (sideways, forward, up) and turn him; zeros put him back on the ground.
- **One row of suits.** All the game's suits in one row of 30 instead of a set and a suit.

**Cheats: play as, spawner.**
- **Play as** another of the game's own playable characters, with its model and motions: York, Emily and three more (not named here: spoilers). It works in free play, no photo mode needed. The game writes who you play into the save: put your own character back before saving.
- **Spawner.** Any of the game's 16 cars by name, put in front of York (outdoors in Greenvale); any item by number and name, given with the game's own call.
- **A partner on the gamepad** (_experimental, not yet checked in the game_): a second character walks by the gamepad's left stick while York keeps the keyboard and mouse. A first try at local co-op: the partner is a figure that enemies do not see, it does not fight, and it keeps York's height instead of finding the ground.

**Mission editor, a first version** (_experimental_; needs `[Debug] MissionLab=1` in `UltimateFix.ini`). F10 → Mission editor: a mission is one character, a talk with a choice of two answers and an item for the first one, and a start banner. Its texts live in `UltimateFix\missions\missionN.ini` (the editor opens it in Notepad), the rest is picked in the menu; "Run the mission now" puts the character, shows the banner and waits for you to walk up and press E. The talk runs in the game's own dialogue mode, with the mod's texts added to the game's messages. Checked in the game so far: the banner, the character, the talk and the choice. Not yet: the item, and what the talk looks like in every place.

## What's new in 0.7.11

**The river fix now belongs to its scene.** In 0.7.10 the river was drawn with its ripples, glints and reflection everywhere. From 0.7.11 the fix works by default only in the scene by the river, "Welcome to Greenvale" (York's arrival), the one place the game shows the river up close; everywhere else the water stays as the port draws it. The row "River water as on the Xbox 360" on the Fixes page has three values: Off, Always, Only in the river scene (`[Fixes] RiverWater=0 / 1 / 2`), switched at once. Checked in the game: fixed in the scene, the port's water right after it. A file that still had the 0.7.10 default (Always) is moved to the new default once; pick Always again if you prefer it.

## What's new in 0.7.10

**The river has water again.** On PC the river was a flat blue strip; on the Xbox 360 it is grey-teal, with moving ripples and glints. Two things were lost in the port: the river's own entry of the game's water table (`_river`: reflection 100 %) is never applied, so the shader multiplied the reflection, the lit waves and the glints by zero; and the wave map repeated so often that the waves were smaller than a pixel. The mod gives the river the values of its entry and draws the wave map 16 times larger (picked by eye against Xbox 360 footage). On by default, row "River water as on the Xbox 360" on the Fixes page (`[Fixes] RiverWater`), switched at once. Checked in the game at the river; other water (the lake, puddles, interiors) has not been compared yet. The full story is under [Engine fixes](README.md#engine-fixes-re-based).

**Photo mode: York's face.** A "York's face" group on the photo page: emotion, brows, lids and mouth, each picked by name with its own strength, plus a wrinkled nose, "over the game's face" and "no blinking". The parts mix (a sad emotion with sceptical brows). Works in the game.

**Photo mode, smaller things:**
- **Suits as you scroll.** York wears the suit the list rests on (after a third of a second); the "Wear" button is gone.
- **Body motions** (_experimental_): any motion of York, the residents, the cutscenes or the Raincoat Killer by number, held on a frame or played. Not yet confirmed in the game.
- **Leaving the photo mode** puts York's own motion and face back, however it is closed.
- **F4 hides the minimap too**, not only the rest of the interface.
- **No dimming behind the menu** on any page: the game's brightness no longer jumps between sections.

## What's new in 0.7.9

**The Mods page, redesigned.** A list on the left (the mod's picture, name, version and author, its switch) and a card on the right for the selected mod: its **picture (350×350)**, name, version, author, whether it is on, what it brings, buttons to move it up or down and to open its folder, and its **whole description**. How to give your mod a picture and a description: see [Making a mod](README.md#making-a-mod).

**The F10 menu on a gamepad.** View + Menu together (on a DualSense Create + Options) opens and closes it. The D-pad or the left stick moves (held, it repeats; sliders speed up), A / Cross picks, B / Circle goes back, Y / Triangle puts the default back, LB / RB change the section, LT / RT scroll a screen, R3 shows a whole description, X / Square opens a mod's folder. The hints show the pad's own button icons (PlayStation ones on a DualSense). While the menu is open the game does not see the pad. Row "Gamepad control of the menu" (`[Menu] PadControl`).

**No more stutter without a gamepad.** With no pad plugged in, Windows took 18–62 ms to answer the game's question about it, twice a second. A background thread asks now, and the game gets the last answer at once (`[Input] PadPollThread`).

**Smaller things:**
- The mouse no longer takes the selection away from the arrows or the pad; the cursor hides while a pad drives the menu.
- Moving the panel (its top strip) and resizing it (the bottom right corner) work on every page.
- Questions (restore defaults, restart) show which button is focused; ← → change it.
- The About page has the website link and an "Open the log folder" button.
- **You can see what waits for a restart.** The "restart" tag turns amber only on a setting whose value differs from what runs now, and its description says what runs. The sidebar counts these settings and offers "Restart the game" (a click, R, or X / Square on a pad). It asks first, because progress since the last save is lost, and it refuses while the game is writing its save (seen with the save protection on).
- **Long descriptions in full.** A description too long for its box ends with "… F1 / R3: in full". F1, R3 on a pad or a click on the box opens it on a card over the list, scrolled with ↑ ↓ or the wheel, with the default, the value now and when a change applies. Esc, F1, B or a click outside closes it. A setting without a description (the photo sliders) gets one from its range and default.
- **An honest photo preview.** The photo mode page no longer darkens the whole picture, so brightness, contrast and the filters are judged on the real image; the list keeps enough opacity to stay readable. Hold the right mouse button (L3 on a pad) to fade the panel away and look at the picture; the game still sees no input, and a small note says to let go. "Picture" is now the second row, under the free camera. With the menu closed, a quiet strip at the bottom names the flying keys and F11 / F4 / F10 / F6 for 6 seconds (again after any F key). It is drawn after the screenshot is taken, so it is never in a picture.
- **Adaptive sharpening** (`SharpenMode=1`): sharpening now follows AMD FidelityFX RCAS. Soft detail gets the full strength and contrasty edges only a little, so branches against the sky and wires get no bright halos. `SharpenMode=0` brings back the old unsharp mask. Not yet checked in the game.

## What's new in 0.7.8

**A redesigned F10 menu:**
- **Crisp text at any resolution.** Text and lines sit on whole pixels, so the menu is as sharp as Windows text at 1080p and 1440p, also after dragging the panel. Titles and headings are set in Segoe UI Semibold.
- **One calm palette, the same as the website.** No green any more. Switches are red or grey pills with a round knob, and buttons and cards have rounded corners.
- **Quiet motion.** The menu fades in, the selection glides between rows, and a switch knob slides. Closing stays instant. You can turn it off with the row "Menu animation" (`[Menu] Animations`).
- **You can see what you changed.** A changed setting gets a grey dot, the sidebar shows how many settings changed in each section, and a slider marks its default. Backspace puts the selected setting back to its default.
- **Notices.** A notice is coloured by what it means (grey for information, amber for a warning, coral for an error). Up to three wait their turn, and with the menu open they appear inside the panel instead of over its title.

**Graphics (F10 → Screen and graphics; each can be switched off):**
- **Filters under the interface** (`FiltersUnderHud=1`): smoothing (SMAA/FXAA), sharpening and the game look apply to the 3D picture only, so subtitles and icons stay sharp.
- **Smoother far shadows** (`FarShadowFilter=1`): the far sun shadow cascade is filtered with the same four taps the game uses for the near ones. Distant shadow edges no longer step and crawl.
- **Smooth glow and depth of field** (`SmoothBloom=1`): the bloom and the blur are upscaled with a cubic filter. Halos around lamps and the out-of-focus background have no square steps at 1440p/4K.
- **Dither against banding** (`Dither=1`): a fixed, invisible noise removes the colour bands in skies, fog and dark gradients.
- **The game's own edge filter** (`GameAliasFilter`, off by default): a 4-tap filter the game has in its final shader but never uses. 1 = as written in the game, 2 = adjusted to the render scale. It is not known whether the Xbox used it.

**Also:**
- `DumpTextures` writes every texture, cube maps included, with no size limit.
- For testers: `[Debug] FrameCapture=1` + Numpad 0 writes one frame draw by draw (shaders, states and textures), a RenderDoc-like capture for this Direct3D 9 game.

## What's new in 0.7.7

- **An Xbox pad without Steam Input.** The game reads pads only through the old Windows joystick interface (WinMM), where an Xbox pad is not listed when Steam Input is off: the pad did nothing at all. The mod then hands the game the pad it reads through XInput, which needs `[Input] XInputFallback=1` (the default; in the F10 menu "Gamepad through XInput (fallback)"). If an older setup has it at 0, switch it on.
- **The log says why a pad is not seen**: the fallback switched off, no XInput library, or what XInput answers for each of its four slots.
- Tested by **EliParker**: an Xbox pad and a DualSense Edge both work, with the automatic icons (PlayStation on the DualSense, Xbox on the Xbox pad). Thank you!

## What's new in 0.7.6

- **PlayStation icons on a DualSense, the Edge included.** With `GlyphTheme=1` (automatic) the icons stayed Xbox for three reasons, all fixed:
  - When the game starts with the pad, it loads its own pad icon atlas (`UPDATA\SOURCE\MESSAGE\BTNWIDE.XPC`) instead of the keyboard one, and the mod looked only for the keyboard atlas, so it changed nothing. The mod now recognises the game's pad atlas by its bytes.
  - A texture pack that replaces the icon atlas hid it from the mod. The atlas is now recognised by the game's original file, before any replacement.
  - A DualSense over Bluetooth is found a few seconds after the start, and the icon set was chosen once, before that. It is now checked on every use.
- **Xbox icons from the mod's drawing again** (`UltimateFix\glyphs\gamepad\xbox.png`), the same look as the PlayStation set. The game's own Xbox atlas is used only when that file is missing.
- Thanks to **EliParker** for the reports and logs with a DualSense Edge.

## What's new in 0.7.5

- **Texture replacements from mods work.** A texture in a mod folder (`UltimateFix\mods\<mod>\textures\<hash>.dds` or `.png`) was found but never loaded (`could not be loaded (88760B59)` in the log): the mod looked for the file in the wrong folder. With a mod that carried textures, the own folders (`UltimateFix\textures\override\`, the DPfix and ZachFix folders) failed the same way. Fixed.

## What's new in 0.7.4

- **York flies** (F10 → Cheats, or F3): WASD relative to the camera, the mouse or Q/E turn, Space up, Ctrl down, Shift four times faster, speed slider on the page. Handy for testing a translation. While he flies the game sees no input, and his physics (ground, fall, collisions) waits; turning it off lands him through the game's own teleport sync. Turn it off near the ground: from a height he falls (invulnerability helps).
- **The lock-on aims at the chest.** The game aims at the enemy's waist and lowers the point by a tenth of the distance, so a few metres away it was the hips or the knees. Now the point stays a little above the waist (`[Fixes] LockOnAim`, `LockOnHeight`).
- **DualSense Edge**: when a Sony pad cannot be opened (another program such as DSX, DS4Windows or HidHide holds or hides it) or reports another model, the log now says so.
- For testers: `[Debug] TraceCamera=1` + Numpad 2 marks the camera at a moment; `[Debug] TraceWater=1` logs how the game draws water and which reflection it renders (Numpad 1 tries other water settings).
- The mod list in the log shows Cyrillic names correctly.

## What's new in 0.7.3

- **A mod describes itself in a plain text file.** Put `mod.txt` (or `info.txt`, or the old `mod.ini`) in the mod folder, and F10 → Mods shows its name, version, author and description:
  ```
  Name: Ukrainian translation
  Author: Little Bit
  Version: 1.0
  Description: The whole game text in Ukrainian
  ```
  Ukrainian keys work too (`Назва`, `Автор`, `Версія`, `Опис`), `:` or `=`, in any encoding Notepad saves (UTF-8, UTF-16, ANSI). A `readme.txt` in a mod folder is no longer taken for a game file.
- For testers: `[Debug] TraceWater=1` logs how the game draws water (for the coming water fix).

## What's new in 0.7.2

- **Updates itself.** At the start the mod asks GitHub once whether a newer release is out (one anonymous request). If so, the start card says so: **F9** downloads it, checks every file, replaces `X3DAudio1_7.dll` and the icons, and restarts the game. `UltimateFix.ini`, mods and save backups stay. If anything fails, everything stays as it was. Off: F10 → "Check for updates" or `[Menu] CheckUpdates=0`.
- **A smaller archive**: only the mod itself (`X3DAudio1_7.dll`, `UltimateFix`, a short readme). The untested DXVK and the separate tools are gone: the mod sets 4 GB and skips the launcher window by itself.
- **The QTE fix can be switched** (F10 → Fixes → "QTE at high FPS").
- The sampling profiler moved to **Ctrl+F9**.

## What's new in 0.7.1

- **Stability.**
  - The world builder and the main thread take turns, and a level change waits for the route thread: the crashes and hangs on entering buildings with company, the ones the Windows 98 compatibility mode used to hide.
  - Residents with a missing route stay in place instead of vanishing to (0, 0, 0).
  - The loader waits 1 ms instead of 16 between requests: fewer hitches while the town streams in.
  - No white "not responding" window during long loads.
  - A memory line in the log every minute, and a notice to save and restart when memory runs low.
- **4 GB of memory by itself**: the mod gives `DP.exe` the 4 GB flag (from the next start), no separate tool needed.
- **The stick-waggle QTE works at high FPS** again (every swap counts as at 60 FPS).
- **Saves.**
  - Every save is checked: the money, the story position and the play time must agree between the header and the record, or the mod warns.
  - 60 backups named by episode, chapter and play time, in a compressed folder (about 1 MB each instead of 8).
- **Graphics.**
  - Any photo filter or your own preset can stay on all the time.
  - Bloom strength and exposure correction.
  - The Xbox 360 look stays on in cutscenes and scripted scenes.
  - No doubled depth-of-field edges at render scale 300 %.
  - Fades, loading screens and the item notice cover the screen to the last pixel above 720p.
  - The near plane follows close cameras, so York is no longer cut at the save phone or in doorways.
- **The game's own Xbox pad icons** instead of the mod's drawing (unpacked from the game's `BTNWIDE.XPC`).
- **Screenshots**: Shift+F11 without the HUD, and "with every filter" in photo mode.
- **Mouse**: sensitivity 25–400 % that really turns faster, and inversion per axis.

Known issues: a light strip along some wall corners with HBAO, the X on the pause map drifts while the map pans, some rare scenes use another tone-map shader where the Xbox 360 look is not applied yet (the mod saves it as `UltimateFix\composite_*.bin` for support).

## What's new in 0.7.0

- **Mod manager** (F10 → Mods): every folder in `UltimateFix\mods\` is a mod; switch them on and off and set their order. A mod can carry a translation (`text\`: `mes_all.json` and `glyphmap.json` of the SWERY Localization Tool), textures (`textures\<hash>.dds`, hashes from `[Debug] DumpTextures=1`), or any game file by its name right in the mod folder (`fontwide.xpc`, `mes_all.mes`, `FONTWIDE.DDS` for the picture inside an XPC). Its name, author and version go in `mod.txt` (`Name: …`, `Author: …`, `Version: …`, `Description: …`).
- **Far texture flicker fixed**: the near plane of the play camera (20.0 instead of 1.0), mipmaps built for textures that have none, steadier far shadows.
- **HBAO**: horizon-based ambient occlusion with a depth-aware blur, worked out at up to 1080 rows on large render scales; **SMAA**; **Xbox 360 look**.
- **High FPS**: physics and the game's other timers at 120–180 FPS; the FPS is unlocked by default, the 60 limit stays on 61–139 Hz screens.
- **Camera**: field of view in play, Xbox 360 quick turn, mouse sensitivity (25–400 %) and inversion per axis.
- **Story page** (episode, chapter, side quests, where to go next by name) and **Residents page** (every resident's timetable, rooms and actions named).
- **Waypoint** as the game's own objective: its beacon over the place, arrows on the minimap edge; shown only in its own area.
- **Photo mode**: 13 filters, temperature, tint, sepia, fade, grain, chromatic aberration, letterbox, four presets of your own.
- **Stability**: no endless hang on an angle that is not a number, XAudio2 kept in memory, the x87 precision of Direct3D 9, a clear message when the graphics device cannot be created.
- **Saves**: 60 backups, each named by episode, chapter and play time.
- A start card that the settings are on **F10**; shorter menu labels; draw distance sliders stop where more adds nothing.

Known issues of 0.7.0, see 0.7.1 above for what is fixed.

