# Text overlay and translator tools

*Українською: [нижче](#українською).*

Four settings in the `[Language]` section of `UltimateFix.ini` (also in the F10 menu, page "Fixes", group "Translator tools").
All of them need a restart of the game.

| Setting | Default | What it does |
|---|---|---|
| `TextOverlay` | 1 | The translation in `UltimateFix\text` (`mes_all.json` of the SWERY Localization Tool, its `glyphmap.json`, optional `*.txt` corrections) replaces records of `MES_ALL.MES` in memory. Nothing happens while the folder has none of these files. |
| `ShowMessageId` | 0 | The number of every text the game draws is printed over it (`0x1A2B`). It is the record's index in `mes_all.json`, in hexadecimal. |
| `GlyphLog` | 0 | `UltimateFix\loc.log` lists the messages that use a font cell that is empty or outside the font atlas, and the draws that overflow the game's glyph batch. |
| `TextDump` | 0 | The `MES_ALL.MES` the game loaded is written to `UltimateFix\mes_dump.json` in the form of the tool's `mes_all.json`. |

**Status: built and checked on the host only. Nothing here has been run in the game.** Two things in particular are not proven:

- There is no DPMsgTool on the machine this was written on. How its JSON maps to the MES file was worked out from the game's
  code and from the SWERY tool's source, not by comparing with a real DPMsgTool file. The mod therefore checks every
  `mes_all.json` against the loaded MES before it uses it (see "The check"), and `TextDump` lets you compare the two
  readings yourself.
- The hooks were checked byte by byte against `DP.exe` 1.01b, but have never executed.

## Quick start

1. In the SWERY Localization Tool save your translation as usual. Copy `Documents\SWERY-Localization-Tool\DP1\Text\Done\mes_all.json`
   to `<game>\UltimateFix\text\mes_all.json`.
2. If you edited the glyph map in the tool, also copy `...\DP1\Text\Meta\glyphmap.json` to `UltimateFix\text\glyphmap.json`.
   Without that file the mod uses the same default map the tool uses (67 characters).
3. Start the game. No packing with DPMsgTool is needed for the text; **the font is not part of this**: your `FONTWIDE.XPC` with
   the Cyrillic glyphs must still be installed the usual way.
4. Read `UltimateFix\loc.log`: how the file compared with the loaded MES, how many records were replaced, what was refused and why.

The game's `mes_all.mes` on disk can be the original English one or an earlier pack of your translation: only the records whose
text differs from what the game loaded are replaced, the others stay byte for byte as loaded.

## What is read, in this order

| File in `UltimateFix\text` | What |
|---|---|
| `glyphmap.json` | The tool's glyph map: `{ "version": 1, "map": { "Б": "Ô", ... } }`. Each key is one character, the value one or two carrier characters. |
| `glyphmap.txt` | Optional, the mod's own: characters that get a font cell directly, one per line: `… = 0x103` or `U+2026 = 0x103`. For characters that are neither in the glyph map nor Latin (see "Characters"). |
| `mes_all.json` | DPMsgTool's JSON as the tool saves it: an array with one element per record number, `{ Id1, Id2, Flag1, Flag2, FirstSegmentLenght, Text, EmptyRecord }`. |
| `*.txt` | Optional corrections, one record per line: `0x1234 = text`, the same markers as in the JSON. Read in alphabetical order after the JSON; a later line wins. A line that starts with `;` is a comment. The text is everything after `= ` up to the end of the line. |

All files are UTF-8 (a byte order mark is fine; UTF-16 is refused).

## How the JSON is read

- **The element's position in the array is the record number** (element 0 is record 0; `ShowMessageId` shows the same number in
  hexadecimal). An element with `"EmptyRecord": true` is skipped.
- **Only `Text` carries content.** From the record being replaced the mod keeps the first, third and fourth header word
  (speaker / window style, flags, duration of the first page). It computes the line count and the width of every line itself,
  by the game's own rule (a cell below `0xE2` counts 1, other cells and icons 2). `Id2` and `FirstSegmentLenght` are therefore
  never used, whatever they hold.
- `Id1`, `Flag1`, `Flag2` are compared with the header words of the loaded record. If they are not the same, that record is
  refused; if that happens in more than one record in twenty, the whole file is refused (it was made from another MES).

### Markers

