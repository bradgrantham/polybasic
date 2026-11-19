// Apple II video: turn screen memory into a 560x384 picture (each Apple dot
// is 2x2 pixels).
//
// Hi-res colour follows the usual simplified NTSC rule, the same one the
// unpacker in apple2-disks-unpacked/_tools uses for its PNGs: a lit dot with a
// lit neighbour is white; a lone lit dot is violet or green by column (blue
// or orange if the byte's high bit is set); an unlit dot between two lit ones
// takes their colour.
#include "a2internal.h"

namespace basic {

namespace {

#include "font.h"

const uint32_t BLACK = 0x000000, WHITE = 0xFFFFFF;
const uint32_t VIOLET = 0xFF44FD, GREEN = 0x14F53C, BLUE = 0x14CFFD, ORANGE = 0xFF6A3C;

// Lo-res colours 0-15, from the same family as the hi-res four.
const uint32_t lores[16] = {
    0x000000, 0xE31E60, 0x604EBD, 0xFF44FD, 0x00A360, 0x9C9C9C, 0x14CFFD, 0xD0C3FF,
    0x607203, 0xFF6A3C, 0x9C9C9C, 0xFFA0D0, 0x14F53C, 0xD0DD8D, 0x72FFD0, 0xFFFFFF,
};

// One Apple scanline to two framebuffer rows.  A line is 280 dots, each two
// pixels wide, except 80-column text, which is 560 dots.
void put_line(uint32_t *fb, int y, const uint32_t *dots, bool wide)
{
    uint32_t *row = fb + y * 2 * A2_FB_W;
    for(int x = 0; x < A2_FB_W; x++) {
        row[x] = row[A2_FB_W + x] = dots[wide ? x : x / 2];
    }
}

void text_line(int y, bool flash_on, int cursor_row, int cursor_col, uint32_t *dots)
{
    int row = y / 8;
    const uint8_t *cells = a2_wide ? a2_text80[row] : &a2_mem[a2_text_row(row)];
    for(int col = 0; col < (a2_wide ? 80 : A2_COLS); col++) {
        // Screen codes: $00-$3F inverse, $40-$7F flashing, $80-$FF normal.
        uint8_t b = cells[col];
        bool inverse = b < 0x40 || (b < 0x80 && flash_on);
        uint8_t ch = b & 0x7F;
        if(b < 0x80) {
            ch = b & 0x3F;
            if(ch < 0x20) { ch |= 0x40; }
        } else if(ch < 0x20) {
            ch |= 0x40;
        }
        if(row == cursor_row && col == cursor_col) { inverse = !inverse; }
        uint8_t bits = font[ch - 32][y & 7];
        for(int k = 0; k < 7; k++) {
            dots[col * 7 + k] = (((bits >> k) & 1) != 0) != inverse ? WHITE : BLACK;
        }
    }
}

void lores_line(int y, uint32_t *dots)
{
    const uint8_t *cells = &a2_mem[a2_text_row(y / 8)];
    for(int col = 0; col < A2_COLS; col++) {
        uint32_t colour = lores[(y & 4) ? cells[col] >> 4 : cells[col] & 15];
        for(int k = 0; k < 7; k++) { dots[col * 7 + k] = colour; }
    }
}

void hires_line(int y, uint32_t *dots)
{
    const uint8_t *bytes = &a2_mem[(a2_video.page2 ? A2_HGR_PAGE2 : A2_HGR_PAGE1) + a2_hires_row(y)];
    bool lit[282], high[282];                     // one spare dot at each end
    lit[0] = lit[281] = high[0] = high[281] = false;
    for(int col = 0; col < A2_COLS; col++) {
        for(int k = 0; k < 7; k++) {
            lit[col * 7 + k + 1] = (bytes[col] >> k) & 1;
            high[col * 7 + k + 1] = bytes[col] >> 7;
        }
    }
    auto colour = [&](int x) {                    // x counts from 1
        if((x - 1) & 1) { return high[x] ? ORANGE : GREEN; }
        return high[x] ? BLUE : VIOLET;
    };
    for(int x = 1; x <= 280; x++) {
        if(lit[x]) {
            dots[x - 1] = (lit[x - 1] || lit[x + 1]) ? WHITE : colour(x);
        } else if(lit[x - 1] && lit[x + 1]) {
            dots[x - 1] = colour(x - 1);
        } else {
            dots[x - 1] = BLACK;
        }
    }
}

}  // namespace

// Text and lo-res always show page 1.  (A real Apple shows text page 2 after
// HGR2: TEXT, which is the program's own memory and never worth looking at.)
void a2_render(uint32_t *fb, bool flash_on, int cursor_row, int cursor_col)
{
    uint32_t dots[A2_FB_W];
    for(int y = 0; y < 192; y++) {
        bool wide = false;
        if(!a2_video.graphics || (a2_video.mixed && y >= 160)) {
            text_line(y, flash_on, cursor_row, cursor_col, dots);
            wide = a2_wide;
        } else if(a2_video.hires) {
            hires_line(y, dots);
        } else {
            lores_line(y, dots);
        }
        put_line(fb, y, dots, wide);
    }
}

}  // namespace basic
