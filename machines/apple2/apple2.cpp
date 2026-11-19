// Apple II machine: text screen, display back ends, memory and DOS 3.3
// commands.  (The graphics statements are in graphics.cpp, and video.cpp
// turns screen memory into a picture.)
//
// The text screen is the Apple's own 40x24 page of screen codes at $400, with
// a cursor, a window and normal/inverse/flash.  It reaches the user in these
// ways:
//
//   stream  characters go out in the order they are printed, as plain text.
//           Cursor moves (HTAB backwards, VTAB, HOME) start a new line, and
//           output wraps at column 40.  This is what a pipe or a file gets,
//           and what a terminal gets until the program positions the cursor.
//   screen  once a program uses HOME or VTAB on a terminal, the top-left 40x24
//           of the terminal mirrors the text page, updated with ANSI sequences.
//   window  the first graphics statement opens a window showing the whole
//           Apple screen, text included, and takes the keyboard there.  The
//           terminal goes back to stream output, so it keeps a transcript.
//   PNG     with --png the last graphics picture the program showed (or the
//           text screen, if it never showed one) is written to a file.
//
// Memory: PEEK and POKE reach the screen pages, free RAM and the few
// locations graphics needs.  Everything else waits for a later phase.
//
// DOS: a Ctrl-D printed at the start of a line (straight after a carriage
// return, as DOS 3.3 requires) begins a command, which runs to the end of the
// line.
// The disk is a directory.  Files are looked for in the scratch directory,
// then in <disk>/raw (exact Apple bytes, as the unpacker leaves them), then in
// <disk>.  Anything written goes to the scratch directory; without one the
// disk is write protected.
#include "a2internal.h"
#include "../common/png.h"
#include "../../core/platform.h"

#include <cmath>
#include <cstdio>

namespace basic {

uint8_t a2_mem[0x10000];
uint8_t a2_text80[24][80];
bool a2_wide;
A2Video a2_video;

namespace {

enum { ROWS = A2_ROWS, MAX_COLS = 80, MAX_FILES = 8 };
enum : uint8_t { WIN_AUTO, WIN_ALWAYS, WIN_NEVER };
enum : uint8_t { M_NORMAL, M_INVERSE, M_FLASH };

struct File {
    bool open;
    bool dirty;
    char name[64];                // as the program named it, normalised
    Vec<char> data;               // 7-bit text, '\n' ends a field, 0 = never written
    uint32_t pos;
    uint32_t reclen;              // OPEN ...,L<n>; 0 for a sequential file
};

struct State {
    bool ready;

    // Text screen.
    uint16_t shown[ROWS][MAX_COLS];   // screen codes the terminal shows; 0xFFFF: unknown
    int ch, cv;                   // cursor, ch relative to the window's left edge
    int left, width, top, bottom; // text window; bottom is one past the last row
    uint8_t mode;
    bool terminal;                // output is a terminal
    bool may_screen;              // allowed to take the terminal over
    bool screen;                  // have taken it over
    bool need_clear;
    int stream_col;               // stream mode: column the output is really at
    bool moved;                   // stream mode: cursor moved, break the line before printing
    bool silent;                  // update the model only (echo of typed input)

    // Keyboard, as $C000 shows it: the last key, with bit 7 set until $C010
    // is touched.
    uint8_t kbd;
    bool kbd_eof;                 // scripted input has run out

    // Window and PNG.
    uint8_t window_mode;
    bool window;                  // the window is open
    bool closed;                  // the user closed it
    bool dirty;                   // the screen changed since it was last shown
    bool flash_shown, cursor_shown;
    bool fast;                    // do not slow down to an Apple's pace
    uint32_t last_pump;           // plat_ticks_ms of the last look at the window
    uint64_t epoch_us, apple_us;  // when the pacing clock started, and Apple time since
    char png[512];
    bool snapped;                 // a PNG has been written

