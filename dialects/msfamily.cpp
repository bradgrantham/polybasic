// Front end for the Microsoft-family dialects: a lexer and a recursive-descent
// parser that emit code directly.  One line in, a run of instructions out.
//
// Errors: a statement that does not parse is replaced by OP_ERR, so the error
// is reported only if execution reaches it, as on the original machines.  The
// rest of that line is dropped.
#include "msfamily.h"
#include "../core/ops.h"

namespace basic {

namespace {

enum : uint8_t { T_EOL, T_NUMBER, T_STRING, T_NAME, T_KW, T_CHAR };

struct FnInfo { uint8_t kw; uint8_t fn; const char *args; bool returns_str; };
// args: one letter per argument, N number, S string, A anything.
const FnInfo fn_info[] = {
    {KW_SGN, FN_SGN, "N", false}, {KW_INT, FN_INT, "N", false},
    {KW_ABS, FN_ABS, "N", false}, {KW_FRE, FN_FRE, "A", false},
    {KW_POS, FN_POS, "A", false}, {KW_SQR, FN_SQR, "N", false},
    {KW_RND, FN_RND, "N", false}, {KW_LOG, FN_LOG, "N", false},
    {KW_EXP, FN_EXP, "N", false}, {KW_COS, FN_COS, "N", false},
    {KW_SIN, FN_SIN, "N", false}, {KW_TAN, FN_TAN, "N", false},
    {KW_ATN, FN_ATN, "N", false}, {KW_LEN, FN_LEN, "S", false},
    {KW_STR, FN_STR, "N", true}, {KW_VAL, FN_VAL, "S", false},
    {KW_ASC, FN_ASC, "S", false}, {KW_CHR, FN_CHR, "N", true},
    {KW_LEFT, FN_LEFT, "SN", true}, {KW_RIGHT, FN_RIGHT, "SN", true},
    {KW_MID, FN_MID2, "SN", true},
};

inline bool is_alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
inline char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }

struct Parser {
    Compiler &c;
    const Keyword *keywords;
    bool crunch;
    const char *p;                // just past the current token
    jmp_buf *jb;
    Vec<uint32_t> eol;            // jumps to patch to the end of the line
    int if_depth;
    const Keyword *found;         // set by match_keyword

    // Current token.
    uint8_t type;
    uint8_t kw;
    const Keyword *kwent;         // T_KW: its table entry
    char ch;
    Value num;
    const char *str;              // T_STRING: contents
    uint32_t len;                 // T_STRING / T_NAME length
    char name[64];                // T_NAME, upper case, without suffix
    uint8_t kind;                 // T_NAME: K_NUM, K_INT or K_STR

    [[noreturn]] void fail(Err e) { longjmp(*jb, e); }

    // ---- lexer ----

    // Longest keyword starting at q; returns its length, or 0.
    uint32_t match_keyword(const char *q, uint8_t *which)
    {
        uint32_t best = 0;
        for(const Keyword *k = keywords; k->text; k++) {
            uint32_t n = 0;
            while(k->text[n] && upper(q[n]) == k->text[n]) { n++; }
            if(!k->text[n] && n > best) {
                best = n;
                *which = k->kw;
                found = k;
            }
        }
        return best;
    }

