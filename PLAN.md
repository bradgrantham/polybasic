# Plan: a modular multi-dialect BASIC

## Goal

Replace `~/trees/basic/basic.cpp` with a new interpreter built from separable modules, so that:

- the **macOS** binary runs every dialect: Microsoft/Ahl, Applesoft, Integer BASIC, and Griffin BASIC;
- the **Griffin** and **Griffin Jr.** binaries are the same interpreter cross-compiled for the
  68000, each linking only its own dialect and platform;
- other platforms can be added by writing one platform module.

The interpreter is compiled to 68000 code. Compiling BASIC *programs* to 68000 code is out of scope.

## Facts the design rests on

| | macOS | Griffin | Griffin Jr. |
|---|---|---|---|
| CPU | host | 68000, 12 MHz, no FPU | 68000, 12 MHz, no FPU |
| RAM for the app | plenty | about 3.5 MB | 842 KB (program + bss + heap) |
| Toolchain | clang, C++23 | containerised `apps/m68k-g++`, C++23, newlib | `m68k-unknown-elf` GCC, newlib; SDK builds C only today |
| Console | terminal | firmware console via TRAP #15 | BIOS console syscalls |
| Graphics | SDL3 3.2 (MacPorts) | ENGINE display lists, app-owned | memory-mapped 320x240 RGB332, two pages, no drawing syscalls |
| Existing BASIC | `basic.cpp` | port of it in `apps/basic`, 765 KB binary | none |

The 765 KB port would leave about 77 KB of heap on Griffin Jr., so the smallest machine sets the
rules for the core.

Recovered Apple II programs available as a test corpus (in `packages/apple2-disks-unpacked`):
486 unique Applesoft and 66 unique Integer BASIC programs. Of the Applesoft ones, 87 use no
graphics, `PEEK`/`POKE` or `CALL`; 40 add graphics; 142 add `PEEK`/`POKE`; 217 add `CALL`/`USR`/`&`.
269 issue DOS commands through `PRINT CHR$(4)`.

## Architecture

Four kinds of module. A build picks one platform, one or more dialects, and optional machines.

```
core/        values, strings, variables, statement IR, executor, expression evaluator,
             PRINT/INPUT formatting, errors, program store (LIST/LOAD/SAVE/NEW)
dialects/    msbasic   applesoft   integer   griffin   (+ extension packs, below)
platform/    platform.h   terminal   sdl3   headless   griffin   griffinjr
machines/    apple2/   (text + lo-res + hi-res screen model, 64K memory with hooks,
                        DOS 3.3 command layer, ROM entry-point stubs; later a 6502)
tests/
```

### Core

- **Statement IR.** A front end turns each program line into a compact sequence of statements
  (opcode plus operands, expressions in postfix), with variables resolved to slots and constants
  pre-converted. Line-number targets are resolved to statement positions at `RUN`. The source text
  is kept alongside for `LIST` and `SAVE`.
- **Runtime stacks** for `FOR` and `GOSUB`, because BASIC allows jumping into and out of loops.
- **Values** are a 32-bit integer or a float, with integer arithmetic whenever both operands are
  integers. The float type is a build setting: `double` on macOS, chosen per 68000 target.
- **Strings** live in one heap with compaction (the Microsoft BASIC model); no allocation per token
  or node. The program and IR live in one arena.
- **Errors** are BASIC error numbers delivered by a single non-local exit to the interpreter loop,
  so `ONERR GOTO` / `ON ERROR` can catch them.
- **Semantics table.** Behaviour that differs between dialects is data consulted at run time:
  truth value (1 or -1), whether `FOR` runs a zero-trip loop once, number formatting and digits,
  `PRINT` comma zone width, `RND` argument meaning, default array bounds, integer-only arithmetic.
- **Language subset.** C++23 with no exceptions, no RTTI, no iostreams, and no node-based standard
  containers in the core. This is what keeps the 68000 binaries small.

### Dialects

Each dialect is one source file that registers itself in a table: a keyword table, tokenizer rules
(for example whether keywords are crunched out of identifiers, and how many characters of a name are
significant), the statements and functions it accepts, and a semantics table.

