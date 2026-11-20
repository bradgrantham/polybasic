// POSIX host platform: terminal console, files, and the command line.
//
// usage: basic [options] [program.bas]
//
//   --dialect NAME    use this dialect whatever the program's first line says
//   --list-dialects
//   --randomize       seed RND from the clock
//   --check           compile only; list the statements that do not compile
//   --disk DIR        Applesoft: the directory DOS commands treat as the disk
//                     (default: the program's directory)
//   --scratch DIR     Applesoft: where files the program writes are kept;
//                     without it the disk is write protected
//   --stream          Applesoft: plain output, never a 40x24 screen
//   --window          Applesoft, Griffin: open the graphics window at once (it opens by
//                     itself at the first graphics statement, when run from a
//                     terminal)
//   --no-window       Applesoft, Griffin: never open it
//   --png FILE        Applesoft, Griffin: write the last picture the program showed to
//                     FILE; no window unless --window is given too
//   --fast            Applesoft: in the window, do not slow down to an Apple's pace
//   --wav FILE        Griffin: write the sound the program made to FILE instead
//                     of playing it
//   --no-sound        Griffin: do not use the sound device
//   --break-after N, --no-sleep    for tests, see below
//
// With a program file: load it, run it, exit.  Without: read lines from
// standard input as direct-mode commands and program lines.  The dialect comes
// from --dialect, else from a first line like "REM APPLESOFT BASIC", else it
// is the first one listed by --list-dialects.
//
// RND starts from the same seed every run unless --randomize is given.
// --break-after and --no-sleep exist for tests: the first interrupts the
// program after N loop iterations or jumps, the second makes WAIT instant.
#include "../../core/basic.h"
#include "../../core/platform.h"
#include "../../machines/apple2/apple2.h"
#include "host.h"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

namespace basic {

extern const Dialect msbasic_dialect;
extern const Dialect applesoft_dialect;
extern const Dialect griffin_dialect;
const Dialect *const dialects[] = {&msbasic_dialect, &applesoft_dialect, &griffin_dialect, nullptr};

static volatile sig_atomic_t break_flag;
static bool echo_input;           // input is not a terminal: show what was read

static void on_sigint(int) { break_flag = 1; }
void host_break() { break_flag = 1; }

void plat_write(const char *s, uint32_t len)
{
    static int terminal = -1;
    if(terminal < 0) { terminal = isatty(STDOUT_FILENO); }
    fwrite(s, 1, len, stdout);
    // Someone watching sees output as it happens, even from a program that
    // then computes for a long time.
    if(terminal) { fflush(stdout); }
}

// plat_poll_key leaves the terminal reading single keys without echo; this
// puts it back.
static bool polling;
static struct termios cooked;

static void stop_polling()
{
    if(polling) {
        tcsetattr(STDIN_FILENO, TCSANOW, &cooked);
        polling = false;
    }
}

int plat_poll_key()
{
    fflush(stdout);
    if(av_window_open()) {
        plat_fb_update();
        return plat_display_key();
    }
    if(!isatty(STDIN_FILENO)) {
        int c = fgetc(stdin);
        return c == EOF ? -2 : c;
    }
    if(!polling) {
        struct termios raw;
        tcgetattr(STDIN_FILENO, &cooked);
        raw = cooked;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);    // Ctrl-C still interrupts
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        polling = true;
        atexit(stop_polling);
    }
    unsigned char c;
    return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}

int plat_read_line(char *buf, int cap)
{
    stop_polling();
    fflush(stdout);
    if(!fgets(buf, cap, stdin)) { return -1; }
    int n = static_cast<int>(strlen(buf));
    while(n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) { buf[--n] = 0; }
    if(echo_input) {
        fwrite(buf, 1, static_cast<size_t>(n), stdout);
        fputc('\n', stdout);
    }
    return n;
}

int plat_read_key()
{
    stop_polling();
    fflush(stdout);
    if(!isatty(STDIN_FILENO)) { return fgetc(stdin); }   // EOF is -1
    // One key, unechoed, without waiting for Return.  Ctrl-C arrives as 3.
    struct termios saved, raw;
    tcgetattr(STDIN_FILENO, &saved);
    raw = saved;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    unsigned char c;
    ssize_t got = read(STDIN_FILENO, &c, 1);
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return got == 1 ? c : -1;
}

bool plat_is_terminal()
{
    return isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);
}

bool plat_input_is_terminal()
{
    return isatty(STDIN_FILENO);
}

