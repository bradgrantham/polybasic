// Program store, compile driver, error reporting and the direct-mode loop.
#include "basic.h"
#include "ops.h"
#include "platform.h"

#include <cstdio>

namespace basic {

// ---- errors ---------------------------------------------------------------

static const char *const error_names[E_COUNT] = {
    "", "NEXT WITHOUT FOR", "SYNTAX", "RETURN WITHOUT GOSUB", "OUT OF DATA",
    "ILLEGAL QUANTITY", "OVERFLOW", "OUT OF MEMORY", "UNDEF'D STATEMENT",
    "BAD SUBSCRIPT", "REDIM'D ARRAY", "DIVISION BY ZERO", "TYPE MISMATCH",
    "STRING TOO LONG", "FORMULA TOO COMPLEX", "CAN'T CONTINUE",
    "UNDEF'D FUNCTION", "FOR WITHOUT NEXT", "NOT AVAILABLE", "FILE",
    "LANGUAGE NOT AVAILABLE", "RANGE ERROR", "WRITE PROTECTED", "END OF DATA",
    "FILE NOT FOUND", "I/O ERROR", "SYNTAX ERROR", "NO BUFFERS AVAILABLE",
    "FILE TYPE MISMATCH", "BREAK", "CHAIN",
};

const char *error_text(Err e) { return e < E_COUNT ? error_names[e] : "?"; }

static Interp *current;          // for out_of_memory(), which has no Interp

void raise(Interp &in, Err e)
{
    longjmp(*in.trap, e);
}

void chain(Interp &in, const char *filename)
{
    snprintf(in.chain_file, sizeof(in.chain_file), "%s", filename);
    raise(in, E_CHAIN);
}

void out_of_memory()
{
    if(current && current->trap) { longjmp(*current->trap, E_OUT_OF_MEMORY); }
    abort();
}

// ---- setup ------------------------------------------------------------------

void init(Interp &in, const Dialect *d)
{
    memset(static_cast<void *>(&in), 0, sizeof(in));
    in.dialect = d;
    in.sem = d->sem;
    in.heap_cap = 16384;
    in.heap = static_cast<char *>(malloc(in.heap_cap));
    in.rnd_state = 0x2545F491u;
    in.width = 80;
    in.dirty = true;
    in.onerr = NO_TARGET;
    current = &in;
}

void new_program(Interp &in)
{
    clear_variables(in);
    in.text.clear();
    in.lines.clear();
    in.syms.clear();
    in.names.clear();
    in.vars.clear();
    in.arrays.clear();
    in.fns.clear();
    in.dirty = true;
    in.can_cont = false;
}

// ---- program text -------------------------------------------------------------

static uint32_t find_line(const Interp &in, int32_t number)
{
    uint32_t lo = 0, hi = in.lines.n;
    while(lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if(in.lines[mid].number < number) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;                    // first line with number >= `number`
}

void store_line(Interp &in, int32_t number, const char *text)
{
    uint32_t at = find_line(in, number);
    bool exists = at < in.lines.n && in.lines[at].number == number;
    in.dirty = true;
    in.can_cont = false;
    if(*text == 0) {
        if(exists) {
            memmove(&in.lines[at], &in.lines[at + 1],
                (in.lines.n - at - 1) * sizeof(SrcLine));
            in.lines.n--;
        }
        return;
    }
    uint32_t off = in.text.n;
    uint32_t len = static_cast<uint32_t>(strlen(text)) + 1;
    in.text.resize(off + len);
    memcpy(in.text.d + off, text, len);
    if(!exists) {
        in.lines.insert_gap(at, 1);
    }
    in.lines[at] = SrcLine{number, off};
}

// If `line` starts with a line number, store it and return true.
static void unescape(char *line);

bool enter_line(Interp &in, char *line)
{
    unescape(line);
    const char *p = line;
    while(*p == ' ') { p++; }
    if(*p < '0' || *p > '9') { return false; }
    int32_t number = 0;
    while(*p >= '0' && *p <= '9') {
        if(number > 214748363) { return false; }
        number = number * 10 + (*p++ - '0');
    }
    while(*p == ' ') { p++; }
    store_line(in, number, p);
    return true;
}

// Program files spell a control character inside a line as <CTRL-D>, <DEL> or
// <$9B>, so that they stay plain text; turn those back into the character.
static void unescape(char *line)
{
    char *o = line;
    for(const char *p = line; *p; ) {
        if(p[0] == '<') {
            if(strncmp(p, "<CTRL-", 6) == 0 && p[6] >= '@' && p[6] <= '_' && p[7] == '>') {
                *o++ = static_cast<char>(p[6] - 64);
                p += 8;
                continue;
            }
            if(strncmp(p, "<DEL>", 5) == 0) {
                *o++ = 0x7F;
                p += 5;
                continue;
            }
            if(p[1] == '$' && p[2] && p[3] && p[4] == '>') {
                char hex[3] = {p[2], p[3], 0};
                char *end;
                long v = strtol(hex, &end, 16);
                if(*end == 0 && v > 0) {
                    *o++ = static_cast<char>(v);
                    p += 5;
                    continue;
                }
            }
        }
        *o++ = *p++;
    }
    *o = 0;
}

// The reverse, for LIST and SAVE: append `text` to `out` with control
// characters spelled out.
static void escape_into(Vec<char> &out, const char *text)
{
    for(const char *p = text; *p; p++) {
        unsigned char c = static_cast<unsigned char>(*p);
        if(c >= 0x20 && c != 0x7F) {
            out.push(*p);
        } else if(c == 0x7F) {
            for(const char *q = "<DEL>"; *q; q++) { out.push(*q); }
        } else {
            for(const char *q = "<CTRL-"; *q; q++) { out.push(*q); }
            out.push(static_cast<char>(c + 64));
            out.push('>');
        }
    }
}

void list_program(Interp &in, int32_t from, int32_t to)
{
    char num[16];
    for(uint32_t k = find_line(in, from); k < in.lines.n && in.lines[k].number <= to; k++) {
        snprintf(num, sizeof(num), "%ld ", static_cast<long>(in.lines[k].number));
        out_text(in, num);
        Vec<char> shown;
        escape_into(shown, in.text.d + in.lines[k].off);
        out_bytes(in, shown.d, shown.n);
        free(shown.d);
        out_char(in, '\n');
        if(plat_break_pending()) { break; }
    }
}

// The first line of a file may name its dialect: "REM APPLESOFT BASIC".
// Returns false if it names one that is not linked in.
static bool header_dialect(Interp &in, const char *line)
{
    const char *p = line;
    while(*p == ' ') { p++; }
    if(strncmp(p, "REM ", 4) != 0) { return true; }
    p += 4;
    const char *word = p;
    while(*p && *p != ' ') { p++; }
    if(strcmp(p, " BASIC") != 0) { return true; }
    uint32_t n = static_cast<uint32_t>(p - word);
    for(int k = 0; dialects[k]; k++) {
        if(strlen(dialects[k]->header) == n && memcmp(dialects[k]->header, word, n) == 0) {
            if(!in.dialect_locked) {
                in.dialect = dialects[k];
                in.sem = dialects[k]->sem;
            }
            return true;
        }
    }
    return in.dialect_locked;
}

Err load_program(Interp &in, const char *filename)
{
    uint32_t len;
    char *data = plat_read_file(filename, &len);
    if(!data) { return E_FILE; }
    new_program(in);
    char line[1024];
    uint32_t i = 0;
    bool first = true;
    Err result = E_NONE;
    while(i < len) {
        uint32_t n = 0;
        while(i < len && data[i] != '\n') {
            if(data[i] != '\r' && n < sizeof(line) - 1) { line[n++] = data[i]; }
            i++;
        }
        i++;
        line[n] = 0;
        if(first && !header_dialect(in, line)) {
            result = E_DOS_LANGUAGE_NOT_AVAILABLE;
            break;
        }
        first = false;
        enter_line(in, line);     // lines without a number are ignored
    }
    free(data);
    return result;
}

bool save_program(Interp &in, const char *filename)
{
    Vec<char> out;
    char num[16];
    for(uint32_t k = 0; k < in.lines.n; k++) {
        int n = snprintf(num, sizeof(num), "%ld ", static_cast<long>(in.lines[k].number));
        for(int j = 0; j < n; j++) { out.push(num[j]); }
        escape_into(out, in.text.d + in.lines[k].off);
        out.push('\n');
    }
    bool ok = plat_write_file(filename, out.d ? out.d : "", out.n);
    free(out.d);
    return ok;
}

// ---- code generation ------------------------------------------------------------

void Compiler::e64(double v)
{
    uint8_t b[8];
    memcpy(b, &v, 8);
    for(int k = 0; k < 8; k++) { e8(b[k]); }
}

void Compiler::patch32(uint32_t at, uint32_t v)
{
    for(int k = 0; k < 4; k++) { in.code[at + k] = static_cast<uint8_t>(v >> (8 * k)); }
}

void Compiler::line_ref(int32_t number)
{
    in.fixups.push(here());
    e32(static_cast<uint32_t>(number));
}

void Compiler::truncate(uint32_t at)
{
    in.code.n = at;
    while(in.fixups.n > 0 && in.fixups.back() >= at) { in.fixups.n--; }
}

uint16_t Compiler::symbol(const char *name, uint32_t len, uint8_t kind, bool is_array)
{
    uint32_t sig = in.sem.sig_name_chars;
    if(sig && len > sig) { len = sig; }
    if(len > 255) { len = 255; }
    for(uint32_t k = 0; k < in.syms.n; k++) {
        const Symbol &s = in.syms[k];
        if(s.kind == kind && s.is_array == is_array && s.name_len == len
            && memcmp(in.names.d + s.name_off, name, len) == 0) {
            return static_cast<uint16_t>(k);
        }
    }
    Symbol s;
    s.name_off = in.names.n;
    s.name_len = static_cast<uint8_t>(len);
    s.kind = kind;
    s.is_array = is_array;
    for(uint32_t k = 0; k < len; k++) { in.names.push(name[k]); }
    in.syms.push(s);

    Value v;
    if(kind == K_STR) {
        v.tag = T_STR;
        v.s = StrRef{0, 0};
    } else {
        v.tag = T_INT;
        v.i = 0;
    }
    in.vars.push(v);
    Array a;
    memset(&a, 0, sizeof(a));
    in.arrays.push(a);
    FnDef f;
    memset(&f, 0, sizeof(f));
    in.fns.push(f);
    return static_cast<uint16_t>(in.syms.n - 1);
}

void Compiler::data_item(const char *p, uint32_t len)
{
    DataItem d;
    d.off = in.datapool.n;
    d.len = len;
    d.line = cur_line;
    for(uint32_t k = 0; k < len; k++) { in.datapool.push(p[k]); }
    in.data.push(d);
}

// Replace each recorded line number with that line's code offset.
static void resolve_fixups(Interp &in)
{
    for(uint32_t k = 0; k < in.fixups.n; k++) {
        uint8_t *p = &in.code[in.fixups[k]];
        int32_t number = static_cast<int32_t>(static_cast<uint32_t>(p[0])
            | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
            | (static_cast<uint32_t>(p[3]) << 24));
        uint32_t target = NO_TARGET;
        uint32_t lo = 0, hi = in.linetab.n;
        while(lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if(in.linetab[mid].number < number) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if(lo < in.linetab.n && in.linetab[lo].number == number) {
            target = in.linetab[lo].code_off;
        }
        for(int j = 0; j < 4; j++) { p[j] = static_cast<uint8_t>(target >> (8 * j)); }
    }
    in.fixups.clear();
}

void compile_program(Interp &in)
{
    clear_variables(in);
    in.code.clear();
    in.linetab.clear();
    in.fixups.clear();
    in.data.clear();
    in.datapool.clear();
    Compiler c{in, -1};
    for(uint32_t k = 0; k < in.lines.n; k++) {
        in.linetab.push(LineAddr{in.lines[k].number, c.here()});
        c.cur_line = in.lines[k].number;
        in.dialect->compile_line(c, in.text.d + in.lines[k].off);
    }
    c.e8(OP_END_PROGRAM);
    resolve_fixups(in);
    in.prog_end = in.code.n;
    in.dirty = false;
    in.can_cont = false;
    in.onerr = NO_TARGET;
    in.err = E_NONE;
}

// ---- running --------------------------------------------------------------------

// The line containing code offset `pc`, or -1 for a direct-mode line.
int line_at(const Interp &in, uint32_t pc)
{
    if(pc >= in.prog_end || in.linetab.n == 0) { return -1; }
    uint32_t lo = 0, hi = in.linetab.n;
    while(hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        if(in.linetab[mid].code_off <= pc) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return in.linetab[lo].number;
}

static void report(Interp &in, const char *what, bool is_error, uint32_t pc)
{
    char buf[80];
    if(in.column != 0) { out_char(in, '\n'); }
    int32_t line = line_at(in, pc);
    if(line >= 0) {
        snprintf(buf, sizeof(buf), "%s%s%s IN %ld\n", is_error ? "?" : "", what,
            is_error ? " ERROR" : "", static_cast<long>(line));
    } else {
        snprintf(buf, sizeof(buf), "%s%s%s\n", is_error ? "?" : "", what,
            is_error ? " ERROR" : "");
    }
    out_text(in, buf);
}

static void report_error(Interp &in, Err e, uint32_t pc)
{
    if(is_dos_error(e)) {
        // The disk system's own message, then where the program stopped.
        if(in.column != 0) { out_char(in, '\n'); }
        out_char(in, '\n');
        out_text(in, error_text(e));
        out_char(in, '\n');
        if(line_at(in, pc) >= 0) { report(in, "BREAK", false, pc); }
    } else {
        report(in, error_text(e), true, pc);
    }
}

// Run from `start` until the program stops, reporting how it stopped.
static Err run_from(Interp &in, uint32_t start)
{
    jmp_buf trap;
    jmp_buf *outer = in.trap;
    in.trap = &trap;
    volatile uint32_t pc = start;
    volatile Err result = E_NONE;
    for(;;) {
        in.sp = 0;
        while(in.fnstack.n > 0) {              // leave any DEF FN call cleanly
            in.vars[in.fnstack.back().param] = in.fnstack.back().saved;
            in.fnstack.n--;
        }
        int e = setjmp(trap);
        if(e == 0) {
            Halt h = execute(in, pc);
            if(in.dialect->machine) { in.dialect->machine->reset_io(in); }
            if(h == H_STOP) { report(in, "BREAK", false, in.opc); }
            break;
        }
        if(e != E_CHAIN && in.dialect->machine) { in.dialect->machine->reset_io(in); }
        // A program can loop through here without ever jumping backwards (one
        // that chains to itself, or whose error handler fails), so this is
        // also a place to notice Ctrl-C.
        if((e == E_CHAIN || in.onerr != NO_TARGET) && plat_break_pending()) { e = E_BREAK; }
        if(e == E_CHAIN) {
            Err le = load_program(in, in.chain_file);
            if(le != E_NONE) {
                result = le == E_FILE ? E_DOS_FILE_NOT_FOUND : le;
                report_error(in, result, in.prog_end);
                break;
            }
            compile_program(in);
            pc = 0;
            continue;
        }
        if(e == E_BREAK) {
            report(in, "BREAK", false, in.opc);
            in.cont_pc = in.opc;
            in.can_cont = in.opc < in.prog_end;
            break;
        }
        // NOT AVAILABLE is this interpreter's shortcoming, not the program's
        // error, so it is reported rather than handed to an ONERR handler
        // that would make no sense of it.
        if(in.onerr != NO_TARGET && e != E_NOT_AVAILABLE && in.opc < in.prog_end) {
            in.err = static_cast<uint8_t>(e);
            in.err_pc = in.stmt_pc;
            in.err_line = line_at(in, in.opc);
            pc = in.onerr;
            continue;
        }
        result = static_cast<Err>(e);
        report_error(in, result, in.opc);
        in.can_cont = false;
        break;
    }
    in.trap = outer;
    return result;
}

Err run_program(Interp &in)
{
    if(in.dirty) {
        compile_program(in);
    } else {
        clear_variables(in);
        in.onerr = NO_TARGET;
        in.err = E_NONE;
    }
    return run_from(in, 0);
}

Err continue_program(Interp &in)
{
    if(!in.can_cont || in.dirty) {
        report(in, error_text(E_CANT_CONTINUE), true, in.prog_end);
        return E_CANT_CONTINUE;
    }
    return run_from(in, in.cont_pc);
}

static bool word_is(const char *line, const char *word, const char **rest)
{
    const char *p = line;
    while(*p == ' ') { p++; }
    for(; *word; word++, p++) {
        char c = *p;
        if(c >= 'a' && c <= 'z') { c = static_cast<char>(c - 32); }
        if(c != *word) { return false; }
    }
    if((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) { return false; }
    while(*p == ' ') { p++; }
    *rest = p;
    return true;
}

// Copy a filename argument, dropping surrounding quotes.
static void filename_arg(const char *arg, char *out, uint32_t cap)
{
    uint32_t n = 0;
    if(*arg == '"') { arg++; }
    while(*arg && *arg != '"' && n < cap - 1) { out[n++] = *arg++; }
    while(n > 0 && out[n - 1] == ' ') { n--; }
    out[n] = 0;
}

Err run_direct(Interp &in, const char *line)
{
    const char *arg;
    char name[256];

    // Commands that manage the program itself are handled here, not compiled.
    if(word_is(line, "NEW", &arg) && !*arg) {
        new_program(in);
        return E_NONE;
    }
    if(word_is(line, "LIST", &arg)) {
        int32_t from = 0, to = 0x7FFFFFFF;
        if(*arg >= '0' && *arg <= '9') {
            from = to = atoi(arg);
            while(*arg >= '0' && *arg <= '9') { arg++; }
        }
        if(*arg == '-' || *arg == ',') {
            arg++;
            to = (*arg >= '0' && *arg <= '9') ? atoi(arg) : 0x7FFFFFFF;
        }
        list_program(in, from, to);
        return E_NONE;
    }
    if(word_is(line, "LOAD", &arg) || (word_is(line, "RUN", &arg) && *arg == '"')) {
        bool then_run = !word_is(line, "LOAD", &arg);
        filename_arg(arg, name, sizeof(name));
        Err le = load_program(in, name);
        if(le != E_NONE) {
            report_error(in, le, in.prog_end);
            return le;
        }
        return then_run ? run_program(in) : E_NONE;
    }
    if(word_is(line, "SAVE", &arg)) {
        filename_arg(arg, name, sizeof(name));
        if(!save_program(in, name)) {
            report(in, error_text(E_FILE), true, in.prog_end);
            return E_FILE;
        }
        return E_NONE;
    }
    if(word_is(line, "RUN", &arg) && !*arg) { return run_program(in); }
    if(word_is(line, "CONT", &arg) && !*arg) { return continue_program(in); }

    // Anything else is compiled after the program and executed.
    if(in.dirty) { compile_program(in); }
    in.code.n = in.prog_end;
    Compiler c{in, -1};
    in.dialect->compile_line(c, line);
    c.e8(OP_END_DIRECT);
    resolve_fixups(in);
    return run_from(in, in.prog_end);
}

void repl(Interp &in)
{
    char line[512];
    for(;;) {
        if(in.sem.prompt) { out_text(in, in.sem.prompt); }
        int len = in_line(in, line, sizeof(line));
        if(len < 0) { break; }
        const char *rest;
        if(word_is(line, "QUIT", &rest) || word_is(line, "BYE", &rest)
            || word_is(line, "SYSTEM", &rest)) {
            break;
        }
        if(enter_line(in, line)) { continue; }
        const char *p = line;
        while(*p == ' ') { p++; }
        if(!*p) { continue; }
        run_direct(in, line);
        if(in.sem.ready) { out_text(in, in.sem.ready); }
    }
}

}  // namespace basic