    void next()
    {
        while(*p == ' ' || *p == '\t') { p++; }
        char first = *p;
        if(!first) {
            type = T_EOL;
            return;
        }
        if(is_digit(first) || (first == '.' && is_digit(p[1]))) {
            const char *q = p;
            while(is_digit(*q)) { q++; }
            if(*q == '.') {
                q++;
                while(is_digit(*q)) { q++; }
            }
            if(*q == 'E' || *q == 'e') {
                // Blanks are allowed inside the exponent: a listing of a
                // tokenised program shows 1E-3 as "1E - 3".
                const char *e = q + 1;
                while(*e == ' ') { e++; }
                if(*e == '+' || *e == '-') { e++; }
                while(*e == ' ') { e++; }
                if(is_digit(*e)) {
                    while(is_digit(*e)) { e++; }
                    q = e;
                }
            }
            parse_number(p, static_cast<uint32_t>(q - p), false, &num);
            p = q;
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
        if(first == '?') {
            p++;
            type = T_KW;
            kw = KW_PRINT;
            return;
        }
        if(is_alpha(first)) {
            lex_word();
            return;
        }
        type = T_CHAR;
        ch = first;
        p++;
    }

    void lex_word()
    {
        uint8_t which = KW_NONE;
        len = 0;
        if(crunch) {
            // Keywords are recognised wherever they start, even inside what
            // looks like a name: FORI=1TO10 is FOR I = 1 TO 10.
            uint32_t n = match_keyword(p, &which);
            if(n) {
                p += n;
                type = T_KW;
                kw = which;
                kwent = found;
                return;
            }
            name[len++] = upper(*p++);
            while(is_alpha(*p) || is_digit(*p)) {
                if(is_alpha(*p) && match_keyword(p, &which)) { break; }
                if(len < sizeof(name)) { name[len++] = upper(*p); }
                p++;
            }
        } else {
            // Keywords must stand alone, so names may be long and may contain them.
            const char *q = p;
            while(is_alpha(*q) || is_digit(*q)) { q++; }
            uint32_t word = static_cast<uint32_t>(q - p);
            uint32_t n = match_keyword(p, &which);
            if(n >= word && (n == word || p[word] == '$' || p[word] == '(')) {
                p += n;
                type = T_KW;
                kw = which;
                kwent = found;
                return;
            }
            if(word > 2 && upper(p[0]) == 'F' && upper(p[1]) == 'N'
                && match_keyword(p, &which) == 2 && which == KW_FN) {
                p += 2;
                type = T_KW;
                kw = KW_FN;
                return;
            }
            for(; p < q; p++) {
                if(len < sizeof(name)) { name[len++] = upper(*p); }
            }
        }
        kind = K_NUM;
        if(*p == '$') {
            kind = K_STR;
            p++;
        } else if(*p == '%') {
            kind = K_INT;
            p++;
        }
        type = T_NAME;
    }

    // Arguments of a machine statement or function, as its `sig` describes;
    // returns how many values were compiled.
    uint8_t ext_args(const char *sig, bool in_parens)
    {
        uint8_t n = 0;
        for(const char *s = sig; s && *s; s++) {
            switch(*s) {
                case '?':
                    if(in_parens ? is_char(')') : at_end()) { return n; }
                    break;
                case 'N': num_expr(); n++; break;
                case 'S': need_str(expr()); n++; break;
                case '@': expect_kw(KW_AT); break;
                default: expect_char(*s); break;
            }
        }
        return n;
    }

    void emit_ext(uint8_t op, uint16_t id, uint8_t nargs)
    {
        c.e8(op);
        c.e16(id);
        c.e8(nargs);
    }

    // HPLOT x,y   HPLOT TO x,y   HPLOT x,y TO x,y TO ...
    // Each point becomes one call: (0 = plot / 1 = draw a line to, x, y).
    void hplot_statement()
    {
        uint16_t id = kwent->id;
        bool line_to = false;
        next();
        if(is_kw(KW_TO)) {
            line_to = true;
            next();
        }
        for(;;) {
            c.e8(OP_INT);
            c.e32(line_to ? 1 : 0);
            num_expr();
            expect_char(',');
            num_expr();
            emit_ext(OP_EXT, id, 3);
            if(!is_kw(KW_TO)) { break; }
            line_to = true;
            next();
        }
    }

    bool is_char(char want) const { return type == T_CHAR && ch == want; }
    bool is_kw(uint8_t want) const { return type == T_KW && kw == want; }
    void expect_char(char want)
    {
        if(!is_char(want)) { fail(E_SYNTAX); }
        next();
    }
    void expect_kw(uint8_t want)
    {
        if(!is_kw(want)) { fail(E_SYNTAX); }
        next();
    }
    bool at_end() const
    {
        return type == T_EOL || is_char(':') || (if_depth > 0 && is_kw(KW_ELSE));
    }

    // ---- expressions: each returns true if the result is a string ----

    void need_num(bool is_str) { if(is_str) { fail(E_TYPE_MISMATCH); } }
    void need_str(bool is_str) { if(!is_str) { fail(E_TYPE_MISMATCH); } }
    void num_expr() { need_num(expr()); }

    // Parse "(e1, e2, ...)" after a name; returns the number of subscripts.
    uint8_t subscripts()
    {
        uint8_t n = 0;
        expect_char('(');
        for(;;) {
            num_expr();
            if(++n > MAX_DIMS) { fail(E_BAD_SUBSCRIPT); }
            if(is_char(',')) {
                next();
                continue;
            }
            break;
        }
        expect_char(')');
        return n;
    }

    bool call_function(const FnInfo &f)
    {
        next();
        expect_char('(');
        uint8_t fn = f.fn;
        for(const char *a = f.args; *a; a++) {
            if(a != f.args) { expect_char(','); }
            bool s = expr();
            if(*a == 'N') { need_num(s); }
            if(*a == 'S') { need_str(s); }
            if(*a == 'A' && s) {
                // FRE("") and POS("") take a dummy of either type: make it a number.
                c.e8(OP_CALL);
                c.e8(FN_LEN);
            }
        }
        if(f.kw == KW_MID && is_char(',')) {
            next();
            num_expr();
            fn = FN_MID3;
        }
        expect_char(')');
        c.e8(OP_CALL);
        c.e8(fn);
        return f.returns_str;
    }

    bool primary()
    {
        if(type == T_NUMBER) {
            if(num.tag == T_INT) {
                c.e8(OP_INT);
                c.e32(static_cast<uint32_t>(num.i));
            } else {
                c.e8(OP_NUM);
                c.e64(num.d);
            }
            next();
            return false;
        }
        if(type == T_STRING) {
            if(len > 0xFFFF) { fail(E_STRING_TOO_LONG); }
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
            char nm[sizeof(name)];
            uint32_t nlen = len;
            uint8_t k = kind;
            memcpy(nm, name, nlen);
            next();
            if(is_char('(')) {
                uint8_t n = subscripts();
                c.e8(OP_LOADA);
                c.e16(c.symbol(nm, nlen, k, true));
                c.e8(n);
            } else {
                c.e8(OP_LOAD);
                c.e16(c.symbol(nm, nlen, k, false));
            }
            return k == K_STR;
        }
        if(is_kw(KW_FN)) {
            next();
            if(type != T_NAME || kind != K_NUM) { fail(E_SYNTAX); }
            uint16_t f = c.symbol(name, len, K_FN, false);
            next();
            expect_char('(');
            num_expr();
            expect_char(')');
            c.e8(OP_CALLFN);
            c.e16(f);
            return false;
        }
        if(is_kw(KW_EXTFN)) {
            const Keyword *k = kwent;
            bool open = k->text[strlen(k->text) - 1] == '(';
            next();
            if(!open) { expect_char('('); }
            uint8_t n = ext_args(k->sig, true);
            expect_char(')');
            emit_ext(OP_EXTFN, k->id, n);
            return false;
        }
        if(type == T_KW) {
            for(const FnInfo &f : fn_info) {
                if(f.kw == kw) { return call_function(f); }
            }
        }
        fail(E_SYNTAX);
    }

    // The right operand of ^ may carry its own sign: 2^-3.
    void exponent()
    {
        if(is_char('-')) {
            next();
            exponent();
            c.e8(OP_NEG);
        } else if(is_char('+')) {
            next();
            exponent();
        } else {
            need_num(primary());
        }
    }

    bool power()
    {
        bool s = primary();
        while(is_char('^')) {
            need_num(s);
            next();
            exponent();
            c.e8(OP_POW);
        }
        return s;
    }

    bool unary()
    {
        if(is_char('-')) {
            next();
            need_num(unary());
            c.e8(OP_NEG);
            return false;
        }
        if(is_char('+')) {
            next();
            need_num(unary());
            return false;
        }
        return power();
    }

    bool term()
    {
        bool s = unary();
        while(is_char('*') || is_char('/')) {
            uint8_t op = ch == '*' ? OP_MUL : OP_DIV;
            need_num(s);
            next();
            need_num(unary());
            c.e8(op);
        }
        return s;
    }

    bool sum()
    {
        bool s = term();
        while(is_char('+') || is_char('-')) {
            char o = ch;
            next();
            bool r = term();
            if(s != r || (s && o == '-')) { fail(E_TYPE_MISMATCH); }
            c.e8(s ? OP_CAT : (o == '+' ? OP_ADD : OP_SUB));
        }
        return s;
    }

    bool relation()
    {
        bool s = sum();
        for(;;) {
            uint8_t mask = 0;
            while(type == T_CHAR && (ch == '<' || ch == '=' || ch == '>')) {
                mask |= ch == '<' ? 1 : (ch == '=' ? 2 : 4);
                next();
            }
            if(!mask) { return s; }
            bool r = sum();
            if(s != r) { fail(E_TYPE_MISMATCH); }
            c.e8(s ? OP_SCMP : OP_CMP);
            c.e8(mask);
            s = false;
        }
    }

    bool negation()
    {
        if(is_kw(KW_NOT)) {
            next();
            need_num(negation());
            c.e8(OP_NOT);
            return false;
        }
        return relation();
    }

    bool conjunction()
    {
        bool s = negation();
        while(is_kw(KW_AND)) {
            need_num(s);
            next();
            need_num(negation());
            c.e8(OP_AND);
        }
        return s;
    }

    bool expr()
    {
        bool s = conjunction();
        while(is_kw(KW_OR)) {
            need_num(s);
            next();
            need_num(conjunction());
            c.e8(OP_OR);
        }
        return s;
    }

    // ---- statements ----

    struct Target { uint16_t slot; uint8_t ndims; uint8_t kind; };

    // A variable to assign to; emits its subscripts.
    Target target()
    {
        if(type != T_NAME) { fail(E_SYNTAX); }
        char nm[sizeof(name)];
        uint32_t nlen = len;
        Target t;
        t.kind = kind;
        t.ndims = 0;
        memcpy(nm, name, nlen);
        next();
        if(is_char('(')) {
            t.ndims = subscripts();
            t.slot = c.symbol(nm, nlen, t.kind, true);
        } else {
            t.slot = c.symbol(nm, nlen, t.kind, false);
        }
        return t;
    }

    void store(const Target &t)
    {
        if(t.ndims) {
            c.e8(OP_STOREA);
            c.e16(t.slot);
            c.e8(t.ndims);
        } else {
            c.e8(OP_STORE);
            c.e16(t.slot);
        }
    }

    void assignment()
    {
        Target t = target();
        expect_char('=');
        bool s = expr();
        if(s != (t.kind == K_STR)) { fail(E_TYPE_MISMATCH); }
        store(t);
    }

    int32_t line_number()
    {
        if(type != T_NUMBER || num.tag != T_INT || num.i < 0) { fail(E_SYNTAX); }
        int32_t n = num.i;
        next();
        return n;
    }

    void jump_to_line(uint8_t op)
    {
        int32_t n = line_number();
        c.e8(op);
        c.line_ref(n);
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
            } else if(is_kw(KW_TAB) || is_kw(KW_SPC)) {
                uint8_t op = kw == KW_TAB ? OP_PRINT_TAB : OP_PRINT_SPC;
                next();
                num_expr();
                expect_char(')');
                c.e8(op);
                newline = true;
            } else {
                c.e8(expr() ? OP_PRINT_STR : OP_PRINT_NUM);
                newline = true;
            }
        }
        if(newline) { c.e8(OP_PRINT_NL); }
    }

