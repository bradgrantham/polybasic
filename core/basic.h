// Core of the interpreter: values, program store, compiled code, executor.
//
// Rules for everything under core/ and dialects/ (they are compiled for the
// 68000 as well as for the host):
//   - no exceptions, no RTTI, no iostreams, no node-based standard containers;
//   - BASIC errors leave through one longjmp to Interp::trap, so no object
//     with a destructor may be live across a call that can raise an error;
//   - multi-byte operands in the code stream are read bytewise or with
//     memcpy, because the 68000 faults on unaligned word access.
#ifndef BASIC_CORE_H
#define BASIC_CORE_H

#include <csetjmp>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace basic {

// ---- errors ---------------------------------------------------------------

enum Err : uint8_t {
    E_NONE, E_NEXT_WITHOUT_FOR, E_SYNTAX, E_RETURN_WITHOUT_GOSUB, E_OUT_OF_DATA,
    E_ILLEGAL_QUANTITY, E_OVERFLOW, E_OUT_OF_MEMORY, E_UNDEFD_STATEMENT,
    E_BAD_SUBSCRIPT, E_REDIMD_ARRAY, E_DIVISION_BY_ZERO, E_TYPE_MISMATCH,
    E_STRING_TOO_LONG, E_FORMULA_TOO_COMPLEX, E_CANT_CONTINUE,
    E_UNDEFD_FUNCTION, E_FOR_WITHOUT_NEXT, E_NOT_AVAILABLE, E_FILE,
    // Errors from a machine's disk operating system (Apple DOS 3.3 wording).
    E_DOS_LANGUAGE_NOT_AVAILABLE, E_DOS_RANGE, E_DOS_WRITE_PROTECTED,
    E_DOS_END_OF_DATA, E_DOS_FILE_NOT_FOUND, E_DOS_IO, E_DOS_SYNTAX,
    E_DOS_NO_BUFFERS, E_DOS_FILE_TYPE_MISMATCH,
    E_BREAK,                      // not an error: Ctrl-C
    E_CHAIN,                      // not an error: run the program in Interp::chain_file
    E_COUNT
};
inline bool is_dos_error(Err e) { return e >= E_DOS_LANGUAGE_NOT_AVAILABLE && e <= E_DOS_FILE_TYPE_MISMATCH; }

const char *error_text(Err e);
[[noreturn]] void out_of_memory();

// ---- a growable array of trivially copyable elements ------------------------

template <class T> struct Vec {
    T *d = nullptr;
    uint32_t n = 0, cap = 0;

    void reserve(uint32_t want)
    {
        if(want <= cap) { return; }
        uint32_t c = cap ? cap * 2 : 16;
        if(c < want) { c = want; }
        T *nd = static_cast<T *>(realloc(d, static_cast<size_t>(c) * sizeof(T)));
        if(!nd) { out_of_memory(); }
        d = nd;
        cap = c;
    }
    void push(const T &v) { reserve(n + 1); d[n++] = v; }
    void resize(uint32_t count) { reserve(count); n = count; }
    void clear() { n = 0; }
    T &operator[](uint32_t i) { return d[i]; }
    const T &operator[](uint32_t i) const { return d[i]; }
    T &back() { return d[n - 1]; }
    // Open a gap of `count` elements at index `at`.
    void insert_gap(uint32_t at, uint32_t count)
    {
        reserve(n + count);
        memmove(d + at + count, d + at, static_cast<size_t>(n - at) * sizeof(T));
        n += count;
    }
};

// ---- values ---------------------------------------------------------------

// A string is a (space, offset, length) reference; nothing owns its bytes.
// The top two bits of `off` say which buffer the offset is in.
struct StrRef {
    uint32_t off;
    uint32_t len;
};
enum : uint32_t {
    SP_HEAP = 0u << 30,           // the garbage-collected string heap
    SP_CODE = 1u << 30,           // a literal inside the compiled code
    SP_DATA = 2u << 30,           // a DATA item
    SP_MASK = 3u << 30,
};

enum : uint8_t { T_INT = 0, T_NUM = 1, T_STR = 2 };   // T_INT must be 0 (calloc)

struct Value {
    uint8_t tag;
    union {
        int32_t i;
        double d;
        StrRef s;
    };
};

// ---- symbols and variables --------------------------------------------------

enum : uint8_t { K_NUM, K_INT, K_STR, K_FN };   // what a name denotes

struct Symbol {
    uint32_t name_off;            // into Interp::names
    uint8_t name_len;
    uint8_t kind;
    bool is_array;
};

enum { MAX_DIMS = 8 };

