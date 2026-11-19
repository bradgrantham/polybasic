// The POSIX host's side of the gfx pack: two pages of RGB332 pixels in
// memory, shown in the display window (display_sdl.cpp) or written as a PNG,
// and sound sent to the sound device or written as a WAV file.
//
// With no window and no sound device (a test, or output that is piped) nothing
// here waits for real time: frames pass at once and sound is taken as fast as
// it is made, so a run gives the same files every time.
#include "host.h"
#include "../../core/platform.h"
#include "../../machines/common/png.h"

#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace basic {

namespace {

enum { FB_W = 320, FB_H = 240, RATE = 15707, BLOCK = 512 };

AvOptions opt;
uint8_t *pages[2];
int shown;
bool window;
uint32_t last_present;
bool (*idle_fn)();

bool device;                      // sound goes to the sound device
bool audio_open;
uint8_t block[BLOCK];
Vec<char> wav;                    // 8-bit samples for --wav

uint32_t rgb(uint8_t c)
{
    uint32_t r = (c >> 5) & 7u, g = (c >> 2) & 7u, b = c & 3u;
    return (r * 255u / 7u) << 16 | (g * 255u / 7u) << 8 | (b * 255u / 3u);
}

void present()
{
    static uint32_t pixels[FB_W * FB_H];
    static uint32_t table[256];
    if(!table[255]) {
        for(int c = 0; c < 256; c++) { table[c] = rgb(static_cast<uint8_t>(c)); }
    }
    const uint8_t *from = pages[shown];
    for(int k = 0; k < FB_W * FB_H; k++) { pixels[k] = table[from[k]]; }
    plat_display_show(pixels);
    last_present = plat_ticks_ms();
}

void pump()
{
    int flags = plat_display_pump();
    if(flags & (DISPLAY_BREAK | DISPLAY_CLOSED)) { host_break(); }
}

void put16(Vec<char> &out, uint32_t v) { out.push(static_cast<char>(v)); out.push(static_cast<char>(v >> 8)); }
void put32(Vec<char> &out, uint32_t v) { put16(out, v & 0xFFFF); put16(out, v >> 16); }

}  // namespace

void av_configure(const AvOptions &o) { opt = o; }
bool av_window_open() { return window; }
void av_idle() { if(idle_fn) { idle_fn(); } }

bool plat_fb_open(int *width, int *height)
{
    if(!pages[0]) {
        pages[0] = static_cast<uint8_t *>(calloc(2, FB_W * FB_H));
        if(!pages[0]) { return false; }
        pages[1] = pages[0] + FB_W * FB_H;
        bool want = opt.window > 0 || (opt.window == 0 && !opt.png && plat_is_terminal());
        window = want && plat_display_open(FB_W, FB_H, "Griffin BASIC");
    }
    *width = FB_W;
    *height = FB_H;
    return true;
}

uint8_t *plat_fb_page(int page) { return pages[page]; }

void plat_fb_show(int page)
{
    shown = page;
    if(window) { present(); }
}

void plat_fb_update()
{
    if(window && plat_ticks_ms() - last_present >= 16) {
        present();
        pump();
    }
}

void plat_fb_vsync()
{
    av_idle();
    if(!window) { return; }
    uint32_t due = last_present + 17;
    if(!opt.no_sleep) {
        uint32_t now = plat_ticks_ms();
        if(due - now < 100) { usleep((due - now) * 1000); }
    }
    present();
    pump();
}

void plat_fb_close(bool wait)
{
    if(!window || !wait) { return; }
    // The window stays as it is; someone at a terminal presses a key to go on.
    present();
    while(plat_display_key() >= 0) { }
    while(plat_input_is_terminal()) {
        int flags = plat_display_pump();
        if(flags != 0 || plat_display_key() >= 0) { break; }
        av_idle();
        usleep(10000);
    }
}

uint32_t plat_clock()
{
    time_t t = time(nullptr);
    struct tm local;
    localtime_r(&t, &local);
    return static_cast<uint32_t>(t + local.tm_gmtoff);
}

// No joystick or paddles on the host yet.
int plat_joystick() { return 0; }
int plat_paddle(int) { return -1; }

int plat_audio_open(const uint8_t **codes)
{
    static const uint8_t levels[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    *codes = levels;
    if(!audio_open) {
        audio_open = true;
        device = opt.sound && !opt.wav && plat_is_terminal() && host_audio_open(RATE);
    }
    return RATE;
}

uint8_t *plat_audio_block(int *count)
{
    // A fifth of a second queued is enough to ride out a slow statement.
    if(device && host_audio_queued() > RATE / 5) { return nullptr; }
    *count = BLOCK;
    return block;
}

void plat_audio_submit()
{
    if(opt.wav) {
        for(int k = 0; k < BLOCK; k++) { wav.push(static_cast<char>(128 + (block[k] - 4) * 24)); }
    }
    if(device) {
        int16_t samples[BLOCK];
        for(int k = 0; k < BLOCK; k++) { samples[k] = static_cast<int16_t>((block[k] - 4) * 5000); }
        host_audio_put(samples, BLOCK);
    }
}

void plat_audio_close(bool now)
{
    if(device) {
        while(!now && host_audio_queued() > 0) {
            if(plat_break_pending()) { break; }
            usleep(10000);
        }
        host_audio_close();
    }
    device = false;
    audio_open = false;
}

void plat_on_idle(bool (*fn)()) { idle_fn = fn; }

void av_finish()
{
    if(opt.png && pages[0]) {
        static uint32_t pixels[FB_W * FB_H];
        for(int k = 0; k < FB_W * FB_H; k++) { pixels[k] = rgb(pages[shown][k]); }
        Vec<char> out;
        png_encode(pixels, FB_W, FB_H, out);
        if(!plat_write_file(opt.png, out.d, out.n)) { fprintf(stderr, "Could not write \"%s\"\n", opt.png); }
        free(out.d);
    }
    if(opt.wav) {
        // PCM, one channel, 8 bits unsigned.
        Vec<char> out;
        const char *riff = "RIFF", *wave = "WAVEfmt ", *data = "data";
        for(int k = 0; k < 4; k++) { out.push(riff[k]); }
        put32(out, 36 + wav.n);
        for(int k = 0; k < 8; k++) { out.push(wave[k]); }
        put32(out, 16);
        put16(out, 1);
        put16(out, 1);
        put32(out, RATE);
        put32(out, RATE);
        put16(out, 1);
        put16(out, 8);
        for(int k = 0; k < 4; k++) { out.push(data[k]); }
        put32(out, wav.n);
        for(uint32_t k = 0; k < wav.n; k++) { out.push(wav[k]); }
        if(!plat_write_file(opt.wav, out.d, out.n)) { fprintf(stderr, "Could not write \"%s\"\n", opt.wav); }
        free(out.d);
    }
}

}  // namespace basic