- **msbasic** is the base: Microsoft 8K-style BASIC, which is what the Ahl listings need.
- **applesoft** shares the msbasic parser with its own keyword and semantics tables, and requires the
  `apple2` machine.
- **integer** has its own parser (different string model, `IF` semantics, `#`, `MOD`), and also
  requires the `apple2` machine.
- **griffin** is msbasic plus `DATE$`, `TIME$`, `TIMER` (already in the Griffin port), long variable
  names with no keyword crunching, and whatever extension packs the build links.

The macOS binary selects a dialect by command-line flag, or from a header line in the program file
(see "Unpacker changes").

### Platform interface and the escape

`platform.h` is a small C-style interface. Every call may report "not available", which the core
turns into a BASIC error, so a missing capability never breaks the link.

- **Console:** put character, read line, read key without waiting, clear, set cursor, size.
- **Files:** open, read, write, close, directory listing.
- **Time:** ticks, wall clock, sleep.
- **Framebuffer primitives (optional):** the set listed under "Simple graphics" below.
- **Sound (optional):** a tone or click.

Griffin's unusual hardware will not fit a fixed interface, so there are two escapes:

1. **Extension packs.** A platform (or any module) can register extra statements and functions:
   a keyword, an argument signature, and a handler. The core parses the arguments as ordinary
   expressions and calls the handler. New capabilities are added without touching the core or a
   dialect's parser. Griffin video will arrive this way.
2. **Raw access on the 68000 targets.** `PEEK`/`POKE` on real memory in byte, word and long sizes,
   and `CALL address`, so a program can reach a register or routine before a statement exists for it.

### Simple graphics (extension pack `gfx`)

Linked into Griffin Jr. and macOS builds of the griffin dialect. Implemented in software over a
framebuffer on Griffin Jr., and over an SDL3 texture on macOS, so the same program runs on both.
Statement names are proposals.

| Statement / function | Meaning |
|---|---|
| `CLS [colour]` | clear the screen to a colour |
| `COLOR fg [,bg]` | set drawing colours |
| `RGB(r,g,b)` | colour value for the display (RGB332 on Griffin Jr.) |
| `PSET x,y` / `POINT(x,y)` | set and read a pixel |
| `LINE x1,y1,x2,y2` | line |
| `RECT x,y,w,h` / `FRECT ...` | outlined and filled rectangle |
| `CIRCLE x,y,r` / `FCIRCLE ...` | outlined and filled circle |
| `GPRINT x,y,text$` | draw text at a pixel position |
| `SCRW`, `SCRH` | screen size |
| `PAGE draw,show` / `VSYNC` | use the two pages for tear-free animation |
| `SPRITE DEFINE n,w,h,...` / `SPRITE DRAW n,x,y` | convert pixel data once into a fast internal form, then draw it with a transparent colour |
| `INKEY$`, `JOY(n)`, `PADDLE(n)` | keyboard and joystick input |

### Griffin video

Its own extension pack, to be designed later. Until then the Griffin build is console-only plus the
raw escape.

### Apple II machine (macOS builds only)

- One screen object: 40x24 text, lo-res, hi-res pages 1 and 2, and the mode switches.
- Back ends: the terminal for text-only programs; an SDL3 window showing the whole screen, opened the
  first time a program uses graphics (or forced by a flag); and headless, which writes a PNG.
- A 64K memory array with hooks for soft switches and the zero-page locations programs really use
  (text window, cursor, `ONERR` flag and code, hi-res page, shape table pointer).
- A DOS 3.3 command layer for `PRINT CHR$(4)` commands, using an unpacked disk directory as the disk.
- Native stubs for the common ROM `CALL` targets (clear to end of line, clear to end of screen,
  `HOME`, bell).

Accepted limit: programs are held as IR, not as Applesoft tokens at `$0801`, so machine-code routines
that walk the program text (`&` handlers, most `USR` uses) will not work.

## Phases

Each phase ends with something runnable and a test that proves it.

1. **Core and msbasic on the macOS terminal.** New source tree, build, and test runner.
   *Check:* the programs from `~/trees/basic` (`STARTREK.BAS`, `lunar.bas`, `life.bas`, and the
   rest) run correctly under scripted input, with any difference from the old interpreter's output
   explained. **Done; see Status.**