struct Array {
    Value *data;                  // null until dimensioned
    uint32_t count;
    uint8_t ndims;
    int32_t dim[MAX_DIMS];        // highest valid subscript in each dimension
};

struct FnDef {
    uint32_t body;                // code offset of the expression
    uint16_t param;               // symbol of the parameter variable
    bool defined;
};

// ---- program and compiled code ---------------------------------------------

struct SrcLine { int32_t number; uint32_t off; };              // text in Interp::text
struct LineAddr { int32_t number; uint32_t code_off; };
struct DataItem { uint32_t off; uint32_t len; int32_t line; };  // in Interp::datapool

enum : uint8_t { F_FOR, F_GOSUB };
struct Frame {
    uint8_t kind;
    uint16_t slot;                // F_FOR: loop variable
    uint32_t pc;                  // F_FOR: loop body; F_GOSUB: return address
    double limit, step;
};
struct FnFrame { Value saved; uint16_t param; uint32_t ret; };
struct Field { uint32_t off; uint32_t len; };

const uint32_t NO_TARGET = 0xFFFFFFFFu;
const uint16_t NO_SLOT = 0xFFFFu;

// ---- dialects ---------------------------------------------------------------

struct Compiler;

// Behaviour that differs between dialects, consulted at run time.
struct Semantics {
    int8_t true_value;            // result of a true comparison: 1 or -1
    bool bitwise_logic;           // AND/OR/NOT work on 16-bit integers
    bool for_pretest;             // FOR skips the body when the range is empty
    uint8_t print_digits;         // significant digits when printing
    uint8_t zone_width;           // PRINT comma zones
    uint8_t tab_base;             // number of the leftmost column for TAB()
    bool num_lead_space;          // non-negative numbers print a leading space
    bool num_trail_space;         // numbers print a trailing space
    uint8_t sig_name_chars;       // significant characters in a name, 0 = all
    bool crunch_keywords;         // keywords are found inside identifiers
    const char *ready;            // printed when returning to direct mode, or null
    const char *prompt;           // printed before each direct-mode line, or null
    const char *input_mark;       // what INPUT prints to ask: "? "
    bool mark_after_prompt;       // INPUT "TEXT";A still prints the mark
    const char *redo;             // complaint when INPUT gets a bad number
    bool stmt_markers;            // compile OP_STMT (needed for ONERR/RESUME)
};

// A machine a dialect runs on (the Apple II for Applesoft).  It owns the
// console, so it can model a screen and intercept output, and it implements
// the dialect's extension statements and functions.  Any hook may raise.
struct Interp;
struct Value;
struct Machine {
    // Console output; must leave in.column at the cursor's column.
    void (*write)(Interp &, const char *s, uint32_t len);
    // True while INPUT is being fed from somewhere other than the keyboard,
    // so no prompt should be shown.
    bool (*input_redirected)(Interp &);
    // As plat_read_line and plat_read_key.
    int (*read_line)(Interp &, char *buf, int cap);
    int (*read_key)(Interp &);
    // An extension statement (result null) or function with its arguments.
    void (*ext)(Interp &, uint16_t id, Value *args, int nargs, Value *result);
    // The program stopped or an error was trapped: console I/O goes back to
    // the screen and keyboard.
    void (*reset_io)(Interp &);
    // Flush files and put the terminal back the way it was found.
    void (*finish)(Interp &);
    // Called on every backward jump and loop iteration (may be null): a turn
    // for the machine to refresh a display and look at its window.
    void (*poll)(Interp &);
};

struct Dialect {
    const char *name;             // for --dialect
    const char *header;           // "REM <header> BASIC" at the top of a file
    Semantics sem;
    // Compile one line of source (no line number) by appending to the code.
    // Must not raise: a statement that does not parse becomes an OP_ERR.
    void (*compile_line)(Compiler &, const char *text);
    const void *frontend;         // front-end data, e.g. its keyword table
    const Machine *machine;       // null: plain console straight to the platform
};

// The dialects linked into this build, null terminated; the first is the default.
extern const Dialect *const dialects[];

// ---- the interpreter ----------------------------------------------------------

enum { STACK_SIZE = 256 };
enum Halt : uint8_t { H_END, H_STOP, H_DIRECT_DONE, H_EOF };

struct Interp {
    const Dialect *dialect;
    Semantics sem;

    // Program source.
    Vec<char> text;
    Vec<SrcLine> lines;
    bool dirty;                   // source changed since it was compiled

