// Integer BASIC (Apple II): its own front end, because little of it parses
// like a Microsoft BASIC.
//
//   - Whole numbers only, -32767 to 32767; going outside is an error.
//   - GOTO and GOSUB take an expression.
//   - IF skips only the one statement after THEN; what follows a colon on
//     the same line runs either way.
//   - A string is a row of characters: A$(3) is from the third character
//     on, A$(3,5) the third to the fifth, and A$(3) = B$ replaces from the
//     third on.  Strings are compared with = and # only and never joined.
//   - TAB and VTAB are statements; there is no HTAB, STOP, DATA or ON.
//   - # and <> both mean "not equal"; MOD is the remainder.
//
// Words must stand apart from names, as they do in a listing ("FOR I=1 TO
// 10", not "FORI=1TO10"): Integer BASIC proper finds keywords by context,
// and this front end only has to read programs listed from tokens.
//
// Not done: a variable A and the array element A(0) are the same thing in
// Integer BASIC and separate here; DIM of a string sets no limit on its
// length.
//
// Written from memory of the Integer BASIC manual, not checked against the
// ROM: operator precedence, the "?" INPUT prints (for numbers, not for
// strings), eight-column PRINT zones, and the wording of the messages.
#include "../core/basic.h"
#include "../core/ops.h"
#include "../machines/apple2/apple2.h"

#include <cstdio>

namespace basic {

namespace {

enum : uint8_t { T_EOL, T_NUMBER, T_STRING, T_NAME, T_WORD, T_CHAR };

enum Word : uint8_t {
    W_NONE,
    // Statements.
    W_LET, W_PRINT, W_INPUT, W_GOTO, W_GOSUB, W_RETURN, W_POP, W_IF, W_FOR, W_NEXT, W_END, W_REM,
    W_DIM, W_TAB, W_VTAB, W_TEXT, W_GR, W_COLOR, W_PLOT, W_HLIN, W_VLIN, W_CALL, W_POKE, W_PR, W_IN,
    W_HIMEM, W_LOMEM, W_LIST, W_CLR, W_RUN, W_IGNORED, W_UNAVAILABLE,
    // Inside statements and expressions.
    W_THEN, W_TO, W_STEP, W_AT, W_AND, W_OR, W_NOT, W_MOD,
    W_PEEK, W_RND, W_SGN, W_ABS, W_PDL, W_LEN, W_ASC, W_SCRN,
};

const struct { const char *text; Word word; } words[] = {
    {"LET", W_LET}, {"PRINT", W_PRINT}, {"INPUT", W_INPUT}, {"GOTO", W_GOTO}, {"GOSUB", W_GOSUB},
    {"RETURN", W_RETURN}, {"POP", W_POP}, {"IF", W_IF}, {"FOR", W_FOR}, {"NEXT", W_NEXT},
    {"END", W_END}, {"REM", W_REM}, {"DIM", W_DIM}, {"TAB", W_TAB}, {"VTAB", W_VTAB},
    {"TEXT", W_TEXT}, {"GR", W_GR}, {"COLOR", W_COLOR}, {"PLOT", W_PLOT}, {"HLIN", W_HLIN},
    {"VLIN", W_VLIN}, {"CALL", W_CALL}, {"POKE", W_POKE}, {"PR", W_PR}, {"IN", W_IN},
    {"HIMEM", W_HIMEM}, {"LOMEM", W_LOMEM}, {"LIST", W_LIST}, {"CLR", W_CLR}, {"RUN", W_RUN},
    {"DSP", W_IGNORED}, {"NODSP", W_IGNORED}, {"TRACE", W_IGNORED}, {"NOTRACE", W_IGNORED},
    {"DEL", W_UNAVAILABLE}, {"NEW", W_UNAVAILABLE}, {"LOAD", W_UNAVAILABLE},
    {"SAVE", W_UNAVAILABLE}, {"CON", W_UNAVAILABLE}, {"AUTO", W_UNAVAILABLE},
    {"MAN", W_UNAVAILABLE},
    {"THEN", W_THEN}, {"TO", W_TO}, {"STEP", W_STEP}, {"AT", W_AT}, {"AND", W_AND}, {"OR", W_OR},
    {"NOT", W_NOT}, {"MOD", W_MOD}, {"PEEK", W_PEEK}, {"RND", W_RND}, {"SGN", W_SGN},
    {"ABS", W_ABS}, {"PDL", W_PDL}, {"LEN", W_LEN}, {"ASC", W_ASC}, {"SCRN", W_SCRN},
};

inline bool is_alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
inline char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }

struct Parser {
    Compiler &c;
    const char *p;                // just past the current token
    jmp_buf *jb;

