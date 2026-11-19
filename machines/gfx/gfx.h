// The gfx extension pack: drawing, sprites, input devices, sound and the clock
// for the griffin dialect.  It is the dialect's Machine; everything it needs
// from the computer comes through the plat_fb_, plat_audio_ and related
// functions in core/platform.h, so the same code serves a host and Griffin Jr.
#ifndef BASIC_GFX_H
#define BASIC_GFX_H

#include "../../core/basic.h"

namespace basic {

// Statement and function ids, as used in the griffin keyword table.
enum : uint16_t {
    GX_CLS, GX_COLOR, GX_PSET, GX_LINE, GX_RECT, GX_FRECT, GX_CIRCLE, GX_FCIRCLE,
    GX_GPRINT, GX_PAGE, GX_VSYNC, GX_SPRITE, GX_TEXT,
    GX_SOUND, GX_BEEP, GX_PLAY,
    GX_RGB, GX_POINT, GX_SCRW, GX_SCRH, GX_INKEY, GX_JOY, GX_PADDLE,
    GX_PLAYING, GX_TIMER, GX_DATE, GX_TIME,
};

extern const Machine gfx_machine;

// ---- sound (sound.cpp) -------------------------------------------------------

enum { SOUND_VOICES = 3 };

// Queue a tone on a voice (0..2): hertz (0 is a rest), milliseconds, and a
// volume from 0 to 15.
void sound_tone(Interp &in, int voice, double hertz, int32_t ms, int volume);
// Queue music strings, one per voice, to start together.
void sound_play(Interp &in, const StrRef *strings, int count);
void sound_off();                         // stop at once and forget what is queued
bool sound_playing(int voice);            // a voice 0..2, or -1 for any
void sound_pump();                        // hand the platform as much as it will take
void sound_finish(Interp &in);            // play out what is queued, then stop

}  // namespace basic

#endif
