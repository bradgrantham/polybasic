// The gfx extension pack: the Machine of the griffin dialect.  See gfx.h.
//
// Drawing is done here, in software, into a page of one-byte RGB332 pixels
// that the platform owns, so a picture is the same on every machine.  The
// first drawing statement of a run clears page 0 and puts it on display; when
// the program stops, the platform lets the picture be looked at and then
// brings the text console back.
#include "gfx.h"
#include "../../core/platform.h"
#include "../apple2/font.h"       // 7x8 characters for GPRINT

#include <cmath>
#include <cstdio>

namespace basic {

namespace {

enum { SPRITES = 32, SPRITE_MAX = 128 };

struct Sprite {
    uint8_t *pixels;              // null until defined
    int16_t w, h;
    int16_t clear;                // the colour that is not drawn, or -1
};

struct Gfx {
    bool tried, ok;               // plat_fb_open was called; it found a framebuffer
    bool shown;                   // graphics, not the text console, is on display
    int w, h;
    uint8_t *page[2];
    int draw, show;
    uint8_t fg, bg;
    Sprite sprite[SPRITES];
    bool timer_set;
    double timer_base;            // TIMER when plat_ticks_ms() was 0
    uint8_t polls;
} g;

// ---- arguments ---------------------------------------------------------------

int32_t arg_int(Interp &in, const Value &v)
{
    if(v.tag == T_INT) { return v.i; }
    double d = std::floor(v.d);
    if(!(d >= -2147483648.0 && d < 2147483648.0)) { raise(in, E_ILLEGAL_QUANTITY); }
    return static_cast<int32_t>(d);
}

int32_t arg_range(Interp &in, const Value &v, int32_t low, int32_t high)
{
    int32_t i = arg_int(in, v);
    if(i < low || i > high) { raise(in, E_ILLEGAL_QUANTITY); }
    return i;
}

uint8_t arg_colour(Interp &in, const Value &v)
{
    return static_cast<uint8_t>(arg_range(in, v, 0, 255));
}

// ---- the screen ---------------------------------------------------------------

// memset for long runs: the C library's on the 68000 goes a byte at a time,
// which makes clearing a page take four frames.  Sixteen bytes a turn here.
void fill(uint8_t *p, uint8_t c, size_t n)
{
    if(n < 32) {
        memset(p, c, n);
        return;
    }
    while(reinterpret_cast<uintptr_t>(p) & 3) {
        *p++ = c;
        n--;
    }
    uint32_t four = c * 0x01010101u;
    uint32_t *q = reinterpret_cast<uint32_t *>(__builtin_assume_aligned(p, 4));
    for(size_t k = n >> 4; k > 0; k--) {
        q[0] = four;
        q[1] = four;
        q[2] = four;
        q[3] = four;
        q += 4;
    }
    p = reinterpret_cast<uint8_t *>(q);
    for(n &= 15; n > 0; n--) { *p++ = c; }
}

// Every drawing statement comes through here first.
void graphics(Interp &in)
{
    if(!g.tried) {
        g.tried = true;
        g.ok = plat_fb_open(&g.w, &g.h);
        if(g.ok) {
            g.page[0] = plat_fb_page(0);
            g.page[1] = plat_fb_page(1);
        }
    }
    if(!g.ok) { raise(in, E_NOT_AVAILABLE); }
    if(!g.shown) {
        g.shown = true;
        g.draw = g.show = 0;
        g.fg = 255;
        g.bg = 0;
        fill(g.page[0], 0, static_cast<size_t>(g.w) * static_cast<size_t>(g.h));
        plat_fb_show(0);
    }
}

void text_screen(bool wait)
{
    if(g.shown) {
        g.shown = false;
        plat_fb_close(wait);
    }
}

// Where row y of the page being drawn on starts.  A 16-bit multiply: the
// 68000 has no other, and a 32-bit one is a library call for every dot.
inline uint8_t *row(int y)
{
    return g.page[g.draw] + static_cast<int16_t>(y) * static_cast<int16_t>(g.w);
}

inline void pset(int x, int y, uint8_t c)
{
    if(x >= 0 && x < g.w && y >= 0 && y < g.h) { row(y)[x] = c; }
}

void hline(int x0, int x1, int y, uint8_t c)
{
    if(y < 0 || y >= g.h) { return; }
    if(x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if(x0 < 0) { x0 = 0; }
    if(x1 >= g.w) { x1 = g.w - 1; }
    if(x0 <= x1) { fill(row(y) + x0, c, static_cast<size_t>(x1 - x0 + 1)); }
}

void vline(int x, int y0, int y1, uint8_t c)
{
    if(x < 0 || x >= g.w) { return; }
    if(y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if(y0 < 0) { y0 = 0; }
    if(y1 >= g.h) { y1 = g.h - 1; }
    uint8_t *p = row(y0) + x;
    for(int y = y0; y <= y1; y++, p += g.w) { *p = c; }
}

// Coordinates far off the screen would make a line or circle take for ever.
void sane(Interp &in, int32_t v)
{
    if(v < -16384 || v > 16383) { raise(in, E_ILLEGAL_QUANTITY); }
}

void line(int x0, int y0, int x1, int y1, uint8_t c)
{
    if(y0 == y1) { hline(x0, x1, y0, c); return; }
    if(x0 == x1) { vline(x0, y0, y1, c); return; }
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for(;;) {
        pset(x0, y0, c);
        if(x0 == x1 && y0 == y1) { break; }
        int e2 = 2 * err;
        if(e2 >= dy) { err += dy; x0 += sx; }
        if(e2 <= dx) { err += dx; y0 += sy; }
    }
}

void circle(int cx, int cy, int r, uint8_t c, bool fill)
{
    int x = r, y = 0, err = 1 - r;
    while(x >= y) {
        if(fill) {
            hline(cx - x, cx + x, cy + y, c);
            hline(cx - x, cx + x, cy - y, c);
            hline(cx - y, cx + y, cy + x, c);
            hline(cx - y, cx + y, cy - x, c);
        } else {
            pset(cx + x, cy + y, c); pset(cx - x, cy + y, c);
            pset(cx + x, cy - y, c); pset(cx - x, cy - y, c);
            pset(cx + y, cy + x, c); pset(cx - y, cy + x, c);
            pset(cx + y, cy - x, c); pset(cx - y, cy - x, c);
        }
        y++;
        if(err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gprint(int x, int y, const char *s, uint32_t len, uint8_t c)
{
    for(uint32_t k = 0; k < len; k++, x += 7) {
        uint8_t ch = static_cast<uint8_t>(s[k]) & 0x7F;
        if(ch < 32 || x <= -7 || x >= g.w) { continue; }
        const uint8_t *rows = font[ch - 32];
        for(int r = 0; r < 8; r++) {
            uint8_t bits = rows[r];
            for(int b = 0; bits; b++, bits >>= 1) {
                if(bits & 1) { pset(x + b, y + r, c); }
            }
        }
    }
}

// ---- sprites ------------------------------------------------------------------

void sprite_define(Interp &in, Value *a, int nargs)
{
    int n = arg_range(in, a[0], 0, SPRITES - 1);
    int w = arg_range(in, a[3], 1, SPRITE_MAX);
    int h = arg_range(in, a[4], 1, SPRITE_MAX);
    int x = arg_range(in, a[1], 0, g.w - w);
    int y = arg_range(in, a[2], 0, g.h - h);
    int clear = nargs > 5 ? arg_range(in, a[5], -1, 255) : 0;
    Sprite &s = g.sprite[n];
    uint8_t *pixels = static_cast<uint8_t *>(realloc(s.pixels, static_cast<size_t>(w * h)));
    if(!pixels) { raise(in, E_OUT_OF_MEMORY); }
    s.pixels = pixels;
    s.w = static_cast<int16_t>(w);
    s.h = static_cast<int16_t>(h);
    s.clear = static_cast<int16_t>(clear);
    for(int r = 0; r < h; r++) {
        memcpy(pixels + r * w, row(y + r) + x, static_cast<size_t>(w));
    }
}

void sprite_draw(Interp &in, Value *a)
{
    const Sprite &s = g.sprite[arg_range(in, a[0], 0, SPRITES - 1)];
    int x = arg_int(in, a[1]), y = arg_int(in, a[2]);
    if(!s.pixels) { raise(in, E_ILLEGAL_QUANTITY); }
    sane(in, x);
    sane(in, y);
    int c0 = x < 0 ? -x : 0, c1 = x + s.w > g.w ? g.w - x : s.w;
    int r0 = y < 0 ? -y : 0, r1 = y + s.h > g.h ? g.h - y : s.h;
    for(int r = r0; r < r1; r++) {
        const uint8_t *from = s.pixels + r * s.w + c0;
        uint8_t *to = row(y + r) + x + c0;
        if(s.clear < 0) {
            if(c1 > c0) { memcpy(to, from, static_cast<size_t>(c1 - c0)); }
            continue;
        }
        uint8_t clear = static_cast<uint8_t>(s.clear);
        for(int c = c0; c < c1; c++, from++, to++) {
            if(*from != clear) { *to = *from; }
        }
    }
}

// ---- the clock ------------------------------------------------------------------

// Seconds since midnight, with a fraction.  The time of day is read once and
// the platform's millisecond counter carries on from there.
double timer()
{
    if(!g.timer_set) {
        g.timer_set = true;
        g.timer_base = plat_clock() % 86400u - plat_ticks_ms() / 1000.0;
    }
    return g.timer_base + plat_ticks_ms() / 1000.0;
}

// "2026-11-19" or "14:05:09" from the clock.
int clock_text(bool date, char *out, size_t cap)
{
    uint32_t t = plat_clock();
    if(!date) {
        uint32_t s = t % 86400u;
        return snprintf(out, cap, "%02u:%02u:%02u", static_cast<unsigned>(s / 3600),
            static_cast<unsigned>(s / 60 % 60), static_cast<unsigned>(s % 60));
    }
    // Days since 1970-01-01 to a date in the Gregorian calendar.
    uint32_t z = t / 86400u + 719468u;
    uint32_t era = z / 146097u;
    uint32_t doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    uint32_t day = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t month = mp < 10u ? mp + 3u : mp - 9u;
    uint32_t year = yoe + era * 400u + (month <= 2u ? 1u : 0u);
    return snprintf(out, cap, "%04u-%02u-%02u", static_cast<unsigned>(year),
        static_cast<unsigned>(month), static_cast<unsigned>(day));
}

void result_text(Interp &in, Value *result, const char *s, uint32_t len)
{
    result->tag = T_STR;
    result->s = str_copy(in, s, len);
}

// ---- statements and functions -----------------------------------------------------

void gfx_ext(Interp &in, uint16_t id, Value *a, int nargs, Value *result)
{
    switch(id) {
        case GX_CLS:
            graphics(in);
            fill(g.page[g.draw], nargs > 0 ? arg_colour(in, a[0]) : g.bg,
                static_cast<size_t>(g.w) * static_cast<size_t>(g.h));
            break;
        case GX_COLOR: {
            uint8_t fg = arg_colour(in, a[0]);
            uint8_t bg = nargs > 1 ? arg_colour(in, a[1]) : g.bg;
            graphics(in);
            g.fg = fg;
            g.bg = bg;
            break;
        }
        case GX_PSET: {
            int x = arg_int(in, a[0]), y = arg_int(in, a[1]);
            uint8_t c = nargs > 2 ? arg_colour(in, a[2]) : g.fg;
            graphics(in);
            if(nargs <= 2) { c = g.fg; }
            pset(x, y, c);
            break;
        }
        case GX_LINE: {
            int v[4];
            for(int k = 0; k < 4; k++) {
                v[k] = arg_int(in, a[k]);
                sane(in, v[k]);
            }
            bool own = nargs > 4;
            uint8_t c = own ? arg_colour(in, a[4]) : 0;
            graphics(in);
            line(v[0], v[1], v[2], v[3], own ? c : g.fg);
            break;
        }
        case GX_RECT: case GX_FRECT: {
            int x = arg_int(in, a[0]), y = arg_int(in, a[1]);
            int w = arg_int(in, a[2]), h = arg_int(in, a[3]);
            sane(in, x);
            sane(in, y);
            sane(in, w);
            sane(in, h);
            bool own = nargs > 4;
            uint8_t c = own ? arg_colour(in, a[4]) : 0;
            graphics(in);
            if(!own) { c = g.fg; }
            if(w <= 0 || h <= 0) { break; }
            if(id == GX_FRECT) {
                for(int r = 0; r < h; r++) { hline(x, x + w - 1, y + r, c); }
            } else {
                hline(x, x + w - 1, y, c);
                hline(x, x + w - 1, y + h - 1, c);
                vline(x, y, y + h - 1, c);
                vline(x + w - 1, y, y + h - 1, c);
            }
            break;
        }
        case GX_CIRCLE: case GX_FCIRCLE: {
            int x = arg_int(in, a[0]), y = arg_int(in, a[1]);
            int r = arg_range(in, a[2], 0, 16383);
            sane(in, x);
            sane(in, y);
            bool own = nargs > 3;
            uint8_t c = own ? arg_colour(in, a[3]) : 0;
            graphics(in);
            circle(x, y, r, own ? c : g.fg, id == GX_FCIRCLE);
            break;
        }
        case GX_GPRINT: {
            int x = arg_int(in, a[0]), y = arg_int(in, a[1]);
            sane(in, x);
            sane(in, y);
            bool own = nargs > 3;
            uint8_t c = own ? arg_colour(in, a[3]) : 0;
            graphics(in);
            gprint(x, y, str_ptr(in, a[2].s), a[2].s.len, own ? c : g.fg);
            break;
        }
        case GX_PAGE: {
            int draw = arg_range(in, a[0], 0, 1), show = arg_range(in, a[1], 0, 1);
            graphics(in);
            g.draw = draw;
            if(show != g.show) {
                g.show = show;
                plat_fb_show(show);
            }
            break;
        }
        case GX_VSYNC: {
            int frames = nargs > 0 ? arg_range(in, a[0], 0, 3600) : 1;
            graphics(in);
            for(int k = 0; k < frames; k++) {
                plat_fb_vsync();
                if(plat_break_pending()) { raise(in, E_BREAK); }
            }
            break;
        }
        case GX_SPRITE:
            graphics(in);
            if(nargs >= 5) {
                sprite_define(in, a, nargs);
            } else {
                sprite_draw(in, a);
            }
            break;
        case GX_TEXT:
            text_screen(false);
            break;

        case GX_SOUND:
            if(nargs == 0) {
                sound_off();
            } else {
                double hertz = a[0].tag == T_INT ? a[0].i : a[0].d;
                int32_t ms = arg_range(in, a[1], 0, 600000);
                int volume = nargs > 2 ? arg_range(in, a[2], 0, 15) : 8;
                int voice = nargs > 3 ? arg_range(in, a[3], 1, SOUND_VOICES) : 1;
                if(!(hertz >= 0 && hertz <= 20000)) { raise(in, E_ILLEGAL_QUANTITY); }
                sound_tone(in, voice - 1, hertz, ms, volume);
            }
            break;
        case GX_BEEP:
            sound_tone(in, 0, 880, 120, 8);
            break;
        case GX_PLAY: {
            StrRef strings[SOUND_VOICES];
            for(int k = 0; k < nargs; k++) { strings[k] = a[k].s; }
            sound_play(in, strings, nargs);
            break;
        }

        case GX_RGB: {
            int32_t r = arg_range(in, a[0], 0, 255), gr = arg_range(in, a[1], 0, 255);
            int32_t b = arg_range(in, a[2], 0, 255);
            result->i = (r & 0xE0) | ((gr & 0xE0) >> 3) | (b >> 6);
            break;
        }
        case GX_POINT: {
            int x = arg_int(in, a[0]), y = arg_int(in, a[1]);
            graphics(in);
            result->i = x >= 0 && x < g.w && y >= 0 && y < g.h ? row(y)[x] : -1;
            break;
        }
        case GX_SCRW: case GX_SCRH:
            // The size can be asked for without switching the display over.
            if(!g.tried) {
                g.tried = true;
                g.ok = plat_fb_open(&g.w, &g.h);
                if(g.ok) {
                    g.page[0] = plat_fb_page(0);
                    g.page[1] = plat_fb_page(1);
                }
            }
            result->i = !g.ok ? 0 : (id == GX_SCRW ? g.w : g.h);
            break;
        case GX_INKEY: {
            int key = plat_poll_key();
            char ch = static_cast<char>(key);
            result_text(in, result, &ch, key >= 0 ? 1 : 0);
            break;
        }
        case GX_JOY: {
            int which = arg_range(in, a[0], 0, 3);
            int j = plat_joystick();
            if(which == 0) {
                result->i = ((j & PLAT_JOY_RIGHT) ? 1 : 0) - ((j & PLAT_JOY_LEFT) ? 1 : 0);
            } else if(which == 1) {
                result->i = ((j & PLAT_JOY_DOWN) ? 1 : 0) - ((j & PLAT_JOY_UP) ? 1 : 0);
            } else if(which == 2) {
                result->i = (j & PLAT_JOY_FIRE) ? 1 : 0;
            } else {
                result->i = j;
            }
            break;
        }
        case GX_PADDLE:
            result->i = plat_paddle(arg_range(in, a[0], 0, 1));
            break;
        case GX_PLAYING: {
            int voice = arg_range(in, a[0], 0, SOUND_VOICES);
            sound_pump();
            result->i = sound_playing(voice - 1) ? in.sem.true_value : 0;
            break;
        }
        case GX_TIMER:
            result->tag = T_NUM;
            result->d = timer();
            break;
        case GX_DATE: case GX_TIME: {
            char buf[16];
            int n = clock_text(id == GX_DATE, buf, sizeof(buf));
            result_text(in, result, buf, static_cast<uint32_t>(n));
            break;
        }
        default:
            raise(in, E_NOT_AVAILABLE);
    }
}

// ---- the console: straight through to the platform -----------------------------------

void gfx_write(Interp &in, const char *s, uint32_t len)
{
    plat_write(s, len);
    for(uint32_t k = 0; k < len; k++) {
        if(s[k] == '\n' || s[k] == '\r') {
            in.column = 0;
        } else {
            in.column++;
        }
    }
}

bool gfx_input_redirected(Interp &) { return false; }

int gfx_read_line(Interp &in, char *buf, int cap)
{
    int len = plat_read_line(buf, cap);
    in.column = 0;
    return len;
}

int gfx_read_key(Interp &) { return plat_read_key(); }

void gfx_reset_io(Interp &)
{
    text_screen(true);
}

void gfx_finish(Interp &in)
{
    sound_finish(in);
    text_screen(true);
}

void gfx_poll(Interp &)
{
    if(g.shown) { plat_fb_update(); }
    // Feeding the sound costs a system call on Griffin Jr., so not every time.
    if((++g.polls & 7) == 0) { sound_pump(); }
}

}  // namespace

extern const Machine gfx_machine = {
    gfx_write, gfx_input_redirected, gfx_read_line, gfx_read_key, gfx_ext, gfx_reset_io,
    gfx_finish, gfx_poll,
};

}  // namespace basic
