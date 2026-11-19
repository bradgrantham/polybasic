// Microsoft 8K-style BASIC: the dialect of the 1978 "BASIC Computer Games"
// listings.
#include "msfamily.h"

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
    {"MID$", KW_MID},
    {nullptr, KW_NONE},
};

extern const Dialect msbasic_dialect = {
    "msbasic",
    "MICROSOFT",
    {
        .true_value = -1,
        .bitwise_logic = true,
        .for_pretest = true,      // as this project's earlier interpreter did
        .print_digits = 6,
        .zone_width = 14,
        .tab_base = 0,
        .num_lead_space = true,
        .num_trail_space = true,
        .sig_name_chars = 0,
        .crunch_keywords = true,
        .ready = "OK\n",
        .prompt = nullptr,
        .input_mark = "? ",
        .mark_after_prompt = true,
        .redo = "?REDO FROM START\n",
        .stmt_markers = false,
    },
    msfamily_compile_line,
    keywords,
    nullptr,
};

}  // namespace basic
