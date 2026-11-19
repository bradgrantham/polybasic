// The compiled form every front end produces and the executor runs: a byte
// stream of stack-machine instructions.  Operands follow the opcode, little
// endian.  Expression types are checked by the front end, so each instruction
// knows whether it is working on numbers or strings.
#ifndef BASIC_OPS_H
#define BASIC_OPS_H

#include <cstdint>

namespace basic {

enum Op : uint8_t {
    OP_END_PROGRAM,   // fell off the end of the program
    OP_END_DIRECT,    // end of a direct-mode line
    OP_ERR,           // u8 error: a statement that did not compile

    // Expressions.
    OP_INT,           // i32
    OP_NUM,           // f64
    OP_STR,           // u16 length, bytes
    OP_LOAD,          // u16 symbol
    OP_LOADA,         // u16 symbol, u8 ndims          (subscripts on the stack)
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_NEG,
    OP_CAT,           // string concatenation
    OP_CMP,           // u8 mask (1 '<', 2 '=', 4 '>'), numbers
    OP_SCMP,          // u8 mask, strings
    OP_AND, OP_OR, OP_NOT,
    OP_CALL,          // u8 built-in function
    OP_CALLFN,        // u16 symbol of a DEF FN function
    OP_FNRET,
    OP_POP,

    // Statements.
    OP_STORE,         // u16 symbol
    OP_STOREA,        // u16 symbol, u8 ndims          (subscripts, then value)
    OP_DIM,           // u16 symbol, u8 ndims
    OP_DEF,           // u16 function symbol, u16 parameter symbol, u32 body length
    OP_PRINT_NUM, OP_PRINT_STR, OP_PRINT_ZONE, OP_PRINT_NL, OP_PRINT_TAB, OP_PRINT_SPC,
    OP_INPUT,         // u8 flags (1: prompt on stack), u8 n, n type bytes
    OP_INPUT_GET,     // u8 field index, u8 type
    OP_READ,          // u8 type
    OP_RESTORE,       // u32 line number, or NO_TARGET
    OP_GOTO,          // u32 code offset
    OP_GOSUB,         // u32 code offset
    OP_RETURN,
    OP_ON,            // u8 kind (0 GOTO, 1 GOSUB), u8 n, n * u32 code offsets
    OP_JF,            // u32 code offset: jump if the condition is zero
    OP_JMP,           // u32 code offset
    OP_FOR,           // u16 symbol                    (start, limit, step on the stack)
    OP_NEXT,          // u16 symbol, or NO_SLOT
    OP_END, OP_STOP, OP_CLEAR, OP_RUN,
    OP_WAIT, OP_WIDTH, OP_RANDOMIZE,

    OP_STMT,          // start of a statement (only when Semantics::stmt_markers)
    OP_GET,           // u8 type: read one key and push it
    OP_ONERR,         // u32 code offset of the handler
    OP_RESUME,        // run the statement that raised the trapped error again
    OP_POPSUB,        // forget the innermost GOSUB
    OP_EXT,           // u16 id, u8 nargs: machine statement
    OP_EXTFN,         // u16 id, u8 nargs: machine function, pushes a number
};

// Built-in functions for OP_CALL.
enum Fn : uint8_t {
    FN_ABS, FN_ATN, FN_COS, FN_EXP, FN_INT, FN_LOG, FN_RND, FN_SGN, FN_SIN,
    FN_SQR, FN_TAN, FN_LEN, FN_VAL, FN_ASC, FN_CHR, FN_STR, FN_LEFT, FN_RIGHT,
    FN_MID2, FN_MID3, FN_FRE, FN_POS,
};

}  // namespace basic

#endif