    void input_statement()
    {
        uint8_t flags = 0;
        next();
        if(type == T_STRING) {
            primary();
            if(!is_char(';') && !is_char(',')) { fail(E_SYNTAX); }
            next();
            flags = 1;
        }
        c.e8(OP_INPUT);
        c.e8(flags);
        uint32_t count_at = c.here();
        c.e8(0);
        uint8_t types[64];
        uint8_t n = 0;
        for(;;) {
            if(n >= sizeof(types)) { fail(E_FORMULA_TOO_COMPLEX); }
            Target t = target();
            c.e8(OP_INPUT_GET);
            c.e8(n);
            c.e8(t.kind);
            store(t);
            types[n++] = t.kind;
            if(!is_char(',')) { break; }
            next();
        }
        // The type list goes right after the count, ahead of the stores.
        c.in.code.insert_gap(count_at + 1, n);
        memcpy(&c.in.code[count_at + 1], types, n);
        c.in.code[count_at] = n;
    }

    void read_statement()
    {
        next();
        for(;;) {
            Target t = target();
            c.e8(OP_READ);
            c.e8(t.kind);
            store(t);
            if(!is_char(',')) { break; }
            next();
        }
    }

    void data_statement()
    {
        const char *q = p;
        for(;;) {
            while(*q == ' ') { q++; }
            if(*q == '"') {
                const char *s = ++q;
                while(*q && *q != '"') { q++; }
                c.data_item(s, static_cast<uint32_t>(q - s));
                if(*q) { q++; }
                while(*q && *q != ',' && *q != ':') { q++; }
            } else {
                const char *s = q;
                while(*q && *q != ',' && *q != ':') { q++; }
                const char *e = q;
                while(e > s && e[-1] == ' ') { e--; }
                c.data_item(s, static_cast<uint32_t>(e - s));
            }
            if(*q != ',') { break; }
            q++;
        }
        p = q;
        next();
    }

