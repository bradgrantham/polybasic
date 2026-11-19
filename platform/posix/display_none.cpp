// The display where there is none: built in place of display_sdl.cpp when
// SDL3 is not installed.  Graphics still works into memory and --png, and
// sound into --wav.
#include "../../core/platform.h"
#include "host.h"

namespace basic {

bool plat_display_open(int, int, const char *) { return false; }
void plat_display_show(const uint32_t *) { }
int plat_display_pump() { return 0; }
int plat_display_key() { return -1; }

bool host_audio_open(int) { return false; }
void host_audio_put(const int16_t *, int) { }
int host_audio_queued() { return 0; }
void host_audio_close() { }

}  // namespace basic
