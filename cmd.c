/*═══════════════════════════════════════════════════════════════════════════════
 *
 *  UZ80 · cmd.c — built-in commands
 *
 *  A flat dispatch table: name, one-line man page, handler.  Each handler
 *  takes the (NUL-terminated) argument tail.  The whole shell is here —
 *  no pipes, no redirection beyond `>`, no quoting, no globbing.  Honest.
 *
 *  Author: F E R M I ∞ H A R T <contact@fermihart.com>
 *  SPDX-License-Identifier: Unlicense
 *  Listening recommended: open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o
 *
 *═══════════════════════════════════════════════════════════════════════════════*/

#include "uz80.h"

/* ── small string helpers ────────────────────────────────────────────────── */
static uint8_t streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

/* trim leading spaces */
static char *skipws(char *s) { while (*s == ' ') s++; return s; }

/* split off the first whitespace-delimited token; returns the rest.
 * mutates `s` by inserting NUL. */
static char *splitws(char *s)
{
    while (*s && *s != ' ') s++;
    if (!*s) return s;
    *s++ = 0;
    return skipws(s);
}

/* very small PRNG — a 16-bit LFSR, seeded from the frame counter.  Good
 * enough for `fortune` and nothing else. */
static uint16_t lfsr_seed;
static uint16_t rnd(void)
{
    uint16_t v = lfsr_seed ? lfsr_seed : UZ_FRAMES16 | 1;
    /* Galois LFSR, taps from a well-known 16-bit polynomial */
    uint8_t lsb = (uint8_t)(v & 1);
    v >>= 1;
    if (lsb) v ^= 0xB400;
    lfsr_seed = v;
    return v;
}

/* ── command handlers ────────────────────────────────────────────────────── */
static void cmd_help(char *arg);

static void cmd_clear(char *arg) { (void)arg; tty_clear(); }
static void cmd_halt (char *arg) { (void)arg; __asm di __endasm; for (;;) __asm halt __endasm; }

static void cmd_echo(char *arg)
{
    /* support `echo TEXT > FILE` — anything after `>` is the filename */
    char *redir = arg;
    while (*redir && *redir != '>') redir++;
    if (*redir == '>') {
        char *fname;
        *redir++ = 0;
        fname = skipws(redir);
        /* strip trailing spaces from arg */
        { char *p = arg; while (*p) p++;
          while (p > arg && p[-1] == ' ') { *--p = 0; } }
        if (*fname) {
            fs_file_t *f = fs_create(fname);
            if (f) fs_write(f, arg);
            else   tty_puts("echo: cannot open\n");
        }
        return;
    }
    tty_puts(arg);
    tty_putc('\n');
}

static void cmd_ls(char *arg)
{
    uint8_t i, n = 0;
    (void)arg;
    for (i = 0; i < FS_FILEMAX; i++) {
        fs_file_t *f = fs_iter(i);
        if (!f) continue;
        if (n && (n & 1)) tty_puts("  ");      /* two columns */
        tty_puts(f->name);
        /* pad to a fixed width for alignment */
        { uint8_t k = 0; const char *p = f->name;
          while (p[k]) k++;
          while (k++ < 12) tty_putc(' '); }
        if (n & 1) tty_putc('\n');
        n++;
    }
    if (n && (n & 1)) tty_putc('\n');
    if (!n) tty_puts("(empty)\n");
}

static void cmd_cat(char *arg)
{
    fs_file_t *f;
    if (!*arg) { tty_puts("usage: cat FILE\n"); return; }
    f = fs_find(arg);
    if (!f) { tty_puts("cat: no such file\n"); return; }
    {
        uint8_t i;
        for (i = 0; i < f->size; i++) tty_putc((char)f->data[i]);
        if (f->size && f->data[f->size - 1] != '\n') tty_putc('\n');
    }
}

static void cmd_rm(char *arg)
{
    if (!*arg) { tty_puts("usage: rm FILE\n"); return; }
    if (fs_delete(arg) < 0) tty_puts("rm: no such file\n");
}