    void dim_statement()
    {
        next();
        for(;;) {
            if(type != T_NAME) { fail(E_SYNTAX); }
            char nm[sizeof(name)];
            uint32_t nlen = len;
            uint8_t k = kind;
            memcpy(nm, name, nlen);
            next();
            uint8_t n = subscripts();
            c.e8(OP_DIM);
            c.e16(c.symbol(nm, nlen, k, true));
            c.e8(n);
            if(!is_char(',')) { break; }
            next();
        }
    }

    void def_statement()
    {
        next();
        expect_kw(KW_FN);
        if(type != T_NAME || kind != K_NUM) { fail(E_SYNTAX); }
        uint16_t f = c.symbol(name, len, K_FN, false);
        next();
        expect_char('(');
        if(type != T_NAME) { fail(E_SYNTAX); }
        uint16_t param = c.symbol(name, len, kind, false);
        next();
        expect_char(')');
        expect_char('=');
        c.e8(OP_DEF);
        c.e16(f);
        c.e16(param);
        uint32_t length_at = c.here();
        c.e32(0);
        uint32_t body = c.here();
        num_expr();
        c.e8(OP_FNRET);
        c.patch32(length_at, c.here() - body);
    }

    void for_statement()
    {
        next();
        if(type != T_NAME || kind == K_STR) { fail(E_SYNTAX); }
        uint16_t slot = c.symbol(name, len, kind, false);
        next();
        expect_char('=');
        num_expr();
        expect_kw(KW_TO);
        num_expr();
        if(is_kw(KW_STEP)) {
            next();
            num_expr();
        } else {
            c.e8(OP_INT);
            c.e32(1);
        }
        c.e8(OP_FOR);
        c.e16(slot);
    }