    // Current token.
    uint8_t type;
    Word word;                    // T_WORD
    char ch;                      // T_CHAR
    int32_t number;               // T_NUMBER
    const char *str;              // T_STRING: contents
    uint32_t len;                 // T_STRING / T_NAME length
    char name[64];                // T_NAME, upper case, without the $
    bool is_string;               // T_NAME ends in $

    [[noreturn]] void fail(Err e) { longjmp(*jb, e); }

    // ---- lexer ----

    void next()
    {
        while(*p == ' ' || *p == '\t') { p++; }
        char first = *p;
        if(!first) {
            type = T_EOL;
            return;
        }
        if(is_digit(first)) {
            int32_t n = 0;
            while(is_digit(*p)) {
                if(n <= 32767) { n = n * 10 + (*p - '0'); }
                p++;
            }
            number = n;
            type = T_NUMBER;
            return;
        }
        if(first == '"') {
            str = ++p;
            while(*p && *p != '"') { p++; }
            len = static_cast<uint32_t>(p - str);
            if(*p) { p++; }
            type = T_STRING;
            return;
        }
        if(is_alpha(first)) {
            uint32_t n = 0;
            while(is_alpha(*p) || is_digit(*p)) {
                if(n < sizeof(name) - 1) { name[n++] = upper(*p); }
                p++;
            }
            name[n] = 0;
            len = n;
            is_string = *p == '$';
            if(is_string) {
                p++;
                type = T_NAME;
                return;
            }
            type = T_NAME;
            for(const auto &w : words) {
                if(strcmp(w.text, name) == 0) {
                    // Five words are words only with their mark after them,
                    // and any other word with "=" after it is a variable of
                    // that name (a recovered program has CALL=936).
                    const char *q = p;
                    while(*q == ' ') { q++; }
                    char mark = w.word == W_COLOR ? '=' : (w.word == W_PR || w.word == W_IN) ? '#'
                        : (w.word == W_HIMEM || w.word == W_LOMEM) ? ':' : 0;
                    if(mark ? *q != mark : (*q == '=' && w.word != W_REM)) { return; }
                    type = T_WORD;
                    word = w.word;
                    return;
                }
            }
            return;
        }
        type = T_CHAR;
        ch = first;
        p++;
    }

    bool is_char(char want) const { return type == T_CHAR && ch == want; }
    bool is_word(Word want) const { return type == T_WORD && word == want; }
    void expect_char(char want)
    {
        if(!is_char(want)) { fail(E_SYNTAX); }
        next();
    }
    void expect_word(Word want)
    {
        if(!is_word(want)) { fail(E_SYNTAX); }
        next();
    }
    bool at_end() const { return type == T_EOL || is_char(':'); }

    // ---- code ----

    void emit_int(int32_t n)
    {
        c.e8(OP_INT);
        c.e32(static_cast<uint32_t>(n));
    }
    void emit_call(uint8_t fn)
    {
        c.e8(OP_CALL);
        c.e8(fn);
    }
    void emit_ext(uint8_t op, uint16_t id, uint8_t nargs)
    {
        c.e8(op);
        c.e16(id);
        c.e8(nargs);
    }
    uint16_t string_symbol() { return c.symbol(name, len, K_STR, false); }