| Marker in `Text` | In the MES record | Status |
|---|---|---|
| `{NEXT_SEGMENT}` | `FFFE`, new line | accepted |
| `{NEXT_FRAME id=N}` | `FFF2 N`, new line and the duration of the next page in frames (60 a second) | accepted; `{NEXT_FRAME id=N, letters=M}` too, `M` is not used |
| `{ICON id=N}` | `FFF4 N`, button icon | accepted, for the icon numbers the loaded file itself uses |
| `{SET_COLOR color=R, G, B}` | `FFFA R G B` | accepted, each 0..255 |
| `{RESET_COLOR}` | `FFF9` | accepted |
| `{SET_SPACING value=N}` | `FFF1 N`, glyph size in pixels (32 is normal) | accepted, 8..128 |
| `{NO_OP:0xNNNN}`, `{UNK:0xNNNN}` | the word `NNNN` itself | accepted **only** when the loaded record has that word (as a glyph or as a code without parameters); otherwise the record is refused |
| `{BR}` | not known | refused, unless the check below establishes it (see there) |
| any other `{NAME ...}` in capitals | | refused |

Why these are taken as known (there was no DPMsgTool to ask): the tool's rule for `FirstSegmentLenght` ("the letters up to the
first `{NEXT_SEGMENT}` or `{NEXT_FRAME}`") is exactly the game's width word of the first line, and the game has two line ends,
`FFFE` and `FFF2` with one parameter. `FFFA` is the only code with three parameters, `FFF9` is its reset and follows it in
every record of the game. Of the game's three codes with one parameter, `FFF4` is the icon and `FFF2` the page, which leaves
`FFF1` for `SET_SPACING`. The labels inside a marker (`id=`, `color=`, `value=`) are not checked, only the numbers.
`{BR}` and the name DPMsgTool gives the code `FFF0` (size back to normal, 5 records in the English file) could not be worked
out from the source.

A `{` that is not followed by a name in capitals is an ordinary character.

### Characters

- A character goes through the glyph map first (Cyrillic to its carrier), then to a font cell: **cell = character code − 0x20**,
  for `U+0020..U+007E` and `U+00A0..U+00FF`. That is what the game's own French and German text files do and what the tool's
  font editor relies on (carrier `¿` draws the cell that shows `Á` in the stock atlas).
- Any other character has no cell, and a record with one is refused. The log names the character. Two ways out: map it in
  `glyphmap.json`, or give it a cell in `glyphmap.txt`.
- The English text has about 100 records with cells that have no such character (`…`, `∞`, a few signs from the Japanese font:
  cells `0x103`, `0x127`, `0x229`..). How DPMsgTool writes those is not known. While you leave such a record untranslated it
  will most likely be refused with "character has no cell", and it simply stays as loaded. If you translate one, put
  the sign into `glyphmap.txt`.

### Pages

A dialogue window shows two lines at a time (three for the system windows, styles `0x38`, `0x3E`, `0x3F`), and it turns pages
by counting lines, not at `{NEXT_FRAME}`. `{NEXT_FRAME id=N}` only carries the duration of the page that starts after it. So in a
two-line window `{NEXT_FRAME}` has to come after an even number of lines; the game's own text does this everywhere
(`text{NEXT_SEGMENT}{NEXT_FRAME id=..}` for a page of one line). **If a `{NEXT_FRAME}` comes after an odd number of lines, the
mod adds an empty line in front of it** and says so in `loc.log`. This is the one place where the result differs from what
DPMsgTool would pack.

## The check

Before anything from `mes_all.json` is used, the whole file is held against the loaded MES, record by record, and the result
is written to `loc.log`:

- the header fields (above);
- for each of `{NEXT_SEGMENT}`, `{NEXT_FRAME}`, `{ICON}`, `{SET_COLOR}`, `{RESET_COLOR}`, `{SET_SPACING}`: in how many records
  the number of markers equals the number of their codes in the loaded record. A translator keeps the markers of a record, so
  this agrees nearly everywhere when the reading is right. If a marker that at least 20 records have agrees in fewer than half
  of them, the mod concludes that it does not understand this file's markers and **uses none of it**;
- `{BR}`: if it occurs, and every record that has `{BR}` or the code `FFF0` has as many of the one as of the other, `{BR}` is
  taken as `FFF0` for this run. Otherwise texts with `{BR}` are refused.

`TextDump=1` writes `UltimateFix\mes_dump.json`: the loaded MES as the mod reads it, in the JSON form. Compare it with the JSON
DPMsgTool makes from the same `.mes` (the tool's `Original\mes_all.json`). Where the two differ (other than in the characters of
the cells mentioned above, which the dump writes as `{UNK:0x....}`, and possibly in how numbers above 32767 or non-Latin
characters are written), the mod reads something differently from DPMsgTool: please report it.

## loc.log