    void next_statement()
    {
        next();
        if(type != T_NAME) {
            c.e8(OP_NEXT);
            c.e16(NO_SLOT);
            return;
        }
        for(;;) {
            if(type != T_NAME || kind == K_STR) { fail(E_SYNTAX); }
            c.e8(OP_NEXT);
            c.e16(c.symbol(name, len, kind, false));
            next();
            if(!is_char(',')) { break; }
            next();
        }
    }

    // After GO: TO or SUB.  Returns the opcode.
    uint8_t go_what()
    {
        if(is_kw(KW_TO)) {
            next();
            return OP_GOTO;
        }
        if(is_kw(KW_SUB)) {
            next();
            return OP_GOSUB;
        }
        fail(E_SYNTAX);
    }

    void on_statement()
    {
        next();
        num_expr();
        uint8_t op;
        if(is_kw(KW_GOTO)) {
            next();
            op = OP_GOTO;
        } else if(is_kw(KW_GOSUB)) {
            next();
            op = OP_GOSUB;
        } else if(is_kw(KW_GO)) {
            next();
            op = go_what();
        } else {
            fail(E_SYNTAX);
        }
        c.e8(OP_ON);
        c.e8(op == OP_GOSUB ? 1 : 0);
        uint32_t count_at = c.here();
        c.e8(0);
        uint8_t n = 0;
        for(;;) {
            c.line_ref(line_number());
            if(++n == 255) { fail(E_FORMULA_TOO_COMPLEX); }
            if(!is_char(',')) { break; }
            next();
        }
        c.in.code[count_at] = n;
    }

