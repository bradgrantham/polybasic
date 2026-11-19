// Shared between the Apple II machine's source files; not for anyone else.
//
// The screen is kept the way the Apple keeps it, in memory: text and lo-res
// in the text page at $400, hi-res pages at $2000 and $4000.  The statements
// change that memory and video.cpp turns it into a picture.
#ifndef BASIC_A2INTERNAL_H
#define BASIC_A2INTERNAL_H

#include "apple2.h"

namespace basic {

extern uint8_t a2_mem[0x10000];

// With the 80-column card on (PR#3) the text screen is this instead of the
// page at $400, which is how the real card's extra memory behaves as far as
// a BASIC program can tell.
extern uint8_t a2_text80[24][80];
extern bool a2_wide;

// The display soft switches ($C050-$C057).
struct A2Video {
    bool graphics;                // $C050 / $C051
    bool mixed;                   // $C053 / $C052: four lines of text at the bottom
    bool page2;                   // $C055 / $C054
    bool hires;                   // $C057 / $C056
};
extern A2Video a2_video;

enum { A2_COLS = 40, A2_ROWS = 24, A2_FB_W = 560, A2_FB_H = 384 };
enum : uint16_t {
    A2_TEXT_PAGE = 0x400, A2_HGR_PAGE1 = 0x2000, A2_HGR_PAGE2 = 0x4000,
    A2_ZP_ONERR = 0xD8,           // bit 7 set while ONERR GOTO is in effect
    A2_ZP_HPAGE = 0xE6,           // $20 or $40: the hi-res page being drawn on
    A2_ZP_SHAPES = 0xE8,          // address of the shape table, low byte first
};

// Where a text row and a hi-res row start within their pages.
inline uint16_t a2_text_row(int row)
{
    return static_cast<uint16_t>(A2_TEXT_PAGE + ((row & 7) << 7) + (row >> 3) * 40);
}
inline uint16_t a2_hires_row(int y)
{
    return static_cast<uint16_t>(((y & 7) << 10) | (((y >> 3) & 7) << 7) | ((y >> 6) * 40));
}

// graphics.cpp: the lo-res and hi-res statements and SCRN.  Returns false
// for an id that is not one of them.
bool a2_graphics_ext(Interp &in, uint16_t id, Value *args, int nargs, Value *result);
int32_t a2_int_arg(Interp &in, const Value &v);

// video.cpp: draw the screen the switches select into A2_FB_W * A2_FB_H
// pixels (0x00RRGGBB).  Flashing characters are inverse while flash_on; a
// cursor is drawn at (cursor_row, cursor_col) unless cursor_row is negative.
void a2_render(uint32_t *fb, bool flash_on, int cursor_row, int cursor_col);

}  // namespace basic

#endif
