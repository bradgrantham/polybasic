// Applesoft BASIC: the Microsoft-family parser with Applesoft's keywords and
// semantics, running on the Apple II machine.
#include "msfamily.h"
#include "../machines/apple2/apple2.h"

namespace basic {

// Every Applesoft token is here, so that every program at least parses.
static const Keyword keywords[] = {
    {"END", KW_END}, {"FOR", KW_FOR}, {"NEXT", KW_NEXT}, {"DATA", KW_DATA},
    {"INPUT", KW_INPUT}, {"DEL", KW_UNSUPPORTED}, {"DIM", KW_DIM}, {"READ", KW_READ},
    {"GR", KW_EXT, A2_GR, ""}, {"TEXT", KW_EXT, A2_TEXT, ""},
    {"PR#", KW_EXT, A2_PR, "N"}, {"IN#", KW_EXT, A2_IN, "N"},
    {"CALL", KW_EXT, A2_CALL, "N"}, {"PLOT", KW_EXT, A2_PLOT, "N,N"},
    {"HLIN", KW_EXT, A2_HLIN, "N,N@N"}, {"VLIN", KW_EXT, A2_VLIN, "N,N@N"},
    {"HGR2", KW_EXT, A2_HGR2, ""}, {"HGR", KW_EXT, A2_HGR, ""},
    {"HCOLOR=", KW_EXT, A2_HCOLOR, "N"}, {"HPLOT", KW_HPLOT, A2_HPLOT, nullptr},
    {"DRAW", KW_EXT, A2_DRAW, "N?@N,N"}, {"XDRAW", KW_EXT, A2_XDRAW, "N?@N,N"},
    {"HTAB", KW_EXT, A2_HTAB, "N"}, {"HOME", KW_EXT, A2_HOME, ""},
    {"ROT=", KW_EXT, A2_ROT, "N"}, {"SCALE=", KW_EXT, A2_SCALE, "N"},
    {"SHLOAD", KW_EXT, A2_SHLOAD, ""}, {"TRACE", KW_EXT, A2_TRACE, ""},
    {"NOTRACE", KW_EXT, A2_NOTRACE, ""}, {"NORMAL", KW_EXT, A2_NORMAL, ""},
    {"INVERSE", KW_EXT, A2_INVERSE, ""}, {"FLASH", KW_EXT, A2_FLASH, ""},
    {"COLOR=", KW_EXT, A2_COLOR, "N"}, {"POP", KW_POP}, {"VTAB", KW_EXT, A2_VTAB, "N"},
    {"HIMEM:", KW_EXT, A2_HIMEM, "N"}, {"LOMEM:", KW_EXT, A2_LOMEM, "N"},
    {"ONERR", KW_ONERR}, {"RESUME", KW_RESUME}, {"RECALL", KW_UNSUPPORTED},
    {"STORE", KW_UNSUPPORTED}, {"SPEED=", KW_EXT, A2_SPEED, "N"}, {"LET", KW_LET},
    {"GOTO", KW_GOTO}, {"RUN", KW_RUN}, {"IF", KW_IF}, {"RESTORE", KW_RESTORE},
    {"GOSUB", KW_GOSUB}, {"RETURN", KW_RETURN}, {"REM", KW_REM}, {"STOP", KW_STOP},
    {"ON", KW_ON}, {"WAIT", KW_EXT, A2_WAIT, "N,N?,N"}, {"LOAD", KW_UNSUPPORTED},
    {"SAVE", KW_UNSUPPORTED}, {"DEF", KW_DEF}, {"POKE", KW_EXT, A2_POKE, "N,N"},
    {"PRINT", KW_PRINT}, {"CONT", KW_UNSUPPORTED}, {"LIST", KW_LIST},
    {"CLEAR", KW_CLEAR}, {"GET", KW_GET}, {"NEW", KW_UNSUPPORTED},
    {"TAB(", KW_TAB}, {"TO", KW_TO}, {"FN", KW_FN}, {"SPC(", KW_SPC},
    {"THEN", KW_THEN}, {"AT", KW_AT}, {"NOT", KW_NOT}, {"STEP", KW_STEP},
    {"AND", KW_AND}, {"OR", KW_OR},
    {"SGN", KW_SGN}, {"INT", KW_INT}, {"ABS", KW_ABS}, {"USR", KW_EXTFN, A2_USR, "N"},
    {"FRE", KW_FRE}, {"SCRN(", KW_EXTFN, A2_SCRN, "N,N"}, {"PDL", KW_EXTFN, A2_PDL, "N"},
    {"POS", KW_POS}, {"SQR", KW_SQR}, {"RND", KW_RND}, {"LOG", KW_LOG},
    {"EXP", KW_EXP}, {"COS", KW_COS}, {"SIN", KW_SIN}, {"TAN", KW_TAN},
    {"ATN", KW_ATN}, {"PEEK", KW_EXTFN, A2_PEEK, "N"}, {"LEN", KW_LEN},
    {"STR$", KW_STR}, {"VAL", KW_VAL}, {"ASC", KW_ASC}, {"CHR$", KW_CHR},
    {"LEFT$", KW_LEFT}, {"RIGHT$", KW_RIGHT}, {"MID$", KW_MID},
    {nullptr, KW_NONE},
};

extern const Dialect applesoft_dialect = {
    "applesoft",
    "APPLESOFT",
    {
        .true_value = 1,
        .bitwise_logic = false,
        .for_pretest = false,     // a FOR body always runs once
        .print_digits = 9,
        .zone_width = 16,
        .tab_base = 1,
        .num_lead_space = false,
        .num_trail_space = false,
        .sig_name_chars = 2,
        .crunch_keywords = true,
        .ready = nullptr,
        .prompt = "]",
        .input_mark = "?",
        .mark_after_prompt = false,
        .redo = "?REENTER\n",
        .stmt_markers = true,
    },
    msfamily_compile_line,
    keywords,
    &apple2_machine,
};

}  // namespace basic
