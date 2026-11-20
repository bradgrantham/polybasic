// The Apple II as seen by a BASIC program: the text, lo-res and hi-res
// screens, the memory behind them, and the DOS 3.3 command layer.  The rest
// of memory, CALL and the paddles come in a later phase; until then they
// raise NOT AVAILABLE.
#ifndef BASIC_APPLE2_H
#define BASIC_APPLE2_H

#include "../../core/basic.h"

namespace basic {

// Extension statement and function ids (Keyword::id in the Applesoft table).
enum A2Ext : uint16_t {
    // Text screen.
    A2_HOME, A2_HTAB, A2_VTAB, A2_NORMAL, A2_INVERSE, A2_FLASH, A2_TEXT,
    // Accepted and ignored.
    A2_SPEED, A2_PR, A2_IN, A2_TRACE, A2_NOTRACE, A2_HIMEM, A2_LOMEM,
    // Graphics.
    A2_GR, A2_COLOR, A2_PLOT, A2_HLIN, A2_VLIN, A2_SCRN, A2_HGR, A2_HGR2, A2_HCOLOR,
    A2_HPLOT, A2_DRAW, A2_XDRAW, A2_ROT, A2_SCALE,
    // Memory.
    A2_PEEK, A2_POKE,
    // Not yet available.
    A2_SHLOAD, A2_CALL, A2_WAIT, A2_PDL, A2_USR,
};

extern const Machine apple2_machine;

struct Apple2Options {
    const char *disk;             // directory standing in for the disk; null: "."
    const char *scratch;          // where files a program writes go; null: write protected
    bool stream;                  // never take over the terminal as a 40x24 screen
    int window;                   // 1: open the window at once; -1: never; 0: on first graphics
    const char *png;              // write the last picture shown to this file; null: do not
    bool fast;                    // in a window, run at full speed
};
void apple2_configure(const Apple2Options &opt);

// True while a DOS EXEC file still has lines to give: they are what is
// "typed" next, at INPUT or at the prompt.  apple2_exec_cancel drops them.
bool apple2_exec_pending();
void apple2_exec_cancel();

}  // namespace basic

#endif