    // ---- expressions: each returns true if the result is a string ----

    void num_expr()
    {
        if(expr()) { fail(E_TYPE_MISMATCH); }
    }

    // A string variable, perhaps with (from) or (from, to) after it.
    void string_value()
    {
        uint16_t sym = string_symbol();
        next();
        c.e8(OP_LOAD);
        c.e16(sym);
        if(!is_char('(')) { return; }
        // A$(I,J) is MID$(LEFT$(A$,J),I).  J is written second and wanted
        // first, so step over I, compile J, then come back for I.
        const char *from = p;
        next();
        skip_expression();
        if(is_char(',')) {
            next();
            num_expr();
            emit_call(FN_LEFT);
        }
        if(!is_char(')')) { fail(E_SYNTAX); }
        const char *after = p;
        p = from;
        next();
        num_expr();
        emit_call(FN_MID2);
        p = after;                          // just past the closing parenthesis
        next();
    }

    // Step over an expression without compiling it: to the comma or the
    // parenthesis that ends it.
    void skip_expression()
    {
        int depth = 0;
        while(type != T_EOL) {
            if(is_char('(')) {
                depth++;
            } else if(is_char(')')) {
                if(depth == 0) { return; }
                depth--;
            } else if(is_char(',') && depth == 0) {
                return;
            }
            next();
        }
        fail(E_SYNTAX);
    }

    bool primary()
    {
        if(type == T_NUMBER) {
            if(number > 32767) { fail(E_OVERFLOW); }
            emit_int(number);
            next();
            return false;
        }
        if(type == T_STRING) {
            c.e8(OP_STR);
            c.e16(static_cast<uint16_t>(len));
            for(uint32_t k = 0; k < len; k++) { c.e8(static_cast<uint8_t>(str[k])); }
            next();
            return true;
        }
        if(is_char('(')) {
            next();
            bool s = expr();
            expect_char(')');
            return s;
        }
        if(type == T_NAME) {
            if(is_string) {
                string_value();
                return true;
            }
            char nm[sizeof(name)];
            uint32_t nlen = len;
            memcpy(nm, name, nlen);
            next();
            if(is_char('(')) {
                next();
                num_expr();
                expect_char(')');
                c.e8(OP_LOADA);
                c.e16(c.symbol(nm, nlen, K_NUM, true));
                c.e8(1);
            } else {
                c.e8(OP_LOAD);
                c.e16(c.symbol(nm, nlen, K_NUM, false));
            }
            return false;
        }
        if(type == T_WORD) {
            Word w = word;
            next();
            switch(w) {
                case W_PEEK: case W_PDL: case W_RND: case W_SGN: case W_ABS:
                    expect_char('(');
                    num_expr();
                    expect_char(')');
                    if(w == W_PEEK) {
                        emit_ext(OP_EXTFN, A2_PEEK, 1);
                    } else if(w == W_PDL) {
                        emit_ext(OP_EXTFN, A2_PDL, 1);
                    } else {
                        emit_call(w == W_RND ? FN_IRND : (w == W_SGN ? FN_SGN : FN_ABS));
                    }
                    return false;
                case W_SCRN:
                    expect_char('(');
                    num_expr();
                    expect_char(',');
                    num_expr();
                    expect_char(')');
                    emit_ext(OP_EXTFN, A2_SCRN, 2);
                    return false;
                case W_LEN: case W_ASC:
                    expect_char('(');
                    if(!expr()) { fail(E_TYPE_MISMATCH); }
                    expect_char(')');
                    emit_call(w == W_LEN ? FN_LEN : FN_ASC);
                    if(w == W_ASC) {            // characters have their high bit set on an Apple
                        emit_int(128);
                        c.e8(OP_ADD);
                    }
                    return false;
                default:
                    break;
            }
        }
        fail(E_SYNTAX);
    }