static void cmd_touch(char *arg)
{
    if (!*arg) { tty_puts("usage: touch FILE\n"); return; }
    if (!fs_create(arg)) tty_puts("touch: cannot create\n");
}

static void cmd_wc(char *arg)
{
    fs_file_t *f;
    uint16_t   lines = 0, words = 0;
    uint8_t    i, in_word = 0;
    if (!*arg) { tty_puts("usage: wc FILE\n"); return; }
    f = fs_find(arg);
    if (!f) { tty_puts("wc: no such file\n"); return; }
    for (i = 0; i < f->size; i++) {
        uint8_t c = f->data[i];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\n' || c == '\t') in_word = 0;
        else if (!in_word) { in_word = 1; words++; }
    }
    tty_putu(lines);  tty_putc(' ');
    tty_putu(words);  tty_putc(' ');
    tty_putu(f->size);
    tty_putc(' ');    tty_puts(arg); tty_putc('\n');
}

static void cmd_cp(char *arg)
{
    char *dst; fs_file_t *fa, *fb; uint8_t i;
    dst = splitws(arg);
    if (!*arg || !*dst) { tty_puts("usage: cp SRC DST\n"); return; }
    fa = fs_find(arg);
    if (!fa) { tty_puts("cp: no such source\n"); return; }
    fb = fs_create(dst);
    if (!fb) { tty_puts("cp: cannot create dst\n"); return; }
    for (i = 0; i < fa->size; i++) fb->data[i] = fa->data[i];
    fb->size = fa->size;
}

static void cmd_mv(char *arg)
{
    char *dst; fs_file_t *fa, *fb; uint8_t i;
    dst = splitws(arg);
    if (!*arg || !*dst) { tty_puts("usage: mv SRC DST\n"); return; }
    fa = fs_find(arg);
    if (!fa) { tty_puts("mv: no such source\n"); return; }
    fb = fs_create(dst);
    if (!fb) { tty_puts("mv: cannot create dst\n"); return; }
    for (i = 0; i < fa->size; i++) fb->data[i] = fa->data[i];
    fb->size = fa->size;
    fa->name[0] = 0; fa->size = 0;
}

static void cmd_pwd  (char *arg) { (void)arg; tty_puts("/\n"); }
static void cmd_cd   (char *arg) { (void)arg; /* nowhere to go */ }
static void cmd_whoami(char *arg){ (void)arg; tty_puts("aufer\n"); }
static void cmd_uname(char *arg)
{
    if (*arg == '-' && arg[1] == 'a')
        tty_puts("uz80 fermihart 2.0 z80 (16k rom) bear-libcs\n");
    else
        tty_puts("uz80\n");
}
static void cmd_date(char *arg)
{
    (void)arg;
    /* no RTC.  print boot uptime as a "stardate". */
    tty_puts("stardate ");
    tty_putu(UZ_FRAMES16 / 50);
    tty_puts(" sec since boot\n");
}
static void cmd_uptime(char *arg)
{
    uint16_t s = UZ_FRAMES16 / 50;
    (void)arg;
    tty_puts("up ");
    tty_putu(s / 60); tty_putc('m'); tty_putc(' ');
    tty_putu(s % 60); tty_puts("s\n");
}

static void cmd_history(char *arg)
{
    /* The history array lives inside tty.c — expose it via a small
     * accessor.  We just call our own hist_get analogue here by walking
     * the readline history through a single tty entry point.  For
     * simplicity v2 prints a hint instead. */
    (void)arg;
    tty_puts("(use UP/DOWN at the prompt:\n");
    tty_puts(" caps+7 = up, caps+6 = down)\n");
}

/* --- fortunes / motd / bear ascii --- */
static void cmd_motd(char *arg)
{
    (void)arg;
    fs_file_t *f = fs_find("motd");
    if (f) { char tmp[2] = {0}; uint8_t i;
        for (i = 0; i < f->size; i++) { tmp[0] = (char)f->data[i]; tty_puts(tmp); }
    }
}

