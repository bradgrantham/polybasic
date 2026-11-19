// Number formatting and parsing.
#include "basic.h"

#include <cmath>
#include <cstdio>

namespace basic {

// Format the way Microsoft BASICs do: up to `print_digits` significant digits,
// no leading zero before the decimal point, no trailing zeros, and scientific
// notation outside 0.01 <= |x| < 10^digits.  Writes a NUL-terminated string
// with a leading '-' for negatives and no padding; returns its length.
int format_number(const Value &v, const Semantics &sem, char *out)
{
    int digits = sem.print_digits;
    char *o = out;

    if(v.tag == T_INT) {
        int32_t limit = 1;
        for(int k = 0; k < digits && limit < 1000000000; k++) { limit *= 10; }
        if(v.i > -limit && v.i < limit) {
            return snprintf(out, 16, "%ld", static_cast<long>(v.i));
        }
    }

    double d = v.tag == T_INT ? static_cast<double>(v.i) : v.d;
    if(d == 0) {
        strcpy(out, "0");
        return 1;
    }
    if(std::isnan(d) || std::isinf(d)) {
        strcpy(out, std::isnan(d) ? "NAN" : (d < 0 ? "-INF" : "INF"));
        return static_cast<int>(strlen(out));
    }
    if(d < 0) {
        *o++ = '-';
        d = -d;
    }

    // "d.ddddde+XX": pull out the digit string and the decimal exponent.
    char tmp[40];
    snprintf(tmp, sizeof(tmp), "%.*e", digits - 1, d);
    char dig[24];
    int nd = 0;
    const char *p = tmp;
    for(; *p && *p != 'e'; p++) {
        if(*p >= '0' && *p <= '9') { dig[nd++] = *p; }
    }
    int exp = atoi(p + 1);
    while(nd > 1 && dig[nd - 1] == '0') { nd--; }

    if(exp >= -2 && exp < digits) {
        if(exp >= 0) {
            for(int k = 0; k <= exp; k++) { *o++ = k < nd ? dig[k] : '0'; }
            if(nd > exp + 1) {
                *o++ = '.';
                for(int k = exp + 1; k < nd; k++) { *o++ = dig[k]; }
            }
        } else {
            *o++ = '.';
            for(int k = -1; k > exp; k--) { *o++ = '0'; }
            for(int k = 0; k < nd; k++) { *o++ = dig[k]; }
        }
    } else {
        *o++ = dig[0];
        if(nd > 1) {
            *o++ = '.';
            for(int k = 1; k < nd; k++) { *o++ = dig[k]; }
        }
        *o++ = 'E';
        *o++ = exp < 0 ? '-' : '+';
        int e = exp < 0 ? -exp : exp;
        if(e >= 100) { *o++ = static_cast<char>('0' + e / 100); }
        *o++ = static_cast<char>('0' + (e / 10) % 10);
        *o++ = static_cast<char>('0' + e % 10);
    }
    *o = 0;
    return static_cast<int>(o - out);
}

// Parse a number at the start of s.  With `whole`, the entire text must be a
// number (surrounding blanks allowed, empty counts as zero); otherwise parsing
// stops at the first character that does not fit, as VAL does.
bool parse_number(const char *s, uint32_t len, bool whole, Value *out)
{
    char buf[64];
    uint32_t n = 0;
    uint32_t i = 0;
    bool is_int = true;
    bool any_digit = false;

    while(i < len && s[i] == ' ') { i++; }
    if(i < len && (s[i] == '+' || s[i] == '-')) { buf[n++] = s[i++]; }
    while(i < len && n < sizeof(buf) - 8) {
        char c = s[i];
        if(c >= '0' && c <= '9') {
            any_digit = true;
        } else if(c == '.' && is_int) {
            is_int = false;
        } else if(c == ' ') {
            i++;                           // blanks inside numbers are ignored
            continue;
        } else {
            break;
        }
        buf[n++] = c;
        i++;
    }
    if(any_digit && i < len && (s[i] == 'E' || s[i] == 'e')) {
        uint32_t j = i + 1;
        while(j < len && s[j] == ' ') { j++; }
        if(j < len && (s[j] == '+' || s[j] == '-')) { j++; }
        while(j < len && s[j] == ' ') { j++; }
        if(j < len && s[j] >= '0' && s[j] <= '9') {
            is_int = false;
            for(; i < j; i++) {
                if(s[i] != ' ') { buf[n++] = s[i]; }
            }
            while(i < len && s[i] >= '0' && s[i] <= '9' && n < sizeof(buf) - 1) {
                buf[n++] = s[i++];
            }
        }
    }
    buf[n] = 0;

    if(whole) {
        while(i < len && s[i] == ' ') { i++; }
        if(i < len) { return false; }
        if(!any_digit && n > 0) { return false; }     // just a sign or a dot
    }

    double d = any_digit ? strtod(buf, nullptr) : 0.0;
    if(is_int && d > -2147483648.0 && d < 2147483648.0) {
        out->tag = T_INT;
        out->i = static_cast<int32_t>(d);
    } else {
        out->tag = T_NUM;
        out->d = d;
    }
    return true;
}

}  // namespace basic
