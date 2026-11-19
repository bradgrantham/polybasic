# griffin-basic

A BASIC interpreter built from separable modules: a core, one front end per dialect, one platform
module per machine, and optional "machines" such as the Apple II.

@PLAN.md
@STATUS.md
@README.md

## Goals

1. **One interpreter, several dialects.** Microsoft 8K-style BASIC (for the Ahl listings),
   Applesoft, Integer BASIC, and Griffin BASIC, each a front end over the same core.
2. **A host build that runs every dialect.** Any POSIX machine with a C++23 compiler. With SDL3 it
   has a graphics window; without SDL3 it still builds, and graphics go to PNG files.
3. **Griffin and Griffin Jr. builds.** The same interpreter cross-compiled for the 68000, each
   linking only its own dialect and platform module. Griffin Jr. has 842 KB for program, bss and
   heap, so it sets the size rules for the core.
4. **Recovered Apple II programs run unchanged.** The host build models enough of an Apple II
   (text, lo-res, hi-res, memory locations, DOS 3.3 commands, common ROM calls) to run programs
   from the unpacked disk collection as they were found.
5. **The same graphics program runs on a host and on Griffin Jr.** A small set of drawing
   statements, the `gfx` extension pack, implemented over SDL3 on a host and over the framebuffer
   on Griffin Jr.
6. **A new platform costs one module.** Everything the core needs from a machine is in
   `core/platform.h`.
7. **Every phase ends with something runnable and a test that proves it.**

Not goals:

- Compiling BASIC programs to 68000 code. Only the interpreter is compiled.
- Machine-code routines that walk an Applesoft program's tokens (`&` handlers, most `USR` uses).
  Programs are held in the interpreter's own compiled form, not as tokens at `$0801`.
- Cycle-exact or ROM-exact Apple II behaviour. The aim is that programs behave recognisably.

## Rules for the code

- **`core/` and `dialects/` are also 68000 code.** C++23, no exceptions, no RTTI, no iostreams, no
  node-based standard containers, no `<initializer_list>`. The rules are at the top of
  `core/basic.h`.
- **BASIC errors leave by `longjmp`,** so no object with a destructor may be live across a call
  that can raise one.
- **Operands in the code stream are read bytewise or with `memcpy`,** because the 68000 faults on
  unaligned word access.
- **Numbers are `double` on every target,** by decision. On the 68000 that means soft-float.
- **The core's C library needs are small:** `malloc`/`realloc`, `setjmp`/`longjmp`, `<cmath>`, and
  `<cstdio>` for number formatting.
- **A missing capability is a return value, never a missing symbol.** Every platform defines every
  function in `core/platform.h`.

## How the pieces connect

- **A platform is one module.** `platform/posix/main.cpp` is the host's: it holds the platform
  functions, the command line, and the list of dialects in the build (`dialects[]`), and it
  configures the Apple II machine. A 68000 platform has its own `main` and `dialects[]` and does
  not link `machines/apple2` (the gfx pack borrows its `font.h`, a header).
- **Extension statements.** A dialect's keyword table marks a keyword `KW_EXT` or `KW_EXTFN` with
  an id and an argument signature (`dialects/msfamily.h`). The shared parser compiles the
  arguments, and the executor calls `Machine::ext` on the dialect's one `Machine`
  (`core/basic.h`). `dialects/applesoft.cpp` with `machines/apple2` is the worked example.
- **The gfx pack** (`machines/gfx`) is the griffin dialect's `Machine`: drawing, sprites, input
  devices and sound, all in portable code over the `plat_fb_*`, `plat_audio_*`, `plat_joystick`
  and related functions in `core/platform.h`. Pictures are drawn in software so they are the
  same on every machine; sound is generated as the program runs, never from an interrupt.
- **Extension packs that register themselves,** as the plan describes, are not built. How `gfx`
  and a later Griffin video pack combine in one dialect is undecided.
- **The display interface** (`plat_display_*` in `core/platform.h`) shows a framebuffer of
  `0x00RRGGBB` pixels and returns keys. `machines/common/png.cpp` turns the same framebuffer into
  a PNG with no library, and the same picture always gives the same bytes.
- **`Machine::poll`** is called on every backward jump. The Apple II machine uses it to keep its
  window alive and to pace programs.

## Conventions

- Answers come with references where possible; otherwise they say how sure they are ("I think
  that...", "I'm pretty sure but haven't checked that...").
- Generally commit after changes but ask when a commit might contain
  PII or keys or data otherwise not typically stored in git
- A command that might hang is run under `perl -e 'alarm 30; exec @ARGV' cmd ...`, not `timeout`,
  which not every host has. `tests/run.sh` does this.
- A new phase starts only on Brad's go-ahead.
- Update `STATUS.md` when a phase or part of one is finished. New open decisions go in
  `PLAN.md`.
- After `tests/run.sh build/basic --update`, each new `.out` is read and each new `.png` looked at
  before it is kept. A test that fails on a new compiler or host is a finding to report, not a
  reason to update the expected output.
- Much Apple II behaviour was written from memory, not from a ROM listing or the manuals: hi-res
  colour rules, shape tables, error codes, `CALL` addresses. `STATUS.md` says which. It is not
  presented as checked.
