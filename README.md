# griffin-basic

A BASIC interpreter built from separable modules: a core runtime, one front end per dialect, and
one platform module per machine. See [CLAUDE.md](CLAUDE.md) for the goals, [PLAN.md](PLAN.md) for
the design and the phases, and [STATUS.md](STATUS.md) for what is done.

## Build and test

```
make            # build/basic
make test       # run tests/run.sh against it
```

Needs a C++23 compiler. SDL3 (found with `pkg-config`) is used for the graphics window; without
it, or with `make SDL3=no`, everything else still builds.

```
make corpus CORPUS=DIR   # parse every recovered Applesoft program in an unpacked disk collection
```

The Griffin Jr. build needs that machine's tree (`JR`, by default `~/trees/other-m68k`) with its
SDK, firmware and emulator built, and its cross toolchain in `~/x-tools`:

```
make griffinjr           # build/griffinjr/BASIC.ELF, to copy onto C: or the SD card
make test-griffinjr      # run the msbasic tests in the Griffin Jr. emulator
make -C platform/griffinjr bench    # time the loop benchmarks in tests/bench
make -C platform/griffinjr sdcard   # build/griffinjr/sd.img: a card (D:) with BASIC and programs
```

On the machine, `basic` at the monitor's prompt gives direct mode and `basic PROG.BAS` runs a
program; `basic -s` uses serial port A (38400 8N1) as the console instead of the screen and
keyboard. The dialect there is griffin unless the file's first line is `REM MICROSOFT BASIC` or
`basic -m` is given.

## Use

```
build/basic program.bas          # load, run, exit
build/basic                      # direct mode: type lines; QUIT to leave
build/basic --list-dialects      # msbasic, applesoft, griffin, integer
build/basic --check program.bas  # compile only; list statements that do not compile
```

The dialect comes from `--dialect NAME`, or from a first line such as `REM APPLESOFT BASIC` (the
unpacker in `apple2-disks-unpacked/_tools` writes one) or `REM GRIFFIN BASIC`, or else is
`msbasic`.

Applesoft programs see the directory they were loaded from as their disk, so
`PRINT CHR$(4);"OPEN SCORES"` finds `SCORES.txt` there (or `raw/SCORES` on an unpacked disk).

```
build/basic --disk DIR prog.bas      # use DIR as the disk instead
build/basic --scratch DIR prog.bas   # let the program write files, into DIR
build/basic --stream prog.bas        # never take over the terminal as a 40x24 screen
```

Without `--scratch` the disk is write protected. In a program file, a control character inside a
line is written `<CTRL-D>`.

Applesoft graphics (`GR`, `HGR`, shape tables) open a window at the first graphics statement when
the program is run from a terminal. The window shows the whole Apple screen and takes the
keyboard; it stays until you press a key after the program ends.

```
build/basic --window prog.bas        # open the window straight away
build/basic --no-window prog.bas     # never open it
build/basic --png out.png prog.bas   # no window; write the last picture shown to out.png
build/basic --fast prog.bas          # in the window, do not slow down to an Apple's pace
```

Griffin BASIC programs use the same window and `--png`, at 320x240, and play sound through the
sound device when run from a terminal:

```
build/basic demos/bounce.bas               # sprites and sound; any key ends
build/basic demos/music.bas                # three-voice music
build/basic --wav out.wav prog.bas         # write the sound to out.wav and do not play it
build/basic --no-sound prog.bas            # do not use the sound device
```

`demos/carols.bas` is the player for a disk of Christmas carols; the carols themselves are
converted from the unpacked Apple II disk and are not in the repository:

```
make carols CAROLS=DIR                     # build/CAROLS.BAS from the disk's directory
build/basic build/CAROLS.BAS
```

Direct-mode commands: `RUN`, `LIST [from][-to]`, `NEW`, `LOAD "file"`, `SAVE "file"`, `CONT`, `QUIT`.
Ctrl-C interrupts a running program.

## Griffin BASIC

Microsoft-style BASIC where names may be long (`score`, `player$`) and keywords must stand alone
(`GOTO 100`, `FOR i = 1 TO 10`), with `ELSE`. A name may not be a keyword (`step`, `mid$`). The
screen is 320x240; a colour is 0 to 255 (three bits red, three green, two blue). Where a colour
may be left out, the one set by `COLOR` is used.