    // THEN or ELSE is followed by a line number or by statements.
    void branch()
    {
        if(type == T_NUMBER) {
            jump_to_line(OP_GOTO);
        } else {
            if_depth++;
            block();
            if_depth--;
        }
    }

    void if_statement()
    {
        next();
        num_expr();
        c.e8(OP_JF);
        uint32_t skip = c.here();
        c.e32(0);
        if(is_kw(KW_THEN)) {
            next();
            branch();
        } else if(is_kw(KW_GOTO)) {
            next();
            jump_to_line(OP_GOTO);
        } else if(is_kw(KW_GO)) {
            next();
            jump_to_line(go_what());
        } else {
            fail(E_SYNTAX);
        }
        if(is_kw(KW_ELSE)) {
            c.e8(OP_JMP);
            uint32_t past_else = c.here();
            c.e32(0);
            c.patch32(skip, c.here());
            next();
            branch();
            eol.push(past_else);
        } else {
            eol.push(skip);
        }
    }

    void skip_statement()
    {
        while(!at_end()) { next(); }
    }

    void get_statement()
    {
        next();
        Target t = target();
        c.e8(OP_GET);
        c.e8(t.kind);
        store(t);
    }

    void statement()
    {
        if(type == T_NAME) {
            assignment();
            return;
        }
        if(is_char('&')) {
            // A machine-language hook: nothing after it can be understood.
            while(type != T_EOL) { next(); }
            fail(E_NOT_AVAILABLE);
        }
        if(type != T_KW) { fail(E_SYNTAX); }
        switch(kw) {
            case KW_LET: next(); assignment(); break;
            case KW_PRINT: print_statement(); break;
            case KW_INPUT: input_statement(); break;
            case KW_IF: if_statement(); break;
            case KW_FOR: for_statement(); break;
            case KW_NEXT: next_statement(); break;
            // These interpreters read the line number and never look at what
            // follows it, so "GOSUB 200 REM AND" and worse are legal.
            case KW_GOTO: next(); jump_to_line(OP_GOTO); skip_statement(); break;
            case KW_GOSUB: next(); jump_to_line(OP_GOSUB); skip_statement(); break;
            case KW_GO: next(); jump_to_line(go_what()); skip_statement(); break;
            case KW_RETURN: next(); c.e8(OP_RETURN); break;
            case KW_END: next(); c.e8(OP_END); break;
            case KW_STOP: next(); c.e8(OP_STOP); break;
            case KW_RUN:
                next();
                if(type == T_NUMBER) {
                    c.e8(OP_CLEAR);
                    jump_to_line(OP_GOTO);
                } else {
                    c.e8(OP_RUN);
                }
                break;
            case KW_REM:
                while(*p) { p++; }
                type = T_EOL;
                break;
            case KW_DATA: data_statement(); break;
            case KW_READ: read_statement(); break;
            case KW_RESTORE:
                next();
                c.e8(OP_RESTORE);
                c.e32(type == T_NUMBER ? static_cast<uint32_t>(line_number()) : NO_TARGET);
                break;
            case KW_DIM: dim_statement(); break;
            case KW_DEF: def_statement(); break;
            case KW_ON: on_statement(); break;
            case KW_CLEAR:
                next();
                while(!at_end()) {
                    expr();
                    c.e8(OP_POP);
                    if(is_char(',')) { next(); }
                }
                c.e8(OP_CLEAR);
                break;
            case KW_WAIT: next(); num_expr(); c.e8(OP_WAIT); break;
            case KW_WIDTH: next(); num_expr(); c.e8(OP_WIDTH); break;
            case KW_RANDOMIZE:
                next();
                if(!at_end()) {
                    num_expr();
                    c.e8(OP_POP);
                }
                c.e8(OP_RANDOMIZE);
                break;
            case KW_GET: get_statement(); break;
            case KW_ONERR:
                next();
                expect_kw(KW_GOTO);
                jump_to_line(OP_ONERR);
                break;
            case KW_RESUME: next(); c.e8(OP_RESUME); break;
            case KW_POP: next(); c.e8(OP_POPSUB); break;
            case KW_EXT: {
                const Keyword *k = kwent;
                next();
                uint8_t n = ext_args(k->sig, false);
                emit_ext(OP_EXT, k->id, n);
                break;
            }
            case KW_HPLOT: hplot_statement(); break;
            case KW_UNSUPPORTED:
                while(!at_end()) { next(); }
                fail(E_NOT_AVAILABLE);
            default: fail(E_SYNTAX);
        }
    }

