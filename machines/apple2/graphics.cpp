// Applesoft's lo-res and hi-res statements, drawing into screen memory the
// way the ROM does: lo-res blocks are nibbles of the text page, hi-res dots
// are bits of the page that location $E6 names.
//
// Written from memory of the Applesoft ROM, not checked against a listing:
// the colour bytes, the way a plotted dot also sets its byte's high bit, and
// the shape-table format should be exact.  Lines use Bresenham's algorithm,
// so a slanted line can differ from the ROM's by a dot here and there, and
// shapes drawn with ROT= values that are not multiples of 16 are close
// rather than identical.
#include "a2internal.h"

#include <cmath>

namespace basic {

namespace {

struct Gfx {
    uint8_t color;                // COLOR=, 0-15
    uint8_t hcolor;               // HCOLOR= as its colour byte
    uint8_t line_color;           // colour byte of the last dot positioned
    int hx, hy;                   // hi-res cursor
    uint8_t rot, scale;
    uint8_t collisions;           // dots of the last shape that met something
    bool ready;
};
Gfx g;

// HCOLOR= 0-7: black, green, violet, white, then the same with the high bit
// (black, orange, blue, white).
const uint8_t hcolor_bytes[8] = {0x00, 0x2A, 0x55, 0x7F, 0x80, 0xAA, 0xD5, 0xFF};

void setup()
{
    if(g.ready) { return; }
    g.ready = true;
    g.hcolor = g.line_color = 0x7F;
    g.scale = 1;
    if(a2_mem[A2_ZP_HPAGE] != 0x40) { a2_mem[A2_ZP_HPAGE] = 0x20; }
}

int32_t arg_in(Interp &in, const Value &v, int32_t lo, int32_t hi)
{
    int32_t n = a2_int_arg(in, v);
    if(n < lo || n > hi) { raise(in, E_ILLEGAL_QUANTITY); }
    return n;
}

// ---- lo-res ----------------------------------------------------------------------------

void lores_plot(int x, int y)
{
    uint8_t &cell = a2_mem[a2_text_row(y / 2) + x];
    if(y & 1) {
        cell = static_cast<uint8_t>((cell & 0x0F) | (g.color << 4));
    } else {
        cell = static_cast<uint8_t>((cell & 0xF0) | g.color);
    }
}

// ---- hi-res ----------------------------------------------------------------------------

uint8_t *hires_byte(int x, int y)
{
    uint16_t page = a2_mem[A2_ZP_HPAGE] == 0x40 ? A2_HGR_PAGE2 : A2_HGR_PAGE1;
    return &a2_mem[page + a2_hires_row(y) + x / 7];
}

// The colour byte as it is used in column x: the two-dot patterns swap
// between even and odd byte columns so that the colour stays the same.
uint8_t colour_at(uint8_t colour, int x)
{
    uint8_t pattern = colour & 0x7F;
    if(((x / 7) & 1) && (pattern == 0x2A || pattern == 0x55)) { colour ^= 0x7F; }
    return colour;
}

void hires_plot(int x, int y, uint8_t colour)
{
    uint8_t *b = hires_byte(x, y);
    uint8_t mask = static_cast<uint8_t>(0x80 | (1 << (x % 7)));
    *b = static_cast<uint8_t>(((*b ^ colour_at(colour, x)) & mask) ^ *b);
}

void hires_position(int x, int y)
{
    g.hx = x;
    g.hy = y;
    g.line_color = g.hcolor;
}

void hires_line_to(int x1, int y1)
{
    int x = g.hx, y = g.hy;
    int dx = x1 > x ? x1 - x : x - x1, dy = y1 > y ? y1 - y : y - y1;
    int sx = x1 > x ? 1 : -1, sy = y1 > y ? 1 : -1;
    int err = dx - dy;
    for(;;) {
        hires_plot(x, y, g.line_color);
        if(x == x1 && y == y1) { break; }
        int e2 = err * 2;
        if(e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if(e2 < dx) {
            err += dx;
            y += sy;
        }
    }
    g.hx = x1;
    g.hy = y1;
}

// ---- shapes ----------------------------------------------------------------------------

// 255 * cos(k * 90/16 degrees), as in the ROM.
const uint8_t cos_table[17] = {
    0xFF, 0xFE, 0xFA, 0xF4, 0xEC, 0xE1, 0xD4, 0xC5, 0xB4, 0xA1, 0x8D, 0x78, 0x61, 0x49, 0x31, 0x18, 0xFF,
};

void shape_move(int direction)
{
    switch(direction & 3) {
        case 0: g.hy = g.hy > 0 ? g.hy - 1 : 191; break;
        case 1: g.hx = g.hx < 279 ? g.hx + 1 : 0; break;
        case 2: g.hy = g.hy < 191 ? g.hy + 1 : 0; break;
        default: g.hx = g.hx > 0 ? g.hx - 1 : 279; break;
    }
}

// A shape is a string of bytes ending in zero.  Each byte holds up to three
// vectors, low bits first: two bits of direction (up, right, down, left) and
// a "plot before moving" bit; the third vector has no plot bit.  Vectors that
// leave nothing but zero bits above them are not there.
void draw_shape(Interp &in, int n, bool exclusive)
{
    uint16_t table = static_cast<uint16_t>(a2_mem[A2_ZP_SHAPES] | (a2_mem[A2_ZP_SHAPES + 1] << 8));
    if(n < 1 || n > a2_mem[table]) { raise(in, E_ILLEGAL_QUANTITY); }
    uint16_t entry = static_cast<uint16_t>(table + 2 * n);
    uint16_t p = static_cast<uint16_t>(table + (a2_mem[entry] | (a2_mem[static_cast<uint16_t>(entry + 1)] << 8)));

    int quadrant = (g.rot >> 4) & 3, fraction = g.rot & 15;
    int along = cos_table[fraction] + 1;                    // out of 256
    int across = (cos_table[16 - fraction] + 1) & 0xFF;
    int along_sum = 0x80, across_sum = 0x80;
    int steps = g.scale ? g.scale : 256;
    uint8_t colour = g.hcolor;

    // A dot collides if XDRAW turns it off, or if DRAW finds it already the
    // colour being drawn.  (From memory of the ROM, like the rest.)
    g.collisions = 0;
    for(uint8_t byte; (byte = a2_mem[p]) != 0; p++) {
        do {
            int direction = (byte & 3) + quadrant;
            for(int k = 0; k < steps; k++) {
                if(byte & 4) {
                    uint8_t *b = hires_byte(g.hx, g.hy);
                    uint8_t bit = static_cast<uint8_t>(1 << (g.hx % 7));
                    if(exclusive) {
                        *b ^= bit;
                        if(!(*b & bit)) { g.collisions++; }
                    } else {
                        if(((*b ^ colour_at(colour, g.hx)) & bit) == 0) { g.collisions++; }
                        hires_plot(g.hx, g.hy, colour);
                    }
                }
                along_sum += along;
                if(along_sum >= 256) {
                    along_sum -= 256;
                    shape_move(direction);
                }
                across_sum += across;
                if(across_sum >= 256) {
                    across_sum -= 256;
                    shape_move(direction + 1);
                }
            }
            byte >>= 3;
        } while(byte);
    }
}

}  // namespace

bool a2_graphics_peek(uint16_t addr, uint8_t *value)
{
    setup();
    switch(addr) {
        case 0x30: *value = static_cast<uint8_t>(g.color * 17); break;
        case 0xE0: *value = static_cast<uint8_t>(g.hx); break;
        case 0xE1: *value = static_cast<uint8_t>(g.hx >> 8); break;
        case 0xE2: *value = static_cast<uint8_t>(g.hy); break;
        case 0xE4: *value = g.hcolor; break;
        case 0xE7: *value = g.scale; break;
        case 0xEA: *value = g.collisions; break;
        case 0xF9: *value = g.rot; break;
        default: return false;
    }
    return true;
}

bool a2_graphics_poke(uint16_t addr, uint8_t value)
{
    setup();
    switch(addr) {
        case 0x30: g.color = value & 15; break;
        case 0xE0: g.hx = (g.hx & 0x100) | value; break;
        case 0xE1: g.hx = (g.hx & 0xFF) | ((value & 1) << 8); break;
        case 0xE2: g.hy = value; break;
        case 0xE4: g.hcolor = value; break;
        case 0xE7: g.scale = value; break;
        case 0xEA: g.collisions = value; break;
        case 0xF9: g.rot = value; break;
        default: return false;
    }
    // A cursor poked off the screen would send the next shape into other memory.
    if(g.hx > 279) { g.hx = 279; }
    if(g.hy > 191) { g.hy = 191; }
    return true;
}

void a2_hires_fill(bool with_colour)
{
    setup();
    for(int y = 0; y < 192; y++) {
        uint8_t *row = hires_byte(0, y);
        for(int col = 0; col < A2_COLS; col++) {
            row[col] = with_colour ? colour_at(g.line_color, col * 7) : 0;
        }
    }
}

int32_t a2_int_arg(Interp &in, const Value &v)
{
    if(v.tag == T_INT) { return v.i; }
    double f = std::floor(v.d);
    if(!(f >= -65535.0 && f < 65536.0)) { raise(in, E_ILLEGAL_QUANTITY); }
    return static_cast<int32_t>(f);
}

bool a2_graphics_ext(Interp &in, uint16_t id, Value *args, int nargs, Value *result)
{
    setup();
    switch(id) {
        case A2_GR:
            a2_video.graphics = true;
            a2_video.mixed = true;
            a2_video.hires = false;
            for(int row = 0; row < 20; row++) { memset(&a2_mem[a2_text_row(row)], 0, A2_COLS); }
            g.color = 0;
            break;
        case A2_COLOR:
            g.color = static_cast<uint8_t>(arg_in(in, args[0], 0, 255) & 15);
            break;
        case A2_PLOT: {
            int x = arg_in(in, args[0], 0, 39), y = arg_in(in, args[1], 0, 47);
            lores_plot(x, y);
            break;
        }
        case A2_HLIN: {
            int x0 = arg_in(in, args[0], 0, 39), x1 = arg_in(in, args[1], 0, 39);
            int y = arg_in(in, args[2], 0, 47);
            for(int x = x0 < x1 ? x0 : x1; x <= (x0 < x1 ? x1 : x0); x++) { lores_plot(x, y); }
            break;
        }
        case A2_VLIN: {
            int y0 = arg_in(in, args[0], 0, 47), y1 = arg_in(in, args[1], 0, 47);
            int x = arg_in(in, args[2], 0, 39);
            for(int y = y0 < y1 ? y0 : y1; y <= (y0 < y1 ? y1 : y0); y++) { lores_plot(x, y); }
            break;
        }
        case A2_SCRN: {
            int x = arg_in(in, args[0], 0, 39), y = arg_in(in, args[1], 0, 47);
            uint8_t cell = a2_mem[a2_text_row(y / 2) + x];
            result->tag = T_INT;
            result->i = (y & 1) ? cell >> 4 : cell & 15;
            break;
        }
        case A2_HGR: case A2_HGR2: {
            bool second = id == A2_HGR2;
            a2_video.graphics = true;
            a2_video.hires = true;
            a2_video.page2 = second;
            a2_video.mixed = !second;
            a2_mem[A2_ZP_HPAGE] = second ? 0x40 : 0x20;
            memset(&a2_mem[second ? A2_HGR_PAGE2 : A2_HGR_PAGE1], 0, 0x2000);
            break;
        }
        case A2_HCOLOR:
            g.hcolor = hcolor_bytes[arg_in(in, args[0], 0, 7)];
            break;
        case A2_HPLOT: {
            int x = arg_in(in, args[1], 0, 279), y = arg_in(in, args[2], 0, 191);
            if(args[0].i) {
                hires_line_to(x, y);
            } else {
                hires_position(x, y);
                hires_plot(x, y, g.line_color);
            }
            break;
        }
        case A2_DRAW: case A2_XDRAW: {
            int n = arg_in(in, args[0], 0, 255);
            if(nargs == 3) {
                int x = arg_in(in, args[1], 0, 279), y = arg_in(in, args[2], 0, 191);
                hires_position(x, y);
            }
            draw_shape(in, n, id == A2_XDRAW);
            break;
        }
        case A2_ROT:
            g.rot = static_cast<uint8_t>(arg_in(in, args[0], 0, 255));
            break;
        case A2_SCALE:
            g.scale = static_cast<uint8_t>(arg_in(in, args[0], 0, 255));
            break;
        default:
            return false;
    }
    return true;
}

}  // namespace basic
