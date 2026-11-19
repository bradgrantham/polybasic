# Status

What exists and what comes next. The phases are defined in [PLAN.md](PLAN.md).

| Phase | | State |
|---|---|---|
| 1 | Core and msbasic on a host terminal | Done |
| 2a | Griffin Jr. build, console only | **Next**; not started |
| 2b | Griffin build, console only; griffin dialect | Not started |
| 3 | Applesoft, text only, with the DOS command layer | Done |
| 4a | Apple II graphics: window and PNG | Done |
| 4b | `gfx` extension pack on an SDL3 host and Griffin Jr. | Not started; needs 2a and the griffin dialect |
| 5 | Applesoft memory access | Partly done |
| 6 | Integer BASIC front end | Not started |
| 7 | Griffin video pack; 6502 core | Not started; each needs a design |

`make test` runs 41 tests and all pass: on a Mac built with clang, against SDL3 and with
`SDL3=no`; on Linux (x86-64) built with GCC 13.3 with `SDL3=no`, where they also pass under
`-fsanitize=address,undefined`, and against SDL 3.4.20 (built from source into `~/.local`; the
window opened on a GNOME desktop through XWayland). The interpreter has
not been built for a 68000, though `core/` and the msbasic dialect compile without warnings
with the Griffin Jr. `m68k-unknown-elf-g++` 15.2 (about 32 KB of text at `-Os`, unlinked).

Nothing exists yet for the 68000: no platform module, no makefile, no size or speed measurement.

## Phase 1: core and msbasic (done)

Host terminal, msbasic dialect. 14 tests: the old interpreter's nine programs under scripted input,
and five language tests in `tests/lang`.

Differences from the old `basic.cpp`, all deliberate:

- Numbers print the Microsoft way: a leading space or minus sign, a trailing space, six significant
  digits, no leading zero (`.5`), and `1E+10` style outside 0.01 to 999999.
- `PRINT` comma zones are 14 columns wide, not 20.
- `STR$` of a non-negative number starts with a space.
- A true comparison is `-1`, and `AND`/`OR`/`NOT` are bitwise on 16-bit integers.
- `A%` integer variables work.
- Errors are reported as `?SYNTAX ERROR IN 120`, and only when execution reaches the bad statement.
- Input that is not from a terminal is echoed, so a transcript shows what was typed.

Kept from the old interpreter: `FOR` skips its body when the range is empty (Microsoft 8K BASIC ran
it once), and `WAIT n` pauses for n milliseconds.

## Phase 3: Applesoft, text only (done)

15 more tests: seven Applesoft tests in `tests/applesoft` and eight recovered programs in
`tests/corpus`.

What was added:

- **Applesoft dialect** (`dialects/applesoft.cpp`): the full Applesoft token list on the shared
  parser, so every statement parses even where it cannot run yet. Semantics: true is 1, logical
  `AND`/`OR`/`NOT`, a `FOR` body always runs once, nine significant digits with no padding spaces,
  16-column comma zones, `TAB` counting from 1, two significant characters in a name, `?REENTER`.
- **Core:** `GET`, `ONERR GOTO`, `RESUME`, `POP`, `RUN n`; a `Machine` interface a dialect can name,
  which owns the console and implements extension statements and functions (`OP_EXT`, `OP_EXTFN`);
  program chaining; dialect chosen from the file's header line; `--check`.
- **Apple II machine** (`machines/apple2`): a 40x24 text screen model (`HOME`, `HTAB`, `VTAB`,
  `INVERSE`, `NORMAL`, `FLASH`, `TEXT`, wrap at column 40, scrolling). Output is plain text until
  a program on a terminal uses `HOME` or `VTAB`; from then the terminal mirrors the screen.
  `--stream` keeps it plain. Piped output is always plain.