    bool unary()
    {
        if(is_char('-')) {
            next();
            if(unary()) { fail(E_TYPE_MISMATCH); }
            c.e8(OP_NEG);
            return false;
        }
        if(is_char('+')) {
            next();
            if(unary()) { fail(E_TYPE_MISMATCH); }
            return false;
        }
        if(is_word(W_NOT)) {
            next();
            if(unary()) { fail(E_TYPE_MISMATCH); }
            c.e8(OP_NOT);
            return false;
        }
        return primary();
    }

    // One level of left-to-right number operators.
    bool power()
    {
        bool s = unary();
        while(is_char('^')) {
            if(s) { fail(E_TYPE_MISMATCH); }
            next();
            if(unary()) { fail(E_TYPE_MISMATCH); }
            c.e8(OP_POW);
            c.e8(OP_INT16);
        }
        return s;
    }

    bool term()
    {
        bool s = power();
        while(is_char('*') || is_char('/') || is_word(W_MOD)) {
            uint8_t op = is_char('*') ? OP_MUL : (is_char('/') ? OP_IDIV : OP_MOD);
            if(s) { fail(E_TYPE_MISMATCH); }
            next();
            if(power()) { fail(E_TYPE_MISMATCH); }
            c.e8(op);
            if(op == OP_MUL) { c.e8(OP_INT16); }
        }
        return s;
    }

    bool sum()
    {
        bool s = term();
        while(is_char('+') || is_char('-')) {
            uint8_t op = is_char('+') ? OP_ADD : OP_SUB;
            if(s) { fail(E_TYPE_MISMATCH); }
            next();
            if(term()) { fail(E_TYPE_MISMATCH); }
            c.e8(op);
            c.e8(OP_INT16);
        }
        return s;
    }

    // = # <> < > <= >=; a string may only be tested for = and #.
    bool relation()
    {
        bool s = sum();
        for(;;) {
            uint8_t mask = 0;
            if(is_char('=')) {
                mask = 2;
                next();
            } else if(is_char('#')) {
                mask = 5;
                next();
            } else if(is_char('<')) {
                mask = 1;
                next();
                if(is_char('=')) {
                    mask = 3;
                    next();
                } else if(is_char('>')) {
                    mask = 5;
                    next();
                }
            } else if(is_char('>')) {
                mask = 4;
                next();
                if(is_char('=')) {
                    mask = 6;
                    next();
                }
            } else {
                return s;
            }
            bool r = sum();
            if(s != r || (s && mask != 2 && mask != 5)) { fail(E_TYPE_MISMATCH); }
            c.e8(s ? OP_SCMP : OP_CMP);
            c.e8(mask);
            s = false;
        }
    }

    bool conjunction()
    {
        bool s = relation();
        while(is_word(W_AND)) {
            if(s) { fail(E_TYPE_MISMATCH); }
            next();
            if(relation()) { fail(E_TYPE_MISMATCH); }
            c.e8(OP_AND);
        }
        return s;
    }

    bool expr()
    {
        bool s = conjunction();
        while(is_word(W_OR)) {
            if(s) { fail(E_TYPE_MISMATCH); }
            next();
            if(conjunction()) { fail(E_TYPE_MISMATCH); }
            c.e8(OP_OR);
        }
        return s;
    }

    // ---- statements ----

    // What an assignment, INPUT or FOR stores into.  For a string with a
    // position, LEFT$(A$, position - 1) is left on the stack for the new
    // characters to be joined to.
    struct Target {
        uint16_t sym;
        bool is_str, is_array, from_position;
    };

