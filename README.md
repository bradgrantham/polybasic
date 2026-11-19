# griffin-basic

A BASIC interpreter built from separable modules: a core runtime, one front end per dialect, and
one platform module per machine. See [PLAN.md](PLAN.md) for the design, the phases, and the current
status.

## Build and test

```
make            # build/basic
make test       # run tests/run.sh against it
```

Needs a C++23 compiler. SDL3 (found with `pkg-config`) is used for the graphics window; without
it, or with `make SDL3=no`, everything else still builds.

```
make corpus     # parse every recovered Applesoft program in ../../apple2-disks-unpacked
```

## Use

```
build/basic program.bas          # load, run, exit
build/basic                      # direct mode: type lines; QUIT to leave
build/basic --list-dialects      # msbasic, applesoft
build/basic --check program.bas  # compile only; list statements that do not compile
```

The dialect comes from `--dialect NAME`, or from a first line such as `REM APPLESOFT BASIC` (the
unpacker in `apple2-disks-unpacked/_tools` writes one), or else is `msbasic`.

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

Direct-mode commands: `RUN`, `LIST [from][-to]`, `NEW`, `LOAD "file"`, `SAVE "file"`, `CONT`, `QUIT`.
Ctrl-C interrupts a running program.

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
| `machines/apple2/apple2.cpp` | Apple II text screen, window, memory and DOS 3.3 command layer |
| `machines/apple2/graphics.cpp` | lo-res and hi-res statements, shape tables |
| `machines/apple2/video.cpp` | screen memory to a picture; `font.h` is its character set |
| `machines/common/png.cpp` | PNG writer |
| `platform/posix/main.cpp` | macOS / POSIX terminal platform and command line |
| `platform/posix/display_sdl.cpp` | the graphics window (SDL3); `display_none.cpp` without it |
| `tests/` | `run.sh`; `programs/` and `lang/` (msbasic), `applesoft/`, `graphics/`, `corpus/` (recovered programs) |
| `tools/corpus.py` | checks the interpreter against the whole recovered collection |
| `tools/mkfont.py` | regenerates `font.h` from a font image |

Code under `core/` and `dialects/` is also meant for the 68000: no exceptions, RTTI, iostreams or
node-based containers. The rules are at the top of `core/basic.h`.

## Adding a test

Put `NAME.bas` (or `NAME.cmd` for a direct-mode session) in one of the directories under `tests`,
with optional `NAME.in` and `NAME.args`. `GET` reads `NAME.in` one byte at a time, newlines included. Run `tests/run.sh build/basic --update`, read the new `NAME.out`
to confirm it is right, and keep it. Tests in `tests/graphics` and `tests/corpus/pictures` also get
a `NAME.png` of the screen; look at that too.