| Statement or function | Meaning |
|---|---|
| `CLS [colour]` | clear the page being drawn on |
| `COLOR colour [,background]` | set the drawing colour and the one `CLS` uses |
| `RGB(r, g, b)` | the colour nearest to r, g, b (each 0 to 255) |
| `PSET x, y [,colour]` / `POINT(x, y)` | set a dot; read one (-1 off the screen) |
| `LINE x1, y1, x2, y2 [,colour]` | a line |
| `RECT x, y, w, h [,colour]` / `FRECT ...` | a rectangle, outlined or filled |
| `CIRCLE x, y, r [,colour]` / `FCIRCLE ...` | a circle, outlined or filled |
| `GPRINT x, y, text$ [,colour]` | text on the picture, 7x8 characters |
| `SCRW`, `SCRH` | the screen's width and height |
| `PAGE draw, show` | which of the two pages (0, 1) to draw on and to show |
| `VSYNC [frames]` | wait for the next frame, or several |
| `SPRITE DEFINE n, x, y, w, h [,clear]` | take sprite n (0 to 31, up to 128x128) from the page; dots of colour `clear` (0 unless given, -1 for none) are not drawn |
| `SPRITE n, x, y` | draw sprite n (`SPRITE DRAW n, x, y` is the same) |
| `TEXT` | back to the text screen |
| `INKEY$` | a key if one is waiting, else `""` |
| `JOY(n)` | joystick: 0 gives x (-1, 0, 1), 1 gives y (down is 1), 2 the button, 3 all the switches |
| `PADDLE(n)` | paddle 0 or 1: 0 to 255, or -1 if there is none |
| `SOUND hertz, ms [,volume [,voice]]` | queue a tone (0 hertz is a rest); volume 0 to 15, voice 1 to 3 |
| `SOUND OFF` | stop all sound at once |
| `BEEP` | a short tone |
| `PLAY a$ [,b$ [,c$]]` | queue music, one string per voice, starting together |
| `PLAYING(n)` | true while voice n (1 to 3; 0 for any) has sound queued |
| `TIMER`, `DATE$`, `TIME$` | seconds since midnight; `2026-11-19`; `14:05:09` |

Sound plays while the program carries on; `IF PLAYING(0) THEN ...` waits for it. In a `PLAY`
string: notes `A` to `G` with `#` or `+` for a sharp and `-` for a flat, then an optional length
(`C4` a quarter note, `C8` an eighth) and dots; `R` or `P` a rest; `O1` to `O8` the octave (A in
`O4` is 440 Hz), `>` and `<` up and down one; `L` the length for notes without one; `T` the tempo
in quarter notes a minute; `V0` to `V15` the volume; `MN`, `ML`, `MS` normal, legato, staccato;
`N1` to `N96` a note by number.

## Layout

| Path | Contents |
|---|---|
| `core/basic.h` | values, the interpreter state, and the interface front ends compile through |
| `core/ops.h` | the compiled instruction set every front end produces |
| `core/exec.cpp` | executor, string heap, console output |
| `core/program.cpp` | program store, compile driver, error reporting, direct mode |
| `core/number.cpp` | number formatting and parsing |
| `core/platform.h` | what a platform module must provide |
| `dialects/msfamily.cpp` | lexer and parser shared by the Microsoft-family dialects |
| `dialects/msbasic.cpp` | Microsoft 8K / Ahl dialect: keyword table and semantics |
| `dialects/applesoft.cpp` | Applesoft dialect: keyword table and semantics |
| `dialects/griffin.cpp` | Griffin dialect: keyword table, semantics, and the gfx statements |
| `dialects/integer.cpp` | Integer BASIC (Apple II): its own lexer and parser, and its error messages |
| `machines/gfx/gfx.cpp` | the gfx pack: drawing, sprites, input devices, the clock |
| `machines/gfx/sound.cpp` | its sound: three voices, tones, music strings |
| `machines/apple2/apple2.cpp` | Apple II text screen, window, memory and DOS 3.3 command layer |
| `machines/apple2/graphics.cpp` | lo-res and hi-res statements, shape tables |
| `machines/apple2/routines.cpp` | machine code that recovered disks `CALL`, recognised and done natively (the carols' notes) |
| `machines/apple2/video.cpp` | screen memory to a picture; `font.h` is its character set |
| `machines/common/png.cpp` | PNG writer |
| `platform/posix/main.cpp` | POSIX host platform (terminal, files) and command line |
| `platform/posix/display_sdl.cpp` | the graphics window and sound device (SDL3); `display_none.cpp` without it |
| `platform/posix/av.cpp` | the host's framebuffer pages and sound stream for the gfx pack; `--png`, `--wav` |
| `platform/griffinjr/` | Griffin Jr. platform (BIOS console or serial port, files, framebuffer, sound, joystick) and its makefile |
| `demos/` | Griffin BASIC programs to look at and listen to |
| `tests/` | `run.sh`; `programs/` and `lang/` (msbasic), `applesoft/`, `graphics/`, `corpus/` (recovered programs), `griffin/` |
| `tests/run-griffinjr.sh` | runs `programs/`, `lang/` and `griffin/` on Griffin Jr. in its emulator; `tests/bench/` are its loop benchmarks |
| `tools/fbcompare.py` | compares a Griffin Jr. framebuffer dump with a host PNG |
| `tools/carols.py` | converts the Christmas Carols disk into `DATA` for `demos/carols.bas` |
| `tools/corpus.py` | checks the interpreter against the whole recovered collection |
| `tools/mkfont.py` | regenerates `font.h` from a font image |

Code under `core/` and `dialects/` is also meant for the 68000: no exceptions, RTTI, iostreams or
node-based containers. The rules are at the top of `core/basic.h`.

## Adding a test

Put `NAME.bas` (or `NAME.cmd` for a direct-mode session) in one of the directories under `tests`,
with optional `NAME.in` and `NAME.args`. `GET` reads `NAME.in` one byte at a time, newlines included. Run `tests/run.sh build/basic --update`, read the new `NAME.out`
to confirm it is right, and keep it. Tests in `tests/graphics` and `tests/corpus/pictures` also get
a `NAME.png` of the screen; look at that too. So do tests in `tests/griffin/pictures`, and those
in `tests/griffin/sound` get a `NAME.wav` to listen to.
