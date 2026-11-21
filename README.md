<div align="center">

<img src="docs/logo.png" width="120" alt="UltimateFIXDP"/>

# UltimateFIXDP

**The all-in-one fix for _Deadly Premonition: The Director's Cut_**
<br>
_Універсальний фікс для_ **Deadly Premonition: The Director's Cut**

[![GitHub release](https://img.shields.io/github/v/release/LittleBitUA/UltimateFIXDP?include_prereleases&style=for-the-badge&color=c4192b)](https://github.com/LittleBitUA/UltimateFIXDP/releases)
[![Downloads](https://img.shields.io/github/downloads/LittleBitUA/UltimateFIXDP/total?style=for-the-badge&color=c4192b&label=downloads)](https://github.com/LittleBitUA/UltimateFIXDP/releases)
[![Website](https://img.shields.io/badge/website-ultimatefixdp.github.io-c4192b?style=for-the-badge)](https://ultimatefixdp.github.io/)
[![License](https://img.shields.io/badge/license-MIT-c4192b?style=for-the-badge)](LICENSE)
[![Steam 1.01b](https://img.shields.io/badge/Steam-1.01b-c4192b?style=for-the-badge&logo=steam)](https://store.steampowered.com/app/247660/)
[![Made by Little Bit](https://img.shields.io/badge/by-%C2%ABLittle%20Bit%C2%BB-c4192b?style=for-the-badge)](https://t.me/LittleBitUA)

<img src="docs/screenshots/menu-graphics.jpg" width="92%" alt="UltimateFIXDP settings menu in game"/>

</div>

---

## 🇬🇧 In English

One DLL in the game folder instead of DPfix, a stability fix, a map fix and a crash fix.
No installer: press **F10** in game and everything is there. The mod also gives the game 4 GB of memory by itself (from the second start).

> **Version 0.7.7.** Most features are tested in game, the rest are marked _experimental_. Back up `savedata\dp.sav` first.
> <br>New in 0.7.7: **an Xbox pad without Steam Input**, and the log says why a pad is not seen — see [What's new](#whats-new-in-077). In 0.7.6: **PlayStation icons on a DualSense (Edge too)**. In 0.7.5: texture replacements in mod folders work. In 0.7.4: **York can fly** (cheats, F3), the lock-on aims at the chest instead of the hips. In 0.7.3: a mod's name, author and version in a plain `mod.txt`. In 0.7.2: **updates itself** (F9 on the start card), a much smaller archive — see [What's new](#whats-new-in-072). In 0.7.1: fixes for the **thread races** behind the crashes and hangs on entering buildings, the **QTE at high FPS**, **4 GB of memory by itself**, checked and compressed save backups, the game's own Xbox icons — see [What's new](#whats-new-in-071).

### Features

- 🛡️ **Stability** — the crash after cutscenes, the zero-frame-time freeze and the Alt+Tab hang are fixed; a crash handler and a freeze watchdog write a dump if anything still goes wrong
- 🚀 **Low FPS fixed** — on some PCs the game sat at ~45 FPS with an idle GPU; **45 → 180 FPS** in our test
- ⏱️ **60 FPS limit at any refresh rate** — at 144 / 180 Hz the game used to run too fast
- 🖥️ **Real output resolution** — the picture and the HUD at your monitor's resolution instead of 1280×720, borderless window, no DPI blur
- ✨ **Graphics** — supersampling 1×–3×, horizon-based ambient occlusion (HBAO), SMAA and FXAA, sharpening, wet reflections, 16× anisotropic filtering, sharper shadows, longer draw distance
- 📺 **Xbox 360 look** — the console's colour grading and TV gamma on top of the PC picture
- 🌫️ **No more flickering far textures** — the game's camera wasted depth precision; far roads, roofs and decals no longer fight each other
- 🏎️ **High FPS physics** — 120–180 FPS with the car, doors, animations and timers at their normal speed; the FPS is unlocked by default
- 🧩 **Mod manager** — `UltimateFix\mods\<mod>\`: translations (SWERY Localization Tool files), texture packs, or any game file by its name (`fontwide.xpc`, `mes_all.mes`); order and on/off in F10
- 🎮 **DualSense** — USB and Bluetooth without Steam Input, **adaptive triggers per weapon**, haptics for driving and shooting, PlayStation button icons
- 📳 **Rumble that works** — the PC port calculated vibration and never sent it to the gamepad
- 🔫 **Aim on L2, shoot on R2** — like a modern shooter, on Xbox pads too; prompts switch between keyboard and gamepad by themselves
- 🗺️ **Full pause map** — all 64 tiles, proper zoom, north up
- 📍 **Waypoints** — F7 sets a mark; the distance in the game's own font and the game's own arrows on the minimap (_arrows are experimental_)
- 💾 **Safe saves** — atomic writes, 60 backups named by episode, chapter and play time, automatic restore of a damaged `dp.sav`
- 📖 **Story and residents** — where the story stands and where to go next, side quests, and the timetable of every resident of Greenvale
- 🖱️ **Camera** — field of view in play, Xbox 360 quick turn, mouse sensitivity and inversion per axis
- 📷 **Photo mode** — free camera, frozen time, 13 filters with grain, sepia, aberration and letterbox, your own presets, FOV, costumes, poses, PNG screenshots
- 🧰 **Tools** — frame statistics panel, built-in profiler, cheats page (money, time, weather, ammo, god mode, York flies)
- 🇺🇦🇬🇧 **Two languages** — Ukrainian and English, switched in the menu

### Engine fixes (RE-based)

Every fix below comes from reverse engineering the game and comparing the PC
port with the Xbox 360 original:

- 🕒 **The "only a reboot helps" slowdown** — the game counted time from
  the Windows uptime counter in single-precision x87 floats, so after days
  without a reboot the frame time lost precision. The mod hands the game a
  counter that starts with the game.
- 🎯 **The 45 FPS mystery** — the game asks Windows about a WinMM gamepad on
  every frame, even with none connected, and on some systems that call takes
  milliseconds. Found with the mod's own profiler; the mod now asks twice a
  second.
- 🌗 **Day and night inside buildings** — the port reads the building list
  (`HOUSE_LIST`) with a wrong byte-order key: 64 of its 81 entries were misread, so
  interiors had the wrong lighting.
- 💥 **The crash after cutscenes** — an NPC told to walk to waypoint `-1`
  read the navigation table out of bounds. Guarded at the lookup.
- ➗ **The zero-delta freeze** — a frame with zero time divided by zero and
  spun forever. Guarded.
- 🎚️ **Difficulty** — the PC port skips the difficulty screen and stays on
  Easy forever. Normal is the default again, and the choice is yours.
- 📳 **Vibration** — the same event-to-rumble table as on Xbox 360, but the
  PC port never sends it to the device. Restored.
- 🚧 **Draw distance** — booths and vending machines vanished 500 units
  away because of a far plane the Xbox version does not have.
- 🌫️ **Flickering far away** — the play camera's near plane sits at 1.0, so
  the 24-bit depth buffer had almost no precision left in the distance. The
  mod moves it to 20.0: far surfaces stop fighting each other.
- ⚙️ **Above 120 FPS** — 80 timers of the game turned the frame time into
  whole ticks, so above 120 FPS they stood still or ran double. They now share
  whole steps.
- 🧮 **Angles that are not a number** — the angle normalisation hangs the game
  for good on a NaN (`jmp $`). It now returns 0 and logs the caller.
- 🧵 **Why "Windows 98 compatibility mode" helped** — two worker threads of the
  game share data with the main thread without any lock: the world builder
  works while the next frame already updates, and a level change clears the
  residents while the route thread still writes a path into one of them (memory
  damage, or a resident who waits for a route forever). One CPU core hid both.
  The mod makes them wait for each other instead, at full speed.
- 🕹️ **The QTE at high FPS** — the stick-waggle QTE multiplied the stick travel
  by the frame time, so at 180 FPS every A↔D swap counted a third and the QTE
  ran out of time. A swap now counts as at 60 FPS.
- 🧠 **2 GB of memory** — the game is 32-bit and sees only 2 GB; a big render
  scale runs it out. The mod gives `DP.exe` the 4 GB flag by itself (from the
  next start, the original kept as `DP.exe.bak`).
- 🧍 **Residents walking into nowhere** — a failed route lookup moved the
  resident to (0, 0, 0); now the resident stays where it stood.

### What's new in 0.7.7

- **An Xbox pad without Steam Input.** The game reads pads only through the old Windows joystick interface (WinMM), where an Xbox pad is not listed when Steam Input is off: the pad did nothing at all. The mod then hands the game the pad it reads through XInput, which needs `[Input] XInputFallback=1` (the default; in the F10 menu "Gamepad through XInput (fallback)"). If an older setup has it at 0, switch it on.
- **The log says why a pad is not seen**: the fallback switched off, no XInput library, or what XInput answers for each of its four slots.
- Tested by **EliParker**: an Xbox pad and a DualSense Edge both work, with the automatic icons (PlayStation on the DualSense, Xbox on the Xbox pad). Thank you!

### What's new in 0.7.6

- **PlayStation icons on a DualSense, the Edge included.** With `GlyphTheme=1` (automatic) the icons stayed Xbox for three reasons, all fixed:
  - When the game starts with the pad, it loads its own pad icon atlas (`UPDATA\SOURCE\MESSAGE\BTNWIDE.XPC`) instead of the keyboard one, and the mod looked only for the keyboard atlas, so it changed nothing. The mod now recognises the game's pad atlas by its bytes.
  - A texture pack that replaces the icon atlas hid it from the mod. The atlas is now recognised by the game's original file, before any replacement.
  - A DualSense over Bluetooth is found a few seconds after the start, and the icon set was chosen once, before that. It is now checked on every use.
- **Xbox icons from the mod's drawing again** (`UltimateFix\glyphs\gamepad\xbox.png`), the same look as the PlayStation set. The game's own Xbox atlas is used only when that file is missing.
- Thanks to **EliParker** for the reports and logs with a DualSense Edge.

### What's new in 0.7.5

- **Texture replacements from mods work.** A texture in a mod folder (`UltimateFix\mods\<mod>\textures\<hash>.dds` or `.png`) was found but never loaded (`could not be loaded (88760B59)` in the log): the mod looked for the file in the wrong folder. With a mod that carried textures, the own folders (`UltimateFix\textures\override\`, the DPfix and ZachFix folders) failed the same way. Fixed.

### What's new in 0.7.4

- **York flies** (F10 → Cheats, or F3): WASD relative to the camera, the mouse or Q/E turn, Space up, Ctrl down, Shift four times faster, speed slider on the page. Handy for testing a translation. While he flies the game sees no input, and his physics (ground, fall, collisions) waits; turning it off lands him through the game's own teleport sync. Turn it off near the ground: from a height he falls (invulnerability helps).
- **The lock-on aims at the chest.** The game aims at the enemy's waist and lowers the point by a tenth of the distance, so a few metres away it was the hips or the knees. Now the point stays a little above the waist (`[Fixes] LockOnAim`, `LockOnHeight`).
- **DualSense Edge**: when a Sony pad cannot be opened (another program such as DSX, DS4Windows or HidHide holds or hides it) or reports another model, the log now says so.
- For testers: `[Debug] TraceCamera=1` + Numpad 2 marks the camera at a moment; `[Debug] TraceWater=1` logs how the game draws water and which reflection it renders (Numpad 1 tries other water settings).
- The mod list in the log shows Cyrillic names correctly.

### What's new in 0.7.3

- **A mod describes itself in a plain text file.** Put `mod.txt` (or `info.txt`, or the old `mod.ini`) in the mod folder, and F10 → Mods shows its name, version, author and description:
  ```
  Name: Ukrainian translation
  Author: Little Bit
  Version: 1.0
  Description: The whole game text in Ukrainian
  ```
  Ukrainian keys work too (`Назва`, `Автор`, `Версія`, `Опис`), `:` or `=`, in any encoding Notepad saves (UTF-8, UTF-16, ANSI). A `readme.txt` in a mod folder is no longer taken for a game file.
- For testers: `[Debug] TraceWater=1` logs how the game draws water (for the coming water fix).

### What's new in 0.7.2

- **Updates itself.** At the start the mod asks GitHub once whether a newer release is out (one anonymous request). If so, the start card says so: **F9** downloads it, checks every file, replaces `X3DAudio1_7.dll` and the icons, and restarts the game. `UltimateFix.ini`, mods and save backups stay. If anything fails, everything stays as it was. Off: F10 → "Check for updates" or `[Menu] CheckUpdates=0`.
- **A smaller archive**: only the mod itself (`X3DAudio1_7.dll`, `UltimateFix`, a short readme). The untested DXVK and the separate tools are gone: the mod sets 4 GB and skips the launcher window by itself.
- **The QTE fix can be switched** (F10 → Fixes → "QTE at high FPS").
- The sampling profiler moved to **Ctrl+F9**.

### What's new in 0.7.1

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

### What's new in 0.7.0

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

### Screenshots

<table>
<tr>
<td><img src="docs/screenshots/menu-controls.jpg" alt="Controls page"/></td>
<td><img src="docs/screenshots/menu-world.jpg" alt="Game and world page"/></td>
</tr>
<tr>
<td><img src="docs/screenshots/menu-fixes.jpg" alt="Fixes page"/></td>
<td><img src="docs/screenshots/menu-about.jpg" alt="About page"/></td>
</tr>
</table>

### Standing on the shoulders of

The code is our own (MIT). The ideas, facts and addresses came from earlier community work:

| Project | Author | What we learned from it |
|---|---|---|
| **[ZachFix](https://github.com/h714je/ZachFix)** | h714je | Research of the port, many ideas |
| **[DPfix](https://github.com/PeterTh/dpfix)** | Peter "Durante" Thoman | Resolution, shadows, texture replacement |
| **[DPStabilityFix](https://github.com/Cesario67/DeadlyPremonitionFix)** | Cesario67 | Saves, timing, DualSense layout |
| **[CrashFix](https://github.com/Behelit09/DeadlyPremonition-CrashFix)** · **[MapFix](https://github.com/Behelit09/Deadly-Premonition-The-Director-s-Cut-MapFix)** · **[FreeCam](https://github.com/Behelit09/DeadlyPremonition-FreeCam)** | Behelit09 | Navigation crash, the full map, free camera |
| **[DPRecomp](https://github.com/LittleBitUA/DPRecomp)** | «Little Bit» | Our Xbox 360 recompilation: the reference the PC port is compared with |
| **[Kenney Input Prompts](https://kenney.nl/assets/input-prompts)** | Kenney | Gamepad button icons (CC0) |

🧪 **Testers:** **Gold flin**, **Tree Fan** and **EliParker**. Thank you for playing the unfinished builds and reporting everything that broke.

### 🤖 Who did what: Claude and Dmytro

Someone wrote to us: *"Claude, fix Deadly Premonition, make no mistakes."* So here it is, honestly.

**Claude** (Anthropic's AI model, working in Claude Code):
- wrote the mod's code: the patches and assembly stubs, the F10 menu, the updater, the checks that compare every patch with the real `DP.exe`. Every commit in this repository is co-authored by Claude;
- read the decompiled game code and the bytes of `DP.exe`, looked for the causes of the bugs and the addresses to fix them at, and carried ideas from the projects above over into our own code;
- wrote the README, the release notes and the website, and packed the test builds and the releases;
- **cannot run the game.** Claude has never seen a frame of it live: everything was written blind, from code, logs and screenshots;
- **made mistakes.** A test build with the wrong DLL inside, a key that clashed with another feature, fixes that took several tries. A person caught each of them in game.

**Dmytro Bidlov** («Little Bit»):
- came up with the project and decides what it is: what gets fixed first, what goes in and what is cut, the MIT license, the rule "our own code only", how the menu and the site look;
- played every test build on Windows and sent back logs, screenshots, saves and shader dumps. Without that, nobody would know that any of this works;
- made [DPRecomp](https://github.com/LittleBitUA/DPRecomp), the Xbox 360 recompilation the PC port is compared with, and the SWERY Localization Tool, and is making the Ukrainian translation this mod was built to carry;
- decides on every release; nothing is published without a check.

What we hold to: *"tested in game"* in this README means a person tested it in game. Everything else is marked _experimental_.

### Installation

1. Back up `savedata\dp.sav`
2. Download the latest version from [Releases](https://github.com/LittleBitUA/UltimateFIXDP/releases)
3. Copy `X3DAudio1_7.dll` and the `UltimateFix` folder into the game folder (next to `DP.exe`)
4. Start the game and press **F10**

> **Requirements:** the Steam version 1.01b of the game. Other builds are detected and the mod stays inactive.
> <br>**DualSense:** turn off Steam Input for the game (Properties → Controller).
> <br>**Do not combine** with DPfix, ZachFix or DPStabilityFix: UltimateFIXDP replaces them.
> <br>**Coming from DP1 Launcher?** This is its successor: remove DPfix (`d3d9.dll`, `DPfix.ini`) first.

The release archive holds only the mod: `X3DAudio1_7.dll`, the `UltimateFix` folder and a short `README_UA.txt`. The source code is this repository. Newer versions: the mod asks GitHub at the start and offers **F9** to update itself (`[Menu] CheckUpdates=0` turns that off).

To uninstall, delete `X3DAudio1_7.dll` (and, for the 4 GB flag, put `DP.exe.bak` back as `DP.exe` or let Steam verify the files).

### Keys

| Key | Action |
|---|---|
| **F10** | Settings menu |
| **F7** | Set a waypoint (on the pause map or in the world) · **Shift+F7** removes it · **Ctrl+F7** to the game's objective |
| **Shift+S** | Quick turn (on a pad: the Xbox 360 combination) |
| **F6** | Photo mode |
| **F5** / **F4** | Free camera only / hide the HUD in photo mode |
| **F11** | Screenshot (PNG, without the menu) |
| **F9** | Profiler (writes a profile to the log) |

### If something goes wrong

- `UltimateFix\ufx.log` says for every fix whether it is active or skipped, and why
- Every feature can be switched off in F10 or in `UltimateFix.ini`; `[Debug] SafeMode=1` starts with fewer hooks
- Report problems with `ufx.log` and the `crash_*.dmp` / `hang_*.dmp` file, if there is one
- After an update, an old `UltimateFix.ini` keeps old values (a few old defaults are updated by the mod itself): delete it to get all the new defaults

### Development

```bash
make                                  # build/X3DAudio1_7.dll
make test DPEXE=/path/to/DP.exe       # checks every patch site against a real DP.exe
```

Stack: **freestanding C++17** (mingw-w64 i686, no C runtime; imports only `kernel32`, `user32`, `gdi32`),
a proxy `X3DAudio1_7.dll` that passes audio calls on to the real library and patches the game in memory.
Every patch checks the game's bytes first: if they differ, only that patch switches itself off.

---

## 🇺🇦 Українською

Одна DLL у теці гри замість DPfix, фіксу стабільності, фіксу мапи й фіксу вильотів.
Без інсталятора: натисніть **F10** у грі, і все там. Мод також сам дає грі 4 ГБ пам'яті (з другого запуску).

> **Версія 0.7.7.** Більшість функцій перевірено в грі, решту позначено як _експериментальні_. Спершу зробіть копію `savedata\dp.sav`.
> <br>Нове в 0.7.7: **Xbox-пад без Steam Input**, і журнал каже, чому пад не видно — див. [Що нового](#що-нового-в-077). У 0.7.6: **іконки PlayStation на DualSense (і Edge)**. У 0.7.5: заміни текстур у теках модів працюють. У 0.7.4: **Йорк літає** (читкоди, F3), автоприціл цілиться в груди, а не в стегна. У 0.7.3: назва, автор і версія мода у звичайному `mod.txt`. У 0.7.2: **оновлюється сам** (F9 на стартовій плашці), значно менший архів — див. [Що нового](#що-нового-в-072). У 0.7.1: виправлено **гонки потоків**, через які гра падала й зависала при вході в будівлі, **QTE на високому FPS**, **4 ГБ пам'яті самі собою**, перевірені й стиснені копії збережень, оригінальні іконки Xbox — див. [Що нового](#що-нового-в-071).

### Можливості

- 🛡️ **Стабільність** — виправлено виліт після катсцен, зависання на нульовому кадрі й зависання після Alt+Tab; обробник вильотів і сторож зависань пишуть дамп, якщо щось усе ж трапиться
- 🚀 **Виправлено низький FPS** — на деяких ПК гра трималась на ~45 FPS при вільній відеокарті; у нашому тесті **45 → 180 FPS**
- ⏱️ **Ліміт 60 FPS на будь-якій частоті екрана** — на 144 / 180 Гц гра раніше бігла надто швидко
- 🖥️ **Справжня вихідна роздільність** — картинка й HUD у роздільності монітора, а не 1280×720, вікно без рамки, без розмиття від масштабування Windows
- ✨ **Графіка** — суперсемплінг 1×–3×, затінення по горизонту (HBAO), SMAA і FXAA, різкість, мокрі відбиття, анізотропна фільтрація 16×, чіткіші тіні, більша дальність промальовки
- 📺 **Вигляд Xbox 360** — кольорокорекція й гамма телевізора консолі поверх картинки PC
- 🌫️ **Далекі текстури більше не блимають** — камера гри марнувала точність глибини; далекі дороги, дахи й написи більше не «перебивають» одне одного
- 🏎️ **Фізика на високому FPS** — 120–180 FPS, а машина, двері, анімації й таймери йдуть з нормальною швидкістю; FPS типово розблоковано
- 🧩 **Менеджер модів** — `UltimateFix\mods\<мод>\`: переклади (файли SWERY Localization Tool), текстурні паки або будь-який файл гри за іменем (`fontwide.xpc`, `mes_all.mes`); порядок і вмикання в F10
- 🎮 **DualSense** — USB і Bluetooth без Steam Input, **адаптивні триґери під кожну зброю**, відгук у машині й при пострілах, іконки кнопок PlayStation
- 📳 **Вібрація, яка працює** — PC-порт рахував її, але не надсилав на ґеймпад
- 🔫 **Приціл на L2, постріл на R2** — як у сучасних шутерах, і на Xbox-падах; підказки самі перемикаються між клавіатурою й ґеймпадом
- 🗺️ **Повна мапа паузи** — усі 64 плитки, нормальний масштаб, північ угорі
- 📍 **Мітки** — F7 ставить мітку; відстань ігровим шрифтом і рідні стрілки гри на мінімапі (_стрілки експериментальні_)
- 💾 **Надійні збереження** — атомарний запис, 60 резервних копій із підписом (епізод, розділ, час гри), автоматичне відновлення пошкодженого `dp.sav`
- 📖 **Сюжет і жителі** — де ви в сюжеті й куди йти далі, побічні завдання і розклад кожного жителя Ґрінвейла
- 🖱️ **Камера** — кут огляду в грі, швидкий розворот як на Xbox 360, чутливість миші й інверсія по кожній осі
- 📷 **Фоторежим** — вільна камера, зупинка часу, 13 фільтрів із зерном, сепією, аберацією й кінорамкою, власні пресети, кут огляду, костюми, пози, знімки PNG
- 🧰 **Інструменти** — панель статистики кадру, вбудований профайлер, сторінка читкодів (гроші, час, погода, набої, невразливість, політ Йорка)
- 🇺🇦🇬🇧 **Дві мови** — українська та англійська, перемикаються в меню

### Виправлення рушія (на основі reverse engineering)

Кожне виправлення нижче — результат розбору коду гри й порівняння PC-порту
з оригіналом для Xbox 360:

- 🕒 **Гальмування, яке лікувалось лише перезавантаженням ПК** — гра
  рахувала час від лічильника роботи Windows у x87 float одинарної точності,
  тож після кількох днів без перезавантаження час кадру втрачав точність.
  Мод дає грі лічильник, який починається разом із нею.
- 🎯 **Загадка 45 FPS** — гра щокадру питає Windows про ґеймпад WinMM,
  навіть коли його немає, а на деяких системах цей виклик триває мілісекунди.
  Знайдено вбудованим профайлером мода; тепер мод питає двічі на секунду.
- 🌗 **День і ніч у будівлях** — порт читає список будівель (`HOUSE_LIST`)
  з неправильним ключем порядку байтів: 64 з 81 записів читались хибно, тож
  у приміщеннях було не те освітлення.
- 💥 **Виліт після катсцен** — NPC, якого відправили до точки `-1`, читав
  таблицю навігації поза її межами. Тепер там стоїть перевірка.
- ➗ **Зависання на нульовому кадрі** — кадр із нульовим часом ділив на нуль
  і крутився вічно. Тепер там стоїть перевірка.
- 🎚️ **Складність** — PC-порт пропускає екран вибору складності й назавжди
  лишає легку. Звичайна знову типова, і вибір за вами.
- 📳 **Вібрація** — та сама таблиця «подія → вібрація», що й на Xbox 360,
  але PC-порт не надсилає її на пристрій. Відновлено.
- 🚧 **Дальність промальовки** — будки й автомати зникали за 500 одиниць
  через дальню площину, якої у версії для Xbox немає.
- 🌫️ **Блимання вдалині** — ближня площина камери гри стоїть на 1.0, тож
  24-бітному буферу глибини вдалині майже не лишалось точності. Мод ставить
  20.0: далекі поверхні більше не «перебивають» одна одну.
- ⚙️ **Понад 120 FPS** — 80 таймерів гри перетворювали час кадру на цілі такти,
  тож понад 120 FPS вони стояли або бігли вдвічі швидше. Тепер вони ділять
  цілі кроки.
- 🧮 **Кут, що не є числом** — нормалізація кута назавжди вішала гру на NaN
  (`jmp $`). Тепер вона повертає 0 і пише в журнал, звідки прийшло число.
- 🧵 **Чому допомагав «режим сумісності з Windows 98»** — два фонові потоки гри
  працюють зі спільними даними без жодного замка: будівник світу працює, поки
  оновлюється наступний кадр, а зміна локації прибирає жителів, поки потік
  маршрутів ще дописує шлях одному з них (псування пам'яті або житель, що вічно
  «чекає маршрут»). Одне ядро ховало обидві вади. Мод змушує їх чекати одне
  одного, без втрати швидкості.
- 🕹️ **QTE на високому FPS** — QTE «розхитування» множило рух стіка на час
  кадру, тож на 180 FPS кожне перемикання A↔D рахувалось на третину і час
  вичерпувався. Тепер перемикання рахується, як на 60 FPS.
- 🧠 **2 ГБ пам'яті** — гра 32-бітна і бачить лише 2 ГБ; великий масштаб
  рендеру їх вичерпує. Мод сам ставить `DP.exe` прапор 4 ГБ (з наступного
  запуску, оригінал лишається як `DP.exe.bak`).
- 🧍 **Жителі, що йдуть «в нікуди»** — невдалий пошук маршруту переносив жителя
  в (0, 0, 0); тепер він лишається там, де стояв.

### Що нового в 0.7.7

- **Xbox-пад без Steam Input.** Гра читає пади лише через старий інтерфейс джойстиків Windows (WinMM), а там Xbox-пада з вимкненим Steam Input немає: пад зовсім не працював. Тоді мод передає грі пад, який читає через XInput. Для цього потрібно `[Input] XInputFallback=1` (так за замовчуванням; у меню F10 «Ґеймпад через XInput (запасний)»). Якщо у старих налаштуваннях там 0, увімкніть.
- **Журнал каже, чому пад не видно**: запасний шлях вимкнено, немає бібліотеки XInput, або що XInput відповідає по кожному з чотирьох слотів.
- Перевірив **EliParker**: і Xbox-пад, і DualSense Edge працюють з автоматичними іконками (PlayStation на DualSense, Xbox на Xbox-паді). Дякуємо!

### Що нового в 0.7.6

- **Іконки PlayStation на DualSense, зокрема на Edge.** З `GlyphTheme=1` (автоматично) іконки лишались Xbox з трьох причин, усі виправлено:
  - Якщо гра стартує з ґеймпадом, вона вантажить свій ґеймпадний атлас іконок (`UPDATA\SOURCE\MESSAGE\BTNWIDE.XPC`), а не клавіатурний, а мод шукав лише клавіатурний і нічого не міняв. Тепер мод впізнає атлас гри побайтово.
  - Пак текстур, що замінює атлас іконок, ховав його від мода. Тепер атлас впізнається за оригінальним файлом гри, ще до заміни.
  - DualSense по Bluetooth знаходиться через кілька секунд після запуску, а набір іконок вибирався один раз, раніше. Тепер він перевіряється щоразу.
- **Іконки Xbox знову з малюнка мода** (`UltimateFix\glyphs\gamepad\xbox.png`), в одному стилі з PlayStation. Власний атлас Xbox гри береться лише тоді, коли цього файлу немає.
- Дякуємо **EliParker** за повідомлення й журнали з DualSense Edge.

### Що нового в 0.7.5

- **Заміни текстур із модів працюють.** Текстуру в теці мода (`UltimateFix\mods\<мод>\textures\<хеш>.dds` або `.png`) мод знаходив, але не завантажував (`could not be loaded (88760B59)` у журналі): файл шукався не в тій теці. Якщо був мод із текстурами, так само не працювали й власні теки (`UltimateFix\textures\override\`, теки DPfix і ZachFix). Виправлено.

### Що нового в 0.7.4

- **Йорк літає** (F10 → Читкоди або F3): WASD відносно камери, мишка або Q/E повертають, Space вгору, Ctrl вниз, Shift учетверо швидше, повзунок швидкості на сторінці. Зручно для перевірки перекладу. Поки він летить, гра не бачить вводу, а його фізика (земля, падіння, зіткнення) чекає; після вимкнення гра сама ставить його на землю, як після свого телепорту. Вимикайте біля землі: з висоти він упаде (згодиться невразливість).
- **Автоприціл цілиться в груди.** Гра цілилась у пояс ворога й ще опускала точку на десяту частину відстані, тож за кілька метрів це були стегна чи коліна. Тепер точка трохи вище пояса (`[Fixes] LockOnAim`, `LockOnHeight`).
- **DualSense Edge**: якщо пад Sony не вдається відкрити (його тримає чи ховає інша програма, як-от DSX, DS4Windows, HidHide) або він іншої моделі, тепер це видно в журналі.
- Для тестувальників: `[Debug] TraceCamera=1` + Numpad 2 позначає камеру в потрібну мить; `[Debug] TraceWater=1` пише, як гра малює воду і яке відбиття рендерить (Numpad 1 пробує інші параметри води).
- Список модів у журналі показує кириличні назви правильно.

### Що нового в 0.7.3

- **Мод описує себе у звичайному текстовому файлі.** Покладіть `mod.txt` (або `info.txt`, або старий `mod.ini`) у теку мода, і F10 → Моди покаже його назву, версію, автора й опис:
  ```
  Назва: Українська локалізація
  Автор: Little Bit
  Версія: 1.0
  Опис: Повний переклад тексту гри українською
  ```
  Англійські ключі теж працюють (`Name`, `Author`, `Version`, `Description`), `:` або `=`, у будь-якому кодуванні Блокнота (UTF-8, UTF-16, ANSI). `readme.txt` у теці мода більше не вважається файлом гри.
- Для тестувальників: `[Debug] TraceWater=1` пише в лог, як гра малює воду (для майбутнього виправлення води).

### Що нового в 0.7.2

- **Оновлюється сам.** На старті мод один раз питає GitHub, чи вийшла нова версія (один анонімний запит). Якщо так, про це скаже стартова плашка: **F9** завантажить її, перевірить кожен файл, замінить `X3DAudio1_7.dll` та іконки й перезапустить гру. `UltimateFix.ini`, моди й копії збережень лишаються. Якщо щось не вдасться, усе лишиться як було. Вимкнути: F10 → «Перевіряти оновлення» або `[Menu] CheckUpdates=0`.
- **Менший архів**: лише сам мод (`X3DAudio1_7.dll`, `UltimateFix`, коротка інструкція). Неперевірений DXVK і окремі програми прибрано: мод сам ставить 4 ГБ і сам прибирає вікно лаунчера.
- **Фікс QTE можна вимкнути** (F10 → Виправлення → «QTE на високому FPS»).
- Профайлер переїхав на **Ctrl+F9**.

### Що нового в 0.7.1

- **Стабільність.**
  - Будівник світу й головний потік працюють по черзі, а зміна локації чекає потік маршрутів. Це прибирає вильоти й зависання при вході в будівлі з супутниками: те, що раніше ховав режим сумісності з Windows 98.
  - Жителі без маршруту лишаються на місці, а не зникають у (0, 0, 0).
  - Потік завантаження чекає між запитами 1 мс замість 16: менше ривків, коли місто підвантажується.
  - Під час довгих завантажень немає білого вікна «не відповідає».
  - Раз на хвилину в журналі рядок про пам'ять, а коли її бракує, повідомлення «збережіться й перезапустіть».
- **4 ГБ пам'яті самі собою**: мод ставить `DP.exe` прапор 4 ГБ (діє з наступного запуску), окрема програма не потрібна.
- **QTE «розхитування» знову проходиться на високому FPS** (кожне перемикання рахується, як на 60 FPS).
- **Збереження.**
  - Кожне збереження перевіряється: гроші, місце в сюжеті й час гри мають збігатися в заголовку й записі, інакше мод попереджає.
  - 60 копій, підписаних епізодом, розділом і часом гри, у стисненій теці (близько 1 МБ кожна замість 8).
- **Графіка.**
  - Будь-який фільтр фоторежиму або власний пресет може діяти весь час.
  - Сила світіння (bloom) і поправка експозиції.
  - Вигляд Xbox 360 тримається і в катсценах та сценах без керування.
  - Без подвоєних країв розмиття на масштабі рендеру 300 %.
  - Затемнення, екрани завантаження й сповіщення про предмет накривають екран до останнього пікселя на роздільності понад 720p.
  - Ближня площина камери сама підтягується, коли камера близько: Йорка більше не зрізає біля телефону й у дверях.
- **Оригінальні іконки ґеймпада Xbox** із самої гри (розпаковуються з її `BTNWIDE.XPC`) замість намальованих модом.
- **Знімки**: Shift+F11 без інтерфейсу, у фоторежимі «з кожним фільтром».
- **Миша**: чутливість 25–400 %, що справді повертає швидше, та інверсія по кожній осі.

Відомі проблеми: світла смужка вздовж деяких кутів з HBAO; X на мапі паузи з'їжджає під час прокручування; у кількох рідкісних сценах гра бере інший шейдер, і вигляд Xbox 360 там ще не діє (мод зберігає його як `UltimateFix\composite_*.bin`, щоб додати підтримку).

### Що нового в 0.7.0

- **Менеджер модів** (F10 → Моди): кожна тека в `UltimateFix\mods\` — це мод; вмикайте, вимикайте й міняйте порядок. Мод може нести переклад (`text\`: `mes_all.json` і `glyphmap.json` із SWERY Localization Tool), текстури (`textures\<хеш>.dds`, хеші з `[Debug] DumpTextures=1`) або будь-який файл гри за іменем просто в теці мода (`fontwide.xpc`, `mes_all.mes`, `FONTWIDE.DDS` для картинки всередині XPC). Назва, автор і версія мода — у `mod.txt` (`Назва: …`, `Автор: …`, `Версія: …`, `Опис: …`).
- **Виправлено блимання далеких текстур**: ближня площина камери (20.0 замість 1.0), міпмапи для текстур без них, стабільніші далекі тіні.
- **HBAO**: затінення по горизонту з розмиттям з урахуванням глибини, на великому масштабі рендеру рахується до 1080 рядків; **SMAA**; **вигляд Xbox 360**.
- **Високий FPS**: фізика й решта таймерів гри на 120–180 FPS; FPS типово розблоковано, ліміт 60 лишається на екранах 61–139 Гц.
- **Камера**: кут огляду в грі, швидкий розворот як на Xbox 360, чутливість миші (25–400 %) та інверсія по кожній осі.
- **Сторінка «Сюжет»** (епізод, розділ, побічні завдання, куди йти далі — назвою місця) і **сторінка «Жителі»** (розклад кожного жителя, кімнати й дії людськими словами).
- **Мітка** як рідна ціль гри: її маяк над місцем, стрілки на краю мінімапи; показується лише у своїй локації.
- **Фоторежим**: 13 фільтрів, температура, відтінок, сепія, вицвітання, зерно, хроматична аберація, кінорамка, чотири власні пресети.
- **Стабільність**: немає вічного зависання на куті, що не є числом; XAudio2 тримається в пам'яті; точність x87 як у Direct3D 9; зрозуміле повідомлення, якщо графічний пристрій не створюється.
- **Збереження**: 60 резервних копій, кожна підписана епізодом, розділом і часом гри.
- Картка на старті про **F10**; коротші підписи в меню; повзунки дальності зупиняються там, де більше нічого не дає.

Відомі проблеми 0.7.0 — що виправлено, див. 0.7.1 вище.

### На чиїх плечах стоїмо

Код наш власний (MIT). Ідеї, факти й адреси взято з попередніх робіт спільноти:

| Проєкт | Автор | Чого ми з нього навчились |
|---|---|---|
| **[ZachFix](https://github.com/h714je/ZachFix)** | h714je | Дослідження порту, багато ідей |
| **[DPfix](https://github.com/PeterTh/dpfix)** | Peter «Durante» Thoman | Роздільність, тіні, підміна текстур |
| **[DPStabilityFix](https://github.com/Cesario67/DeadlyPremonitionFix)** | Cesario67 | Збереження, час, розкладка DualSense |
| **[CrashFix](https://github.com/Behelit09/DeadlyPremonition-CrashFix)** · **[MapFix](https://github.com/Behelit09/Deadly-Premonition-The-Director-s-Cut-MapFix)** · **[FreeCam](https://github.com/Behelit09/DeadlyPremonition-FreeCam)** | Behelit09 | Виліт навігації, повна мапа, вільна камера |
| **[DPRecomp](https://github.com/LittleBitUA/DPRecomp)** | «Little Bit» | Наша рекомпіляція версії для Xbox 360: еталон, з яким порівнюємо PC-порт |
| **[Kenney Input Prompts](https://kenney.nl/assets/input-prompts)** | Kenney | Іконки кнопок ґеймпада (CC0) |

🧪 **Тестувальники:** **Gold flin**, **Tree Fan** і **EliParker**. Дякуємо, що грали в недороблені збірки й повідомляли про все, що ламалось.

### 🤖 Хто що робив: Claude і Dmytro

Нам написали: *«Claude, fix Deadly Premonition, make no mistakes»*. Тож чесно.

**Claude** (ШІ-модель Anthropic, працює в Claude Code):
- написав код мода: правки й асемблерні вставки, меню F10, оновлювач, перевірки, які звіряють кожну правку зі справжнім `DP.exe`. Кожен коміт у цьому репозиторії має Claude як співавтора;
- читав декомпільований код гри й байти `DP.exe`, шукав причини багів і адреси, де їх виправити, переносив ідеї з проєктів вище у власний код;
- написав README, описи релізів і сайт, пакував тестові збірки й релізи;
- **не може запустити гру.** Claude жодного разу не бачив її наживо: усе написано наосліп, з коду, логів і скриншотів;
- **помилявся.** Тестова збірка з не тим DLL усередині, клавіша, що заважала іншій функції, виправлення, які вдавалися не з першого разу. Кожну таку помилку в грі знайшла людина.

**Dmytro Bidlov** («Little Bit»):
- придумав проєкт і вирішує, яким він буде: що виправляти першим, що додати, а що прибрати, ліцензія MIT, правило «лише власний код», вигляд меню й сайту;
- пройшов кожну тестову збірку на Windows і надсилав логи, скриншоти, збереження й дампи шейдерів. Без цього ніхто б не знав, що хоч щось тут працює;
- зробив [DPRecomp](https://github.com/LittleBitUA/DPRecomp), рекомпіляцію версії для Xbox 360, з якою порівнюємо PC-порт, і SWERY Localization Tool, і робить український переклад, заради якого цей мод і з'явився;
- вирішує щодо кожного релізу: без перевірки нічого не виходить.

Чого ми тримаємось: *«перевірено в грі»* у цьому README означає, що це перевірила в грі людина. Решту позначено як _експериментальне_.

### Встановлення

1. Зробіть копію `savedata\dp.sav`
2. Завантажте останню версію з [Releases](https://github.com/LittleBitUA/UltimateFIXDP/releases)
3. Скопіюйте `X3DAudio1_7.dll` і теку `UltimateFix` у теку гри (поруч із `DP.exe`)
4. Запустіть гру й натисніть **F10**

> **Вимоги:** Steam-версія гри 1.01b. Інші збірки мод розпізнає і не вмикається.
> <br>**DualSense:** вимкніть Steam Input для гри (Властивості → Контролер).
> <br>**Не поєднуйте** з DPfix, ZachFix і DPStabilityFix: UltimateFIXDP їх замінює.
> <br>**Користувались DP1 Launcher?** Це його наступник: спершу приберіть DPfix (`d3d9.dll`, `DPfix.ini`).

В архіві релізу лише мод: `X3DAudio1_7.dll`, тека `UltimateFix` і коротка `README_UA.txt`. Вихідний код — у цьому репозиторії. Нові версії: на старті мод питає GitHub і пропонує **F9**, щоб оновитися самому (`[Menu] CheckUpdates=0` вимикає це).

Щоб видалити мод, видаліть `X3DAudio1_7.dll` (а щоб прибрати прапор 4 ГБ, поверніть `DP.exe.bak` як `DP.exe` або перевірте файли в Steam).

### Клавіші

| Клавіша | Дія |
|---|---|
| **F10** | Меню налаштувань |
| **F7** | Поставити мітку (на мапі паузи або у світі) · **Shift+F7** прибирає · **Ctrl+F7** на ціль гри |
| **Shift+S** | Швидкий розворот (на ґеймпаді: комбінація Xbox 360) |
| **F6** | Фоторежим |
| **F5** / **F4** | Лише вільна камера / сховати HUD у фоторежимі |
| **F11** | Знімок екрана (PNG, без меню) |
| **F9** | Профайлер (пише профіль у журнал) |

### Якщо щось не так

- `UltimateFix\ufx.log` пише про кожне виправлення, чи воно ввімкнене, чи пропущене, і чому
- Будь-яку функцію можна вимкнути в F10 або в `UltimateFix.ini`; `[Debug] SafeMode=1` запускає мод із меншою кількістю хуків
- Про проблеми пишіть із файлом `ufx.log` і файлом `crash_*.dmp` / `hang_*.dmp`, якщо він є
- Після оновлення старий `UltimateFix.ini` зберігає старі значення (кілька старих типових мод оновлює сам): видаліть його, щоб отримати всі нові типові

### Розробка

```bash
make                                  # build/X3DAudio1_7.dll
make test DPEXE=/шлях/до/DP.exe       # звіряє кожне місце правки зі справжнім DP.exe
```

Стек: **C++17 без C runtime** (mingw-w64 i686; імпорти лише `kernel32`, `user32`, `gdi32`),
підмінний `X3DAudio1_7.dll`, який передає звукові виклики справжній бібліотеці й виправляє гру в пам'яті.
Кожна правка спершу звіряє байти гри: якщо вони інші, вимикається лише ця правка.

---

## ⚖️ Disclaimer

Цей проєкт не афілійований із Access Games / Rising Star Games / Toybox Inc.
Усі права на гру належать правовласникам. Проєкт не містить файлів гри.
Це фанатська ініціатива, безкоштовно і з відкритою душею.

This project is not affiliated with Access Games / Rising Star Games / Toybox Inc.
All rights to the game belong to their respective owners. The project ships no game files.
This is a fan-made, free, open initiative.

---

<div align="center">

Створено **Dmytro Bidlov** з любов'ю від [«Little Bit»](https://t.me/LittleBitUA) 🇺🇦
&nbsp;·&nbsp;
Made by **Dmytro Bidlov** with love from [«Little Bit»](https://t.me/LittleBitUA) 🇺🇦

Спасибі Swery (Hidetaka Suehiro) за безсмертну, абсурдну й щемливу гру. ✦

</div>