2. **68000 builds, console only.** Griffin and Griffin Jr. platforms; add C++ to the Griffin Jr. SDK
   build. Done this early so size and speed problems show up before features pile on.
   *Check:* both boot to a prompt and run the phase 1 programs in their emulators; binary size and a
   loop benchmark are recorded, with a size target for Griffin Jr. agreed after the first measurement.
3. **Applesoft, text only.** Screen statements (`HOME`, `HTAB`, `VTAB`, `INVERSE`, `NORMAL`, `TEXT`,
   `SPEED=`), `GET`, `ASC`, `RESTORE`, `ONERR`/`RESUME`/`POP`, `%` variables, Applesoft semantics, and
   the DOS command layer for text files and program chaining.
   *Check:* all 87 text-only recovered programs load without syntax errors; a chosen subset runs to
   completion under scripted input. **Done; see Status.**
4. **Graphics.** SDL3 and headless back ends; Applesoft `GR` and `HGR` statements including shape
   tables; the `gfx` extension pack on macOS and Griffin Jr.
   *Check:* headless PNGs from recovered graphics programs are reviewed and then kept as references;
   one `gfx` demo program produces the same picture on macOS and in the Griffin Jr. emulator.
   **The Apple II half is done; see Status. The `gfx` pack waits for the griffin dialect (phase 2).**
5. **Applesoft memory access.** `PEEK`/`POKE`/`WAIT` hooks and the ROM `CALL` stubs.
   *Check:* the 33 soft-switch-only programs and the 62 ROM-call-only programs load and a subset runs.
6. **Integer BASIC front end.**
   *Check:* the 66 recovered Integer programs load; a subset runs.
7. **Later, each needing its own design:** Griffin video extension pack; a 6502 core for programs
   that `CALL` into RAM.

This repository (`griffin-basic`) holds the new interpreter. `~/trees/basic` is left as it is.
Griffin's `apps/basic` switches to the new build at the end of phase 2.

## Unpacker changes

Small changes to `packages/apple2-disks-unpacked/_tools` so the corpus feeds the interpreter directly
(**done**, and the tree was unpacked again):

- Start each `.bas` with an unnumbered header line, `REM APPLESOFT BASIC` or `REM INTEGER BASIC`.
  The loader uses it to pick the dialect and otherwise ignores unnumbered lines.
- Keep writing embedded control characters as `<CTRL-D>`; the loader converts them back, and
  `LIST` and `SAVE` write them the same way.

## Decisions made

- **Float type:** double everywhere, including the 68000. Programs that need speed use integer
  arithmetic, which the integer fast path keeps cheap.
- **Griffin dialect:** its own dialect, with long names and keywords that need delimiters.
- **`gfx` statements:** derived from the table above.
- **Phase 1 tests:** the programs from `~/trees/basic`, copied into `tests/programs`.

## Decisions still open

1. **Griffin Jr. toolchain:** its README points at `~/x-tools/m68k-unknown-elf`, which is not present
   on this Mac; either install it or reuse Griffin's containerised toolchain.
2. **Ahl listings:** more of them would widen the msbasic tests; a source for the files is needed.
3. **RND seeding:** `RND` currently gives the same sequence every run (as the original machines did,
   and so tests are repeatable); `--randomize` changes that. The default could be flipped.
4. **Applesoft keyboard input is upper-cased**, as on an Apple II or II+, so `IF A$ = "Y"` works
   when typing in lower case. Programs that expect lower-case text entry would need a switch.
5. **`CHAIN` (ProDOS)** starts the named program afresh; it should keep the variables.
6. **Pacing in the window.** A program in the graphics window is slowed to roughly an Apple's
   speed (a millisecond per loop iteration and per screen statement) so that delay loops and
   animation behave; `--fast` turns that off. The charges are guesses, and the default could be
   the other way round.
7. **Hi-res line drawing** uses Bresenham's algorithm, not the ROM's, so slanted lines can differ
   from a real Apple's by a dot. Matching the ROM exactly needs its listing.

## Status

