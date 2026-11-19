// A minimal PNG writer, for the headless display back end.
#ifndef BASIC_PNG_H
#define BASIC_PNG_H

#include "../../core/basic.h"

namespace basic {

// Encode width*height pixels (0x00RRGGBB, top row first) as a PNG.  The same
// picture always gives the same bytes, so a test can compare files directly.
void png_encode(const uint32_t *pixels, int width, int height, Vec<char> &out);

}  // namespace basic

#endif
