# UltimateFIXDP

**[English](#english) · [Українська](#українська)**

A fix and quality-of-life mod for **Deadly Premonition: The Director's Cut** (PC, Steam).
One DLL, no exe patching, settings menu in game. MIT licensed.

> **Status: public testing.** Many features have been tested in game, some are still experimental (marked below).
> Back up your save before you start.

---

## English

### What it is

UltimateFIXDP is a proxy `X3DAudio1_7.dll`. The game loads it like the real audio library; the mod passes every audio call through to the real one, patches the game in memory, and leaves the files on disk alone.

Every patch first checks the game's code byte by byte. If something does not match (another build, or another mod already patched that spot), only that patch switches itself off and the log says why. The rest of the mod keeps working, and the game is never left half-patched.

**Supported:** Steam version 1.01b only (`DP.exe` with PE timestamp `0x529721DC`). Other builds are detected and the mod stays inactive.

### Features

#### Stability and fixes
- **The "only a PC reboot helps" slowdown is fixed.** The game's timer lost precision after long Windows uptime (x87 float time), so the game ran on a counter that grew with uptime. The mod hands it a counter that starts with the game.
- **Low FPS on some systems.** The game asked Windows about a WinMM gamepad on every frame, even with none connected, and on some PCs that call is very slow (45 FPS with the GPU at 4%). The mod now asks twice a second. In our test it went from 45 to 180 FPS.
- **Crashes**: the NPC-after-cutscene crash, the freeze on a zero frame time (division by zero), the hang after Alt+Tab in windowed mode, and others. A crash handler and a freeze watchdog write a dump and a log line if anything still goes wrong.
- **Day and night inside buildings.** The port read the building list (`HOUSE_LIST`) with a wrong key, so most interiors had the wrong lighting.
- **Safer saves**: the game wipes `dp.sav` before writing it. The mod writes to a temporary file and replaces the save in one step, keeps backups, and restores the last good copy if a save is damaged.
- **60 FPS limit that works at any refresh rate.** At 144/180 Hz the game used to run too fast.
- **Normal difficulty by default** (the PC port forced Easy).
- **No launcher window**: the game starts straight away. Hold Shift to get the old launcher back once.

#### Display and graphics
- **Borderless window** on the whole monitor, aspect-correct scaling, no blur on 125%/150% DPI screens.
- **Real output resolution**: the back buffer and the HUD are drawn at your monitor's resolution instead of 1280×720, so text and the HUD are sharp.
- **Supersampling** of the 3D scene (1×, 1.5×, 2×, 3×), sharper shadows.
- **SSAO**, **FXAA**, **sharpening**, 16× anisotropic filtering, **wet reflections**.
- **Longer draw distance** for objects that popped in at short range on PC.
- **Optional DXVK (Vulkan)** scripts in the release (**experimental, not tested yet**).

#### DualSense and gamepads
- **DualSense over USB or Bluetooth, read directly**: no Steam Input needed.
- **Adaptive triggers per weapon**: pistol, magnum, shotgun, SMG, rifle and rocket launcher all feel different. Turned off while driving.
- **Rumble works.** The PC port calculated vibration and never sent it anywhere. Extra haptics: engine hum while driving, a kick on every shot.
- **Aim on L2, shoot on R2**, like a modern shooter (Xbox pads too).
- **PlayStation button icons** in game, automatic switching between keyboard and pad prompts.

#### Map and navigation
- **Full pause map**: all 64 tiles, proper zoom, north up (optional).
- **Waypoint**: F7 on the pause map sets it at the map centre, F7 in the world sets it where you stand. A distance label in the game's own font shows the way. In 0.6.5+ the waypoint also becomes an objective point of the game's own map, so the game draws its own arrows on the minimap edge (**experimental**).

#### Photo mode and extras (**experimental**)
- Free camera, frozen time, filters, field of view, costumes, poses, HUD toggle, PNG screenshots.
- Cheats page: money, time of day, weather, ammo, god mode.
- Tools: a frame statistics panel, a built-in sampling profiler, a freeze watchdog.

### Installation

1. **Back up** `savedata\dp.sav`.
2. Download the release archive and copy `X3DAudio1_7.dll` and the `UltimateFix` folder into the game folder (next to `DP.exe`).
3. Start the game. Press **F10** for the settings menu (English or Ukrainian). It can be moved and resized.
4. Settings are saved to `UltimateFix.ini`, and the log goes to `UltimateFix\ufx.log`.

Optional extras in the release:
- `ufx_laa.exe` lets `DP.exe` use 4 GB of memory (useful with high supersampling). It keeps a `DP.exe.bak`, and `ufx_laa.exe --undo` reverts it.
- `ufx_play.exe` is the quick start without the launcher. The mod installs it by itself; `ufx_play.exe --undo` reverts it.
- `dxvk\Install_DXVK.bat` / `Remove_DXVK.bat` turn DXVK on and off (experimental).

**For DualSense:** turn off Steam Input for the game (Properties → Controller).

**Uninstall:** delete `X3DAudio1_7.dll` (and `UltimateFix\` if you like).

**Do not combine with** ZachFix or DPStabilityFix: they patch the same places, and DPStabilityFix uses the same DLL name. UltimateFIXDP covers most of what they do.

### Keys

| Key | Action |
|---|---|
| F10 | Settings menu |
| F7 | Set the waypoint (on the pause map or in the world) · Shift+F7 removes it |
| F6 | Photo mode (camera, frozen time, panel) |
| F5 / F4 | Free camera only / hide the HUD in photo mode |
| F11 | Screenshot (PNG, without the menu) |
| F9 | Profiler (writes a profile to the log) |
| DualSense touchpad click | Same as F7 |

### If something goes wrong

- Look in `UltimateFix\ufx.log`: every patch says whether it is active or skipped, and why.
- Every feature can be switched off in F10 or in `UltimateFix.ini`.
- `[Debug] SafeMode=1` starts the mod with fewer hooks.
- When reporting a problem, attach `ufx.log` and, if there is one, the `crash_*.dmp` / `hang_*.dmp` file.
- An old `UltimateFix.ini` keeps old values. If defaults look wrong after an update, delete it and the mod writes a fresh one.

### Building from source

Built with mingw-w64 (i686), freestanding C++17 without the C runtime. The DLL imports only `kernel32`, `user32` and `gdi32`.

```
make                                  # build/X3DAudio1_7.dll (needs i686-w64-mingw32-g++ and xxd)
make test DPEXE=/path/to/DP.exe       # checks every patch site and stub against a real DP.exe
```

Layout: `src/` the mod (`modules/` one file per feature, `sites.h` every patched address with its expected bytes, `ui/` the F10 menu), `tests/` host tests, `tools/` the launcher stub, the LAA patcher and save/archive helpers.

### Credits

The code is our own. Ideas and research came from earlier community work: **ZachFix**, **DPfix**, **DPStabilityFix**, **CrashFix**, **MapFix** and **FreeCam**. Thank you to their authors.
The gamepad icons come from [Kenney Input Prompts](https://kenney.nl/assets/input-prompts) (CC0). DXVK is bundled as an optional extra under its own license (`dxvk/LICENSE-DXVK.txt`).

Deadly Premonition belongs to its owners. This project ships no game files.

### License

MIT, see [LICENSE](LICENSE).

---

## Українська

### Що це

UltimateFIXDP — мод із виправленнями та зручностями для **Deadly Premonition: The Director's Cut** (PC, Steam). Це підмінний `X3DAudio1_7.dll`. Гра вантажить його як справжню бібліотеку звуку, а мод передає всі звукові виклики справжній бібліотеці, виправляє гру в пам'яті і не змінює жодних файлів на диску.

Кожна правка спершу звіряє код гри байт у байт. Якщо щось не збігається (інша збірка або інший мод уже змінив це місце), вимикається лише ця правка, а в журналі написано чому. Решта мода працює далі, і гра ніколи не лишається наполовину виправленою.

**Підтримується** лише Steam-версія 1.01b (`DP.exe` з міткою часу PE `0x529721DC`). Інші збірки мод розпізнає і не вмикається.

### Можливості

#### Стабільність і виправлення
- **Виправлено гальмування, яке лікувалось лише перезавантаженням ПК.** Таймер гри втрачав точність, коли Windows довго працює (час у x87 float), бо гра рахувала час від лічильника, що росте з часом роботи системи. Мод дає їй лічильник, який починається разом із грою.
- **Низький FPS на деяких системах.** Гра щокадру питала Windows про ґеймпад WinMM, навіть коли його немає, а на деяких ПК цей виклик дуже повільний (45 FPS при завантаженні відеокарти 4%). Тепер мод питає двічі на секунду. У нашому тесті FPS зріс з 45 до 180.
- **Вильоти**: виліт NPC після катсцени, зависання на нульовому кроці кадру (ділення на нуль), зависання після Alt+Tab у вікні та інші. Обробник вильотів і сторож зависань пишуть дамп і рядок у журнал, якщо щось усе ж трапиться.
- **День і ніч у будівлях.** PC-порт читав список будівель (`HOUSE_LIST`) з неправильним ключем, тож у більшості приміщень було не те освітлення.
- **Надійніші збереження**: гра стирає `dp.sav` перед записом. Мод пише в тимчасовий файл і замінює сейв одним кроком, тримає резервні копії, а пошкоджений сейв відновлює з останньої доброї.
- **Ліміт 60 FPS на будь-якій частоті екрана.** На 144/180 Гц гра раніше бігла надто швидко.
- **Звичайна складність за замовчуванням** (PC-порт примусово ставив легку).
- **Без вікна лаунчера**: гра стартує одразу. Утримуйте Shift, щоб один раз відкрити старий лаунчер.

#### Екран і графіка
- **Вікно без рамки** на весь монітор, масштабування зі збереженням пропорцій, без розмиття на екранах 125%/150%.
- **Справжня вихідна роздільність**: задній буфер і HUD малюються в роздільності монітора, а не 1280×720, тож текст та інтерфейс чіткі.
- **Суперсемплінг** 3D-сцени (1×, 1,5×, 2×, 3×), чіткіші тіні.
- **SSAO**, **FXAA**, **різкість**, анізотропна фільтрація 16×, **мокрі відбиття**.
- **Більша дальність промальовки** для об'єктів, що на PC з'являлись надто близько.
- **Необов'язкові скрипти DXVK (Vulkan)** у релізі (**експериментально, ще не перевірено**).

#### DualSense і ґеймпади
- **DualSense напряму через USB або Bluetooth**: Steam Input не потрібен.
- **Адаптивні триґери під кожну зброю**: пістолет, магнум, дробовик, автомат, гвинтівка й гранатомет відчуваються по-різному. У машині вимикаються.
- **Вібрація працює.** PC-порт рахував її, але нікуди не надсилав. Додатковий відгук: гул двигуна в машині, поштовх на кожен постріл.
- **Приціл на L2, постріл на R2**, як у сучасних шутерах (і для Xbox-падів).
- **Іконки кнопок PlayStation** у грі, автоматичне перемикання підказок між клавіатурою й ґеймпадом.

#### Мапа й навігація
- **Повна мапа паузи**: усі 64 плитки, нормальний масштаб, північ угорі (за бажанням).
- **Мітка**: F7 на мапі паузи ставить її в центрі мапи, F7 у світі — там, де ви стоїте. Шлях показує напис із відстанню ігровим шрифтом. З версії 0.6.5 мітка ще й стає точкою цілі на рідній мапі гри, тож гра сама малює свої стрілки на краю мінімапи (**експериментально**).

#### Фоторежим і додатки (**експериментально**)
- Вільна камера, зупинка часу, фільтри, кут огляду, костюми, пози, приховування HUD, знімки PNG.
- Сторінка читкодів: гроші, час доби, погода, набої, невразливість.
- Інструменти: панель статистики кадру, вбудований профайлер, сторож зависань.

### Установка

1. **Зробіть копію** `savedata\dp.sav`.
2. Завантажте архів релізу і скопіюйте `X3DAudio1_7.dll` і теку `UltimateFix` у теку гри (поруч із `DP.exe`).
3. Запустіть гру. **F10** відкриває меню налаштувань (українською або англійською). Меню можна переносити й розтягувати.
4. Налаштування зберігаються в `UltimateFix.ini`, журнал пишеться в `UltimateFix\ufx.log`.

Необов'язкові додатки в релізі:
- `ufx_laa.exe` дозволяє `DP.exe` використовувати 4 ГБ пам'яті (корисно з великим суперсемплінгом). Він робить копію `DP.exe.bak`, а `ufx_laa.exe --undo` повертає як було.
- `ufx_play.exe` — швидкий старт без лаунчера. Мод ставить його сам; `ufx_play.exe --undo` повертає як було.
- `dxvk\Install_DXVK.bat` / `Remove_DXVK.bat` вмикають і вимикають DXVK (експериментально).

**Для DualSense:** вимкніть Steam Input для гри (Властивості → Контролер).

**Видалення:** видаліть `X3DAudio1_7.dll` (і за бажанням `UltimateFix\`).

**Не поєднуйте з** ZachFix і DPStabilityFix: вони змінюють ті самі місця, а DPStabilityFix ще й має ту саму назву DLL. UltimateFIXDP робить більшість того, що й вони.

### Клавіші

| Клавіша | Дія |
|---|---|
| F10 | Меню налаштувань |
| F7 | Поставити мітку (на мапі паузи або у світі) · Shift+F7 прибирає |
| F6 | Фоторежим (камера, зупинка часу, панель) |
| F5 / F4 | Лише вільна камера / сховати HUD у фоторежимі |
| F11 | Знімок екрана (PNG, без меню) |
| F9 | Профайлер (пише профіль у журнал) |
| Клік по тачпаду DualSense | Те саме, що F7 |

### Якщо щось не так

- Загляньте в `UltimateFix\ufx.log`: кожна правка пише, чи вона ввімкнена, чи пропущена, і чому.
- Будь-яку функцію можна вимкнути в F10 або в `UltimateFix.ini`.
- `[Debug] SafeMode=1` запускає мод із меншою кількістю хуків.
- Коли повідомляєте про проблему, додайте `ufx.log` і, якщо є, файл `crash_*.dmp` / `hang_*.dmp`.
- Старий `UltimateFix.ini` зберігає старі значення. Якщо після оновлення типові налаштування виглядають не так, видаліть його, і мод створить новий.

### Збірка з коду

Збирається mingw-w64 (i686), C++17 без C runtime. DLL імпортує лише `kernel32`, `user32` і `gdi32`.

```
make                                  # build/X3DAudio1_7.dll (потрібні i686-w64-mingw32-g++ і xxd)
make test DPEXE=/шлях/до/DP.exe       # звіряє кожне місце правки й кожну заглушку зі справжнім DP.exe
```

Структура:
- `src/` — сам мод: `modules/` по файлу на функцію, `sites.h` з кожною адресою правки та очікуваними байтами, `ui/` з меню F10;
- `tests/` — тести на хості;
- `tools/` — заглушка лаунчера, патчер LAA, помічники для сейвів і архівів.

### Подяки

Код наш власний. Ідеї та дослідження взято з попередніх робіт спільноти: **ZachFix**, **DPfix**, **DPStabilityFix**, **CrashFix**, **MapFix** і **FreeCam**. Дякуємо їхнім авторам.
Іконки ґеймпадів — [Kenney Input Prompts](https://kenney.nl/assets/input-prompts) (CC0). DXVK додано як необов'язковий додаток під його власною ліцензією (`dxvk/LICENSE-DXVK.txt`).

Deadly Premonition належить своїм правовласникам. Проєкт не містить файлів гри.

### Ліцензія

MIT, див. [LICENSE](LICENSE).