static void cmd_fortune(char *arg)
{
    fs_file_t *f = fs_find("fortune");
    uint8_t    i, n = 0, pick, start, end;
    (void)arg;
    if (!f) { tty_puts("(no fortunes)\n"); return; }
    /* Count blank-line-delimited records. */
    n = 1;
    for (i = 1; i < f->size; i++)
        if (f->data[i - 1] == '\n' && f->data[i] == '\n') n++;
    if (!n) return;
    pick = (uint8_t)(rnd() % n);
    /* Walk to record `pick`. */
    start = 0;
    for (i = 1, n = 0; i < f->size && n < pick; i++)
        if (f->data[i - 1] == '\n' && f->data[i] == '\n') { n++; start = (uint8_t)(i + 1); }
    end = start;
    while (end < f->size) {
        if (end + 1 < f->size && f->data[end] == '\n' && f->data[end + 1] == '\n')
            break;
        end++;
    }
    for (i = start; i < end; i++) tty_putc((char)f->data[i]);
    tty_putc('\n');
}

/* `cowsay TEXT` — the standard ascii cow with a speech bubble */
static void cmd_cowsay(char *arg)
{
    uint8_t i, n;
    if (!*arg) arg = (char *)"moo";
    n = 0; while (arg[n] && n < 26) n++;

    tty_putc(' '); for (i = 0; i < n + 2; i++) tty_putc('-'); tty_putc('\n');
    tty_putc('<'); tty_putc(' '); tty_puts(arg); tty_putc(' '); tty_putc('>'); tty_putc('\n');
    tty_putc(' '); for (i = 0; i < n + 2; i++) tty_putc('-'); tty_putc('\n');
    tty_puts("        \\   ^__^\n");
    tty_puts("         \\  (oo)\\_____\n");
    tty_puts("            (__)\\     )\n");
    tty_puts("                ||---w|\n");
    tty_puts("                ||   ||\n");
}

static void cmd_bear(char *arg)
{
    (void)arg;
    tty_puts(" bb. eee  aa  rrr\n");
    tty_puts(" bb. ee  a a  rr.\n");
    tty_puts(" bbb eee a a  r r\n");
    tty_puts("       libcs\n");
    tty_puts("strong by default,\n");
    tty_puts("lean by design,\n");
    tty_puts("private by nature.\n");
}

/* --- sound --- */
static void cmd_beep(char *arg)
{
    (void)arg;
    /* ~440 Hz, ~100 ms.  cycles ≈ 44, half-period in tight-loop units. */
    beep_tone(40, 200);
}

static void cmd_play(char *arg)
{
    /* Two embedded tunes.  Each step is (cycles, half).  cycles ~ duration;
     * half ~ pitch (smaller = higher).  Calibrated by ear in emulation. */
    static const uint16_t boot[] = {
        20, 320,   20, 240,   30, 180,   50, 120,   0, 0
    };
    static const uint16_t alert[] = {
        30, 100,   30, 200,   30, 100,   30, 200,   0, 0
    };
    const uint16_t *t = boot;
    uint8_t i;
    if (streq(arg, "alert")) t = alert;
    for (i = 0; t[i * 2]; i++) beep_tone(t[i * 2], t[i * 2 + 1]);
}

/* --- man --- */
typedef struct {
    const char *name;
    const char *desc;
    void      (*fn)(char *);
} cmd_t;

extern const cmd_t CMDS[];

static void cmd_forth_thunk(char *arg) { (void)arg; forth_repl(); }

/* `forthdemo` — evaluate a Forth script straight from C, so the proof of
 * life doesn't depend on the Spectrum keyboard producing every symbol the
 * language uses.  The script exercises: literals, arithmetic, stack ops,
 * IO, colon-definition, and re-using a defined word. */