    // Compiled program; a direct-mode line is compiled after prog_end.
    Vec<uint8_t> code;
    Vec<LineAddr> linetab;
    Vec<uint32_t> fixups;         // code offsets holding line numbers to resolve
    uint32_t prog_end;
    Vec<DataItem> data;
    Vec<char> datapool;
    uint32_t data_index;

    // Symbols; vars, arrays and fns are indexed by symbol.
    Vec<char> names;
    Vec<Symbol> syms;
    Vec<Value> vars;
    Vec<Array> arrays;
    Vec<FnDef> fns;

    // String heap.
    char *heap;
    uint32_t heap_cap, heap_top;

    // Execution state.
    Value stack[STACK_SIZE];
    int sp;
    Vec<Frame> ctrl;
    Vec<FnFrame> fnstack;
    uint32_t pc;                  // next instruction
    uint32_t opc;                 // instruction being executed (for errors)
    uint32_t cont_pc;
    bool can_cont;
    int column;
    int width;
    uint32_t rnd_state;
    double rnd_last;
    Vec<char> inbuf;
    Vec<Field> fields;
    jmp_buf *trap;

    // Error trapping (ONERR GOTO / RESUME) and program chaining.
    bool dialect_locked;          // chosen on the command line: ignore file headers
    uint32_t stmt_pc;             // start of the statement being executed
    uint32_t onerr;               // handler's code offset, or NO_TARGET
    uint32_t err_pc;              // statement that raised the trapped error
    uint8_t err;                  // the trapped error
    int32_t err_line;
    char chain_file[512];
};

void init(Interp &in, const Dialect *d);

// Raise a BASIC error (longjmp to in.trap).
[[noreturn]] void raise(Interp &in, Err e);

// Program store.
void new_program(Interp &in);
void store_line(Interp &in, int32_t number, const char *text);   // empty text deletes
// Store `line` if it starts with a line number, and say whether it did.
// Rewrites it in place: <CTRL-D> and the like become the characters they name.
bool enter_line(Interp &in, char *line);
void list_program(Interp &in, int32_t from, int32_t to);
// E_NONE, E_FILE, or E_DOS_LANGUAGE_NOT_AVAILABLE when the file's header line
// names a dialect that is not linked in.
Err load_program(Interp &in, const char *filename);
bool save_program(Interp &in, const char *filename);

// Compiling and running.
void compile_program(Interp &in);
void clear_variables(Interp &in);
Err run_program(Interp &in);                       // RUN; returns the error, if any
Err run_direct(Interp &in, const char *line);      // one direct-mode line
Err continue_program(Interp &in);                  // CONT
void repl(Interp &in);                             // read and obey lines until EOF
Halt execute(Interp &in, uint32_t pc);             // executor proper; raises on error
// Compile and list the statements that did not compile, one per line of
// output.  Returns how many are real errors (not merely unsupported).
int check_program(Interp &in);
// Stop the running program and run the one in `filename` instead.
[[noreturn]] void chain(Interp &in, const char *filename);
int line_at(const Interp &in, uint32_t pc);        // line number, or -1

// Console output with column tracking.
void out_char(Interp &in, char c);
void out_text(Interp &in, const char *s);
void out_bytes(Interp &in, const char *s, uint32_t len);
// Console input through the machine, if the dialect has one.
int in_line(Interp &in, char *buf, int cap);
int in_key(Interp &in);

// Strings.
const char *str_ptr(const Interp &in, StrRef s);
StrRef str_alloc(Interp &in, uint32_t len);        // may collect: re-read operands after
StrRef str_copy(Interp &in, const char *p, uint32_t len);

// Numbers.
int format_number(const Value &v, const Semantics &sem, char *out);   // no padding
bool parse_number(const char *s, uint32_t len, bool whole, Value *out);

// ---- code generation interface for front ends --------------------------------

struct Compiler {
    Interp &in;
    int32_t cur_line;             // line being compiled, -1 in direct mode

    uint32_t here() const { return in.code.n; }
    void e8(uint8_t b) { in.code.push(b); }
    void e16(uint16_t v) { e8(v & 0xFF); e8(v >> 8); }
    void e32(uint32_t v) { e16(v & 0xFFFF); e16(v >> 16); }
    void e64(double v);
    void patch32(uint32_t at, uint32_t v);
    // Emit a 32-bit operand that holds a line number now and the code offset
    // of that line once the whole program has been compiled.
    void line_ref(int32_t number);
    // Throw away everything emitted from `at` on (a statement that failed).
    void truncate(uint32_t at);
    // Find or create a symbol; the name must already be upper case.
    uint16_t symbol(const char *name, uint32_t len, uint8_t kind, bool is_array);
    void data_item(const char *p, uint32_t len);
};

}  // namespace basic

#endif
