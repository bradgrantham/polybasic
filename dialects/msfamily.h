// Front end shared by the Microsoft-family dialects (Microsoft 8K / Ahl,
// Applesoft, Griffin).  A dialect supplies a keyword table; the keywords it
// leaves out simply do not exist in that dialect.
#ifndef BASIC_MSFAMILY_H
#define BASIC_MSFAMILY_H

#include "../core/basic.h"

namespace basic {

enum Kw : uint8_t {
    KW_NONE,
    // Statements.
    KW_END, KW_FOR, KW_NEXT, KW_DATA, KW_INPUT, KW_DIM, KW_READ, KW_LET,
    KW_GOTO, KW_RUN, KW_IF, KW_RESTORE, KW_GOSUB, KW_RETURN, KW_REM, KW_STOP,
    KW_ON, KW_WAIT, KW_DEF, KW_PRINT, KW_CLEAR, KW_WIDTH, KW_RANDOMIZE, KW_GO,
    KW_GET, KW_ONERR, KW_RESUME, KW_POP,
    KW_EXT,                       // machine statement: Keyword::id and ::sig
    KW_HPLOT,                     // machine statement with its own syntax
    KW_UNSUPPORTED,               // recognised, but fails if executed
    // Words inside statements.
    KW_TAB, KW_SPC, KW_TO, KW_SUB, KW_FN, KW_THEN, KW_ELSE, KW_STEP,
    KW_NOT, KW_AND, KW_OR, KW_AT,
    KW_EXTFN,                     // machine function: Keyword::id and ::sig
    // Functions (kept together: see is_function in msfamily.cpp).
    KW_SGN, KW_INT, KW_ABS, KW_FRE, KW_POS, KW_SQR, KW_RND, KW_LOG, KW_EXP,
    KW_COS, KW_SIN, KW_TAN, KW_ATN, KW_LEN, KW_STR, KW_VAL, KW_ASC, KW_CHR,
    KW_LEFT, KW_RIGHT, KW_MID,
};

struct Keyword {
    const char *text;             // upper case; may end in '$' or '('
    uint8_t kw;
    // For KW_EXT, KW_HPLOT and KW_EXTFN: the id handed to Machine::ext, and
    // the argument syntax.  In `sig`, N is a numeric expression, S a string
    // expression, ',' a comma, '@' the word AT, and everything after a '?'
    // may be left out.
    uint16_t id;
    const char *sig;
};

// The table is terminated by an entry with a null text.
void msfamily_compile_line(Compiler &c, const char *text);

}  // namespace basic

#endif