`UltimateFix\loc.log` is written anew at every start (when one of the tools has something to say). Examples:

```
glyph map: 67 characters from glyphmap.json
text: mes_all.json: 20003 elements (the loaded MES has 20003 record numbers), 17237 compared with their records: ...
text: mes_all.json: {NEXT_SEGMENT} and the code FFFE: 14200 records have one of them, in 14188 of those as many of the one as of the other
text: mes_all.json, record 4660 (0x1234), column 17 of the text: the character U+2026 has no cell: ...; the record stays as loaded
text: 9120 records differ from the loaded MES_ALL.MES and are replaced, 8100 say the same and stay byte for byte, 17 texts refused
glyph: message 0x1234: cell 0x0A5 (row 2, column 37) is empty in the font atlas
batch: message 0x0456: more than 768 glyph quads in one draw, the rest is not drawn (...)
```

## What is refused, and limits

- A record number that has no record in the loaded MES (records cannot be added), and a record with codes the mod does not
  handle (`FFF6` and codes the game's files never use).
- A line of more than 384 characters (one draw holds 768 quads and a coloured glyph takes two); a record of more than 4096 words.
- The two template records `0x3B70` and `0x3B73` ("... has been moved to your toolbox") may not become longer than they are
  and must keep their one `**`: the game copies them, with the item's name in place of `**`, into a fixed buffer of 513 words
  without a limit. For the same reason keep item names short: the mod cannot tell which records are item names, so it does not
  limit them (the game's own are a few words; a name of several hundred characters would overflow that buffer).
- The same two records with the `**` as the very last thing of the text: the game swallows whatever stands right behind
  the `**` (a `{NEXT_FRAME}`, a full stop) and loses the end of the text when nothing stands there. Keep the `**` and what
  follows it as the loaded text has them.
- An empty `Text` for a record that has text in the loaded file: the game is not safe with an empty record when it uses
  it as an item name. Write a single space for a blank one.
- An icon number the loaded file never uses; a colour above 255; a size outside 8..128.
- A refused record stays as the game loaded it. Nothing is ever written to the game's files.

## Good to know

- The merged text is built once, when the game binds its text file (before the title screen). Changing the files needs a restart.
- The records keep their order in the file and their numbers: the game walks them in file order for NPC talk.
- Subtitles of voiced scenes are timed by `Flag2` and the numbers of `{NEXT_FRAME}`: keep them as they are.
- `**` in a text is where the game puts a number or an item name.
- Text that is not in `MES_ALL.MES` is not covered: numbers and words drawn from the exe (`%d Yards`, `Suspect`), the credits,
  and text painted into textures.
- `[Debug] SafeMode=1` switches all four tools off.

---

## Українською

Чотири налаштування в розділі `[Language]` файла `UltimateFix.ini` (і в меню F10, сторінка «Виправлення», група «Інструменти
перекладача»). Усі потребують перезапуску гри.

| Налаштування | Типово | Що робить |
|---|---|---|
| `TextOverlay` | 1 | Переклад із теки `UltimateFix\text` (`mes_all.json` зі SWERY Localization Tool, його `glyphmap.json`, за бажання виправлення в `*.txt`) замінює повідомлення гри в пам'яті. Поки в теці немає цих файлів, нічого не відбувається. |
| `ShowMessageId` | 0 | Над кожним текстом гри пишеться його номер (`0x1A2B`). Це порядковий номер запису в `mes_all.json`, у шістнадцятковому вигляді. |
| `GlyphLog` | 0 | У `UltimateFix\loc.log` потрапляють повідомлення, які беруть порожню клітинку шрифту або клітинку за межами атласу, і ті, що не вмістилися в один прохід малювання. |
| `TextDump` | 0 | `MES_ALL.MES`, який завантажила гра, записується в `UltimateFix\mes_dump.json` у вигляді `mes_all.json` інструмента. |

**Стан: зібрано й перевірено лише на комп'ютері розробки. У грі нічого з цього не запускалося.** Окремо не доведено два моменти:

- DPMsgTool під рукою не було. Відповідність його JSON до файла MES виведено з коду гри та з коду SWERY Localization Tool, а не
  порівнянням зі справжнім файлом DPMsgTool. Тому мод перед використанням звіряє кожен `mes_all.json` із завантаженим MES
  (розділ «Перевірка»), а `TextDump` дає змогу порівняти обидва прочитання самостійно.
- Місця втручання в гру звірено побайтово з `DP.exe` 1.01b, але цей код ще жодного разу не виконувався.

### Як почати

1. Збережіть переклад у SWERY Localization Tool як завжди. Скопіюйте
   `Документи\SWERY-Localization-Tool\DP1\Text\Done\mes_all.json` у `<гра>\UltimateFix\text\mes_all.json`.
2. Якщо ви змінювали мапу гліфів в інструменті, скопіюйте також `...\DP1\Text\Meta\glyphmap.json` у
   `UltimateFix\text\glyphmap.json`. Без цього файла мод бере ту саму типову мапу, що й інструмент (67 символів).
3. Запустіть гру. Пакувати текст через DPMsgTool не треба. **Шрифту це не стосується**: ваш `FONTWIDE.XPC` з кириличними
   літерами має бути встановлений як завжди.
4. Відкрийте `UltimateFix\loc.log`: там видно, як файл зійшовся із завантаженим MES, скільки записів замінено, що відхилено й чому.

Файл `mes_all.mes` у теці гри може бути оригінальним англійським або попереднім паком вашого перекладу: замінюються лише
записи, текст яких відрізняється від завантаженого грою, решта лишається байт у байт як була.

### Які файли читаються (у такому порядку)

| Файл у `UltimateFix\text` | Що це |
|---|---|
| `glyphmap.json` | Мапа гліфів інструмента: `{ "version": 1, "map": { "Б": "Ô", ... } }`. Ключ: один символ, значення: один або два символи-носії. |
| `glyphmap.txt` | Необов'язковий, власний формат мода: символи, яким клітинка шрифту задається напряму, по одному в рядку: `… = 0x103` або `U+2026 = 0x103`. |
| `mes_all.json` | JSON від DPMsgTool, як його зберігає інструмент: масив, один елемент на номер запису. |
| `*.txt` | Необов'язкові виправлення, один запис у рядку: `0x1234 = текст`, ті самі маркери, що й у JSON. Читаються за абеткою після JSON; пізніший рядок перемагає. Рядок, що починається з `;`, це коментар. |

Усі файли в UTF-8 (UTF-16 відхиляється).

### Як читається JSON

- **Місце елемента в масиві це номер запису** (той самий номер показує `ShowMessageId`). Елемент з `"EmptyRecord": true`
  пропускається.
- **Зміст береться лише з `Text`.** Із запису, який замінюється, мод зберігає перше, третє й четверте слово заголовка (мовець
  і стиль вікна, прапори, тривалість першої сторінки). Кількість рядків і ширину кожного рядка мод рахує сам за правилом гри,
  тож `Id2` і `FirstSegmentLenght` не використовуються взагалі.
- `Id1`, `Flag1`, `Flag2` порівнюються зі словами заголовка завантаженого запису. Якщо не збігаються, запис відхиляється; якщо
  таких більше ніж один із двадцяти, відхиляється весь файл (його зроблено з іншого MES).

### Маркери

| Маркер у `Text` | У записі MES | Стан |
|---|---|---|
| `{NEXT_SEGMENT}` | `FFFE`, новий рядок | приймається |
| `{NEXT_FRAME id=N}` | `FFF2 N`, новий рядок і тривалість наступної сторінки в кадрах (60 на секунду) | приймається; `{NEXT_FRAME id=N, letters=M}` теж, `M` не використовується |
| `{ICON id=N}` | `FFF4 N`, піктограма кнопки | приймається для номерів, які є в самому завантаженому файлі |
| `{SET_COLOR color=R, G, B}` | `FFFA R G B` | приймається, кожне 0..255 |
| `{RESET_COLOR}` | `FFF9` | приймається |
| `{SET_SPACING value=N}` | `FFF1 N`, розмір літер у пікселях (звичайний 32) | приймається, 8..128 |
| `{NO_OP:0xNNNN}`, `{UNK:0xNNNN}` | саме слово `NNNN` | приймається **лише** тоді, коли завантажений запис має це слово; інакше запис відхиляється |
| `{BR}` | невідомо | відхиляється, якщо перевірка нижче не встановить його значення |
| будь-який інший `{НАЗВА ...}` великими літерами | | відхиляється |

Що означає `{BR}` і як DPMsgTool називає код `FFF0` (повернення звичайного розміру, 5 записів в англійському файлі), з коду
інструментів вивести не вдалося.

### Символи

- Символ спершу проходить через мапу гліфів (кирилиця стає носієм), потім перетворюється на клітинку шрифту:
  **клітинка = код символу − 0x20**, для `U+0020..U+007E` і `U+00A0..U+00FF`.
- Будь-який інший символ клітинки не має, і запис із ним відхиляється; журнал називає символ. Вихід: додати його в
  `glyphmap.json` або дати йому клітинку в `glyphmap.txt`.
- В англійському тексті близько 100 записів мають клітинки без такого символу (`…`, `∞`, кілька знаків японського шрифту). Як їх
  записує DPMsgTool, невідомо. Поки такий запис не перекладено, він, найімовірніше, буде відхилений із причиною «символ не має
  клітинки» і просто лишиться як був. Якщо перекладаєте такий запис, додайте знак у `glyphmap.txt`.

### Сторінки

Вікно діалогу показує по два рядки (системні вікна по три) і гортає сторінки, рахуючи рядки, а не за `{NEXT_FRAME}`.
`{NEXT_FRAME id=N}` лише несе тривалість сторінки, що починається після нього. Тому у вікні на два рядки `{NEXT_FRAME}` має
стояти після парної кількості рядків; у тексті гри так всюди (`текст{NEXT_SEGMENT}{NEXT_FRAME id=..}` для сторінки з одного
рядка). **Якщо `{NEXT_FRAME}` стоїть після непарної кількості рядків, мод сам додає перед ним порожній рядок** і пише про це
в `loc.log`. Це єдине місце, де результат відрізняється від того, що запакував би DPMsgTool.

### Перевірка

Перш ніж узяти щось із `mes_all.json`, мод звіряє весь файл із завантаженим MES і пише підсумок у `loc.log`:

- поля заголовка (див. вище);
- для кожного з маркерів `{NEXT_SEGMENT}`, `{NEXT_FRAME}`, `{ICON}`, `{SET_COLOR}`, `{RESET_COLOR}`, `{SET_SPACING}`: у скількох
  записах кількість маркерів дорівнює кількості відповідних кодів у завантаженому записі. Перекладач зберігає маркери запису,
  тож за правильного прочитання це збігається майже всюди. Якщо маркер, який є щонайменше у 20 записах, збігається менш ніж
  у половині з них, мод вважає, що не розуміє маркери цього файла, і **не бере з нього нічого**;
- `{BR}`: якщо він трапляється і в кожному записі, де є `{BR}` або код `FFF0`, їх порівну, `{BR}` на цей запуск вважається
  кодом `FFF0`. Інакше тексти з `{BR}` відхиляються.

`TextDump=1` записує `UltimateFix\mes_dump.json`: завантажений MES так, як його читає мод, у вигляді JSON. Порівняйте його з
JSON, який DPMsgTool робить із того самого `.mes` (`Original\mes_all.json` інструмента). Якщо вони різняться чимось, крім
символів згаданих вище клітинок (мод пише їх як `{UNK:0x....}`), мод читає щось інакше, ніж DPMsgTool: повідомте про це.

### Що відхиляється

- Номер, якого немає в завантаженому MES (додавати записи не можна), і запис із кодами, яких мод не обробляє.
- Рядок, довший за 384 символи; запис, довший за 4096 слів.
- Два шаблонні записи `0x3B70` і `0x3B73` не можуть стати довшими, ніж є, і мають зберегти своє одне `**`: гра копіює їх
  разом із назвою предмета замість `**` у буфер на 513 слів без перевірки. З тієї ж причини назви предметів мають лишатися
  короткими: мод не знає, які записи є назвами предметів, тому їх не обмежує.
- Ті самі два записи, якщо `**` стоїть у самому кінці тексту: гра «з'їдає» те, що стоїть одразу за `**` (`{NEXT_FRAME}`,
  крапку), і губить кінець тексту, якщо там нічого немає. Лишайте `**` і те, що за ним, як у завантаженому тексті.
- Порожній `Text` для запису, який у завантаженому файлі не порожній: гра небезпечно поводиться з порожнім записом, коли
  бере його як назву предмета. Для порожнього рядка напишіть один пробіл.
- Номер піктограми, якого немає в завантаженому файлі; колір понад 255; розмір поза 8..128.
- Відхилений запис лишається таким, яким його завантажила гра. У файли гри мод нічого не пише.

### Варто знати

- Злитий текст будується один раз, коли гра підключає свій текстовий файл. Щоб побачити зміни, гру треба перезапустити.
- Субтитри озвучених сцен синхронізовано через `Flag2` і числа в `{NEXT_FRAME}`: не змінюйте їх.
- `**` у тексті це місце, куди гра підставляє число або назву предмета.
- Текст, якого немає в `MES_ALL.MES`, сюди не входить: числа й слова з exe (`%d Yards`, `Suspect`), титри, написи в текстурах.
- `[Debug] SafeMode=1` вимикає всі чотири інструменти.