    // One statement; on a parse error it becomes OP_ERR and the line ends.
    void guarded_statement()
    {
        if(c.in.sem.stmt_markers) { c.e8(OP_STMT); }
        uint32_t start = c.here();
        uint32_t eol_count = eol.n;
        jmp_buf here;
        jmp_buf *outer = jb;
        jb = &here;
        int e = setjmp(here);
        if(e == 0) {
            statement();
            if(!at_end()) { fail(E_SYNTAX); }
        } else {
            c.truncate(start);
            eol.n = eol_count;
            c.e8(OP_ERR);
            c.e8(static_cast<uint8_t>(e));
            type = T_EOL;
        }
        jb = outer;
    }

    // Statements separated by colons, up to the end of the line or an ELSE.
    void block()
    {
        for(;;) {
            while(is_char(':')) { next(); }
            if(type == T_EOL || is_kw(KW_ELSE)) { return; }
            guarded_statement();
        }
    }
};

}  // namespace

void msfamily_compile_line(Compiler &c, const char *text)
{
    Parser ps{c, static_cast<const Keyword *>(c.in.dialect->frontend),
        c.in.sem.crunch_keywords, text, nullptr, {}, 0, nullptr,
        T_EOL, KW_NONE, nullptr, 0, {}, nullptr, 0, {}, K_NUM};
    ps.next();
    ps.block();
    if(ps.type != T_EOL) {                 // an ELSE with no IF
        c.e8(OP_ERR);
        c.e8(E_SYNTAX);
    }
    for(uint32_t k = 0; k < ps.eol.n; k++) { c.patch32(ps.eol[k], c.here()); }
    free(ps.eol.d);
}

}  // namespace basic
