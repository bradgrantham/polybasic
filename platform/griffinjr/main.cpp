// Griffin Jr. platform: the BIOS console (53x24 video text, PS/2 keyboard) or
// serial port A, files on C: and D:, and the command line.
//
// usage: basic [-s] [--marks] [--time] [--break-after N] [--no-sleep] [program.bas]
//
//   -s    use serial port A (38400 8N1) as the console instead of the screen
//         and keyboard.  Input is echoed and Ctrl-D ends the input.  The
//         emulator's standard output is this port, which is how
//         tests/run-griffinjr.sh sees what a program printed.
//
// With a program file: load it, run it, exit to the monitor.  Without: direct
// mode.  The rest are for tests: --break-after and --no-sleep as on the host,
// and --marks sends STX (2) on the serial port before anything else and EOT
// (4) on the way out, so a script can tell where the interpreter's output
// starts and ends.  --time prints how long the program ran, in frames (60ths
// of a second on an NTSC machine).
//
// Everything goes through the BIOS calls in the SDK's syscalls.h; the only
// newlib functions used here are the ones the core already needs.
#include "../../core/basic.h"
#include "../../core/platform.h"

#include <cstdio>

extern "C" {
#include "syscalls.h"
}

namespace basic {

extern const Dialect msbasic_dialect;
const Dialect *const dialects[] = {&msbasic_dialect, nullptr};

static bool serial;               // -s: the console is serial port A
static bool marks;                // --marks
static bool at_eof;               // serial: Ctrl-D was read
static long break_after = -1;     // --break-after: interrupt after this many checks
static bool no_sleep;             // --no-sleep: WAIT returns at once
static bool break_flag;           // Ctrl-C was typed

// Keys typed while the program was running, found when looking for Ctrl-C.
enum { AHEAD = 32 };
static uint8_t ahead[AHEAD];
static uint8_t ahead_n;

void plat_write(const char *s, uint32_t len)
{
    if(!serial) {
        sys_write(1, len, s);
        return;
    }
    for(uint32_t k = 0; k < len; k++) {
        if(s[k] == '\n') { sys_serial_putc('\r'); }
        sys_serial_putc(s[k]);
    }
}

// The next key from the keyboard or the port without waiting: -1 if none.
static int source_poll()
{
    return serial ? sys_serial_getc() : sys_pollchar();
}

// Move what has been typed into `ahead`, noting a Ctrl-C.
static void drain()
{
    int c;
    while(ahead_n < AHEAD && (c = source_poll()) >= 0) {
        if(c == 3) {
            break_flag = true;
            ahead_n = 0;          // what was typed before the break is dropped
        } else {
            ahead[ahead_n++] = static_cast<uint8_t>(c);
        }
    }
}

// The next key: waits if `wait`, else -1 when there is none.  -2 at the end of
// serial input.
static int next_key(bool wait)
{
    for(;;) {
        if(at_eof) { return -2; }
        int c;
        if(ahead_n > 0) {
            c = ahead[0];
            memmove(ahead, ahead + 1, --ahead_n);
        } else if(serial || !wait) {
            c = source_poll();
        } else {
            c = sys_getchar();    // lets the BIOS show its cursor while it waits
        }
        if(c < 0) {
            if(!wait) { return -1; }
            continue;
        }
        if(serial && c == 4) {
            at_eof = true;
            return -2;
        }
        return c;
    }
}

int plat_poll_key()
{
    return next_key(false);
}

int plat_read_key()
{
    int c = next_key(true);
    return c < 0 ? -1 : c;
}

// A line editor of our own, so that keys typed ahead are not lost and the
// serial console behaves like the screen: echo, Backspace, Return.
int plat_read_line(char *buf, int cap)
{
    int n = 0;
    bool after_cr = false;
    for(;;) {
        int c = next_key(true);
        if(c < 0) {
            if(n == 0) { return -1; }
            break;
        }
        if(c == '\n' && after_cr) {                 // the LF of a CR LF pair
            after_cr = false;
            continue;
        }
        after_cr = false;
        if(c == '\r' || c == '\n') {
            // A CR LF pair split across two reads: the LF is still to come and
            // must not end the next line.  Peek for it.
            if(c == '\r') { after_cr = true; }
            break;
        }
        if(c == 3) {
            break_flag = true;
            n = 0;
            break;
        }
        if(c == 8 || c == 127) {
            if(n > 0) {
                n--;
                plat_write("\b \b", 3);
            }
        } else if(c >= 32 && n < cap - 1) {
            buf[n++] = static_cast<char>(c);
            char ch = static_cast<char>(c);
            plat_write(&ch, 1);
        }
    }
    if(after_cr) {
        // Swallow an LF that follows at once, without waiting for one.
        int c = next_key(false);
        if(c >= 0 && c != '\n' && ahead_n < AHEAD) {
            memmove(ahead + 1, ahead, ahead_n++);
            ahead[0] = static_cast<uint8_t>(c);
        }
    }
    buf[n] = 0;
    plat_write("\n", 1);
    return n;
}

bool plat_is_terminal() { return false; }            // no ANSI cursor controls
bool plat_input_is_terminal() { return !serial; }

bool plat_break_pending()
{
    if(break_after >= 0 && break_after-- == 0) { return true; }
    // The core asks on every backward jump, and a BIOS call costs a trap, so
    // the keyboard is looked at on one call in 64.
    static uint8_t count;
    if((++count & 63) == 0) { drain(); }
    if(!break_flag) { return false; }
    break_flag = false;
    return true;
}

void plat_sleep_ms(uint32_t ms)
{
    if(!no_sleep) { sys_sleep_ms(ms); }
}

uint32_t plat_ticks_ms()
{
    // Frames since boot; 60 a second is exact enough (NTSC is 59.95, PAL 50.08).
    return sys_ticks() * 50u / 3u;
}

uint32_t plat_entropy()
{
    return sys_ticks() ^ (sys_time() << 8);
}

char *plat_read_file(const char *name, uint32_t *len)
{
    int fd = sys_open(O_RDONLY, name);
    if(fd < 0) { return nullptr; }
    int size = sys_lseek(fd, 0, SEEK_END);
    char *data = nullptr;
    if(size >= 0 && sys_lseek(fd, 0, SEEK_SET) == 0) {
        data = static_cast<char *>(malloc(size > 0 ? static_cast<size_t>(size) : 1));
        int got = 0;
        while(data && got < size) {
            int n = sys_read(fd, static_cast<unsigned>(size - got), data + got);
            if(n <= 0) {
                free(data);
                data = nullptr;
            } else {
                got += n;
            }
        }
    }
    sys_close(fd);
    *len = static_cast<uint32_t>(size > 0 ? size : 0);
    return data;
}

bool plat_write_file(const char *name, const char *data, uint32_t len)
{
    int fd = sys_open(O_WRONLY | O_CREAT | O_TRUNC, name);
    if(fd < 0) { return false; }
    bool ok = true;
    while(ok && len > 0) {
        int n = sys_write(fd, len, data);
        if(n <= 0) {
            ok = false;
        } else {
            data += n;
            len -= static_cast<uint32_t>(n);
        }
    }
    return sys_close(fd) == 0 && ok;
}

bool plat_delete_file(const char *name)
{
    return sys_unlink(name) == 0;
}

bool plat_list_dir(const char *dir, void (*fn)(void *ctx, const char *name), void *ctx)
{
    int h = sys_opendir(dir);
    if(h < 0) { return false; }
    static struct dirent e;
    while(sys_readdir(h, &e) == 1) { fn(ctx, e.name); }
    sys_closedir(h);
    return true;
}

// No display behind the plat_display_* interface yet (phase 4b).
bool plat_display_open(int, int, const char *) { return false; }
void plat_display_show(const uint32_t *) {}
int plat_display_pump() { return 0; }
int plat_display_key() { return -1; }

static void say(const char *s)
{
    plat_write(s, static_cast<uint32_t>(strlen(s)));
}

}  // namespace basic