    Target target()
    {
        if(type != T_NAME) { fail(E_SYNTAX); }
        Target t = {0, is_string, false, false};
        if(is_string) {
            t.sym = string_symbol();
            next();
            if(is_char('(')) {
                next();
                c.e8(OP_LOAD);
                c.e16(t.sym);
                num_expr();
                emit_int(1);
                c.e8(OP_SUB);
                emit_call(FN_LEFT);
                expect_char(')');
                t.from_position = true;
            }
            return t;
        }
        char nm[sizeof(name)];
        uint32_t nlen = len;
        memcpy(nm, name, nlen);
        next();
        if(is_char('(')) {
            next();
            num_expr();
            expect_char(')');
            t.is_array = true;
        }
        t.sym = c.symbol(nm, nlen, K_NUM, t.is_array);
        return t;
    }

    // Store the value on the stack into a target.
    void store(const Target &t)
    {
        if(t.from_position) { c.e8(OP_CAT); }
        c.e8(t.is_array ? OP_STOREA : OP_STORE);
        c.e16(t.sym);
        if(t.is_array) { c.e8(1); }
    }

    void assignment()
    {
        Target t = target();
        expect_char('=');
        if(expr() != t.is_str) { fail(E_TYPE_MISMATCH); }
        store(t);
    }

    void print_statement()
    {
        bool newline = true;
        next();
        while(!at_end()) {
            if(is_char(',')) {
                c.e8(OP_PRINT_ZONE);
                newline = false;
                next();
            } else if(is_char(';')) {
                newline = false;
                next();
            } else {
                c.e8(expr() ? OP_PRINT_STR : OP_PRINT_NUM);
                newline = true;
            }
        }
        if(newline) { c.e8(OP_PRINT_NL); }
    }

    // INPUT ["words",] variable, variable...  A number is asked for with a
    // "?"; a string is not, and takes the whole line, commas and all.
    void input_statement()
    {
        uint8_t flags = 0;
        next();
        if(type == T_STRING) {
            primary();
            expect_char(',');
            flags = 1;
        }
        uint32_t flags_at = c.here() + 1;
        c.e8(OP_INPUT);
        c.e8(flags);
        uint32_t count_at = c.here();
        c.e8(0);
        uint8_t types[32];
        uint8_t n = 0;
        for(;;) {
            if(n >= sizeof(types)) { fail(E_FORMULA_TOO_COMPLEX); }
            Target t = target();
            c.e8(OP_INPUT_GET);
            c.e8(n);
            c.e8(t.is_str ? K_STR : K_NUM);
            if(!t.is_str) { c.e8(OP_INT16); }
            store(t);
            types[n++] = t.is_str ? K_STR : K_NUM;
            if(!is_char(',')) { break; }
            next();
        }
        if(types[0] == K_STR) { flags |= 2; }
        if(n == 1 && types[0] == K_STR) { flags |= 4; }
        c.in.code[flags_at] = flags;
        c.in.code.insert_gap(count_at + 1, n);
        memcpy(&c.in.code[count_at + 1], types, n);
        c.in.code[count_at] = n;
    }

    // GOTO and GOSUB: a plain number is settled when the program is
    // compiled, anything else when it runs.
    void branch(bool gosub)
    {
        if(type == T_NUMBER) {
            const char *q = p;
            while(*q == ' ') { q++; }
            if(!*q || *q == ':') {
                c.e8(gosub ? OP_GOSUB : OP_GOTO);
                c.line_ref(number);
                next();
                return;
            }
        }
        num_expr();
        c.e8(gosub ? OP_GOSUBX : OP_GOTOX);
    }

    void for_statement()
    {
        next();
        if(type != T_NAME || is_string) { fail(E_SYNTAX); }
        uint16_t slot = c.symbol(name, len, K_NUM, false);
        next();
        expect_char('=');
        num_expr();
        expect_word(W_TO);
        num_expr();
        if(is_word(W_STEP)) {
            next();
            num_expr();
        } else {
            emit_int(1);
        }
        c.e8(OP_FOR);
        c.e16(slot);
    }

