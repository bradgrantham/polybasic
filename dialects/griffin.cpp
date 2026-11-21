// Griffin BASIC: Microsoft-style BASIC with long variable names, keywords
// that must stand alone (so GOTO 100, not GOTO100), ELSE, the clock, and the
// gfx extension pack: drawing, sprites, input devices and sound.
#include "msfamily.h"
#include "../machines/gfx/gfx.h"

namespace basic {

static const Keyword keywords[] = {
    {"END", KW_END}, {"FOR", KW_FOR}, {"NEXT", KW_NEXT}, {"DATA", KW_DATA},
    {"INPUT", KW_INPUT}, {"DIM", KW_DIM}, {"READ", KW_READ}, {"LET", KW_LET},
    {"GOTO", KW_GOTO}, {"RUN", KW_RUN}, {"IF", KW_IF}, {"RESTORE", KW_RESTORE},
    {"GOSUB", KW_GOSUB}, {"RETURN", KW_RETURN}, {"REM", KW_REM}, {"STOP", KW_STOP},
    {"ON", KW_ON}, {"WAIT", KW_WAIT}, {"DEF", KW_DEF}, {"PRINT", KW_PRINT},
    {"CLEAR", KW_CLEAR}, {"WIDTH", KW_WIDTH}, {"RANDOMIZE", KW_RANDOMIZE},
    {"GO", KW_GO}, {"TAB(", KW_TAB}, {"SPC(", KW_SPC}, {"TO", KW_TO},
    {"SUB", KW_SUB}, {"FN", KW_FN}, {"THEN", KW_THEN}, {"ELSE", KW_ELSE},
    {"STEP", KW_STEP}, {"NOT", KW_NOT}, {"AND", KW_AND}, {"OR", KW_OR},
    {"SGN", KW_SGN}, {"INT", KW_INT}, {"ABS", KW_ABS}, {"FRE", KW_FRE},
    {"POS", KW_POS}, {"SQR", KW_SQR}, {"RND", KW_RND}, {"LOG", KW_LOG},
    {"EXP", KW_EXP}, {"COS", KW_COS}, {"SIN", KW_SIN}, {"TAN", KW_TAN},
    {"ATN", KW_ATN}, {"LEN", KW_LEN}, {"STR$", KW_STR}, {"VAL", KW_VAL},
    {"ASC", KW_ASC}, {"CHR$", KW_CHR}, {"LEFT$", KW_LEFT}, {"RIGHT$", KW_RIGHT},
    {"MID$", KW_MID}, {"GET", KW_GET},

    // The clock.
    {"TIMER", KW_EXTFN, GX_TIMER, "-"}, {"DATE$", KW_EXTFN, GX_DATE, "-"},
    {"TIME$", KW_EXTFN, GX_TIME, "-"},

    // Drawing.  A colour is 0 to 255 (RGB332); where one may be left out the
    // colour set by COLOR is used.
    {"CLS", KW_EXT, GX_CLS, "?N"},                    // CLS [colour]
    {"COLOR", KW_EXT, GX_COLOR, "N?,N"},              // COLOR drawing [,background]
    {"RGB", KW_EXTFN, GX_RGB, "N,N,N"},               // RGB(r,g,b), each 0 to 255
    {"PSET", KW_EXT, GX_PSET, "N,N?,N"},              // PSET x,y [,colour]
    {"POINT", KW_EXTFN, GX_POINT, "N,N"},             // POINT(x,y); -1 off the screen
    {"LINE", KW_EXT, GX_LINE, "N,N,N,N?,N"},          // LINE x1,y1,x2,y2 [,colour]
    {"RECT", KW_EXT, GX_RECT, "N,N,N,N?,N"},          // RECT x,y,w,h [,colour]
    {"FRECT", KW_EXT, GX_FRECT, "N,N,N,N?,N"},
    {"CIRCLE", KW_EXT, GX_CIRCLE, "N,N,N?,N"},        // CIRCLE x,y,r [,colour]
    {"FCIRCLE", KW_EXT, GX_FCIRCLE, "N,N,N?,N"},
    {"GPRINT", KW_EXT, GX_GPRINT, "N,N,S?,N"},        // GPRINT x,y,text$ [,colour]
    {"SCRW", KW_EXTFN, GX_SCRW, "-"}, {"SCRH", KW_EXTFN, GX_SCRH, "-"},
    {"PAGE", KW_EXT, GX_PAGE, "N,N"},                 // PAGE draw,show (each 0 or 1)
    {"VSYNC", KW_EXT, GX_VSYNC, "?N"},                // VSYNC [frames]
    {"TEXT", KW_EXT, GX_TEXT, ""},                    // back to the text screen
    // SPRITE DEFINE n,x,y,w,h [,clear]   take a sprite from the page being drawn on
    // SPRITE n,x,y  or  SPRITE DRAW n,x,y
    {"SPRITE", KW_EXT, GX_SPRITE, "~DEFINE N,N,N,N,N?,N|~DRAW N,N,N|N,N,N"},

    // Input devices.
    {"INKEY$", KW_EXTFN, GX_INKEY, "-"},              // a key if one is waiting, else ""
    {"JOY", KW_EXTFN, GX_JOY, "N"},                   // JOY(0) x, (1) y, (2) fire, (3) all
    {"PADDLE", KW_EXTFN, GX_PADDLE, "N"},             // PADDLE(0 or 1): 0 to 255, or -1

    // Sound.
    {"SOUND", KW_EXT, GX_SOUND, "~OFF|N,N?,N?,N"},    // SOUND hz,ms [,volume [,voice]]
    {"BEEP", KW_EXT, GX_BEEP, ""},
    {"PLAY", KW_EXT, GX_PLAY, "S?,S?,S"},             // PLAY a$ [,b$ [,c$]]
    {"PLAYING", KW_EXTFN, GX_PLAYING, "N"},           // PLAYING(voice 1 to 3, or 0 for any)
    {nullptr, KW_NONE},
};

extern const Dialect griffin_dialect = {
    "griffin",
    "GRIFFIN",
    {
        .true_value = -1,
        .bitwise_logic = true,
        .for_pretest = true,
        .print_digits = 6,
        .zone_width = 14,
        .tab_base = 0,
        .num_lead_space = true,
        .num_trail_space = true,
        .sig_name_chars = 0,
        .crunch_keywords = false,
        .ready = "OK\n",
        .prompt = nullptr,
        .input_mark = "? ",
        .mark_after_prompt = true,
        .redo = "?REDO FROM START\n",
        .stmt_markers = false,
    },
    msfamily_compile_line,
    keywords,
    &gfx_machine,
    nullptr,
};

}  // namespace basic