static void cmd_forthdemo(char *arg)
{
    (void)arg;
    tty_puts("uforth one-shot demo:\n");
    tty_puts("> 3 5 add print cr\n");
    forth_eval("3 5 add print cr");
    tty_puts("> def sq dup mul end  7 sq print cr\n");
    forth_eval("def sq dup mul end  7 sq print cr");
    tty_puts("> def cube dup sq mul end  3 cube print cr\n");
    forth_eval("def cube dup sq mul end  3 cube print cr");
    tty_puts("stack now: ");
    forth_eval("stack");
}

static void cmd_man(char *arg)
{
    uint8_t i;
    if (!*arg) { tty_puts("usage: man CMD\n"); return; }
    for (i = 0; CMDS[i].name; i++) {
        if (streq(CMDS[i].name, arg)) {
            tty_puts(CMDS[i].name); tty_putc(' '); tty_putc('-'); tty_putc(' ');
            tty_puts(CMDS[i].desc); tty_putc('\n');
            return;
        }
    }
    tty_puts("man: no entry\n");
}

/* ── dispatch table ──────────────────────────────────────────────────────── */
const cmd_t CMDS[] = {
    { "help",    "this list",                     cmd_help    },
    { "clear",   "clear the screen",              cmd_clear   },
    { "cls",     "alias for clear",               cmd_clear   },
    { "echo",    "print, or `echo TEXT > FILE`",  cmd_echo    },
    { "ls",      "list files in /",               cmd_ls      },
    { "cat",     "print a file",                  cmd_cat     },
    { "rm",      "remove a file",                 cmd_rm      },
    { "touch",   "create empty file",             cmd_touch   },
    { "wc",      "line word byte count of FILE",  cmd_wc      },
    { "cp",      "copy SRC DST",                  cmd_cp      },
    { "mv",      "rename SRC DST",                cmd_mv      },
    { "pwd",     "print working directory",       cmd_pwd     },
    { "cd",      "change directory (a no-op)",    cmd_cd      },
    { "uname",   "show kernel id  (-a long)",     cmd_uname   },
    { "whoami",  "print the current user",        cmd_whoami  },
    { "date",    "fake date (stardate, sec)",     cmd_date    },
    { "uptime",  "time since boot",               cmd_uptime  },
    { "history", "show recent commands",          cmd_history },
    { "halt",    "stop the CPU",                  cmd_halt    },
    { "motd",    "re-display the motd",           cmd_motd    },
    { "fortune", "random quote from /fortune",    cmd_fortune },
    { "cowsay",  "ascii cow says TEXT",           cmd_cowsay  },
    { "bear",    "bear libcs ascii banner",       cmd_bear    },
    { "beep",    "short 440 hz beep",             cmd_beep    },
    { "play",    "play TUNE  (boot | alert)",     cmd_play    },
    { "man",     "one-line man page for CMD",     cmd_man     },
    { "forth",     "drop into uforth (bye to leave)", cmd_forth_thunk },
    { "forthdemo", "evaluate a canned uforth script", cmd_forthdemo   },
    { 0, 0, 0 }
};

static void cmd_help(char *arg)
{
    uint8_t i, col = 0;
    (void)arg;
    tty_puts("commands:\n");
    for (i = 0; CMDS[i].name; i++) {
        const char *n = CMDS[i].name;
        uint8_t     k = 0;
        while (n[k]) k++;
        if (col + k + 1 > COLS) { tty_putc('\n'); col = 0; }
        tty_puts(n); tty_putc(' ');
        col = (uint8_t)(col + k + 1);
    }
    if (col) tty_putc('\n');
    tty_puts("type `man CMD` for details.\n");
}

void cmd_dispatch(char *line)
{
    char    *arg;
    uint8_t  i;

    line = skipws(line);
    if (!*line) return;
    arg = splitws(line);

    for (i = 0; CMDS[i].name; i++) {
        if (streq(CMDS[i].name, line)) { CMDS[i].fn(arg); return; }
    }
    tty_puts(line);
    tty_puts(": not found (try help)\n");
}
