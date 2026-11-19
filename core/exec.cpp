// The executor: runs compiled code.  Also the string heap and console output.
#include "basic.h"
#include "ops.h"
#include "platform.h"

#include <cmath>
#include <cstdio>

namespace basic {

// ---- console ----------------------------------------------------------------

void out_bytes(Interp &in, const char *s, uint32_t len)
{
    if(len == 0) { return; }
    if(in.dialect->machine) {
        in.dialect->machine->write(in, s, len);
        return;
    }
    plat_write(s, len);
    for(uint32_t k = 0; k < len; k++) {
        if(s[k] == '\n' || s[k] == '\r') {
            in.column = 0;
        } else {
            in.column++;
        }
    }
}

void out_char(Interp &in, char c) { out_bytes(in, &c, 1); }
void out_text(Interp &in, const char *s) { out_bytes(in, s, static_cast<uint32_t>(strlen(s))); }

int in_line(Interp &in, char *buf, int cap)
{
    if(in.dialect->machine) { return in.dialect->machine->read_line(in, buf, cap); }
    int len = plat_read_line(buf, cap);
    in.column = 0;
    return len;
}

int in_key(Interp &in)
{
    if(in.dialect->machine) { return in.dialect->machine->read_key(in); }
    return plat_read_key();
}

static void out_spaces(Interp &in, int count)
{
    static const char blanks[] = "                                ";
    while(count > 0) {
        int k = count > 32 ? 32 : count;
        out_bytes(in, blanks, static_cast<uint32_t>(k));
        count -= k;
    }
}

// ---- string heap --------------------------------------------------------------
//
// Strings are immutable byte runs in one buffer, allocated by bumping heap_top.
// When the buffer fills, every live string (reachable from the value stack, a
// string variable or a string array) is copied to a fresh buffer.  Substrings
// share their parent's bytes, so LEFT$/MID$/RIGHT$ allocate nothing.

const char *str_ptr(const Interp &in, StrRef s)
{
    uint32_t off = s.off & ~SP_MASK;
    switch(s.off & SP_MASK) {
        case SP_CODE: return reinterpret_cast<const char *>(in.code.d) + off;
        case SP_DATA: return in.datapool.d + off;
        default: return in.heap + off;
    }
}

template <class F> static void each_string_root(Interp &in, F f)
{
    for(int k = 0; k < in.sp; k++) {
        if(in.stack[k].tag == T_STR) { f(in.stack[k].s); }
    }
    for(uint32_t k = 0; k < in.syms.n; k++) {
        if(in.syms[k].kind != K_STR) { continue; }
        if(in.syms[k].is_array) {
            Array &a = in.arrays[k];
            for(uint32_t e = 0; a.data && e < a.count; e++) { f(a.data[e].s); }
        } else {
            f(in.vars[k].s);
        }
    }
    for(uint32_t k = 0; k < in.fnstack.n; k++) {
        if(in.fnstack[k].saved.tag == T_STR) { f(in.fnstack[k].saved.s); }
    }
}

static void collect(Interp &in, uint32_t need)
{
    uint32_t live = 0;
    each_string_root(in, [&](StrRef &s) {
        if((s.off & SP_MASK) == SP_HEAP) { live += s.len; }
    });
    uint32_t cap = in.heap_cap;
    while(cap < live + need + cap / 4) { cap *= 2; }
    char *fresh = static_cast<char *>(malloc(cap));
    if(!fresh) { raise(in, E_OUT_OF_MEMORY); }
    uint32_t top = 0;
    each_string_root(in, [&](StrRef &s) {
        if((s.off & SP_MASK) == SP_HEAP && s.len > 0) {
            memcpy(fresh + top, in.heap + s.off, s.len);
            s.off = top;
            top += s.len;
        }
    });
    free(in.heap);
    in.heap = fresh;
    in.heap_cap = cap;
    in.heap_top = top;
}

StrRef str_alloc(Interp &in, uint32_t len)
{
    if(len > 0x00FFFFFFu) { raise(in, E_STRING_TOO_LONG); }
    if(in.heap_top + len > in.heap_cap) { collect(in, len); }
    StrRef s{in.heap_top, len};
    in.heap_top += len;
    return s;
}

StrRef str_copy(Interp &in, const char *p, uint32_t len)
{
    // p may point outside the heap only: a collection would invalidate it.
    StrRef s = str_alloc(in, len);
    memcpy(in.heap + s.off, p, len);
    return s;
}

// ---- small helpers -------------------------------------------------------------

static inline uint16_t rd16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static inline double num(const Value &v) { return v.tag == T_INT ? v.i : v.d; }

static inline void push(Interp &in, const Value &v)
{
    if(in.sp >= STACK_SIZE) { raise(in, E_FORMULA_TOO_COMPLEX); }
    in.stack[in.sp++] = v;
}
static inline void push_int(Interp &in, int32_t i)
{
    Value v;
    v.tag = T_INT;
    v.i = i;
    push(in, v);
}
static inline void push_num(Interp &in, double d)
{
    if(std::isinf(d) || std::isnan(d)) { raise(in, E_OVERFLOW); }
    Value v;
    v.tag = T_NUM;
    v.d = d;
    push(in, v);
}
static inline void push_str(Interp &in, StrRef s)
{
    Value v;
    v.tag = T_STR;
    v.s = s;
    push(in, v);
}

// Convert to an integer the way subscripts and counts are: round toward
// minus infinity, and complain if it does not fit.
static int32_t to_int(Interp &in, const Value &v)
{
    if(v.tag == T_INT) { return v.i; }
    double f = std::floor(v.d);
    if(!(f >= -2147483648.0 && f < 2147483648.0)) { raise(in, E_ILLEGAL_QUANTITY); }
    return static_cast<int32_t>(f);
}

static int32_t to_int16(Interp &in, const Value &v)
{
    int32_t i = to_int(in, v);
    if(i < -32768 || i > 32767) { raise(in, E_ILLEGAL_QUANTITY); }
    return i;
}

// A double result that happens to be a whole number goes back to the integer
// fast path.
static inline void push_whole_or_num(Interp &in, double d)
{
    if(d >= -2147483648.0 && d < 2147483648.0 && d == std::floor(d)) {
        push_int(in, static_cast<int32_t>(d));
    } else {
        push_num(in, d);
    }
}

static double next_random(Interp &in)
{
    uint32_t x = in.rnd_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    in.rnd_state = x;
    in.rnd_last = (x >> 8) / 16777216.0;
    return in.rnd_last;
}

// ---- variables ---------------------------------------------------------------

static void dimension(Interp &in, uint16_t slot, int ndims, const int32_t *dims)
{
    Array &a = in.arrays[slot];
    if(a.data) { raise(in, E_REDIMD_ARRAY); }
    if(ndims < 1 || ndims > MAX_DIMS) { raise(in, E_BAD_SUBSCRIPT); }
    uint32_t count = 1;
    for(int k = 0; k < ndims; k++) {
        if(dims[k] < 0) { raise(in, E_ILLEGAL_QUANTITY); }
        uint64_t c = static_cast<uint64_t>(count) * (static_cast<uint32_t>(dims[k]) + 1);
        if(c > 0x00FFFFFFu) { raise(in, E_OUT_OF_MEMORY); }
        count = static_cast<uint32_t>(c);
        a.dim[k] = dims[k];
    }
    a.data = static_cast<Value *>(calloc(count, sizeof(Value)));   // T_INT zeros
    if(!a.data) { raise(in, E_OUT_OF_MEMORY); }
    a.count = count;
    a.ndims = static_cast<uint8_t>(ndims);
    if(in.syms[slot].kind == K_STR) {
        for(uint32_t e = 0; e < count; e++) {
            a.data[e].tag = T_STR;
            a.data[e].s = StrRef{0, 0};
        }
    }
}

// Pop `ndims` subscripts and return the element they select.
static Value *element(Interp &in, uint16_t slot, int ndims)
{
    Array &a = in.arrays[slot];
    int32_t idx[MAX_DIMS];
    if(ndims > MAX_DIMS) { raise(in, E_BAD_SUBSCRIPT); }
    for(int k = ndims - 1; k >= 0; k--) { idx[k] = to_int(in, in.stack[--in.sp]); }
    if(!a.data) {
        int32_t dims[MAX_DIMS];
        for(int k = 0; k < ndims; k++) { dims[k] = 10; }
        dimension(in, slot, ndims, dims);
    }
    if(a.ndims != ndims) { raise(in, E_BAD_SUBSCRIPT); }
    uint32_t off = 0;
    for(int k = 0; k < ndims; k++) {
        if(idx[k] < 0 || idx[k] > a.dim[k]) { raise(in, E_BAD_SUBSCRIPT); }
        off = off * (static_cast<uint32_t>(a.dim[k]) + 1) + static_cast<uint32_t>(idx[k]);
    }
    return &a.data[off];
}

// Convert a value for storage in a variable of the given kind.
static void coerce(Interp &in, uint8_t kind, Value &v)
{
    if(kind == K_INT) {
        int32_t i = to_int16(in, v);
        v.tag = T_INT;
        v.i = i;
    } else if(kind == K_STR) {
        // A literal in a direct-mode line vanishes when the next line is typed.
        if((v.s.off & SP_MASK) == SP_CODE && (v.s.off & ~SP_MASK) >= in.prog_end) {
            v.s = str_copy(in, str_ptr(in, v.s), v.s.len);
        }
    }
}

void clear_variables(Interp &in)
{
    for(uint32_t k = 0; k < in.syms.n; k++) {
        Value &v = in.vars[k];
        if(in.syms[k].kind == K_STR) {
            v.tag = T_STR;
            v.s = StrRef{0, 0};
        } else {
            v.tag = T_INT;
            v.i = 0;
        }
        free(in.arrays[k].data);
        in.arrays[k].data = nullptr;
        in.arrays[k].count = 0;
        in.fns[k].defined = false;
    }
    in.ctrl.clear();
    in.fnstack.clear();
    in.heap_top = 0;
    in.data_index = 0;
}

// ---- instruction lengths (for skipping over code) --------------------------------

static uint32_t op_length(const uint8_t *p)
{
    switch(*p) {
        case OP_ERR: case OP_CMP: case OP_SCMP: case OP_CALL: case OP_READ: case OP_GET:
            return 2;
        case OP_LOAD: case OP_STORE: case OP_FOR: case OP_NEXT: case OP_CALLFN:
        case OP_INPUT_GET:
            return 3;
        case OP_LOADA: case OP_STOREA: case OP_DIM: case OP_EXT: case OP_EXTFN:
            return 4;
        case OP_INT: case OP_RESTORE: case OP_GOTO: case OP_GOSUB: case OP_JF: case OP_JMP:
        case OP_ONERR:
            return 5;
        case OP_NUM: case OP_DEF:
            return 9;
        case OP_STR:
            return 3u + rd16(p + 1);
        case OP_INPUT:
            return 3u + p[2];
        case OP_ON:
            return 3u + 4u * p[2];
        default:
            return 1;
    }
}

// The body of a FOR whose range is empty is skipped: find the matching NEXT.
static uint32_t skip_loop_body(Interp &in, uint32_t pc)
{
    int depth = 0;
    for(;;) {
        uint8_t op = in.code[pc];
        if(op == OP_END_PROGRAM || op == OP_END_DIRECT) { raise(in, E_FOR_WITHOUT_NEXT); }
        if(op == OP_FOR) {
            depth++;
        } else if(op == OP_NEXT) {
            if(depth == 0) { return pc + 3; }
            depth--;
        }
        pc += op_length(&in.code[pc]);
    }
}

// ---- INPUT ---------------------------------------------------------------------

// Split in.inbuf from `start` into comma-separated fields, appending to in.fields.
static void split_fields(Interp &in, uint32_t start)
{
    const char *b = in.inbuf.d;
    uint32_t n = in.inbuf.n;
    uint32_t i = start;
    for(;;) {
        while(i < n && b[i] == ' ') { i++; }
        Field f;
        if(i < n && b[i] == '"') {
            f.off = ++i;
            while(i < n && b[i] != '"') { i++; }
            f.len = i - f.off;
            if(i < n) { i++; }
            while(i < n && b[i] != ',') { i++; }
        } else {
            f.off = i;
            while(i < n && b[i] != ',') { i++; }
            uint32_t end = i;
            while(end > f.off && b[end - 1] == ' ') { end--; }
            f.len = end - f.off;
        }
        in.fields.push(f);
        if(i >= n) { break; }
        i++;                                    // the comma
    }
}

// Returns false at end of input.
static bool do_input(Interp &in, int count, const uint8_t *types, bool prompted)
{
    char line[256];
    const Machine *m = in.dialect->machine;
    for(;;) {
        in.inbuf.clear();
        in.fields.clear();
        bool quiet = m && m->input_redirected(in);
        if(!quiet && (!prompted || in.sem.mark_after_prompt)) { out_text(in, in.sem.input_mark); }
        while(static_cast<int>(in.fields.n) < count) {
            if(in.fields.n > 0 && !quiet) {
                out_char(in, '?');
                out_text(in, in.sem.input_mark);
            }
            int len = in_line(in, line, sizeof(line));
            if(len < 0) { return false; }
            uint32_t start = in.inbuf.n;
            for(int k = 0; k < len; k++) { in.inbuf.push(line[k]); }
            split_fields(in, start);
        }
        bool ok = true;
        for(int k = 0; k < count && ok; k++) {
            Value v;
            if(types[k] != K_STR) {
                ok = parse_number(in.inbuf.d + in.fields[k].off, in.fields[k].len, true, &v);
            }
        }
        if(ok) { break; }
        out_text(in, in.sem.redo);
    }
    if(static_cast<int>(in.fields.n) > count && !(m && m->input_redirected(in))) {
        out_text(in, "?EXTRA IGNORED\n");
    }
    return true;
}

// ---- the executor ----------------------------------------------------------------

static int compare_strings(const Interp &in, StrRef a, StrRef b)
{
    uint32_t n = a.len < b.len ? a.len : b.len;
    int c = n ? memcmp(str_ptr(in, a), str_ptr(in, b), n) : 0;
    if(c != 0) { return c; }
    return a.len < b.len ? -1 : (a.len > b.len ? 1 : 0);
}

static void call_builtin(Interp &in, uint8_t fn)
{
    Value *top = &in.stack[in.sp - 1];
    switch(fn) {
        case FN_ABS:
            if(top->tag == T_INT && top->i != INT32_MIN) {
                if(top->i < 0) { top->i = -top->i; }
            } else {
                double d = std::fabs(num(*top));
                top->tag = T_NUM;
                top->d = d;
            }
            break;
        case FN_INT:
            if(top->tag != T_INT) {
                double d = std::floor(top->d);
                in.sp--;
                push_whole_or_num(in, d);
            }
            break;
        case FN_SGN: {
            double d = num(*top);
            top->tag = T_INT;
            top->i = d < 0 ? -1 : (d > 0 ? 1 : 0);
            break;
        }
        case FN_SQR: {
            double d = num(*top);
            if(d < 0) { raise(in, E_ILLEGAL_QUANTITY); }
            in.sp--;
            push_num(in, std::sqrt(d));
            break;
        }
        case FN_LOG: {
            double d = num(*top);
            if(d <= 0) { raise(in, E_ILLEGAL_QUANTITY); }
            in.sp--;
            push_num(in, std::log(d));
            break;
        }
        case FN_ATN: { double d = num(*top); in.sp--; push_num(in, std::atan(d)); break; }
        case FN_COS: { double d = num(*top); in.sp--; push_num(in, std::cos(d)); break; }
        case FN_SIN: { double d = num(*top); in.sp--; push_num(in, std::sin(d)); break; }
        case FN_TAN: { double d = num(*top); in.sp--; push_num(in, std::tan(d)); break; }
        case FN_EXP: { double d = num(*top); in.sp--; push_num(in, std::exp(d)); break; }
        case FN_RND: {
            double d = num(*top);
            in.sp--;
            if(d < 0) {
                uint32_t seed;
                float f = static_cast<float>(d);
                memcpy(&seed, &f, sizeof(seed));
                in.rnd_state = seed ? seed : 1;
                push_num(in, next_random(in));
            } else if(d == 0) {
                push_num(in, in.rnd_last);
            } else {
                push_num(in, next_random(in));
            }
            break;
        }
        case FN_LEN: {
            uint32_t len = top->s.len;
            top->tag = T_INT;
            top->i = static_cast<int32_t>(len);
            break;
        }
        case FN_VAL: {
            Value v;
            parse_number(str_ptr(in, top->s), top->s.len, false, &v);
            *top = v;
            break;
        }
        case FN_ASC: {
            if(top->s.len == 0) { raise(in, E_ILLEGAL_QUANTITY); }
            int32_t c = static_cast<uint8_t>(*str_ptr(in, top->s));
            top->tag = T_INT;
            top->i = c;
            break;
        }
        case FN_CHR: {
            int32_t c = to_int(in, *top);
            if(c < 0 || c > 255) { raise(in, E_ILLEGAL_QUANTITY); }
            in.sp--;
            char ch = static_cast<char>(c);
            push_str(in, str_copy(in, &ch, 1));
            break;
        }
        case FN_STR: {
            char buf[40];
            int n = 0;
            if(in.sem.num_lead_space && num(*top) >= 0) { buf[n++] = ' '; }
            n += format_number(*top, in.sem, buf + n);
            in.sp--;
            push_str(in, str_copy(in, buf, static_cast<uint32_t>(n)));
            break;
        }
        case FN_LEFT: case FN_RIGHT: {
            int32_t n = to_int(in, *top);
            if(n < 0) { raise(in, E_ILLEGAL_QUANTITY); }
            in.sp--;
            StrRef &s = in.stack[in.sp - 1].s;
            uint32_t take = static_cast<uint32_t>(n) < s.len ? static_cast<uint32_t>(n) : s.len;
            if(fn == FN_RIGHT) { s.off += s.len - take; }
            s.len = take;
            break;
        }
        case FN_MID2: case FN_MID3: {
            int32_t count = 0x7FFFFFFF;
            if(fn == FN_MID3) {
                count = to_int(in, *top);
                if(count < 0) { raise(in, E_ILLEGAL_QUANTITY); }
                in.sp--;
            }
            int32_t start = to_int(in, in.stack[in.sp - 1]);
            if(start < 1) { raise(in, E_ILLEGAL_QUANTITY); }
            in.sp--;
            StrRef &s = in.stack[in.sp - 1].s;
            uint32_t from = static_cast<uint32_t>(start - 1);
            if(from >= s.len) {
                s.len = 0;
            } else {
                uint32_t avail = s.len - from;
                s.off += from;
                s.len = static_cast<uint32_t>(count) < avail ? static_cast<uint32_t>(count) : avail;
            }
            break;
        }
        case FN_FRE:
            in.sp--;
            push_int(in, static_cast<int32_t>(in.heap_cap - in.heap_top));
            break;
        case FN_POS:
            in.sp--;
            push_int(in, in.column);
            break;
        default:
            raise(in, E_NOT_AVAILABLE);
    }
}

// Every backward jump and loop iteration comes through here, so a program
// that never ends can still be interrupted, and the machine gets regular
// turns to keep its display going.
static void break_check(Interp &in)
{
    const Machine *m = in.dialect->machine;
    if(m && m->poll) { m->poll(in); }
    if(plat_break_pending()) { raise(in, E_BREAK); }
}

static void jump(Interp &in, uint32_t target)
{
    if(target == NO_TARGET) { raise(in, E_UNDEFD_STATEMENT); }
    break_check(in);
    in.pc = target;
}

Halt execute(Interp &in, uint32_t start)
{
    in.pc = start;
    for(;;) {
        const uint8_t *code = in.code.d;
        in.opc = in.pc;
        const uint8_t *p = code + in.pc;
        uint8_t op = *p;
        switch(op) {
            case OP_END_PROGRAM:
            case OP_END:
                in.can_cont = false;
                return H_END;
            case OP_END_DIRECT:
                return H_DIRECT_DONE;
            case OP_STOP:
                in.pc += 1;
                in.cont_pc = in.pc;
                in.can_cont = true;
                return H_STOP;
            case OP_ERR:
                raise(in, static_cast<Err>(p[1]));

            // ---- expressions ----
            case OP_INT:
                push_int(in, static_cast<int32_t>(rd32(p + 1)));
                in.pc += 5;
                break;
            case OP_NUM: {
                Value v;
                v.tag = T_NUM;
                memcpy(&v.d, p + 1, 8);
                push(in, v);
                in.pc += 9;
                break;
            }
            case OP_STR: {
                uint16_t len = rd16(p + 1);
                push_str(in, StrRef{SP_CODE | (in.pc + 3), len});
                in.pc += 3u + len;
                break;
            }
            case OP_LOAD:
                push(in, in.vars[rd16(p + 1)]);
                in.pc += 3;
                break;
            case OP_LOADA: {
                Value *e = element(in, rd16(p + 1), p[3]);
                push(in, *e);
                in.pc += 4;
                break;
            }
            case OP_ADD: case OP_SUB: case OP_MUL: {
                Value &b = in.stack[in.sp - 1];
                Value &a = in.stack[in.sp - 2];
                in.sp--;
                if(a.tag == T_INT && b.tag == T_INT) {
                    int32_t r;
                    bool over = op == OP_ADD ? __builtin_add_overflow(a.i, b.i, &r)
                        : op == OP_SUB ? __builtin_sub_overflow(a.i, b.i, &r)
                        : __builtin_mul_overflow(a.i, b.i, &r);
                    if(!over) {
                        a.i = r;
                        in.pc += 1;
                        break;
                    }
                }
                double x = num(a), y = num(b);
                double r = op == OP_ADD ? x + y : op == OP_SUB ? x - y : x * y;
                in.sp--;
                push_num(in, r);
                in.pc += 1;
                break;
            }
            case OP_DIV: {
                Value &b = in.stack[in.sp - 1];
                Value &a = in.stack[in.sp - 2];
                if(num(b) == 0) { raise(in, E_DIVISION_BY_ZERO); }
                in.sp -= 2;
                if(a.tag == T_INT && b.tag == T_INT && b.i != -1 && a.i % b.i == 0) {
                    push_int(in, a.i / b.i);
                } else {
                    push_num(in, num(a) / num(b));
                }
                in.pc += 1;
                break;
            }
            case OP_POW: {
                Value &b = in.stack[in.sp - 1];
                Value &a = in.stack[in.sp - 2];
                double x = num(a), y = num(b);
                bool ints = a.tag == T_INT && b.tag == T_INT && b.i >= 0;
                if(x == 0 && y < 0) { raise(in, E_DIVISION_BY_ZERO); }
                if(x < 0 && y != std::floor(y)) { raise(in, E_ILLEGAL_QUANTITY); }
                double r = std::pow(x, y);
                in.sp -= 2;
                if(ints) {
                    push_whole_or_num(in, r);
                } else {
                    push_num(in, r);
                }
                in.pc += 1;
                break;
            }
            case OP_NEG: {
                Value &a = in.stack[in.sp - 1];
                if(a.tag == T_INT && a.i != INT32_MIN) {
                    a.i = -a.i;
                } else {
                    double d = -num(a);
                    a.tag = T_NUM;
                    a.d = d;
                }
                in.pc += 1;
                break;
            }
            case OP_CAT: {
                uint32_t total = in.stack[in.sp - 2].s.len + in.stack[in.sp - 1].s.len;
                StrRef r = str_alloc(in, total);          // may move the operands
                StrRef a = in.stack[in.sp - 2].s;
                StrRef b = in.stack[in.sp - 1].s;
                memcpy(in.heap + r.off, str_ptr(in, a), a.len);
                memcpy(in.heap + r.off + a.len, str_ptr(in, b), b.len);
                in.sp--;
                in.stack[in.sp - 1].s = r;
                in.pc += 1;
                break;
            }
            case OP_CMP: case OP_SCMP: {
                Value &b = in.stack[in.sp - 1];
                Value &a = in.stack[in.sp - 2];
                int c;
                if(op == OP_SCMP) {
                    c = compare_strings(in, a.s, b.s);
                } else if(a.tag == T_INT && b.tag == T_INT) {
                    c = a.i < b.i ? -1 : (a.i > b.i ? 1 : 0);
                } else {
                    double x = num(a), y = num(b);
                    c = x < y ? -1 : (x > y ? 1 : 0);
                }
                uint8_t bit = c < 0 ? 1 : (c == 0 ? 2 : 4);
                in.sp--;
                a.tag = T_INT;
                a.i = (p[1] & bit) ? in.sem.true_value : 0;
                in.pc += 2;
                break;
            }
            case OP_AND: case OP_OR: {
                Value &b = in.stack[in.sp - 1];
                Value &a = in.stack[in.sp - 2];
                int32_t r;
                if(in.sem.bitwise_logic) {
                    int32_t x = to_int16(in, a), y = to_int16(in, b);
                    r = op == OP_AND ? (x & y) : (x | y);
                } else {
                    bool x = num(a) != 0, y = num(b) != 0;
                    r = (op == OP_AND ? (x && y) : (x || y)) ? 1 : 0;
                }
                in.sp--;
                a.tag = T_INT;
                a.i = r;
                in.pc += 1;
                break;
            }
            case OP_NOT: {
                Value &a = in.stack[in.sp - 1];
                int32_t r = in.sem.bitwise_logic ? ~to_int16(in, a) : (num(a) == 0 ? 1 : 0);
                a.tag = T_INT;
                a.i = r;
                in.pc += 1;
                break;
            }
            case OP_CALL:
                call_builtin(in, p[1]);
                in.pc += 2;
                break;
            case OP_CALLFN: {
                uint16_t slot = rd16(p + 1);
                FnDef &f = in.fns[slot];
                if(!f.defined) { raise(in, E_UNDEFD_FUNCTION); }
                if(in.fnstack.n >= 64) { raise(in, E_FORMULA_TOO_COMPLEX); }
                FnFrame fr;
                fr.saved = in.vars[f.param];
                fr.param = f.param;
                fr.ret = in.pc + 3;
                in.fnstack.push(fr);
                Value arg = in.stack[--in.sp];
                coerce(in, in.syms[f.param].kind, arg);
                in.vars[f.param] = arg;
                in.pc = f.body;
                break;
            }
            case OP_FNRET: {
                FnFrame fr = in.fnstack.back();
                in.fnstack.n--;
                in.vars[fr.param] = fr.saved;
                in.pc = fr.ret;
                break;
            }
            case OP_POP:
                in.sp--;
                in.pc += 1;
                break;

            // ---- assignment and declarations ----
            case OP_STORE: {
                uint16_t slot = rd16(p + 1);
                Value v = in.stack[in.sp - 1];
                coerce(in, in.syms[slot].kind, v);
                in.sp--;
                in.vars[slot] = v;
                in.pc += 3;
                break;
            }
            case OP_STOREA: {
                uint16_t slot = rd16(p + 1);
                coerce(in, in.syms[slot].kind, in.stack[in.sp - 1]);
                Value v = in.stack[--in.sp];
                Value *e = element(in, slot, p[3]);
                *e = v;
                in.pc += 4;
                break;
            }
            case OP_DIM: {
                int ndims = p[3];
                int32_t dims[MAX_DIMS];
                if(ndims > MAX_DIMS) { raise(in, E_BAD_SUBSCRIPT); }
                for(int k = ndims - 1; k >= 0; k--) { dims[k] = to_int(in, in.stack[--in.sp]); }
                dimension(in, rd16(p + 1), ndims, dims);
                in.pc += 4;
                break;
            }
            case OP_DEF: {
                FnDef &f = in.fns[rd16(p + 1)];
                f.param = rd16(p + 3);
                f.body = in.pc + 9;
                f.defined = true;
                in.pc += 9u + rd32(p + 5);
                break;
            }

            // ---- PRINT ----
            case OP_PRINT_NUM: {
                char buf[40];
                Value v = in.stack[--in.sp];
                int n = 0;
                if(in.sem.num_lead_space && num(v) >= 0) { buf[n++] = ' '; }
                n += format_number(v, in.sem, buf + n);
                if(in.sem.num_trail_space) { buf[n++] = ' '; }
                out_bytes(in, buf, static_cast<uint32_t>(n));
                in.pc += 1;
                break;
            }
            case OP_PRINT_STR: {
                StrRef s = in.stack[--in.sp].s;
                out_bytes(in, str_ptr(in, s), s.len);
                in.pc += 1;
                break;
            }
            case OP_PRINT_ZONE:
                out_spaces(in, in.sem.zone_width - in.column % in.sem.zone_width);
                in.pc += 1;
                break;
            case OP_PRINT_NL:
                out_char(in, '\n');
                in.pc += 1;
                break;
            case OP_PRINT_TAB: {
                int32_t col = to_int(in, in.stack[--in.sp]) - in.sem.tab_base;
                if(col > 255) { col = 255; }
                out_spaces(in, col - in.column);
                in.pc += 1;
                break;
            }
            case OP_PRINT_SPC: {
                int32_t n = to_int(in, in.stack[--in.sp]);
                if(n > 255) { n = 255; }
                out_spaces(in, n);
                in.pc += 1;
                break;
            }

            // ---- INPUT, READ, DATA ----
            case OP_INPUT: {
                const Machine *m = in.dialect->machine;
                if(p[1] & 1) {
                    StrRef s = in.stack[--in.sp].s;
                    if(!(m && m->input_redirected(in))) { out_bytes(in, str_ptr(in, s), s.len); }
                }
                if(!do_input(in, p[2], p + 3, p[1] & 1)) { return H_EOF; }
                in.pc += 3u + p[2];
                break;
            }
            case OP_INPUT_GET: {
                Field f = in.fields[p[1]];
                if(p[2] == K_STR) {
                    StrRef s = str_alloc(in, f.len);
                    memcpy(in.heap + s.off, in.inbuf.d + f.off, f.len);
                    push_str(in, s);
                } else {
                    Value v;
                    parse_number(in.inbuf.d + f.off, f.len, true, &v);
                    push(in, v);
                }
                in.pc += 3;
                break;
            }
            case OP_READ: {
                if(in.data_index >= in.data.n) { raise(in, E_OUT_OF_DATA); }
                DataItem &d = in.data[in.data_index++];
                if(p[1] == K_STR) {
                    push_str(in, StrRef{SP_DATA | d.off, d.len});
                } else {
                    Value v;
                    if(!parse_number(in.datapool.d + d.off, d.len, true, &v)) {
                        raise(in, E_SYNTAX);
                    }
                    push(in, v);
                }
                in.pc += 2;
                break;
            }
            case OP_RESTORE: {
                uint32_t line = rd32(p + 1);
                in.data_index = 0;
                if(line != NO_TARGET) {
                    while(in.data_index < in.data.n
                        && in.data[in.data_index].line < static_cast<int32_t>(line)) {
                        in.data_index++;
                    }
                }
                in.pc += 5;
                break;
            }

            // ---- control flow ----
            case OP_GOTO:
                jump(in, rd32(p + 1));
                break;
            case OP_JMP:
                in.pc = rd32(p + 1);
                break;
            case OP_JF: {
                Value v = in.stack[--in.sp];
                if(num(v) == 0) {
                    in.pc = rd32(p + 1);
                } else {
                    in.pc += 5;
                }
                break;
            }
            case OP_GOSUB: {
                if(in.ctrl.n >= 4096) { raise(in, E_OUT_OF_MEMORY); }
                Frame f;
                f.kind = F_GOSUB;
                f.slot = 0;
                f.pc = in.pc + 5;
                f.limit = f.step = 0;
                in.ctrl.push(f);
                jump(in, rd32(p + 1));
                break;
            }
            case OP_RETURN: {
                while(in.ctrl.n > 0 && in.ctrl.back().kind != F_GOSUB) { in.ctrl.n--; }
                if(in.ctrl.n == 0) { raise(in, E_RETURN_WITHOUT_GOSUB); }
                in.pc = in.ctrl.back().pc;
                in.ctrl.n--;
                break;
            }
            case OP_ON: {
                int32_t sel = to_int(in, in.stack[--in.sp]);
                uint8_t count = p[2];
                uint32_t next = in.pc + 3u + 4u * count;
                if(sel < 0 || sel > 255) { raise(in, E_ILLEGAL_QUANTITY); }
                if(sel < 1 || sel > count) {
                    in.pc = next;
                    break;
                }
                if(p[1] == 1) {
                    if(in.ctrl.n >= 4096) { raise(in, E_OUT_OF_MEMORY); }
                    Frame f;
                    f.kind = F_GOSUB;
                    f.slot = 0;
                    f.pc = next;
                    f.limit = f.step = 0;
                    in.ctrl.push(f);
                }
                jump(in, rd32(p + 3 + 4 * (sel - 1)));
                break;
            }
            case OP_FOR: {
                uint16_t slot = rd16(p + 1);
                Value step = in.stack[in.sp - 1];
                Value limit = in.stack[in.sp - 2];
                Value init = in.stack[in.sp - 3];
                in.sp -= 3;
                coerce(in, in.syms[slot].kind, init);
                in.vars[slot] = init;
                // A loop on the same variable replaces one still open in this
                // subroutine.
                for(uint32_t k = in.ctrl.n; k > 0; k--) {
                    Frame &f = in.ctrl[k - 1];
                    if(f.kind == F_GOSUB) { break; }
                    if(f.slot == slot) {
                        in.ctrl.n = k - 1;
                        break;
                    }
                }
                double st = num(step), lim = num(limit), v = num(init);
                if(in.sem.for_pretest && (st >= 0 ? v > lim : v < lim)) {
                    in.pc = skip_loop_body(in, in.pc + 3);
                    break;
                }
                if(in.ctrl.n >= 4096) { raise(in, E_OUT_OF_MEMORY); }
                Frame f;
                f.kind = F_FOR;
                f.slot = slot;
                f.pc = in.pc + 3;
                f.limit = lim;
                f.step = st;
                f.whole = false;
                f.ilimit = f.istep = 0;
                if(st >= -2147483648.0 && st < 2147483648.0 && st == std::floor(st)) {
                    double whole_limit = st >= 0 ? std::floor(lim) : std::ceil(lim);
                    if(whole_limit >= -2147483648.0 && whole_limit < 2147483648.0) {
                        f.whole = true;
                        f.istep = static_cast<int32_t>(st);
                        f.ilimit = static_cast<int32_t>(whole_limit);
                    }
                }
                in.ctrl.push(f);
                in.pc += 3;
                break;
            }
            case OP_NEXT: {
                uint16_t slot = rd16(p + 1);
                uint32_t k = in.ctrl.n;
                while(k > 0 && in.ctrl[k - 1].kind == F_FOR
                    && slot != NO_SLOT && in.ctrl[k - 1].slot != slot) {
                    k--;
                }
                if(k == 0 || in.ctrl[k - 1].kind != F_FOR) { raise(in, E_NEXT_WITHOUT_FOR); }
                in.ctrl.n = k;
                Frame &f = in.ctrl[k - 1];
                Value &var = in.vars[f.slot];
                int32_t r;
                bool again;
                if(f.whole && var.tag == T_INT && !__builtin_add_overflow(var.i, f.istep, &r)) {
                    var.i = r;
                    again = f.istep >= 0 ? r <= f.ilimit : r >= f.ilimit;
                } else {
                    double v = num(var) + f.step;
                    var.tag = T_NUM;
                    var.d = v;
                    again = f.step >= 0 ? v <= f.limit : v >= f.limit;
                }
                if(again) {
                    break_check(in);
                    in.pc = f.pc;
                } else {
                    in.ctrl.n--;
                    in.pc += 3;
                }
                break;
            }

            // ---- miscellaneous statements ----
            case OP_CLEAR:
                clear_variables(in);
                in.pc += 1;
                break;
            case OP_RUN:
                clear_variables(in);
                in.onerr = NO_TARGET;
                in.sp = 0;
                in.pc = 0;
                break;
            case OP_STMT:
                in.stmt_pc = in.pc;
                in.pc += 1;
                break;
            case OP_GET: {
                int key = in_key(in);
                if(key < 0) { return H_EOF; }
                if(p[1] == K_STR) {
                    char ch = static_cast<char>(key);
                    push_str(in, str_copy(in, &ch, 1));
                } else {
                    if(key < '0' || key > '9') { raise(in, E_SYNTAX); }
                    push_int(in, key - '0');
                }
                in.pc += 2;
                break;
            }
            case OP_ONERR:
                in.onerr = rd32(p + 1);
                if(in.onerr == NO_TARGET) { raise(in, E_UNDEFD_STATEMENT); }
                in.pc += 5;
                break;
            case OP_RESUME:
                if(in.err == E_NONE) { raise(in, E_CANT_CONTINUE); }
                in.err = E_NONE;
                in.pc = in.err_pc;
                break;
            case OP_POPSUB:
                while(in.ctrl.n > 0 && in.ctrl.back().kind != F_GOSUB) { in.ctrl.n--; }
                if(in.ctrl.n == 0) { raise(in, E_RETURN_WITHOUT_GOSUB); }
                in.ctrl.n--;
                in.pc += 1;
                break;
            case OP_EXT: case OP_EXTFN: {
                const Machine *m = in.dialect->machine;
                int nargs = p[3];
                if(!m) { raise(in, E_NOT_AVAILABLE); }
                Value result;
                result.tag = T_INT;
                result.i = 0;
                m->ext(in, rd16(p + 1), &in.stack[in.sp - nargs], nargs,
                    op == OP_EXTFN ? &result : nullptr);
                in.sp -= nargs;
                if(op == OP_EXTFN) { push(in, result); }
                in.pc += 4;
                break;
            }
            case OP_WAIT: {
                int32_t ms = to_int(in, in.stack[--in.sp]);
                if(ms > 0) { plat_sleep_ms(static_cast<uint32_t>(ms)); }
                in.pc += 1;
                break;
            }
            case OP_WIDTH:
                in.width = to_int(in, in.stack[--in.sp]);
                in.pc += 1;
                break;
            case OP_RANDOMIZE: {
                uint32_t seed = plat_entropy();
                in.rnd_state = seed ? seed : 1;
                in.pc += 1;
                break;
            }
            default:
                raise(in, E_SYNTAX);
        }
    }
}

// ---- checking a program without running it ------------------------------------

int check_program(Interp &in)
{
    char buf[96];
    int errors = 0;
    compile_program(in);
    for(uint32_t pc = 0; pc < in.prog_end; pc += op_length(&in.code[pc])) {
        if(in.code[pc] != OP_ERR) { continue; }
        Err e = static_cast<Err>(in.code[pc + 1]);
        if(e != E_NOT_AVAILABLE) { errors++; }
        snprintf(buf, sizeof(buf), "%ld: %s\n", static_cast<long>(line_at(in, pc)), error_text(e));
        plat_write(buf, static_cast<uint32_t>(strlen(buf)));
    }
    return errors;
}

}  // namespace basic