    void next_statement()
    {
        next();
        for(;;) {
            if(type != T_NAME || is_string) { fail(E_SYNTAX); }
            c.e8(OP_NEXT);
            c.e16(c.symbol(name, len, K_NUM, false));
            next();
            if(!is_char(',')) { break; }
            next();
        }
    }

    void dim_statement()
    {
        next();
        for(;;) {
            if(type != T_NAME) { fail(E_SYNTAX); }
            bool string = is_string;
            char nm[sizeof(name)];
            uint32_t nlen = len;
            memcpy(nm, name, nlen);
            next();
            expect_char('(');
            num_expr();
            expect_char(')');
            if(string) {
                c.e8(OP_POP);               // a string grows as it needs to
            } else {
                c.e8(OP_DIM);
                c.e16(c.symbol(nm, nlen, K_NUM, true));
                c.e8(1);
            }
            if(!is_char(',')) { break; }
            next();
        }
    }

    // A machine statement taking `count` numbers with commas between; AT
    // stands for the comma before the last of HLIN's and VLIN's.
    void machine_statement(uint16_t id, uint8_t count, bool at)
    {
        next();
        for(uint8_t k = 0; k < count; k++) {
            if(k > 0) {
                if(at && k == count - 1) {
                    expect_word(W_AT);
                } else {
                    expect_char(',');
                }
            }
            num_expr();
        }
        emit_ext(OP_EXT, id, count);
    }

    // The word, then the character that belongs to it: COLOR=, PR#, HIMEM:
    void word_with(char mark, uint16_t id)
    {
        next();
        expect_char(mark);
        num_expr();
        emit_ext(OP_EXT, id, 1);
    }

    void statement()
    {
        if(type == T_NAME) {
            assignment();
            return;
        }
        if(type != T_WORD) { fail(E_SYNTAX); }
        switch(word) {
            case W_LET: next(); assignment(); break;
            case W_PRINT: print_statement(); break;
            case W_INPUT: input_statement(); break;
            case W_GOTO: next(); branch(false); break;
            case W_GOSUB: next(); branch(true); break;
            case W_RETURN: next(); c.e8(OP_RETURN); break;
            case W_POP: next(); c.e8(OP_POPSUB); break;
            case W_END: next(); c.e8(OP_END); break;
            case W_CLR: next(); c.e8(OP_CLEAR); break;
            case W_RUN: next(); c.e8(OP_RUN); break;
            case W_FOR: for_statement(); break;
            case W_NEXT: next_statement(); break;
            case W_DIM: dim_statement(); break;
            case W_REM:
                while(*p) { p++; }
                type = T_EOL;
                break;
            case W_IF: {
                next();
                num_expr();
                expect_word(W_THEN);
                c.e8(OP_JF);
                uint32_t skip = c.here();
                c.e32(0);
                if(type == T_NUMBER) {
                    branch(false);
                } else {
                    statement();
                }
                c.patch32(skip, c.here());
                break;
            }
            case W_TAB: machine_statement(A2_HTAB, 1, false); break;
            case W_VTAB: machine_statement(A2_VTAB, 1, false); break;
            case W_CALL: machine_statement(A2_CALL, 1, false); break;
            case W_POKE: machine_statement(A2_POKE, 2, false); break;
            case W_PLOT: machine_statement(A2_PLOT, 2, false); break;
            case W_HLIN: machine_statement(A2_HLIN, 3, true); break;
            case W_VLIN: machine_statement(A2_VLIN, 3, true); break;
            case W_TEXT: next(); emit_ext(OP_EXT, A2_TEXT, 0); break;
            case W_GR: next(); emit_ext(OP_EXT, A2_GR, 0); break;
            case W_COLOR: word_with('=', A2_COLOR); break;
            case W_PR: word_with('#', A2_PR); break;
            case W_IN: word_with('#', A2_IN); break;
            case W_HIMEM: word_with(':', A2_HIMEM); break;
            case W_LOMEM: word_with(':', A2_LOMEM); break;
            case W_LIST: {
                next();
                uint32_t from = 0, to = 0x7FFFFFFF;
                if(type == T_NUMBER) {
                    from = to = static_cast<uint32_t>(number);
                    next();
                    if(is_char(',')) {
                        next();
                        if(type != T_NUMBER) { fail(E_SYNTAX); }
                        to = static_cast<uint32_t>(number);
                        next();
                    }
                }
                c.e8(OP_LIST);
                c.e32(from);
                c.e32(to);
                break;
            }
            case W_IGNORED:
                next();
                while(!at_end()) { next(); }
                break;
            case W_UNAVAILABLE:
                while(!at_end()) { next(); }
                fail(E_NOT_AVAILABLE);
            default:
                fail(E_SYNTAX);
        }
    }