- **DOS 3.3 command layer:** `OPEN`, `CLOSE`, `READ`, `WRITE`, `APPEND`, `POSITION`, `DELETE`,
  `RENAME`, `LOCK`, `UNLOCK`, `VERIFY`, `CATALOG`, `RUN`, with `L`, `R` and `B` parameters, so
  sequential and random-access text files work. The disk is a directory (`--disk`, by default the
  program's own); an unpacked disk works as it is. It is write protected unless `--scratch DIR`
  says where written files go, so running a recovered program never alters the unpacked tree.
  A Ctrl-D counts only at the start of a line, as on DOS 3.3.

Accepted and ignored: `SPEED=`, `IN#`, `TRACE`, `NOTRACE`, `HIMEM:`, `LOMEM:`, and the DOS
commands `MON`, `NOMON`, `MAXFILES`, `FP`, `INT`, `IN#`.

Raise `?NOT AVAILABLE ERROR` when executed: `USR`, `PDL`, `SHLOAD`, `&`,
`LIST`/`DEL`/`LOAD`/`SAVE`/`STORE`/`RECALL` inside a program, the DOS commands `BRUN`, `BSAVE`,
`EXEC`, `INIT`, `LOAD`, `SAVE`, and any `PEEK`, `POKE` or `CALL` address not listed under phases 4
and 5 below.

Corpus results (`make corpus`, 492 distinct Applesoft programs):

| Tier | Programs | Parse cleanly | Only unavailable statements | Do not parse |
|---|---|---|---|---|
| text | 93 | 87 | 2 | 4 |
| graphics | 40 | 37 | 1 | 2 |
| memory | 144 | 125 | 10 | 9 |
| machine | 215 | 171 | 24 | 20 |

The tier counts differ a little from the 87/40/142/217 in `PLAN.md` because the
classifier was rewritten. The four text-tier programs that do not parse are damaged or mistyped on
their disks (listed with reasons in `tools/corpus-known.txt`). The first failing lines of the 31 in other tiers were read: truncated last lines, machine code or garbage stored after the program, Beagle Basic's
renamed keywords, and genuine typing errors such as a missing parenthesis. One is deliberate: a
Beagle Bros program uses a bare `ER` to force a `?SYNTAX ERROR` into its `ONERR` handler.

## Phase 4a: Apple II graphics (done)

Lo-res, hi-res, shape tables, the window and PNG output. 11 more tests: five graphics tests in
`tests/graphics` and six recovered graphics programs in `tests/corpus/pictures`. Each also has a
reference picture, compared byte for byte.

What was added:

- **Screen memory.** The machine now has the Apple's 64K (`a2_mem`). The text screen is the real
  page of screen codes at `$400`, so lo-res (which shares it) and text interact as they do on an
  Apple; the hi-res pages are at `$2000` and `$4000` in the Apple's own layout.
- **Lo-res:** `GR`, `COLOR=`, `PLOT`, `HLIN`, `VLIN`, `SCRN(`.
- **Hi-res:** `HGR`, `HGR2`, `HCOLOR=`, `HPLOT` in all its forms, and shape tables: `DRAW`,
  `XDRAW`, `ROT=`, `SCALE=`. `SHLOAD` (shapes from cassette) stays unavailable.
- **`PEEK` and `POKE`, as far as graphics needs them:** the display switches (`POKE -16304,0` and
  the other seven), the text and hi-res pages, free RAM from `$800` to `$BFFF`, page 3
  (`$300`-`$3CF`), the shape-table pointer (232, 233), the hi-res drawing page (230), and
  `POKE 216,0` to switch `ONERR` off. Phase 5 added more, below.
- **`BLOAD`** loads a binary file into that memory, so pictures and shape tables on a disk work.
- **Rendering** (`machines/apple2/video.cpp`): a 560x384 picture of whatever the switches select,
  with mixed text and graphics. Hi-res colours use the same rule and values as the unpacker's
  PNGs. The character set (`font.h`) is generated from an Apple IIe font image by `tools/mkfont.py`.
- **Window** (`platform/posix/display_sdl.cpp`, SDL3): opens at the first graphics statement when
  the program is run from a terminal, or at once with `--window`. It shows the whole Apple screen
  and takes the keyboard for `GET` and `INPUT`; the terminal keeps a plain transcript. It stays up
  after the program ends until a key is pressed. Without SDL3 the build still works, minus the
  window.
- **PNG** (`--png FILE`, `machines/common/png.cpp`): the last graphics picture the program showed,
  or the text screen if it showed none. No library is needed.
- **Core:** a `poll` hook in `Machine`, called on every backward jump, which keeps the window
  alive during long loops.

Running the collection headless (`--png`, no input, stopped after 300,000 loop iterations): of the
89 distinct recovered programs that use graphics statements and no `CALL`, 33 ran to their end or
to a prompt, 18 were still drawing when stopped, 24 stopped at something not available yet
(mostly `PEEK`/`POKE` outside the list above, paddles and `CALL`), 8 stopped with
`?ILLEGAL QUANTITY ERROR`, 5 with `?SYNTAX ERROR` and 1 with `?UNDEF'D STATEMENT ERROR`. All 89
pictures were looked at. The `?ILLEGAL QUANTITY` cases that were read are programs that draw past the screen
edge after random drift, or that `DRAW` from a shape table they expected to be in memory already.

Written from memory of the Applesoft ROM rather than checked against a listing: the hi-res colour
bytes, a plotted dot also setting its byte's high bit, `HPLOT TO` using the colour of the last dot
plotted, the shape-table format, `GR` setting `COLOR=0` and leaving the cursor on line 24, and the
rotation table. Shapes at `ROT=` values that are not multiples of 16 are approximate.

Not done: flashing text flashes in the window but is plain inverse in a PNG; text
page 2 is never shown.

## Phase 5: Applesoft memory access (partly done)

Done early because a recovered program, the Latin Idiom Translator, needed them; one more test,
`tests/applesoft/wide.bas`, covers them:

- **80 columns:** `PR#3` (as a statement or a DOS command) switches the text screen to 80x24 and
  `PR#0` back; both clear it. With 80 columns on, typed lower case is kept, as on a //e.
- **Keyboard:** `PEEK(-16384)`, `POKE -16368,0` and `WAIT`, so "press any key" loops work. A key
  seen there and not cleared is still the first key `GET` or `INPUT` receives.
- **Page zero:** the text window (32-35), the cursor (36, 37), the error code (222) and error
  line (218, 219) for `ONERR` handlers.
- **`CALL`** for a few Monitor routines: -936 (home), -958 and -868 (clear to end of screen and
  of line), -912 (scroll), -198 (bell), -3288 (accepted, nothing to do).
- **`?NOT AVAILABLE ERROR` is never handed to an `ONERR` handler.** It is this interpreter's
  shortcoming, not the program's error, and a handler that itself used something unavailable
  used to loop for ever with nothing on the screen.
- Terminal output is flushed as it is written.

Ctrl-C still always stops the program, even one with an `ONERR` handler that expects to catch it
(code 255) as this one does. The error codes and `CALL` addresses are from memory of the manuals.

Not done: every other `PEEK`, `POKE` and `CALL` address, `PDL`, `USR`, and the phase's check
(loading the soft-switch-only and ROM-call-only recovered programs and running a subset).
