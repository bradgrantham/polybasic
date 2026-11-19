# Status

What exists and what comes next. The phases are defined in [PLAN.md](PLAN.md).

| Phase | | State |
|---|---|---|
| 1 | Core and msbasic on a host terminal | Done |
| 2a | Griffin Jr. build, console only | Done, except that a size target is not agreed yet |
| 2b | Griffin build, console only | Not started (the griffin dialect is done, under 4b) |
| 3 | Applesoft, text only, with the DOS command layer | Done |
| 4a | Apple II graphics: window and PNG | Done |
| 4b | Griffin dialect; `gfx` pack with sound, on a host and Griffin Jr. | Done |
| 5 | Applesoft memory access | Partly done |
| 6 | Integer BASIC front end | Not started |
| 7 | Griffin video pack; 6502 core | Not started; each needs a design |

`make test` runs 51 tests and all pass: on a Mac built with clang, against SDL3 and with
`SDL3=no`; on Linux (x86-64) built with GCC 13.3 with `SDL3=no`, where they also pass under
`-fsanitize=address,undefined`, and against SDL 3.4.20 (built from source into `~/.local`; the
window opened on a GNOME desktop through XWayland). `make test-griffinjr` runs the 24
msbasic and griffin tests on Griffin Jr. in its emulator and all pass. Nothing exists yet for Griffin.

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

## Phase 2a: Griffin Jr., console only (done; size target open)

The msbasic dialect as a program for the Griffin Jr. BIOS monitor, built with the SDK's own
toolchain (`m68k-unknown-elf` GCC 15.2) against the SDK in `~/trees/other-m68k`.

- **Platform module** (`platform/griffinjr/main.cpp`): the BIOS console (53x24 text, PS/2
  keyboard) with its own line editor, type-ahead and Ctrl-C; `LOAD` and `SAVE` on C: and D:;
  `WAIT`. `basic` at the monitor's prompt gives direct mode, `basic PROG.BAS` runs a program
  and returns. `-s` puts the console on serial port A instead. The module uses BIOS calls only;
  no display yet (`plat_display_open` returns false).
- **Makefile** (`platform/griffinjr/Makefile`, or `make griffinjr` at the top): compiles `core/`,
  `dialects/msfamily.cpp`, `dialects/msbasic.cpp` and the module, and links with the SDK's
  `crt0.o`, `user.ld` and `libsys.a`, without libstdc++.
- **C++ in the SDK build** was done in the SDK itself (its commit `311259e`: static constructors
  and a C++ rule). The interpreter still has no static constructors.
- **Tests** (`tests/run-griffinjr.sh`, `make test-griffinjr`): each test boots the emulator with
  the interpreter and the programs on a C: volume, types `basic -s --marks ...` at the monitor,
  and feeds the test's input down the serial port. All 18 msbasic tests (`tests/programs` and
  `tests/lang`) give byte for byte the output the host is held to, the `SIN`, `COS` and `SQR`
  ones included. The whole run takes about 3 seconds.
- **Checked by hand in the emulator:** on the video console, the prompt, entering lines, `LIST`,
  `RUN`, `PRINT FRE(0)`, `LOAD "SINE.BAS"` and `QUIT`; on the serial console, Backspace and
  CR LF line ends. Not tried: Backspace and Ctrl-C on the video console, `SAVE`, D:, and real
  hardware.

- **A card image** (`make -C platform/griffinjr sdcard` gives `build/griffinjr/sd.img`, 64 MB
  FAT32): the interpreter, a `README.TXT` and ten programs. Tried as D: in the emulator: `dir`,
  `basic SINEJR.BAS` and the start of `basic ADVENTURE.BAS` on the video console.

Four programs were written for the card and are tests on both machines (so 18 msbasic tests
now, where phase 1 had 14): `mandel` (the Mandelbrot set in characters; 56 seconds on Griffin
Jr.), `biorhythm`, `adventure` (a seven-room text adventure) and `sinejr` (Ahl's Sine Wave
narrowed to 53 columns). The other Ahl programs are wider than Griffin Jr.'s 53 columns in
places and wrap.

Size, at `-O2 -fno-ivopts` (the default):

| | bytes |
|---|---|
| text | 130,072 |
| data | 1,760 |
| bss | 4,240 |
| `BASIC.ELF` on the volume (stripped) | 140,352 |

That leaves about 706 KB of the 842 KB for the heap and the 16 KB stack. At `-Os` the text is
95,168 bytes, of which the interpreter's own objects are about 33 KB; the rest is newlib
(`printf`, `strtod`, the maths library) and soft-float. No size target is agreed yet.

Loop benchmarks (`tests/bench`, `make -C platform/griffinjr bench`), in frames of 1/60 s as
counted by the BIOS in the emulator, at 12 MHz:

| Program | What it does | `-O2 -fno-ivopts` | `-Os` |
|---|---|---|---|
| `forloop` | empty `FOR`/`NEXT`, 20,000 times | 98 (1.6 s) | 111 |
| `intmath` | integer arithmetic and an `IF`, 5,000 times | 293 (4.9 s) | 464 |
| `float` | floating-point arithmetic, 2,000 times | 373 (6.2 s) | 481 |
| `trig` | `SIN`, `COS`, `SQR`, 500 times | 704 (11.7 s) | 743 |
| `gosub` | `GOSUB`, an array, a `GOTO` loop, 3,000 times | 476 (7.9 s) | 712 |
| `strings` | string building and slicing, 1,000 times | 136 (2.3 s) | 194 |