int main(int argc, char **argv)
{
    using namespace basic;
    const char *program = nullptr;
    bool timed = false;

    for(int k = 1; k < argc; k++) {
        if(strcmp(argv[k], "-s") == 0) {
            serial = true;
        } else if(strcmp(argv[k], "--time") == 0) {
            timed = true;
        } else if(strcmp(argv[k], "--marks") == 0) {
            marks = true;
        } else if(strcmp(argv[k], "--break-after") == 0 && k + 1 < argc) {
            break_after = atol(argv[++k]);
        } else if(strcmp(argv[k], "--no-sleep") == 0) {
            no_sleep = true;
        } else if(argv[k][0] == '-') {
            say("usage: basic [-s] [--marks] [--time] [--break-after N] [--no-sleep] [program.bas]\n");
            return 2;
        } else {
            program = argv[k];
        }
    }

    if(marks) { sys_serial_putc(2); }
    static Interp in;
    init(in, dialects[0]);
    if(!serial) {
        static struct con_info ci;
        if(sys_con_info(&ci) == 0 && ci.cols > 0) { in.width = static_cast<int>(ci.cols); }
    }

    int status = 0;
    if(program) {
        Err e = load_program(in, program);
        if(e != E_NONE) {
            say("Could not open \"");
            say(program);
            say("\"\n");
            status = 1;
        } else {
            uint32_t start = sys_ticks();
            status = run_program(in) == E_NONE ? 0 : 1;
            if(timed) {
                char buf[40];
                snprintf(buf, sizeof(buf), "%s[%lu frames]\n", in.column != 0 ? "\n" : "",
                    static_cast<unsigned long>(sys_ticks() - start));
                in.column = 0;
                say(buf);
            }
        }
    } else {
        if(!serial) { say("GRIFFIN BASIC\n"); }
        repl(in);
    }
    if(in.column != 0) { say("\n"); }
    if(marks) { sys_serial_putc(4); }
    return status;
}