**Phase 1 is done** (macOS terminal, msbasic dialect). `make test` runs 14 tests: the nine programs
from `~/trees/basic` under scripted input, and five language tests in `tests/lang`.

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

**Phase 3 is done** (Applesoft, text only). `make test` now runs 29 tests: the 14 above, seven
Applesoft tests in `tests/applesoft`, and eight recovered programs in `tests/corpus`.

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

Accepted and ignored: `SPEED=`, `PR#`, `IN#`, `TRACE`, `NOTRACE`, `HIMEM:`, `LOMEM:`, and the DOS
commands `MON`, `NOMON`, `MAXFILES`, `FP`, `INT`, `PR#`, `IN#`. `BLOAD` only checks the file exists.

Raise `?NOT AVAILABLE ERROR` when executed, until their phases: graphics statements, `PEEK`,
`POKE`, `CALL`, `WAIT`, `USR`, `PDL`, `&`, `LIST`/`DEL`/`LOAD`/`SAVE`/`STORE`/`RECALL` inside a
program, and the DOS commands `BRUN`, `BSAVE`, `EXEC`, `INIT`, `LOAD`, `SAVE`. Because `POKE 216,0`
is the usual way to switch `ONERR` off, that waits for phase 5 too.

Corpus results (`make corpus`, 492 distinct Applesoft programs):

| Tier | Programs | Parse cleanly | Only unavailable statements | Do not parse |
|---|---|---|---|---|
| text | 93 | 87 | 2 | 4 |
| graphics | 40 | 37 | 1 | 2 |
| memory | 144 | 125 | 10 | 9 |
| machine | 215 | 171 | 24 | 20 |

The tier counts differ a little from the 87/40/142/217 near the top of this plan because the
classifier was rewritten. The four text-tier programs that do not parse are damaged or mistyped on
their disks (listed with reasons in `tools/corpus-known.txt`). For the 31 in other tiers I read the
first failing lines of each: truncated last lines, machine code or garbage stored after the program, Beagle Basic's
renamed keywords, and genuine typing errors such as a missing parenthesis. One is deliberate: a
Beagle Bros program uses a bare `ER` to force a `?SYNTAX ERROR` into its `ONERR` handler.

**Phase 4 is done for the Apple II** (lo-res, hi-res, shape tables, the window and PNG output).
`make test` now runs 40 tests: the 29 above, five graphics tests in `tests/graphics`, and six
recovered graphics programs in `tests/corpus/pictures`. Each of those eleven also has a reference
picture, compared byte for byte.

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
  `POKE 216,0` to switch `ONERR` off. Any other address still raises `?NOT AVAILABLE ERROR`;
  phase 5 fills those in.
- **`BLOAD`** loads a binary file into that memory, so pictures and shape tables on a disk work.
- **Rendering** (`machines/apple2/video.cpp`): a 560x384 picture of whatever the switches select,
  with mixed text and graphics. Hi-res colours use the same rule and values as the unpacker's
  PNGs. The character set comes from the font image in `~/trees/apple2e` (`tools/mkfont.py`).
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
`?ILLEGAL QUANTITY ERROR`, 5 with `?SYNTAX ERROR` and 1 with `?UNDEF'D STATEMENT ERROR`. I looked
at all 89 pictures. The `?ILLEGAL QUANTITY` cases I read were programs that draw past the screen
edge after random drift, or that `DRAW` from a shape table they expected to be in memory already.

Written from memory of the Applesoft ROM rather than checked against a listing: the hi-res colour
bytes, a plotted dot also setting its byte's high bit, `HPLOT TO` using the colour of the last dot
plotted, the shape-table format, `GR` setting `COLOR=0` and leaving the cursor on line 24, and the
rotation table. Shapes at `ROT=` values that are not multiples of 16 are approximate.

Not done in this phase: the `gfx` extension pack (it belongs to the griffin dialect, which is
deferred with phase 2); flashing text flashes in the window but is plain inverse in a PNG; text
page 2 is never shown.

**Brought forward from phase 5**, because the Latin Idiom Translator (`STARTUP` on its ProDOS
disk) needed them; `tests/applesoft/wide.bas` covers them:

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