That is roughly 12,000 empty loop iterations or 3,000 simple integer statements a second.

Two things found on the way:

- **`FOR`/`NEXT` counted in floating point,** which on the 68000 is soft-float. A loop with a
  whole-number step is now counted in integers while its variable holds one (`Frame::whole` in
  `core/basic.h`). The empty loop went from 569 frames to 111 at `-Os`. This is a core change
  and the host tests still pass.
- **GCC 15.2 for the 68000 gets `format_number` wrong at `-O2`:** a digit after the decimal
  point comes out as another character (`38812.5` printed as `38812.` and a blank). `-O1` and
  `-Os` are right, and so is `-O2 -fno-ivopts`, with only `core/number.cpp` compiled
  differently each time. I think this is a compiler bug in that optimisation pass rather than
  a fault in the source, which is clean under the host's undefined-behaviour sanitizer, but the
  generated code was not read far enough to prove it. The tests are what guard against others
  like it.

A profile (instruction counts from the emulator's `--trace`) puts 84 % of the integer
benchmark inside `execute` itself, spread over the operations, with no single hot spot left.

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

## Phase 4b: the griffin dialect and the gfx pack (done)

Griffin BASIC with drawing, sprites, input devices and sound, on the host and on Griffin Jr.
`README.md` lists the statements.

- **Griffin dialect** (`dialects/griffin.cpp`): the msbasic statements on the shared parser with
  long variable names and keywords that must stand alone (`GOTO 100`, not `GOTO100`), `ELSE`,
  `TIMER`, `DATE$`, `TIME$`. A file whose first line is `REM GRIFFIN BASIC` selects it. A name
  may contain a keyword but not be one: `step` and `mid$` are taken.
- **Parser** (`dialects/msfamily.cpp`): an extension statement's syntax may have alternatives
  introduced by a bare word (`SPRITE DEFINE`, `SOUND OFF`); extension functions may take no
  parentheses (`TIMER`, `INKEY$`) and may return strings.
- **The gfx pack** (`machines/gfx`) is the dialect's `Machine`. It draws in software into pages
  of RGB332 pixels the platform owns, so a picture is the same on every machine. Sound is three
  square-wave voices mixed into the nine output levels a platform plays, fed from queues as the
  program runs and while it waits; `PLAY` takes GW-BASIC/MSX-style music strings.
- **Platform interface** (`core/platform.h`): framebuffer pages, a sound stream, joystick,
  paddles, the clock, and an idle hook. The host implements them in `platform/posix/av.cpp` over
  the display window, `--png`, the SDL3 sound device and `--wav`; Griffin Jr. over its
  framebuffer and the BIOS audio stream and joystick calls.
- **On Griffin Jr.** graphics go on the framebuffer page the console does not use and are shown
  from the first drawing statement; when the program stops the picture stays until a key is
  pressed, then the text screen returns with its contents. A program that shows the second page
  has drawn over the console, which is then cleared.
- **Tests:** 7 in `tests/griffin` (language, an error session, three pictures, one sound test
  with a WAV). On Griffin Jr. the three pictures are compared pixel for pixel with the host's
  PNGs (`tools/fbcompare.py` against the emulator's memory dump) and match, including
  `rosette`, whose coordinates come from `SIN` and `COS`. This is the phase's check.
- **Demos** (`demos/`, also on the card image): `bounce` (sprites and sound), `rosette`, `music`
  (Ode to Joy on three voices).

Measured on Griffin Jr. in the emulator:

- The program is now 149,888 bytes of text, 1,760 of data and 6,700 of bss: about 158 KB of the
  842 KB.
- A 6-second tone played with no gap while a program ran loops, built strings and waited at
  `INPUT` (checked in the emulator's audio recording). While sound plays, a BASIC loop runs at
  about half speed (0.78 s became 1.61 s); the BIOS's audio interrupt alone takes a third of the
  CPU.
- The tones in the emulator's recording of `tests/griffin/sound/tune.bas` are at the same
  frequencies, in the same order, as in the host's WAV.
- `bounce` moves six sprites at about 13 frames a second. Most of that time is the interpreter
  running some fifty statements a frame, not the drawing. Clearing a whole page takes over a
  frame and a half, so a program should rub out what moved, not use `CLS` every frame.

Not done, or not tried:

- Nothing was seen or heard by a person yet: on the host the window and sound device need a
  terminal session, and on Griffin Jr. the checks were memory dumps, text dumps and recordings.
- The host has no joystick or paddles (`JOY` gives 0, `PADDLE` -1). On Griffin Jr. `JOY` and
  `PADDLE` call the BIOS but were not tried with the emulator's `--joy` and `--paddle`.
- `PRINT` and `INPUT` use the text screen, which is hidden while a picture is shown; text on a
  picture is `GPRINT`.
- With the console on a host terminal, sound stops being fed while the program waits at
  `INPUT`.
- PAL machines play at 15,625 Hz, not 15,707; the rate is taken from the BIOS but was not tried.
- The griffin dialect is not in a Griffin build (phase 2b), and Griffin video is its own pack.

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
