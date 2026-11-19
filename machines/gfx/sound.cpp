// Sound for the gfx pack: three square-wave voices mixed into the nine output
// levels a platform plays (core/platform.h), and the music strings of PLAY.
//
// Each voice has a queue of events: a tone or a rest, and how long it lasts.
// SOUND and PLAY append to the queues; sound_pump turns queued events into
// output whenever the platform has room, which happens as the program runs and
// while it waits.  Nothing here runs from an interrupt.
//
// Integers only: this runs on a 68000 beside the program.  Positions inside a
// wave are in 16.16 fixed-point samples.
#include "gfx.h"
#include "../../core/platform.h"

namespace basic {

namespace {

enum { QUEUE = 64 };

struct Event {
    uint32_t half;                // half a period in 16.16 samples; 0 is a rest
    uint32_t samples;
    uint8_t amp;                  // 0..4 output levels either side of silence
};

struct Voice {
    Event queue[QUEUE];
    uint8_t head, count;
    // The event being played.
    bool busy;
    bool high;
    uint8_t amp;
    uint32_t half;
    int32_t toggle;               // 16.16 samples until the wave next changes sign
    uint32_t left;                // samples left
    // What a music string leaves set for the next one.
    uint8_t octave, volume, style;
    uint16_t length, tempo;
};

struct Sound {
    bool tried, open;
    uint32_t rate;
    Voice voice[SOUND_VOICES];
} s;

enum { STYLE_NORMAL, STYLE_LEGATO, STYLE_STACCATO };

bool start()
{
    if(!s.tried) {
        s.tried = true;
        for(Voice &v : s.voice) {
            v.octave = 4;
            v.volume = 8;
            v.length = 4;
            v.tempo = 120;
            v.style = STYLE_NORMAL;
        }
    }
    if(!s.open) {
        int rate = plat_audio_open();
        if(rate <= 0) { return false; }
        s.rate = static_cast<uint32_t>(rate);
        s.open = true;
    }
    return true;
}

bool voice_active(const Voice &v) { return v.busy || v.count > 0; }

bool any_active()
{
    for(const Voice &v : s.voice) {
        if(voice_active(v)) { return true; }
    }
    return false;
}

// Add `count` samples of v's square wave to out.
void add_square(Voice &v, uint8_t *out, uint32_t count)
{
    while(count > 0) {
        // Whole samples before the wave changes sign: at least one.
        uint32_t run = (static_cast<uint32_t>(v.toggle) + 0xFFFFu) >> 16;
        if(run > count) { run = count; }
        uint8_t delta = v.high ? v.amp : static_cast<uint8_t>(-v.amp);
        for(uint32_t k = 0; k < run; k++) { out[k] = static_cast<uint8_t>(out[k] + delta); }
        out += run;
        count -= run;
        v.toggle -= static_cast<int32_t>(run << 16);
        if(v.toggle <= 0) {
            v.toggle += static_cast<int32_t>(v.half);
            v.high = !v.high;
        }
    }
}

// Fill `count` output levels from the queues; silence where they run out.
void fill(uint8_t *out, uint32_t count)
{
    enum { BIAS = 16 };           // keeps the sums of three voices inside a byte
    memset(out, BIAS, count);
    for(Voice &v : s.voice) {
        uint32_t at = 0;
        while(at < count) {
            if(!v.busy) {
                if(v.count == 0) { break; }
                const Event &e = v.queue[v.head];
                v.head = static_cast<uint8_t>((v.head + 1) % QUEUE);
                v.count--;
                v.busy = true;
                v.half = e.half;
                v.amp = e.amp;
                v.left = e.samples;
                v.high = true;
                v.toggle = static_cast<int32_t>(e.half);
            }
            uint32_t run = count - at;
            if(run > v.left) { run = v.left; }
            if(v.half != 0 && v.amp != 0) { add_square(v, out + at, run); }
            at += run;
            v.left -= run;
            if(v.left == 0) { v.busy = false; }
        }
    }
    for(uint32_t k = 0; k < count; k++) {
        int level = out[k] - (BIAS - 4);
        out[k] = static_cast<uint8_t>(level < 0 ? 0 : (level > 8 ? 8 : level));
    }
}

// Append an event, waiting for the voice's queue to drain if it is full.
void push(Interp &in, Voice &v, uint32_t half, uint32_t samples, uint8_t amp)
{
    if(samples == 0) { return; }
    while(v.count == QUEUE) {
        sound_pump();
        if(plat_break_pending()) { raise(in, E_BREAK); }
    }
    Event &e = v.queue[(v.head + v.count) % QUEUE];
    e.half = half;
    e.samples = samples;
    e.amp = amp;
    v.count++;
}

// Half a period, in 16.16 samples, of a tone given in thousandths of a hertz.
uint32_t half_period(uint32_t millihertz)
{
    uint64_t half = static_cast<uint64_t>(s.rate) * 32768000u / millihertz;
    if(half < 0x10000u) { half = 0x10000u; }           // above half the sample rate
    if(half > 0x7FFFFFFFu) { half = 0x7FFFFFFFu; }
    return static_cast<uint32_t>(half);
}

uint8_t amplitude(int volume) { return static_cast<uint8_t>((volume + 3) / 4); }

// ---- music strings ------------------------------------------------------------------

// Octave 4, in thousandths of a hertz: C to B, with A at 440.
const uint32_t scale[12] = {
    261626, 277183, 293665, 311127, 329628, 349228, 369994, 391995, 415305, 440000,
    466164, 493883,
};
// Semitones above C of the notes A to G.
const int8_t letter[7] = {9, 11, 0, 2, 4, 5, 7};

struct Cursor {
    const char *p, *end;
};

char peek(const Cursor &c)
{
    char ch = c.p < c.end ? *c.p : 0;
    return (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 32) : ch;
}

void blanks(Cursor &c)
{
    while(c.p < c.end && (*c.p == ' ' || *c.p == ';')) { c.p++; }
}

// A number, or -1 if there is none.
int32_t number(Cursor &c)
{
    blanks(c);
    if(c.p >= c.end || *c.p < '0' || *c.p > '9') { return -1; }
    int32_t n = 0;
    while(c.p < c.end && *c.p >= '0' && *c.p <= '9') {
        if(n < 100000) { n = n * 10 + (*c.p - '0'); }
        c.p++;
    }
    return n;
}

int32_t need(Interp &in, Cursor &c, int32_t low, int32_t high)
{
    int32_t n = number(c);
    if(n < low || n > high) { raise(in, E_ILLEGAL_QUANTITY); }
    return n;
}

// How long a note or rest lasts, in samples, from an optional length and dots.
uint32_t duration(Interp &in, Voice &v, Cursor &c)
{
    int32_t length = number(c);
    if(length < 0) {
        length = v.length;
    } else if(length < 1 || length > 64) {
        raise(in, E_ILLEGAL_QUANTITY);
    }
    // A whole note is four beats.
    uint32_t samples = s.rate * 240u / v.tempo / static_cast<uint32_t>(length);
    uint32_t extra = samples / 2;
    for(blanks(c); peek(c) == '.'; c.p++, blanks(c)) {
        samples += extra;
        extra /= 2;
    }
    return samples;
}

// Queue a note `semitone` steps above C in octave `octave` (or a rest, if
// semitone is negative).
void note(Interp &in, Voice &v, int octave, int semitone, uint32_t samples)
{
    if(semitone < 0) {
        push(in, v, 0, samples, 0);
        return;
    }
    while(semitone >= 12) { semitone -= 12; octave++; }
    uint32_t f = scale[semitone];
    f = octave >= 4 ? f << (octave - 4) : f >> (4 - octave);
    uint32_t sounding = v.style == STYLE_LEGATO ? samples
        : (v.style == STYLE_STACCATO ? samples * 3 / 4 : samples * 7 / 8);
    push(in, v, half_period(f), sounding, amplitude(v.volume));
    push(in, v, 0, samples - sounding, 0);
}

// Take one element of a music string.  Returns false at its end.
bool element(Interp &in, Voice &v, Cursor &c)
{
    blanks(c);
    char ch = peek(c);
    if(!ch) { return false; }
    c.p++;
    if(ch >= 'A' && ch <= 'G') {
        int semitone = letter[ch - 'A'];
        char mark = peek(c);
        if(mark == '#' || mark == '+') {
            semitone++;
            c.p++;
        } else if(mark == '-') {
            semitone--;
            c.p++;
        }
        int octave = v.octave;
        if(semitone < 0) {
            semitone += 12;
            octave--;
        }
        if(octave < 0) { raise(in, E_ILLEGAL_QUANTITY); }
        note(in, v, octave, semitone, duration(in, v, c));
        return true;
    }
    switch(ch) {
        case 'R': case 'P':
            note(in, v, 0, -1, duration(in, v, c));
            break;
        case 'N': {
            // Notes numbered in semitones from C of octave 1; 0 is a rest.
            int32_t n = need(in, c, 0, 96);
            uint32_t samples = s.rate * 240u / v.tempo / v.length;
            if(n == 0) {
                note(in, v, 0, -1, samples);
            } else {
                note(in, v, 1 + (n - 1) / 12, (n - 1) % 12, samples);
            }
            break;
        }
        case 'O': v.octave = static_cast<uint8_t>(need(in, c, 1, 8)); break;
        case '>': if(v.octave < 8) { v.octave++; } break;
        case '<': if(v.octave > 1) { v.octave--; } break;
        case 'L': v.length = static_cast<uint16_t>(need(in, c, 1, 64)); break;
        case 'T': v.tempo = static_cast<uint16_t>(need(in, c, 32, 255)); break;
        case 'V': v.volume = static_cast<uint8_t>(need(in, c, 0, 15)); break;
        case 'M': {
            char m = peek(c);
            c.p++;
            if(m == 'N') {
                v.style = STYLE_NORMAL;
            } else if(m == 'L') {
                v.style = STYLE_LEGATO;
            } else if(m == 'S') {
                v.style = STYLE_STACCATO;
            } else if(m != 'F' && m != 'B') {          // foreground and background: accepted
                raise(in, E_ILLEGAL_QUANTITY);
            }
            break;
        }
        default:
            raise(in, E_ILLEGAL_QUANTITY);
    }
    return true;
}

}  // namespace

void sound_tone(Interp &in, int voice, double hertz, int32_t ms, int volume)
{
    if(!start()) { return; }
    uint32_t samples = static_cast<uint32_t>(ms / 1000) * s.rate
        + static_cast<uint32_t>(ms % 1000) * s.rate / 1000u;
    uint32_t millihertz = static_cast<uint32_t>(hertz * 1000.0);
    Voice &v = s.voice[voice];
    if(millihertz == 0) {
        push(in, v, 0, samples, 0);
    } else {
        push(in, v, half_period(millihertz), samples, amplitude(volume));
    }
    sound_pump();
}

void sound_play(Interp &in, const StrRef *strings, int count)
{
    if(!start()) { return; }
    // The strings are taken an element at a time in turn, and nothing is
    // played meanwhile unless a queue fills, so the voices start together.
    Cursor cursor[SOUND_VOICES];
    bool more[SOUND_VOICES];
    for(int k = 0; k < count; k++) {
        cursor[k].p = str_ptr(in, strings[k]);
        cursor[k].end = cursor[k].p + strings[k].len;
        more[k] = true;
    }
    for(bool any = true; any;) {
        any = false;
        for(int k = 0; k < count; k++) {
            if(more[k]) {
                more[k] = element(in, s.voice[k], cursor[k]);
                any = any || more[k];
            }
        }
    }
    sound_pump();
}

void sound_off()
{
    for(Voice &v : s.voice) {
        v.head = v.count = 0;
        v.busy = false;
    }
    if(s.open) {
        plat_audio_close(true);
        s.open = false;
    }
}

bool sound_playing(int voice)
{
    if(voice < 0) { return any_active(); }
    return voice_active(s.voice[voice]);
}

void sound_pump()
{
    if(!s.open) { return; }
    while(any_active()) {
        int count;
        uint8_t *out = plat_audio_block(&count);
        if(!out) { return; }
        fill(out, static_cast<uint32_t>(count));
        plat_audio_submit();
    }
}

void sound_finish(Interp &)
{
    if(!s.open) { return; }
    while(any_active()) {
        sound_pump();
        if(plat_break_pending()) {
            sound_off();
            return;
        }
    }
    plat_audio_close(false);
    s.open = false;
}

}  // namespace basic