uint32_t plat_ticks_ms()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint32_t>(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static long break_after = -1;    // --break-after: interrupt after this many checks
static bool no_sleep;             // --no-sleep: WAIT returns at once

bool plat_break_pending()
{
    // The core asks on every backward jump, so counting the calls gives tests
    // a way to stop a program that never ends.
    if(break_after >= 0 && break_after-- == 0) { return true; }
    if(!break_flag) { return false; }
    break_flag = 0;
    return true;
}

void plat_sleep_ms(uint32_t ms)
{
    fflush(stdout);
    if(no_sleep) { return; }
    // In slices, so that sound is kept fed and a window stays alive.
    while(ms > 0) {
        uint32_t step = ms > 10 ? 10 : ms;
        av_idle();
        usleep(step * 1000);
        ms -= step;
    }
}

uint32_t plat_entropy()
{
    return static_cast<uint32_t>(time(nullptr)) ^ (static_cast<uint32_t>(getpid()) << 16);
}

char *plat_read_file(const char *name, uint32_t *len)
{
    FILE *f = fopen(name, "rb");
    if(!f) { return nullptr; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = static_cast<char *>(malloc(size > 0 ? static_cast<size_t>(size) : 1));
    if(data && size > 0 && fread(data, 1, static_cast<size_t>(size), f) != static_cast<size_t>(size)) {
        free(data);
        data = nullptr;
    }
    fclose(f);
    *len = static_cast<uint32_t>(size > 0 ? size : 0);
    return data;
}

bool plat_write_file(const char *name, const char *data, uint32_t len)
{
    FILE *f = fopen(name, "wb");
    if(!f) { return false; }
    bool ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

bool plat_delete_file(const char *name)
{
    return unlink(name) == 0;
}

bool plat_list_dir(const char *dir, void (*fn)(void *ctx, const char *name), void *ctx)
{
    DIR *d = opendir(dir);
    if(!d) { return false; }
    while(struct dirent *e = readdir(d)) {
        if(strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) { fn(ctx, e->d_name); }
    }
    closedir(d);
    return true;
}

static const Dialect *find_dialect(const char *name)
{
    for(int k = 0; dialects[k]; k++) {
        if(strcasecmp(dialects[k]->name, name) == 0) { return dialects[k]; }
    }
    return nullptr;
}

}  // namespace basic

static void usage()
{
    fprintf(stderr,
        "usage: basic [--dialect NAME] [--list-dialects] [--randomize] [--check]\n"
        "             [--disk DIR] [--scratch DIR] [--stream]\n"
        "             [--window] [--no-window] [--png FILE] [--fast]\n"
        "             [--wav FILE] [--no-sound]\n"
        "             [--break-after N] [--no-sleep] [program.bas]\n");
}

int main(int argc, char **argv)
{
    using namespace basic;
    const Dialect *dialect = nullptr;
    const char *program = nullptr;
    const char *disk = nullptr;
    const char *scratch = nullptr;
    const char *png = nullptr;
    const char *wav = nullptr;
    bool sound = true;
    bool randomize = false, check = false, stream = false, fast = false;
    int window = 0;

    for(int k = 1; k < argc; k++) {
        if(strcmp(argv[k], "--dialect") == 0 && k + 1 < argc) {
            dialect = find_dialect(argv[++k]);
            if(!dialect) {
                fprintf(stderr, "unknown dialect \"%s\" (try --list-dialects)\n", argv[k]);
                return 2;
            }
        } else if(strcmp(argv[k], "--randomize") == 0) {
            randomize = true;
        } else if(strcmp(argv[k], "--check") == 0) {
            check = true;
        } else if(strcmp(argv[k], "--stream") == 0) {
            stream = true;
        } else if(strcmp(argv[k], "--window") == 0) {
            window = 1;
        } else if(strcmp(argv[k], "--no-window") == 0) {
            window = -1;
        } else if(strcmp(argv[k], "--fast") == 0) {
            fast = true;
        } else if(strcmp(argv[k], "--png") == 0 && k + 1 < argc) {
            png = argv[++k];
        } else if(strcmp(argv[k], "--wav") == 0 && k + 1 < argc) {
            wav = argv[++k];
        } else if(strcmp(argv[k], "--no-sound") == 0) {
            sound = false;
        } else if(strcmp(argv[k], "--disk") == 0 && k + 1 < argc) {
            disk = argv[++k];
        } else if(strcmp(argv[k], "--scratch") == 0 && k + 1 < argc) {
            scratch = argv[++k];
        } else if(strcmp(argv[k], "--break-after") == 0 && k + 1 < argc) {
            break_after = atol(argv[++k]);
        } else if(strcmp(argv[k], "--no-sleep") == 0) {
            no_sleep = true;
        } else if(strcmp(argv[k], "--list-dialects") == 0) {
            for(int d = 0; dialects[d]; d++) { printf("%s\n", dialects[d]->name); }
            return 0;
        } else if(argv[k][0] == '-') {
            usage();
            return 2;
        } else {
            program = argv[k];
        }
    }
    if(check && !program) {
        usage();
        return 2;
    }

    // The Apple II disk defaults to the directory the program came from.
    static char program_dir[1024];
    if(!disk && program) {
        snprintf(program_dir, sizeof(program_dir), "%s", program);
        char *slash = strrchr(program_dir, '/');
        if(slash) {
            *slash = 0;
            disk = program_dir;
        }
    }
    if(scratch) { mkdir(scratch, 0777); }         // one level; fine if it exists
    apple2_configure(Apple2Options{disk, scratch, stream, check ? -1 : window, check ? nullptr : png, fast});

    av_configure(AvOptions{check ? nullptr : png, check ? nullptr : wav, check ? -1 : window, sound, no_sleep});

    signal(SIGINT, on_sigint);
    echo_input = !isatty(STDIN_FILENO);

    static Interp in;
    init(in, dialect ? dialect : dialects[0]);
    in.dialect_locked = dialect != nullptr;
    if(randomize) { in.rnd_state = plat_entropy() | 1; }

    int status = 0;
    if(program) {
        Err e = load_program(in, program);
        if(e != E_NONE) {
            fprintf(stderr, e == E_FILE ? "Could not open \"%s\"\n"
                : "\"%s\" is in a dialect this build does not include\n", program);
            return 1;
        }
        if(check) {
            status = check_program(in) == 0 ? 0 : 1;
        } else {
            status = run_program(in) == E_NONE ? 0 : 1;
            // A program that gave DOS an EXEC file has left lines to obey
            // at the prompt; Ctrl-C puts an end to them.
            while(apple2_exec_pending()) {
                int e = direct_step(in);
                if(e < 0) { break; }
                if(e == E_BREAK) { apple2_exec_cancel(); }
            }
        }
    } else {
        repl(in);
    }
    if(in.dialect->machine) { in.dialect->machine->finish(in); }
    av_finish();
    if(in.column != 0) { fputc('\n', stdout); }
    fflush(stdout);
    return status;
}
