# Text overlay and translator tools

*Українською: [нижче](#українською).*

Four settings in the `[Language]` section of `UltimateFix.ini` (also in the F10 menu, page "Fixes", group "Translator tools").
All of them need a restart of the game.

| Setting | Default | What it does |
|---|---|---|
| `TextOverlay` | 1 | Lines `0x1234 = text` from `UltimateFix\text\*.txt` replace those records of `MES_ALL.MES` in memory. Nothing happens while the folder has no `.txt` file. |
| `ShowMessageId` | 0 | The id of every text the game draws is printed over it (`0x1A2B`): the number to write in a text file. |
| `GlyphLog` | 0 | `UltimateFix\loc.log` lists the messages that use a font cell that is empty or outside the font atlas, and the draws that overflow the game's glyph batch. |
| `TextDump` | 0 | The `MES_ALL.MES` the game loaded is written to `UltimateFix\mes_dump.txt` in the format of the text files. |

**Status: built and checked on the host only.** The encoder is tested against the real `MES_ALL.MES` files of the game (every
record of the US, EN, FR and GE files turns into text and back into the same words), and every patched address is checked
against `DP.exe` 1.01b. None of it has been run in the game yet.

## Quick start

1. Set `TextDump=1`, start the game and wait for the main menu (if the file is not there yet, load a save), then quit.
   `UltimateFix\mes_dump.txt` now holds every record of the text file the game uses (your translated one, if you have one
   installed). Set `TextDump=0` again.
2. Create the folder `UltimateFix\text` next to `DP.exe`.
3. If your font atlas has characters the stock US atlas does not have (Cyrillic), copy `docs\glyphmap.example.txt` of the mod to
   `UltimateFix\text\glyphmap.txt` and add your characters (see "The glyph map").
4. Copy the lines you want to change from `mes_dump.txt` to a new file in `UltimateFix\text`, for example `fixes.txt`,
   and edit them. Save as **UTF-8**.
5. Start the game. `UltimateFix\loc.log` says how many records were replaced and which lines were refused and why.

To find the id of a text you see in the game, set `ShowMessageId=1`.

## The text files

- Any number of `*.txt` files in `UltimateFix\text` (the name `glyphmap.txt` is reserved for the glyph map). They are read in
  alphabetical order; when the same id appears twice, the later line wins (`loc.log` says so).
- UTF-8, with or without a byte order mark. A file saved as UTF-16 ("Unicode" in old Notepad) is refused.
- One record per line: `0x1234 = text`. The id is hexadecimal and starts with `0x`. Blanks around the text are dropped.
- A line that starts with `;` is a comment. Empty lines are fine.
- Only records that exist in the loaded `MES_ALL.MES` can be replaced; ids cannot be added or renumbered.

### Tags

| Tag | Meaning | Game code |
|---|---|---|
| `{n}` | new line | `FFFE` |
| `{page}` or `{page 120}` | the page ends here; the number is the time of the **next** page in frames (60 a second) | `FFF2 t` |
| `{color R G B}` … `{/color}` | text colour, each value 0..255 (`{colour}` works too, commas are allowed) | `FFFA r g b`, `FFF9` |
| `{size 48}` … `{/size}` | glyph size in pixels, 8..128 (the normal size is 32; the game's own text uses 32, 48 and 64) | `FFF1 s`, `FFF0` |
| `{icon 3}` | a button icon. Only numbers that the loaded `MES_ALL.MES` itself uses are accepted | `FFF4 i` |
| `{sp}` | a space. Needed only for a space at the very start or the very end of the text (blanks there are dropped otherwise) | cell of the space |
| `{#1A3}` | the atlas cell `1A3` (hexadecimal) directly, for a glyph the glyph map has no character for | the cell |
| `{FFF5}` | the marker of a record without a window background. It only works as the very first thing of the text | `FFF5` |
| `{{` and `}}` | the characters `{` and `}` | their cells |
| `**` | written as it is: the place where the game puts a number or an item name | two cells `0A` |

Tag names are not case sensitive. Every other character is looked up in the glyph map.

### What the mod works out by itself

- the width word `N` of every line (a cell below `0xE2` counts 1, from `0xE2` on 2, an icon 2);
- the line count of the record;
- the three header words of the record (speaker / window style, flags, time of the first page) are **kept from the record that
  is replaced**; they cannot be changed from a text file;
- `{page}` without a number takes the time the original record has for that page (the first `{page}` the first time, and so
  on). A record without the timer flag gets 0, as the game's own data has it. If a timed record gets more pages than the
  original has, write the time yourself: `{page 120}`;
- in windows with two-line pages (all speaker styles except `0x38`, `0x3E`, `0x3F`), a `{page}` after a single line adds the
  empty second line the game's own records have there. `Text{page}More` and `Text{n}{page}More` give the same record.

### Pages

A dialogue window shows two lines per page and turns the page every two lines, whatever the text says. `{page}` does not
start a page by itself: it ends a line and carries the time of the page that starts with the next line. So:

- put `{page}` after every two lines (or after one line, the mod fills the second);
- a `{page}` after 3, 5, 7… lines is refused;
- voiced lines are timed to the voice. When you split a text differently, the times of the original pages no longer fit
  exactly; nothing breaks, the page just turns a little early or late.

Records of the styles `0x38`, `0x3E` and `0x3F` (system windows, three lines per page) get their `{page}` exactly where you
write it, with no filling and no check: the game's own records are not regular there either.

## The glyph map

The game has no character encoding. A text word is the number of a cell in the font atlas `FONTWIDE` (64 columns × 38 rows of
32×32 pixels; cell = row × 64 + column, counted from 0). `UltimateFix\text\glyphmap.txt` tells the mod which cell each
character is:

```
; comment
Б = 0xB2
б = 0xC0
U+0404 = 0x8A
```

- the character itself or `U+XXXX`; the cell as `0x..` or a decimal number, `0..0x97F`;
- built in (no file needed): the cells of the stock US atlas, that is ASCII and Latin-1. `docs\glyphmap.example.txt` lists
  them all;
- lines of the file win over the built-in table. Several characters may share one cell (a Cyrillic `О` drawn as the Latin `O`:
  `О = 0x2F`);
- cells below `0xE2` are narrow: the game draws the left half of the cell and advances by half a cell. Cells from `0xE2`
  on are wide. This threshold is in the game's code and the mod does not change it;
- the mod does not know your atlas. If the map names the wrong cell, the wrong glyph is drawn. `GlyphLog=1` reports cells
  that are empty.

In `mes_dump.txt`, a cell is shown as the character the map has for it. A translated file whose Cyrillic letters sit in
Latin-1 cells therefore comes out as Latin-1 letters until `glyphmap.txt` names them; cells with no character come out as
`{#XXX}`.

## loc.log

`UltimateFix\loc.log` is rewritten at every start. Examples:

```
text: fixes.txt line 12, id 0x0017, column 9 of the text: the character U+0416 has no cell in the glyph map (add it to text\glyphmap.txt); line skipped
text: fixes.txt line 20, id 0x0020, column 31 of the text: {page} comes after 3 lines, a page of this window has two; line skipped
text: 2 records replaced from 1 files, 2 lines refused
glyph: message 0x42EF: cell 0x127 (row 4, column 39) is empty in the font atlas
glyph: message 0x1A2B: cell 0x9C0 is beyond the font atlas (its cells are 0x000..0x97F)
batch: message 0x0F21: more than 768 glyph quads in one draw, the rest is not drawn (a glyph of any colour but black takes two)
```

A refused line never reaches the game: the record keeps its original text.

## What is refused, and limits

- a character with no cell in the glyph map, text that is not UTF-8, unknown tags, a `}` alone;
- numbers out of range (colour above 255, size outside 8..128, a page time of `0xFFEF` or more, a cell beyond `0x97F`);
- an icon number the loaded file never uses;
- a line of more than 384 glyphs (one draw holds 768 quads and a coloured glyph takes two);
- a record of more than 4096 words;
- the records `0x3B70` and `0x3B73` ("… has been moved to your toolbox") longer than they are in the loaded file: the game
  copies them into a fixed buffer together with an item name and does not check the length;
- an id that has no record, or a record that contains a control code this version does not handle (`FFF6` and the unused
  codes; the western files have none).

## Good to know

- The game's files are not changed. Remove the text files and the original text is back.
- The overlay works on top of whatever `MES_ALL.MES` the game loads, so it can carry corrections for an installed
  translation.
- Numbers, money, `%d Yards`, the credits and text drawn inside textures are not in `MES_ALL.MES`; the overlay does not
  reach them.
- `ShowMessageId` draws its labels with the game's own small digits (cells of `0`–`9`, `A`–`F`, `x`), so the atlas must
  still have them.
- The glyph log reads the atlas once, when the first frame with the text resources is shown. It understands DXT1, DXT3,
  DXT5 and 32-bit textures and treats black (or transparent) cells as empty.
- With `[Debug] SafeMode=1` all four tools are off.

---

## Українською

Чотири налаштування в розділі `[Language]` файла `UltimateFix.ini` (і в меню F10, сторінка «Виправлення», група «Інструменти
перекладача»). Усі потребують перезапуску гри.

| Налаштування | Типово | Що робить |
|---|---|---|
| `TextOverlay` | 1 | Рядки `0x1234 = текст` із файлів `UltimateFix\text\*.txt` замінюють відповідні записи `MES_ALL.MES` у пам'яті. Поки в теці немає жодного `.txt`, нічого не відбувається. |
| `ShowMessageId` | 0 | Над кожним текстом гри пишеться його номер (`0x1A2B`): саме його пишуть у текстовому файлі. |
| `GlyphLog` | 0 | У `UltimateFix\loc.log` потрапляють повідомлення, які беруть порожню клітинку шрифту або клітинку за межами атласу, і ті, що не вмістилися в один прохід малювання. |
| `TextDump` | 0 | `MES_ALL.MES`, який завантажила гра, записується в `UltimateFix\mes_dump.txt` у вигляді текстових файлів накладки. |

**Стан: зібрано й перевірено лише на комп'ютері розробки.** Кодувальник перевірено на справжніх `MES_ALL.MES` гри (кожен запис
US, EN, FR і GE перетворюється на текст і назад у ті самі слова), усі адреси звірено з `DP.exe` 1.01b. У самій грі це ще не
запускали.

### Як почати

1. Поставте `TextDump=1`, запустіть гру й дочекайтеся головного меню (якщо файла ще немає, завантажте збереження), потім
   вийдіть. У `UltimateFix\mes_dump.txt` буде весь текст, яким користується гра (ваш переклад, якщо він установлений).
   Поверніть `TextDump=0`.
2. Створіть теку `UltimateFix\text` поруч із `DP.exe`.
3. Якщо у вашому шрифті є літери, яких немає в стандартному атласі US (кирилиця), скопіюйте `docs\glyphmap.example.txt` з моду
   в `UltimateFix\text\glyphmap.txt` і допишіть свої літери (див. «Мапа літер»).
4. Скопіюйте потрібні рядки з `mes_dump.txt` у новий файл у теці `UltimateFix\text`, наприклад `fixes.txt`, і виправте їх.
   Збережіть у **UTF-8**.
5. Запустіть гру. У `UltimateFix\loc.log` буде написано, скільки записів замінено і які рядки відхилено та чому.

Щоб дізнатися номер тексту, який ви бачите в грі, увімкніть `ShowMessageId=1`.

### Текстові файли

- Скільки завгодно файлів `*.txt` у теці `UltimateFix\text` (ім'я `glyphmap.txt` зайняте мапою літер). Читаються за абеткою;
  якщо номер трапляється двічі, діє пізніший рядок (про це буде запис у `loc.log`).
- Один запис на рядок: `0x1234 = текст`. Номер шістнадцятковий, з `0x` на початку. Пробіли довкола тексту відкидаються.
- Рядок, що починається з `;`, це коментар.
- Замінити можна лише записи, які є в завантаженому `MES_ALL.MES`; додавати нові номери не можна.

### Теги

| Тег | Значення |
|---|---|
| `{n}` | новий рядок |
| `{page}` або `{page 120}` | кінець сторінки; число це час **наступної** сторінки в кадрах (60 на секунду) |
| `{color R G B}` … `{/color}` | колір тексту, кожне число 0..255 |
| `{size 48}` … `{/size}` | розмір літер у пікселях, 8..128 (звичайний 32) |
| `{icon 3}` | піктограма кнопки; приймаються лише ті номери, які є в самому `MES_ALL.MES` |
| `{sp}` | пробіл; потрібен лише на самому початку чи в самому кінці тексту |
| `{#1A3}` | клітинка атласу `1A3` напряму, якщо для неї немає літери в мапі |
| `{FFF5}` | позначка запису без тла вікна; діє лише як найперше в тексті |
| `{{` і `}}` | самі дужки `{` і `}` |
| `**` | пишеться як є: сюди гра підставляє число або назву предмета |

Мод сам рахує ширину кожного рядка й кількість рядків. Три службові слова запису (мовець і вигляд вікна, прапорці, час першої
сторінки) **беруться з того запису, який замінюється**.

### Сторінки

Вікно діалогу показує два рядки на сторінку й гортає по два рядки, хай що написано в тексті. Тому:

- ставте `{page}` після кожних двох рядків (або після одного: порожній другий мод додасть сам);
- `{page}` після 3, 5, 7… рядків буде відхилено;
- `{page}` без числа бере час відповідної сторінки з початкового запису. Якщо у вас сторінок більше, ніж було, і запис
  озвучений, напишіть час самі: `{page 120}`;
- озвучені репліки розраховані на голос. Якщо поділити текст інакше, сторінка може перегорнутися трохи раніше чи пізніше;
  нічого не ламається.

У записах системних вікон (вигляд `0x38`, `0x3E`, `0x3F`, по три рядки на сторінку) `{page}` ставиться рівно там, де ви його
написали, без доповнення й без перевірки.

### Мапа літер

Гра не має кодування: слово тексту це номер клітинки в атласі шрифту `FONTWIDE` (64 стовпці × 38 рядків по 32×32 пікселі;
клітинка = рядок × 64 + стовпець, рахуючи з 0). Файл `UltimateFix\text\glyphmap.txt` каже моду, в якій клітинці кожна літера:

```
; коментар
Б = 0xB2
б = 0xC0
U+0404 = 0x8A
```

- літера як є або `U+XXXX`; клітинка як `0x..` або десяткове число, `0..0x97F`;
- без файла мод знає лише клітинки стандартного атласу US (ASCII і Latin-1), їх усі перелічено в `docs\glyphmap.example.txt`;
- рядки файла важливіші за вбудовану таблицю. Кілька літер можуть мати одну клітинку (кирилична `О`, намальована як
  латинська `O`: `О = 0x2F`);
- клітинки до `0xE2` вузькі (гра малює ліву половину клітинки), від `0xE2` широкі. Це зашито в грі, мод цього не змінює;
- мод не знає вашого атласу: якщо в мапі вказано не ту клітинку, намалюється не та літера. `GlyphLog=1` покаже порожні клітинки.

У `mes_dump.txt` клітинка показується тією літерою, яку для неї знає мапа. Тому переклад, у якому кирилиця лежить на клітинках
Latin-1, виглядатиме як латинські літери з діакритикою, доки ви не опишете ці клітинки в `glyphmap.txt`; клітинки без літери
виглядають як `{#XXX}`.

### Що відхиляється

- літера без клітинки в мапі, текст не в UTF-8, невідомий тег, одинока `}`;
- числа поза межами (колір понад 255, розмір не 8..128, клітинка понад `0x97F`);
- номер піктограми, якого немає в самому файлі гри;
- рядок, довший за 384 літери; запис, довший за 4096 слів;
- записи `0x3B70` і `0x3B73`, довші, ніж у завантаженому файлі (гра копіює їх у буфер сталого розміру разом із назвою предмета);
- номер без запису.

Відхилений рядок до гри не доходить: запис лишається з початковим текстом. Причина й місце помилки будуть у `loc.log`.

### Варто знати

- Файли гри не змінюються. Приберіть текстові файли, і повернеться початковий текст.
- Накладка працює поверх будь-якого `MES_ALL.MES`, який завантажує гра, тож нею зручно виправляти вже встановлений переклад.
- Числа, гроші, `%d Yards`, титри й текст, намальований у текстурах, лежать не в `MES_ALL.MES`: накладка їх не змінює.
- З `[Debug] SafeMode=1` усі чотири інструменти вимкнені.
