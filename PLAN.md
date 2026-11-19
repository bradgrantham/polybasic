# Plan: a modular multi-dialect BASIC

The goals are in [CLAUDE.md](CLAUDE.md) and the current state in [STATUS.md](STATUS.md). This file is
the design, the phases and the decisions.

## Facts the design rests on

| | SDL3 host | Griffin | Griffin Jr. |
|---|---|---|---|
| CPU | host | 68000, 12 MHz, no FPU | 68000, 12 MHz, no FPU |
| RAM for the app | plenty | about 3.5 MB | 842 KB (program + bss + heap + a 16 KB stack) |
| Toolchain | a C++23 compiler (clang so far) | containerised `apps/m68k-g++`, C++23, newlib | `m68k-unknown-elf` GCC 15.2 with `g++` and libstdc++ (crosstool-NG, `~/x-tools`), newlib; SDK builds C only today |
| Console | terminal | firmware console via TRAP #15 | BIOS console syscalls via TRAP #15: 53x24 text on video page 0, PS/2 keyboard |
| Graphics | SDL3 window, or PNG files without it | ENGINE display lists, app-owned | memory-mapped 320x240 RGB332, two pages, no drawing syscalls |

The smallest machine sets the rules for the core.

The Griffin Jr. column was checked against the SDK in `~/trees/other-m68k` on 2026-11-19
(`sdk/README.md`, `sdk/Makefile`, `generated/user.ld`, `generated/syscalls.h`). Points that
matter for phase 2a:

- The user region is 0x8000-0xDA7FF. `sbrk` stops 16 KB below the top, which is the stack.
  Framebuffer page 1 (75 KB) is free RAM for a program that does not double buffer.
- The SDK's `crt0.S` does not run static constructors (no `crti`/`crtn`, no `.init_array`
  walk), and `sdk/user.ld` has no `.init_array` section. The core and msbasic have none today.
  Griffin already does this and is the model if Griffin Jr. needs it: `apps/lib/app.ld` keeps
  `.init_array`/`.fini_array` between the `__init_array_start`/`_end` symbols, and
  `apps/lib/crt0.s` calls newlib's `__libc_init_array` before `main` and supplies empty
  `_init`/`_fini` (the firmware's own `linker.ld` and `crt0.s` do the same for the ROM).
- The console is the screen and PS/2 keyboard only. The emulator's stdout is serial port A, not
  the console; a test either reads the screen with `--text-dump` and types with `--type`, or
  the platform module talks to `/dev/ttya`.
- Files are on C: (768 KB flash volume) and D: (SD card) through newlib `fopen` or `sys_open`.
- Time is `sys_ticks` (frames, about 60 Hz) and `sys_sleep_ms`; `clock()` advances in whole
  frames.

Recovered Apple II programs available as a test corpus (the unpacked disk collection, a separate
tree):
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
  integers. The float type is a build setting: `double` on a host, chosen per 68000 target.
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

The host binary selects a dialect by command-line flag, or from a header line in the program file
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

Linked into Griffin Jr. and host builds of the griffin dialect. Implemented in software over a
framebuffer on Griffin Jr., and over an SDL3 texture on a host, so the same program runs on both.
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

### Apple II machine (host builds only)

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

1. **Core and msbasic on a host terminal.** New source tree, build, and test runner.
   *Check:* the old interpreter's test programs (`STARTREK.BAS`, `lunar.bas`, `life.bas`, and the
   rest) run correctly under scripted input, with any difference from the old interpreter's output
   explained. **Done.**
2. **68000 builds, console only.** Two parts, Griffin Jr. first because it is the smaller machine
   and so shows size and speed problems soonest.
   - **2a. Griffin Jr.:** a platform module and makefile; add C++ to the Griffin Jr. SDK build.
   - **2b. Griffin:** a platform module and makefile; the griffin dialect (`DATE$`, `TIME$`,
     `TIMER`, long names).

   *Check, for each:* it boots to a prompt and runs the phase 1 programs in its emulator; binary
   size and a loop benchmark are recorded, with a size target for Griffin Jr. agreed after the
   first measurement. **2a is next.**
3. **Applesoft, text only.** Screen statements (`HOME`, `HTAB`, `VTAB`, `INVERSE`, `NORMAL`, `TEXT`,
   `SPEED=`), `GET`, `ASC`, `RESTORE`, `ONERR`/`RESUME`/`POP`, `%` variables, Applesoft semantics, and
   the DOS command layer for text files and program chaining.
   *Check:* all 87 text-only recovered programs load without syntax errors; a chosen subset runs to
   completion under scripted input. **Done.**
4. **Graphics.** Two parts.
   - **4a. Apple II:** SDL3 and headless back ends; Applesoft `GR` and `HGR` statements including
     shape tables. *Check:* headless PNGs from recovered graphics programs are reviewed and then
     kept as references. **Done.**
   - **4b. The `gfx` extension pack** on an SDL3 host and Griffin Jr. It needs phase 2a and the
     griffin dialect. *Check:* one `gfx` demo program produces the same picture on the host and
     in the Griffin Jr. emulator.
5. **Applesoft memory access.** `PEEK`/`POKE`/`WAIT` hooks and the ROM `CALL` stubs.
   *Check:* the 33 soft-switch-only programs and the 62 ROM-call-only programs load and a subset runs.
   **Partly done.**
6. **Integer BASIC front end.**
   *Check:* the 66 recovered Integer programs load; a subset runs.
7. **Later, each needing its own design:** Griffin video extension pack; a 6502 core for programs
   that `CALL` into RAM.

The old interpreter's tree is left as it is. Griffin's `apps/basic` switches to the new build at the
end of phase 2b.

## Unpacker changes

Small changes to the unpacker (the disk collection's `_tools`) so the corpus feeds the interpreter directly
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
- **Phase 1 tests:** the old interpreter's test programs, copied into `tests/programs`.

## Decisions still open

1. **Griffin Jr. toolchain:** the SDK's own `m68k-unknown-elf` GCC, or Griffin's containerised
   toolchain.
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
