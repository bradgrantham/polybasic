// Shared between the files of the POSIX host platform.
#ifndef BASIC_POSIX_HOST_H
#define BASIC_POSIX_HOST_H

#include <cstdint>

namespace basic {

// av.cpp: the framebuffer and sound behind the gfx pack.
struct AvOptions {
    const char *png;              // write the page on display at exit here, or null
    const char *wav;              // write everything played here, or null
    int window;                   // 1 open a window, -1 never, 0 only for a terminal user
    bool sound;                   // play through the sound device, for a terminal user
    bool no_sleep;                // do not wait for frames
};
void av_configure(const AvOptions &opt);
bool av_window_open();            // keys come from the window, not the terminal
void av_finish();                 // write the PNG and WAV files
void av_idle();                   // call the function given to plat_on_idle

// main.cpp
void host_break();                // as if Ctrl-C had been typed

// display_sdl.cpp or display_none.cpp: the sound device.
bool host_audio_open(int rate);                       // false: there is none
void host_audio_put(const int16_t *samples, int count);
void host_audio_flush();                             // play what is held back
int host_audio_queued();                              // samples not yet played
void host_audio_close();

}  // namespace basic

#endif
