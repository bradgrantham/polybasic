// What the core needs from the machine it runs on.  Exactly one platform
// module is linked into a build and defines these functions.
//
// A capability a platform lacks is reported by the return value; the core
// turns that into a BASIC error, so nothing here may fail to link.
#ifndef BASIC_PLATFORM_H
#define BASIC_PLATFORM_H

#include <cstdint>

namespace basic {

// Console.
void plat_write(const char *s, uint32_t len);
// Read one line without its terminator into buf (NUL terminated, truncated to
// cap-1).  Returns the length, or -1 at end of input.
int plat_read_line(char *buf, int cap);
// Read one key without echo and without waiting for Return; -1 at end of input.
int plat_read_key();
// Look for a key without waiting: the key, -1 if none is ready, -2 at end of
// input.
int plat_poll_key();
// True if the console is a terminal that obeys ANSI cursor controls.
bool plat_is_terminal();
// True once after the user asks to interrupt the program (Ctrl-C).
bool plat_break_pending();

// True if input comes from a keyboard rather than a file or a pipe.
bool plat_input_is_terminal();

// Time.
void plat_sleep_ms(uint32_t ms);
uint32_t plat_ticks_ms();         // milliseconds since some fixed moment
uint32_t plat_entropy();          // a seed for RANDOMIZE

// Whole-file access for LOAD and SAVE.  plat_read_file returns a malloc'd
// buffer the caller frees, or null.
char *plat_read_file(const char *name, uint32_t *len);
bool plat_write_file(const char *name, const char *data, uint32_t len);
bool plat_delete_file(const char *name);
// Call fn(ctx, name) for every entry in a directory; false if it cannot be read.
bool plat_list_dir(const char *dir, void (*fn)(void *ctx, const char *name), void *ctx);

// Display (optional): one window showing a framebuffer the caller draws.
// plat_display_open returns false where there is no display, and then the
// other three are never called.
bool plat_display_open(int width, int height, const char *title);
// Show width*height pixels, each 0x00RRGGBB, top row first.
void plat_display_show(const uint32_t *pixels);
// Handle the window's events; returns DISPLAY_ flags, each reported once.
enum { DISPLAY_BREAK = 1, DISPLAY_CLOSED = 2 };
int plat_display_pump();
// The next key typed in the window, as ASCII (Return is 13, the left arrow
// and Delete are 8, the right arrow is 21), or -1 if there is none waiting.
int plat_display_key();

// ---- what the gfx extension pack draws on, reads and plays through -------------
//
// Framebuffer (optional): two pages of pixels, one byte each, RGB332 (red in
// the top three bits, blue in the bottom two), top row first.  Page 0 is the
// one a program draws on unless it asks otherwise.  plat_fb_open returns false
// where there is none, and then the other plat_fb_ functions are never called.
bool plat_fb_open(int *width, int *height);
uint8_t *plat_fb_page(int page);          // 0 or 1; width bytes to a row
void plat_fb_show(int page);              // the page on display from the next frame
void plat_fb_vsync();                     // wait for the next frame
// Called often while a program runs: a platform that has to copy the shown
// page to a window does so here when one is due.
void plat_fb_update();
// Put the text console back.  With `wait` the program has stopped: let the
// picture be looked at first (until a key is pressed, where someone is there).
void plat_fb_close(bool wait);

// The clock: seconds since 1970-01-01 00:00 local time, 0 if it is not known.
uint32_t plat_clock();

// Joystick switches as PLAT_JOY_ bits, and a paddle (0 or 1) as 0..255, or -1
// if there is none.
enum { PLAT_JOY_UP = 1, PLAT_JOY_DOWN = 2, PLAT_JOY_LEFT = 4, PLAT_JOY_RIGHT = 8, PLAT_JOY_FIRE = 16 };
int plat_joystick();
int plat_paddle(int n);

// Sound (optional): a stream of output levels, 0 to 8 with 4 as silence,
// played at a fixed rate.  plat_audio_open starts the stream and returns its
// rate in levels a second, or 0 where there is no sound.  plat_audio_block
// returns room for *count levels, or null when the platform has enough queued
// for now; plat_audio_submit plays what was written there.  plat_audio_close
// lets what is queued finish (or drops it, if `now`) and stops.
int plat_audio_open();
uint8_t *plat_audio_block(int *count);
void plat_audio_submit();
void plat_audio_close(bool now);

// Ask for fn to be called regularly while the platform waits (for a key, a
// line of input, a sleep or a frame), so that sound can be kept fed.
void plat_on_idle(void (*fn)());

}  // namespace basic

#endif