    // DOS.
    bool collecting;
    bool mid_line;                // the last character printed was not a return
    char cmd[256];
    uint32_t cmd_len;
    int reading, writing;         // file index, or -1
    File files[MAX_FILES];
    char disk[400], scratch[400];
    bool has_scratch;
    bool stream_only;
};

State a2;

uint32_t framebuffer[A2_FB_W * A2_FB_H];

int cols() { return a2_wide ? 80 : 40; }

uint8_t &cell(int row, int col)
{
    return a2_wide ? a2_text80[row][col] : a2_mem[a2_text_row(row) + col];
}

void open_window();

void setup()
{
    if(a2.ready) { return; }
    a2.ready = true;
    for(int r = 0; r < ROWS; r++) { memset(&cell(r, 0), 0xA0, 40); }
    a2.width = 40;
    a2.bottom = ROWS;
    a2.reading = a2.writing = -1;
    a2.terminal = plat_is_terminal();
    a2.may_screen = a2.terminal && !a2.stream_only;
    if(!a2.disk[0]) { strcpy(a2.disk, "."); }
    if(a2.window_mode == WIN_ALWAYS) { open_window(); }
}

// ---- text screen ------------------------------------------------------------------

void emit(const char *s) { plat_write(s, static_cast<uint32_t>(strlen(s))); }

// Stream mode: honour a pending cursor move by starting a new line.
void stream_break()
{
    if(a2.moved && a2.stream_col != 0) {
        emit("\n");
        a2.stream_col = 0;
    }
    a2.moved = false;
}

void scroll()
{
    for(int r = a2.top; r < a2.bottom - 1; r++) {
        memcpy(&cell(r, a2.left), &cell(r + 1, a2.left), static_cast<size_t>(a2.width));
    }
    memset(&cell(a2.bottom - 1, a2.left), 0xA0, static_cast<size_t>(a2.width));
    a2.dirty = true;
}

// A character as the screen code for the current mode: normal has the high
// bit set, inverse is $00-$3F, flashing $40-$7F.
uint8_t screen_code(uint8_t c)
{
    switch(a2.mode) {
        case M_INVERSE: return c & 0x3F;
        case M_FLASH: return static_cast<uint8_t>((c & 0x3F) | 0x40);
        default: return c | 0x80;
    }
}

// The other way: the ASCII character a screen code shows, and how.
char screen_char(uint8_t code, uint8_t *mode)
{
    *mode = code >= 0x80 ? M_NORMAL : code >= 0x40 ? M_FLASH : M_INVERSE;
    uint8_t c = code >= 0x80 ? code & 0x7F : code & 0x3F;
    if(c < 0x20) { c |= 0x40; }
    return c == 0x7F ? ' ' : static_cast<char>(c);
}

void newline()
{
    if(!a2.screen && !a2.silent) {
        stream_break();
        emit("\n");
        a2.stream_col = 0;
    }
    a2.ch = 0;
    if(++a2.cv >= a2.bottom) {
        scroll();
        a2.cv = a2.bottom - 1;
    }
}

void put(uint8_t c)
{
    c &= 0x7F;
    if(c == '\n' || c == '\r') {
        newline();
        return;
    }
    if(c == 8) {
        if(a2.ch > 0) {
            a2.ch--;
            if(!a2.screen && !a2.silent && a2.stream_col > 0) {
                emit("\b");
                a2.stream_col--;
            }
        }
        return;
    }
    if(c == 7) {
        if(a2.terminal && !a2.silent) { emit("\a"); }
        return;
    }
    if(c < 32 || c == 0x7F) { return; }
    if(!a2.screen && !a2.silent) {
        stream_break();
        while(a2.stream_col < a2.ch) {
            emit(" ");
            a2.stream_col++;
        }
        char one = static_cast<char>(c);
        plat_write(&one, 1);
        a2.stream_col++;
    }
    cell(a2.cv, a2.left + a2.ch) = screen_code(c);
    a2.dirty = true;
    if(++a2.ch >= a2.width) { newline(); }
}

// Screen mode: make the terminal match the text page.  Rows that graphics
// covers are shown blank.
void sync()
{
    static Vec<char> out;
    char seq[32];
    out.clear();
    auto add = [](const char *s) {
        for(; *s; s++) { out.push(*s); }
    };
    if(a2.need_clear) {
        add("\033[0m\033[2J");
        memset(a2.shown, 0xFF, sizeof(a2.shown));
        a2.need_clear = false;
    }
    int tr = -1, tc = -1;
    uint8_t cur = M_NORMAL;
    for(int r = 0; r < ROWS; r++) {
        for(int c = 0; c < cols(); c++) {
            bool hidden = a2_video.graphics && !(a2_video.mixed && r >= 20);
            uint8_t code = hidden ? 0xA0 : cell(r, c);
            if(code == a2.shown[r][c]) { continue; }
            uint8_t mode;
            char ch = screen_char(code, &mode);
            if(tr != r || tc != c) {
                snprintf(seq, sizeof(seq), "\033[%d;%dH", r + 1, c + 1);
                add(seq);
            }
            if(mode != cur) {
                cur = mode;
                add(cur == M_INVERSE ? "\033[0;7m" : cur == M_FLASH ? "\033[0;5;7m" : "\033[0m");
            }
            out.push(ch);
            a2.shown[r][c] = code;
            tr = r;
            tc = c + 1;
        }
    }
    if(cur != M_NORMAL) { add("\033[0m"); }
    snprintf(seq, sizeof(seq), "\033[%d;%dH", a2.cv + 1, a2.left + a2.ch + 1);
    add(seq);
    plat_write(out.d, out.n);
}

void enter_screen()
{
    if(a2.may_screen && !a2.screen) {
        a2.screen = true;
        a2.need_clear = true;
    }
}

// ---- window and PNG ------------------------------------------------------------------

void open_window()
{
    if(a2.window || a2.closed || a2.window_mode == WIN_NEVER) { return; }
    if(a2.window_mode == WIN_AUTO && !a2.terminal) { return; }
    if(!plat_display_open(A2_FB_W, A2_FB_H, "Apple II")) {
        a2.window_mode = WIN_NEVER;
        return;
    }
    // The terminal goes back to being a transcript.
    if(a2.screen) {
        char seq[32];
        sync();
        snprintf(seq, sizeof(seq), "\033[0m\033[%d;1H\n", ROWS);
        emit(seq);
        a2.screen = false;
        a2.stream_col = 0;
        a2.moved = false;
    }
    a2.may_screen = false;
    a2.window = true;
    a2.dirty = true;
    a2.epoch_us = static_cast<uint64_t>(plat_ticks_ms()) * 1000;
    a2.apple_us = 0;
}

// Bring the window up to date.  While waiting for a key the cursor blinks.
void show(bool waiting)
{
    bool flash_on = (plat_ticks_ms() / 267) & 1;
    bool cursor = waiting && flash_on;
    if(!a2.dirty && flash_on == a2.flash_shown && cursor == a2.cursor_shown) { return; }
    a2_render(framebuffer, flash_on, cursor ? a2.cv : -1, a2.left + a2.ch);
    plat_display_show(framebuffer);
    a2.dirty = false;
    a2.flash_shown = flash_on;
    a2.cursor_shown = cursor;
}

// Look at the window's events: Ctrl-C there, or closing it, stops the program.
void pump(Interp &in)
{
    int flags = plat_display_pump();
    if(flags & DISPLAY_CLOSED) {
        a2.window = false;
        a2.closed = true;
    }
    if(flags) { raise(in, E_BREAK); }
}

// A turn for the window, charged `cost` microseconds of Apple time.  Unless
// --fast was given the program is held to that pace, so delay loops and
// animation take about as long as they did.  The charges are rough: about a
// millisecond for a loop iteration and for a screen statement.
void tick(Interp &in, uint32_t cost)
{
    uint32_t now = plat_ticks_ms();
    if(!a2.fast) {
        uint64_t real = static_cast<uint64_t>(now) * 1000 - a2.epoch_us;
        a2.apple_us += cost;
        if(a2.apple_us > real + 20000) {
            show(false);
            plat_sleep_ms(static_cast<uint32_t>((a2.apple_us - real) / 1000));
            now = plat_ticks_ms();
        } else if(real > a2.apple_us + 100000) {
            a2.apple_us = real;                   // no credit for time spent waiting
        }
    }
    if(now - a2.last_pump >= 16) {
        a2.last_pump = now;
        pump(in);
        show(false);
    }
}

// The keyboard is the window's once it is open, unless input is scripted.
bool window_keyboard() { return a2.window && plat_input_is_terminal(); }

int window_key(Interp &in)
{
    for(;;) {
        pump(in);
        int key = plat_display_key();
        if(key >= 0) { return key; }
        show(true);
        plat_sleep_ms(5);
    }
}

char up(char c);
char typed(char c);

int window_line(Interp &in, char *buf, int cap)
{
    int n = 0;
    for(;;) {
        int key = window_key(in);
        if(key == '\r') { break; }
        if(key == 8 && n > 0) {
            // Unlike the Apple, rub the character out as well as backing up.
            n--;
            if(a2.ch > 0) {
                a2.ch--;
            } else if(a2.cv > a2.top) {
                a2.cv--;
                a2.ch = a2.width - 1;
            }
            cell(a2.cv, a2.left + a2.ch) = 0xA0;
            a2.dirty = true;
            a2.moved = true;
        }
        if(key < 32 || key > 126 || n >= cap - 1) { continue; }
        buf[n] = typed(static_cast<char>(key));
        put(static_cast<uint8_t>(buf[n++]));
    }
    buf[n] = 0;
    newline();
    a2.mid_line = false;
    in.column = a2.ch;
    return n;
}

void snapshot()
{
    if(!a2.png[0]) { return; }
    Vec<char> out;
    a2_render(framebuffer, true, -1, 0);
    png_encode(framebuffer, A2_FB_W, A2_FB_H, out);
    plat_write_file(a2.png, out.d, out.n);
    free(out.d);
    a2.snapped = true;
    a2.dirty = true;
}

// A display switch changed: graphics is what opens the window.
void video_changed()
{
    if(a2_video.graphics) { open_window(); }
    a2.dirty = true;
}

// ---- memory ---------------------------------------------------------------------------

// The code PEEK(222) gives an ONERR handler.  (From memory of the Applesoft
// and DOS manuals' tables.)
uint8_t error_code(uint8_t e)
{
    switch(e) {
        case E_NEXT_WITHOUT_FOR: return 0;
        case E_SYNTAX: return 16;
        case E_RETURN_WITHOUT_GOSUB: return 22;
        case E_OUT_OF_DATA: return 42;
        case E_ILLEGAL_QUANTITY: return 53;
        case E_OVERFLOW: return 69;
        case E_OUT_OF_MEMORY: return 77;
        case E_UNDEFD_STATEMENT: return 90;
        case E_BAD_SUBSCRIPT: return 107;
        case E_REDIMD_ARRAY: return 120;
        case E_DIVISION_BY_ZERO: return 133;
        case E_TYPE_MISMATCH: return 163;
        case E_STRING_TOO_LONG: return 176;
        case E_FORMULA_TOO_COMPLEX: return 191;
        case E_UNDEFD_FUNCTION: return 224;
        case E_DOS_LANGUAGE_NOT_AVAILABLE: return 1;
        case E_DOS_RANGE: return 2;
        case E_DOS_WRITE_PROTECTED: return 4;
        case E_DOS_END_OF_DATA: return 5;
        case E_DOS_FILE_NOT_FOUND: return 6;
        case E_DOS_IO: return 8;
        case E_DOS_SYNTAX: return 11;
        case E_DOS_NO_BUFFERS: return 12;
        case E_DOS_FILE_TYPE_MISMATCH: return 13;
        case E_BREAK: return 255;
        default: return 16;
    }
}

// PR#3 switches the 80-column card on and PR#0 back to 40 columns; either
// clears the screen.  Other slots have nothing in them.
void select_output(int slot)
{
    bool wide = slot == 3;
    if((slot != 0 && slot != 3) || wide == a2_wide) { return; }
    a2_wide = wide;
    for(int r = 0; r < ROWS; r++) { memset(&cell(r, 0), 0xA0, static_cast<size_t>(cols())); }
    a2.left = 0;
    a2.width = cols();
    a2.top = 0;
    a2.bottom = ROWS;
    a2.ch = a2.cv = 0;
    a2.moved = true;
    a2.need_clear = true;
    a2.dirty = true;
}

// Fill the keyboard latch if a key has been typed.
void poll_keyboard()
{
    if(a2.kbd & 0x80) { return; }
    int key = window_keyboard() ? plat_display_key() : plat_poll_key();
    if(key == -2) { a2.kbd_eof = true; }
    if(key < 0) { return; }
    if(key == '\n') { key = '\r'; }
    if(key == 127) { key = 8; }
    a2.kbd = static_cast<uint8_t>(typed(static_cast<char>(key & 0x7F))) | 0x80;
}

uint8_t peek(Interp &in, uint16_t addr);
void a2_ext(Interp &in, uint16_t id, Value *args, int nargs, Value *result);

// WAIT addr,mask[,flip]: until (PEEK(addr) XOR flip) AND mask is not zero.
void wait(Interp &in, uint16_t addr, int32_t mask, int32_t flip)
{
    if(a2.screen) { sync(); }
    while(((peek(in, addr) ^ flip) & mask & 0xFF) == 0) {
        // Waiting for a key that scripted input will never supply.
        if(a2.kbd_eof || plat_break_pending()) { raise(in, E_BREAK); }
        if(a2.window) {
            pump(in);
            show(true);
        }
        plat_sleep_ms(5);
    }
}

// CALL: the Monitor routines programs use for the screen, done natively.
void call(Interp &in, uint16_t addr)
{
    switch(addr) {
        case 0xFC58:                              // HOME
            a2_ext(in, A2_HOME, nullptr, 0, nullptr);
            break;
        case 0xFC42: case 0xFC9C: {               // clear to end of screen, of line
            int last = addr == 0xFC42 ? a2.bottom - 1 : a2.cv;
            for(int r = a2.cv; r <= last; r++) {
                int from = r == a2.cv ? a2.ch : 0;
                memset(&cell(r, a2.left + from), 0xA0, static_cast<size_t>(a2.width - from));
            }
            break;
        }
        case 0xFC70:                              // scroll the window up a line
            scroll();
            break;
        case 0xFF3A:                              // bell
            put(7);
            break;
        case 0xF328:                              // tidy the stack after an ONERR; nothing to tidy here
            break;
        default:
            raise(in, E_NOT_AVAILABLE);
    }
}


// Memory that is only memory: page 3 (where short machine-code routines and
// shape tables are kept), the text page, and everything above it up to the
// I/O space: free RAM and the hi-res pages.
bool plain_ram(uint16_t addr)
{
    return (addr >= 0x300 && addr < 0x3D0) || (addr >= A2_TEXT_PAGE && addr < 0xC000)
        || addr == A2_ZP_HPAGE || addr == A2_ZP_SHAPES || addr == A2_ZP_SHAPES + 1
        || addr == 0xD6;                          // the "run on any command" flag
}

uint16_t address(Interp &in, const Value &v)
{
    int32_t n = a2_int_arg(in, v);
    if(n < 0) { n += 65536; }                     // POKE -16304,0 style
    if(n < 0 || n > 65535) { raise(in, E_ILLEGAL_QUANTITY); }
    return static_cast<uint16_t>(n);
}

// The display switches act on any access, read or write.
bool video_switch(uint16_t addr)
{
    if(addr < 0xC050 || addr > 0xC057) { return false; }
    if(addr == 0xC051 && a2_video.graphics) { snapshot(); }
    bool on = addr & 1;
    switch(addr & 6) {
        case 0: a2_video.graphics = !on; break;
        case 2: a2_video.mixed = on; break;
        case 4: a2_video.page2 = on; break;
        default: a2_video.hires = on; break;
    }
    video_changed();
    return true;
}

uint8_t peek(Interp &in, uint16_t addr)
{
    if(video_switch(addr)) { return 0; }
    if(addr >= 0xC000 && addr <= 0xC00F) {
        poll_keyboard();
        return a2.kbd;
    }
    if(addr >= 0xC010 && addr <= 0xC01F) {
        a2.kbd &= 0x7F;
        return 0;
    }
    switch(addr) {
        case 0x20: return static_cast<uint8_t>(a2.left);
        case 0x21: return static_cast<uint8_t>(a2.width);
        case 0x22: return static_cast<uint8_t>(a2.top);
        case 0x23: return static_cast<uint8_t>(a2.bottom);
        case 0x24: return static_cast<uint8_t>(a2.ch);
        case 0x25: return static_cast<uint8_t>(a2.cv);
        case 0xDA: return static_cast<uint8_t>(in.err_line);
        case 0xDB: return static_cast<uint8_t>(in.err_line >> 8);
        case 0xDE: return error_code(in.err);
        default: break;
    }
    if(addr == A2_ZP_ONERR) { return in.onerr != NO_TARGET ? 0x80 : 0; }
    if(!plain_ram(addr)) { raise(in, E_NOT_AVAILABLE); }
    return a2_mem[addr];
}

void poke(Interp &in, uint16_t addr, uint8_t value)
{
    if(video_switch(addr)) { return; }
    if(addr >= 0xC010 && addr <= 0xC01F) {
        a2.kbd &= 0x7F;
        return;
    }
    // The text window and cursor, kept within the screen whatever is poked.
    int v = value;
    switch(addr) {
        case 0x20: a2.left = v < cols() ? v : cols() - 1; break;
        case 0x21: a2.width = v < 1 ? 1 : v; break;
        case 0x22: a2.top = v < ROWS ? v : ROWS - 1; break;
        case 0x23: a2.bottom = v > ROWS ? ROWS : v; break;
        case 0x24: a2.ch = v; a2.moved = true; break;
        case 0x25: a2.cv = v < ROWS ? v : ROWS - 1; a2.moved = true; break;
        default: v = -1; break;
    }
    if(v >= 0) {
        if(a2.left + a2.width > cols()) { a2.width = cols() - a2.left; }
        if(a2.bottom <= a2.top) { a2.bottom = a2.top + 1; }
        if(a2.ch >= a2.width) { a2.ch = a2.width - 1; }
        return;
    }
    if(addr == A2_ZP_ONERR) {
        if(value < 0x80) { in.onerr = NO_TARGET; }    // POKE 216,0 turns ONERR GOTO off
        return;
    }
    if(!plain_ram(addr)) { raise(in, E_NOT_AVAILABLE); }
    a2_mem[addr] = value;
}

// ---- DOS: finding files ---------------------------------------------------------------

char up(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }

// A typed character as the program gets it: upper case, as on an Apple II or
// II+, unless the 80-column card is on, which means a //e with lower case.
char typed(char c) { return a2_wide ? c : up(c); }

bool same_name(const char *a, const char *b)
{
    for(; *a && *b; a++, b++) {
        if(up(*a) != up(*b)) { return false; }
    }
    return *a == *b;
}

// A file name the way the unpacker writes it: no control characters, no
// surrounding blanks, and '_' for the characters a host path cannot hold.
void normalise(const char *name, char *out, uint32_t cap)
{
    uint32_t n = 0;
    while(*name == ' ') { name++; }
    for(; *name && n < cap - 1; name++) {
        char c = static_cast<char>(*name & 0x7F);
        if(c < 0x20 || c == 0x7F) { continue; }
        out[n++] = (c == '/' || c == ':' || c == '\\') ? '_' : c;
    }
    while(n > 0 && out[n - 1] == ' ') { n--; }
    out[n] = 0;
}

struct Search {
    const char *want;             // name plus extension
    char got[128];
    bool found;
};

void search_entry(void *ctx, const char *entry)
{
    Search *s = static_cast<Search *>(ctx);
    if(!s->found && same_name(entry, s->want)) {
        snprintf(s->got, sizeof(s->got), "%s", entry);
        s->found = true;
    }
}

// Look in `dir` for name+ext, ignoring case; on success write its path.
bool find_in(const char *dir, const char *name, const char *ext, char *path, uint32_t cap)
{
    char want[128];
    Search s;
    snprintf(want, sizeof(want), "%s%s", name, ext);
    s.want = want;
    s.found = false;
    if(!plat_list_dir(dir, search_entry, &s) || !s.found) { return false; }
    snprintf(path, cap, "%s/%s", dir, s.got);
    return true;
}

enum Want { W_TEXT, W_PROGRAM, W_ANY };

bool resolve(const char *name, Want want, char *path, uint32_t cap)
{
    char raw[420];
    snprintf(raw, sizeof(raw), "%s/raw", a2.disk);
    if(want == W_PROGRAM || want == W_ANY) {
        if(a2.has_scratch && find_in(a2.scratch, name, ".bas", path, cap)) { return true; }
        if(find_in(a2.disk, name, ".bas", path, cap)) { return true; }
        if(want == W_PROGRAM) { return false; }
    }
    if(a2.has_scratch && find_in(a2.scratch, name, ".txt", path, cap)) { return true; }
    if(a2.has_scratch && find_in(a2.scratch, name, "", path, cap)) { return true; }
    if(find_in(raw, name, "", path, cap)) { return true; }
    if(find_in(a2.disk, name, ".txt", path, cap)) { return true; }
    return find_in(a2.disk, name, "", path, cap);
}

// ---- DOS: open files ------------------------------------------------------------------

int find_open(const char *name)
{
    for(int k = 0; k < MAX_FILES; k++) {
        if(a2.files[k].open && same_name(a2.files[k].name, name)) { return k; }
    }
    return -1;
}

int open_file(Interp &in, const char *name, int32_t reclen)
{
    if(!*name) { raise(in, E_DOS_SYNTAX); }
    int k = find_open(name);
    if(k >= 0) {
        a2.files[k].pos = 0;
        if(reclen > 0) { a2.files[k].reclen = static_cast<uint32_t>(reclen); }
        return k;
    }
    for(k = 0; k < MAX_FILES && a2.files[k].open; k++) { }
    if(k == MAX_FILES) { raise(in, E_DOS_NO_BUFFERS); }
    File &f = a2.files[k];
    f.open = true;
    f.dirty = false;
    snprintf(f.name, sizeof(f.name), "%s", name);
    f.data.clear();
    f.pos = 0;
    f.reclen = reclen > 0 ? static_cast<uint32_t>(reclen) : 0;
    char path[560];
    if(resolve(name, W_TEXT, path, sizeof(path))) {
        uint32_t len;
        char *d = plat_read_file(path, &len);
        if(!d) { raise(in, E_DOS_IO); }
        f.data.reserve(len);
        for(uint32_t i = 0; i < len; i++) {
            char c = static_cast<char>(d[i] & 0x7F);
            f.data.push(c == '\r' ? '\n' : c);
        }
        free(d);
    }
    return k;
}

void close_file(Interp &in, int k)
{
    File &f = a2.files[k];
    if(!f.open) { return; }
    f.open = false;
    if(a2.reading == k) { a2.reading = -1; }
    if(a2.writing == k) { a2.writing = -1; }
    if(f.dirty && a2.has_scratch) {
        char path[560];
        snprintf(path, sizeof(path), "%s/%s.txt", a2.scratch, f.name);
        f.dirty = false;
        if(!plat_write_file(path, f.data.d ? f.data.d : "", f.data.n)) { raise(in, E_DOS_IO); }
    }
}

bool at_end_of_data(const File &f) { return f.pos >= f.data.n || f.data[f.pos] == 0; }

void file_put(uint8_t c)
{
    File &f = a2.files[a2.writing];
    c &= 0x7F;
    if(c == '\r') { c = '\n'; }
    if(f.pos >= f.data.n) {
        uint32_t old = f.data.n;
        f.data.resize(f.pos + 1);
        memset(f.data.d + old, 0, f.pos + 1 - old);
    }
    f.data[f.pos++] = static_cast<char>(c);
    f.dirty = true;
}

int file_line(Interp &in, char *buf, int cap)
{
    File &f = a2.files[a2.reading];
    if(at_end_of_data(f)) { raise(in, E_DOS_END_OF_DATA); }
    int n = 0;
    while(!at_end_of_data(f) && f.data[f.pos] != '\n') {
        if(n < cap - 1) { buf[n++] = f.data[f.pos]; }
        f.pos++;
    }
    if(f.pos < f.data.n && f.data[f.pos] == '\n') { f.pos++; }
    buf[n] = 0;
    return n;
}

// ---- DOS: commands ---------------------------------------------------------------------

enum Verb {
    V_APPEND, V_BLOAD, V_BRUN, V_BSAVE, V_CATALOG, V_CHAIN, V_CLOSE, V_DELETE,
    V_EXEC, V_INIT, V_LOAD, V_LOCK, V_OPEN, V_POSITION, V_READ, V_RENAME,
    V_RUN, V_SAVE, V_UNLOCK, V_VERIFY, V_WRITE, V_DASH, V_PR, V_IGNORED,
};

const struct { const char *text; Verb verb; } verbs[] = {
    {"APPEND", V_APPEND}, {"BLOAD", V_BLOAD}, {"BRUN", V_BRUN}, {"BSAVE", V_BSAVE},
    {"CATALOG", V_CATALOG}, {"CAT", V_CATALOG}, {"CHAIN", V_CHAIN}, {"CLOSE", V_CLOSE},
    {"DELETE", V_DELETE}, {"EXEC", V_EXEC}, {"INIT", V_INIT}, {"LOAD", V_LOAD},
    {"LOCK", V_LOCK}, {"OPEN", V_OPEN}, {"POSITION", V_POSITION}, {"READ", V_READ},
    {"RENAME", V_RENAME}, {"RUN", V_RUN}, {"SAVE", V_SAVE}, {"UNLOCK", V_UNLOCK},
    {"VERIFY", V_VERIFY}, {"WRITE", V_WRITE}, {"-", V_DASH},
    // Accepted, with nothing to do here.
    {"FP", V_IGNORED}, {"INT", V_IGNORED}, {"MON", V_IGNORED}, {"NOMON", V_IGNORED},
    {"MAXFILES", V_IGNORED}, {"PR#", V_PR}, {"IN#", V_IGNORED},
    {"PREFIX", V_IGNORED}, {"FLUSH", V_IGNORED}, {"CREATE", V_IGNORED},
};

void print(const char *s)
{
    for(; *s; s++) { put(static_cast<uint8_t>(*s)); }
}

struct Listing {
    Vec<char> names;              // NUL-separated
    Vec<uint32_t> index;
};

void listing_add(void *ctx, const char *entry)
{
    Listing *l = static_cast<Listing *>(ctx);
    if(entry[0] == '.') { return; }
    l->index.push(l->names.n);
    for(const char *p = entry; ; p++) {
        l->names.push(*p);
        if(!*p) { break; }
    }
}

const char *sort_pool;
int by_name(const void *a, const void *b)
{
    return strcmp(sort_pool + *static_cast<const uint32_t *>(a),
        sort_pool + *static_cast<const uint32_t *>(b));
}

bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && same_name(s + n - t, tail);
}

// One catalog line per file.  With a raw/ directory (an unpacked disk) that
// is the list of files; otherwise the directory itself is.
void catalog_dir(const char *dir, bool is_raw)
{
    Listing l;
    char path[560], line[80];
    if(plat_list_dir(dir, listing_add, &l)) {
        sort_pool = l.names.d;
        qsort(l.index.d, l.index.n, sizeof(uint32_t), by_name);
        for(uint32_t k = 0; k < l.index.n; k++) {
            const char *entry = l.names.d + l.index[k];
            char type = 'B';
            char name[128];
            snprintf(name, sizeof(name), "%s", entry);
            if(is_raw) {
                if(find_in(a2.disk, entry, ".bas", path, sizeof(path))) {
                    type = 'A';
                } else if(find_in(a2.disk, entry, ".txt", path, sizeof(path))) {
                    type = 'T';
                }
            } else if(ends_with(entry, ".bas")) {
                type = 'A';
                name[strlen(name) - 4] = 0;
            } else if(ends_with(entry, ".txt") && !same_name(entry, "CATALOG.txt")) {
                type = 'T';
                name[strlen(name) - 4] = 0;
            } else {
                continue;
            }
            uint32_t len = 0;
            snprintf(path, sizeof(path), "%s/%s", dir, entry);
            char *d = plat_read_file(path, &len);
            if(!d) { continue; }                   // a directory
            free(d);
            snprintf(line, sizeof(line), " %c %03u %s\n", type,
                static_cast<unsigned>(len / 256 + 2), name);
            print(line);
        }
    }
    free(l.names.d);
    free(l.index.d);
}

void catalog()
{
    char raw[420];
    snprintf(raw, sizeof(raw), "%s/raw", a2.disk);
    print("\nDISK VOLUME 254\n\n");
    Listing probe;
    bool has_raw = plat_list_dir(raw, listing_add, &probe);
    free(probe.names.d);
    free(probe.index.d);
    catalog_dir(has_raw ? raw : a2.disk, has_raw);
    if(a2.has_scratch) { catalog_dir(a2.scratch, false); }
    print("\n");
}

// BLOAD: a binary file into memory.  A file from a DOS 3.3 disk starts with
// its address and length; any other needs an A parameter.
void bload(Interp &in, const char *name, int32_t at)
{
    char raw[420], path[560];
    snprintf(raw, sizeof(raw), "%s/raw", a2.disk);
    if(!*name) { raise(in, E_DOS_SYNTAX); }
    if(!find_in(raw, name, "", path, sizeof(path)) && !find_in(a2.disk, name, "", path, sizeof(path))) {
        raise(in, resolve(name, W_ANY, path, sizeof(path)) ? E_DOS_FILE_TYPE_MISMATCH : E_DOS_FILE_NOT_FOUND);
    }
    uint32_t len;
    char *file = plat_read_file(path, &len);
    if(!file) { raise(in, E_DOS_IO); }
    const uint8_t *d = reinterpret_cast<const uint8_t *>(file);
    uint32_t skip = 0, count = len;
    int32_t addr = at;
    if(len >= 4) {
        // Stored data is padded to a whole sector, so the length in the
        // header may be up to 255 less than what follows it.
        uint32_t stated = d[2] | (static_cast<uint32_t>(d[3]) << 8);
        if(stated > 0 && stated <= len - 4 && len - 4 - stated < 256) {
            skip = 4;
            count = stated;
            if(addr < 0) { addr = d[0] | (d[1] << 8); }
        }
    }
    Err e = E_NONE;
    if(addr < 0) {
        e = E_DOS_FILE_TYPE_MISMATCH;
    } else if(addr > 0xFFFF || static_cast<uint32_t>(addr) + count > 0x10000) {
        e = E_DOS_RANGE;
    } else {
        for(uint32_t k = 0; k < count && e == E_NONE; k++) {
            if(!plain_ram(static_cast<uint16_t>(addr + static_cast<int32_t>(k)))) { e = E_NOT_AVAILABLE; }
        }
    }
    if(e == E_NONE) {
        memcpy(&a2_mem[addr], d + skip, count);
        a2.dirty = true;
    }
    free(file);
    if(e != E_NONE) { raise(in, e); }
}

// A number in a command parameter: decimal or $hex.
int32_t param_value(const char **pp)
{
    const char *p = *pp;
    int32_t v = 0;
    if(*p == '$') {
        v = static_cast<int32_t>(strtol(p + 1, const_cast<char **>(&p), 16));
    } else {
        v = static_cast<int32_t>(strtol(p, const_cast<char **>(&p), 10));
    }
    *pp = p;
    return v;
}

void dos_command(Interp &in, const char *text)
{
    while(*text == ' ') { text++; }
    if(!*text) { return; }                        // a bare Ctrl-D ends READ and WRITE

    // The verb is the longest command name the text starts with.
    Verb verb = V_IGNORED;
    uint32_t best = 0;
    for(const auto &v : verbs) {
        uint32_t n = 0;
        while(v.text[n] && up(text[n]) == v.text[n]) { n++; }
        if(!v.text[n] && n > best) {
            best = n;
            verb = v.verb;
        }
    }
    if(best == 0) { raise(in, E_DOS_SYNTAX); }
    if(verb == V_IGNORED) { return; }

    // File name, then ",Lnn" style parameters.
    const char *p = text + best;
    char name[64], second[64], tmp[128];
    uint32_t n = 0;
    while(*p && *p != ',' && n < sizeof(tmp) - 1) { tmp[n++] = *p++; }
    tmp[n] = 0;
    normalise(tmp, name, sizeof(name));
    second[0] = 0;
    int32_t L = -1, R = -1, B = -1, A = -1;
    if(verb == V_RENAME && *p == ',') {
        p++;
        n = 0;
        while(*p && *p != ',' && n < sizeof(tmp) - 1) { tmp[n++] = *p++; }
        tmp[n] = 0;
        normalise(tmp, second, sizeof(second));
    }
    while(*p == ',') {
        p++;
        while(*p == ' ') { p++; }
        char letter = up(*p);
        if(letter) { p++; }
        int32_t v = param_value(&p);
        if(letter == 'L') { L = v; }
        if(letter == 'R') { R = v; }
        if(letter == 'B') { B = v; }
        if(letter == 'A') { A = v; }
        while(*p && *p != ',') { p++; }           // S, D, V: nothing to do
    }

    char path[560];
    switch(verb) {
        case V_OPEN:
            open_file(in, name, L);
            break;
        case V_CLOSE:
            if(*name) {
                int k = find_open(name);
                if(k >= 0) { close_file(in, k); }
            } else {
                for(int k = 0; k < MAX_FILES; k++) { close_file(in, k); }
            }
            break;
        case V_READ: case V_WRITE: {
            if(verb == V_WRITE && !a2.has_scratch) { raise(in, E_DOS_WRITE_PROTECTED); }
            int k = find_open(name);
            if(k < 0) { k = open_file(in, name, L); }
            File &f = a2.files[k];
            if(R >= 0) {
                f.pos = static_cast<uint32_t>(R) * f.reclen + (B > 0 ? static_cast<uint32_t>(B) : 0);
            } else if(B >= 0) {
                f.pos = static_cast<uint32_t>(B);
            }
            if(verb == V_READ) {
                a2.reading = k;
            } else {
                a2.writing = k;
            }
            break;
        }
        case V_APPEND: {
            int k = open_file(in, name, L);
            File &f = a2.files[k];
            while(!at_end_of_data(f)) { f.pos++; }
            break;
        }
        case V_POSITION: {
            int k = find_open(name);
            if(k < 0) { k = open_file(in, name, L); }
            File &f = a2.files[k];
            for(int32_t skip = R; skip > 0; skip--) {
                if(at_end_of_data(f)) { raise(in, E_DOS_END_OF_DATA); }
                while(!at_end_of_data(f) && f.data[f.pos] != '\n') { f.pos++; }
                if(f.pos < f.data.n) { f.pos++; }
            }
            break;
        }
        case V_DELETE: {
            int k = find_open(name);
            if(k >= 0) {
                a2.files[k].dirty = false;
                close_file(in, k);
            }
            if(!*name) { raise(in, E_DOS_SYNTAX); }
            if(a2.has_scratch && (find_in(a2.scratch, name, ".txt", path, sizeof(path))
                || find_in(a2.scratch, name, ".bas", path, sizeof(path)))) {
                if(!plat_delete_file(path)) { raise(in, E_DOS_IO); }
            } else if(resolve(name, W_ANY, path, sizeof(path))) {
                raise(in, E_DOS_WRITE_PROTECTED);  // it is on the disk, not in the scratch
            } else {
                raise(in, E_DOS_FILE_NOT_FOUND);
            }
            break;
        }
        case V_RENAME: {
            if(!*name || !*second) { raise(in, E_DOS_SYNTAX); }
            if(!a2.has_scratch) { raise(in, E_DOS_WRITE_PROTECTED); }
            if(!find_in(a2.scratch, name, ".txt", path, sizeof(path))) {
                raise(in, resolve(name, W_ANY, path, sizeof(path))
                    ? E_DOS_WRITE_PROTECTED : E_DOS_FILE_NOT_FOUND);
            }
            uint32_t len;
            char *d = plat_read_file(path, &len);
            char to[560];
            snprintf(to, sizeof(to), "%s/%s.txt", a2.scratch, second);
            bool ok = d && plat_write_file(to, d, len) && plat_delete_file(path);
            free(d);
            if(!ok) { raise(in, E_DOS_IO); }
            break;
        }
        case V_BLOAD:
            bload(in, name, A);
            break;
        case V_PR:
            select_output(atoi(name));
            break;
        case V_LOCK: case V_UNLOCK: case V_VERIFY:
            if(!resolve(name, W_ANY, path, sizeof(path))) { raise(in, E_DOS_FILE_NOT_FOUND); }
            break;
        case V_CATALOG:
            catalog();
            break;
        case V_RUN: case V_CHAIN: case V_DASH:
            // CHAIN should keep the variables; here it starts the program afresh.
            if(!resolve(name, W_PROGRAM, path, sizeof(path))) { raise(in, E_DOS_FILE_NOT_FOUND); }
            in.column = a2.ch;
            chain(in, path);
        default:
            raise(in, E_NOT_AVAILABLE);            // BRUN, BSAVE, EXEC, INIT, LOAD, SAVE
    }
}

// ---- the machine's hooks -----------------------------------------------------------------

void a2_write(Interp &in, const char *s, uint32_t len)
{
    setup();
    for(uint32_t k = 0; k < len; k++) {
        uint8_t c = static_cast<uint8_t>(s[k]);
        if(a2.collecting) {
            if(c == '\n' || c == '\r') {
                a2.collecting = false;
                a2.cmd[a2.cmd_len] = 0;
                in.column = a2.ch;
                dos_command(in, a2.cmd);
            } else if(a2.cmd_len < sizeof(a2.cmd) - 1) {
                a2.cmd[a2.cmd_len++] = static_cast<char>(c & 0x7F);
            }
            continue;
        }
        if((c & 0x7F) == 4 && !a2.mid_line) {
            a2.collecting = true;
            a2.cmd_len = 0;
            a2.reading = a2.writing = -1;
            continue;
        }
        a2.mid_line = !(c == '\n' || c == '\r');
        if(a2.writing >= 0) {
            file_put(c);
        } else {
            put(c);
        }
    }
    in.column = a2.ch;
    if(a2.screen) { sync(); }
}

bool a2_input_redirected(Interp &)
{
    setup();
    return a2.reading >= 0;
}

int a2_read_line(Interp &in, char *buf, int cap)
{
    setup();
    if(a2.reading >= 0) { return file_line(in, buf, cap); }
    // A key the program has already seen at $C000 and not cleared is still
    // the first thing typed.
    int held = -1;
    if(a2.kbd & 0x80) {
        held = a2.kbd & 0x7F;
        a2.kbd &= 0x7F;
    }
    int len = 0;
    if(held == '\r') {
        buf[0] = 0;
    } else {
        if(held >= 0) {
            buf[0] = static_cast<char>(held);
            put(static_cast<uint8_t>(held));
            buf++;
            cap--;
        }
        if(window_keyboard()) { return window_line(in, buf, cap) + (held >= 0); }
        if(a2.screen) { sync(); }
        len = plat_read_line(buf, cap);
        if(len < 0) { return -1; }
    }
    // What was typed is on the screen now (the terminal or the platform echoed
    // it): bring the model up to date without printing it again.
    a2.silent = true;
    for(int k = 0; k < len; k++) {
        buf[k] = typed(buf[k]);
        put(static_cast<uint8_t>(buf[k]));
    }
    newline();
    a2.silent = false;
    a2.mid_line = false;
    a2.stream_col = 0;
    a2.moved = false;
    if(a2.screen) {
        a2.need_clear = true;
        sync();
    }
    in.column = a2.ch;
    return len + (held >= 0 && held != '\r');
}

int a2_read_key(Interp &in)
{
    setup();
    if(a2.reading >= 0) {
        File &f = a2.files[a2.reading];
        if(at_end_of_data(f)) { raise(in, E_DOS_END_OF_DATA); }
        char c = f.data[f.pos++];
        return c == '\n' ? '\r' : c;
    }
    if(a2.screen) { sync(); }
    if(a2.kbd & 0x80) {                           // a key the program already saw at $C000
        a2.kbd &= 0x7F;
        return a2.kbd;
    }
    int key = window_keyboard() ? window_key(in) : plat_read_key();
    if(key < 0) { return -1; }
    if(key == 3) { raise(in, E_BREAK); }
    if(key == '\n') { key = '\r'; }
    if(key == 127) { key = 8; }                   // the delete key is the left arrow
    return typed(static_cast<char>(key));
}

void a2_ext(Interp &in, uint16_t id, Value *args, int nargs, Value *result)
{
    setup();
    switch(id) {
        case A2_HOME:
            enter_screen();
            for(int r = a2.top; r < a2.bottom; r++) {
                memset(&cell(r, a2.left), 0xA0, static_cast<size_t>(a2.width));
            }
            a2.cv = a2.top;
            a2.ch = 0;
            a2.moved = true;
            break;
        case A2_HTAB: {
            int32_t n = a2_int_arg(in, args[0]);
            if(n < 0 || n > 255) { raise(in, E_ILLEGAL_QUANTITY); }
            int col = n > 0 ? n - 1 : 0;
            if(col >= a2.width) { col = a2.width - 1; }
            if(col < a2.ch) { a2.moved = true; }
            a2.ch = col;
            break;
        }
        case A2_VTAB: {
            int32_t n = a2_int_arg(in, args[0]);
            if(n < 1 || n > ROWS) { raise(in, E_ILLEGAL_QUANTITY); }
            enter_screen();
            if(n - 1 != a2.cv) { a2.moved = true; }
            a2.cv = n - 1;
            break;
        }
        case A2_PEEK:
            result->tag = T_INT;
            result->i = peek(in, address(in, args[0]));
            break;
        case A2_POKE: {
            uint16_t addr = address(in, args[0]);
            int32_t value = a2_int_arg(in, args[1]);
            if(value < 0 || value > 255) { raise(in, E_ILLEGAL_QUANTITY); }
            poke(in, addr, static_cast<uint8_t>(value));
            break;
        }
        case A2_GR:
            // Lo-res with four lines of text below it: the window shrinks to
            // those and the cursor goes to the last one.
            a2_graphics_ext(in, id, args, nargs, result);
            a2.left = 0;
            a2.width = cols();
            a2.top = 20;
            a2.bottom = ROWS;
            if(a2.cv != ROWS - 1) { a2.moved = true; }
            a2.cv = ROWS - 1;
            video_changed();
            break;
        case A2_HGR: case A2_HGR2:
            a2_graphics_ext(in, id, args, nargs, result);
            video_changed();
            break;
        case A2_TEXT:
            if(a2_video.graphics) { snapshot(); }
            a2_video.graphics = false;
            a2.left = 0;
            a2.width = cols();
            a2.top = 0;
            a2.bottom = ROWS;
            if(a2.cv != ROWS - 1) { a2.moved = true; }
            a2.cv = ROWS - 1;
            break;
        case A2_NORMAL: a2.mode = M_NORMAL; break;
        case A2_INVERSE: a2.mode = M_INVERSE; break;
        case A2_FLASH: a2.mode = M_FLASH; break;
        case A2_PR:
            select_output(a2_int_arg(in, args[0]));
            break;
        case A2_WAIT:
            wait(in, address(in, args[0]), a2_int_arg(in, args[1]), nargs > 2 ? a2_int_arg(in, args[2]) : 0);
            break;
        case A2_CALL:
            call(in, address(in, args[0]));
            break;
        case A2_SPEED: case A2_IN: case A2_TRACE: case A2_NOTRACE:
        case A2_HIMEM: case A2_LOMEM:
            break;
        default:
            if(!a2_graphics_ext(in, id, args, nargs, result)) { raise(in, E_NOT_AVAILABLE); }
            break;
    }
    a2.dirty = true;
    in.column = a2.ch;
    if(a2.screen) { sync(); }
    if(a2.window) { tick(in, 1000); }
}

void a2_poll(Interp &in)
{
    if(a2.window) { tick(in, 1000); }
}

// The program stopped or an error was trapped: output and input go back to
// the screen and keyboard, as DOS does.
void a2_reset_io(Interp &)
{
    a2.collecting = false;
    a2.mid_line = false;
    a2.reading = a2.writing = -1;
}

void a2_finish(Interp &in)
{
    if(!a2.ready) { return; }
    jmp_buf trap;
    jmp_buf *outer = in.trap;
    in.trap = &trap;
    if(setjmp(trap) == 0) {
        for(int k = 0; k < MAX_FILES; k++) { close_file(in, k); }
    }
    in.trap = outer;
    if(a2.png[0] && (a2_video.graphics || !a2.snapped)) { snapshot(); }
    if(a2.window) {
        // Leave the picture up until a key is pressed or the window is closed
        // (unless input is scripted, when nobody is there to do either).
        while(plat_display_key() >= 0) { }
        a2.dirty = true;
        show(false);
        while(plat_input_is_terminal() && plat_display_pump() == 0 && plat_display_key() < 0) {
            show(false);
            plat_sleep_ms(10);
        }
        a2.window = false;
    }
    if(a2.screen) {
        char seq[32];
        sync();
        snprintf(seq, sizeof(seq), "\033[0m\033[%d;1H\n", ROWS);
        emit(seq);
        a2.screen = false;
    }
}

}  // namespace

extern const Machine apple2_machine = {
    a2_write, a2_input_redirected, a2_read_line, a2_read_key, a2_ext, a2_reset_io, a2_finish,
    a2_poll,
};

void apple2_configure(const Apple2Options &opt)
{
    snprintf(a2.disk, sizeof(a2.disk), "%s", opt.disk ? opt.disk : ".");
    a2.has_scratch = opt.scratch != nullptr;
    if(opt.scratch) { snprintf(a2.scratch, sizeof(a2.scratch), "%s", opt.scratch); }
    a2.stream_only = opt.stream;
    a2.fast = opt.fast;
    snprintf(a2.png, sizeof(a2.png), "%s", opt.png ? opt.png : "");
    // A window opens by itself only for someone sitting at a terminal who has
    // not asked for a PNG instead.
    a2.window_mode = opt.window > 0 ? WIN_ALWAYS : (opt.window < 0 || opt.png) ? WIN_NEVER : WIN_AUTO;
}

}  // namespace basic