    // One statement; on a parse error it becomes OP_ERR and the line ends.
    void guarded_statement()
    {
        uint32_t start = c.here();
        jmp_buf here;
        jb = &here;
        int e = setjmp(here);
        if(e == 0) {
            statement();
            if(!at_end()) { fail(E_SYNTAX); }
        } else {
            c.truncate(start);
            c.e8(OP_ERR);
            c.e8(static_cast<uint8_t>(e));
            type = T_EOL;
        }
        jb = nullptr;
    }
};

void compile_line(Compiler &c, const char *text)
{
    Parser ps{c, text, nullptr, T_EOL, W_NONE, 0, 0, nullptr, 0, {}, false};
    ps.next();
    for(;;) {
        while(ps.is_char(':')) { ps.next(); }
        if(ps.type == T_EOL) { break; }
        ps.guarded_statement();
    }
}

// "*** SYNTAX ERR" and, in a program, "STOPPED AT 120".
void report(Interp &in, Err e, int32_t line)
{
    const char *what;
    switch(e) {
        case E_BREAK: what = nullptr; break;
        case E_SYNTAX: case E_TYPE_MISMATCH: what = "SYNTAX"; break;
        case E_OVERFLOW: case E_DIVISION_BY_ZERO: what = ">32767"; break;
        case E_ILLEGAL_QUANTITY: case E_BAD_SUBSCRIPT: what = "RANGE"; break;
        case E_UNDEFD_STATEMENT: what = "BAD BRANCH"; break;
        case E_RETURN_WITHOUT_GOSUB: what = "BAD RETURN"; break;
        case E_NEXT_WITHOUT_FOR: what = "BAD NEXT"; break;
        case E_OUT_OF_MEMORY: what = "MEM FULL"; break;
        case E_STRING_TOO_LONG: what = "STR OVFL"; break;
        case E_REDIMD_ARRAY: what = "DIM"; break;
        default: what = error_text(e); break;
    }
    char buf[80];
    if(in.column != 0) { out_char(in, '\n'); }
    if(what) {
        snprintf(buf, sizeof(buf), "*** %s ERR\n", what);
        out_text(in, buf);
    }
    if(line >= 0) {
        snprintf(buf, sizeof(buf), "STOPPED AT %ld\n", static_cast<long>(line));
        out_text(in, buf);
    }
}

}  // namespace

extern const Dialect integer_dialect = {
    "integer",
    "INTEGER",
    {
        .true_value = 1,
        .bitwise_logic = false,
        .for_pretest = false,
        .print_digits = 9,
        .zone_width = 8,
        .tab_base = 1,
        .num_lead_space = false,
        .num_trail_space = false,
        .sig_name_chars = 0,
        .crunch_keywords = false,
        .ready = nullptr,
        .prompt = ">",
        .input_mark = "?",
        .mark_after_prompt = true,
        .redo = "RETYPE LINE\n",
        .stmt_markers = false,
    },
    compile_line,
    nullptr,
    &apple2_machine,
    report,
};

}  // namespace basic
