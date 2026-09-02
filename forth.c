/*═══════════════════════════════════════════════════════════════════════════════
 *
 *  UZ80 · forth.c — uForth, a tiny Forth interpreter
 *
 *  History: the Jupiter Ace (Sinclair-pedigree Z80 machine, 1982) shipped
 *  Forth in ROM where every other home computer of its era shipped BASIC.
 *  UZ80 follows that tradition.
 *
 *  Architecture: classic "indirect threaded" Forth — but simpler than that.
 *  We use a token-threaded byte-coded inner interpreter so dictionary
 *  entries are compact and no native code generation is needed.
 *
 *  Two stacks, both 16-bit cells:
 *    DSTACK   data stack (parameter stack)
 *    RSTACK   return stack (call frames, loop indices)
 *
 *  Dictionary:                                 (newest at the head)
 *    +0: link to previous head  (16 bits)
 *    +2: flags byte             (immediate, primitive, hidden, ...)
 *    +3: name length            (1 byte)
 *    +4: name bytes             (up to FW_NAMEMAX)
 *    +N: body...
 *      primitive  : one byte = primitive op code
 *      colon-def  : zero or more cells (each cell is a token or literal)
 *
 *  Public commands (cmd.c):
 *    forth                — drop into the uForth REPL.  `bye` to leave.
 *
 *  Primitives: arithmetic, comparison, stack (dup drop swap over rot tuck
 *  nip), memory (@ ! c@ c!), I/O (. .s cr emit space), return-stack
 *  (>r r> r@), words, bye, exit.  Outer-interpreter immediates, handled
 *  in interpret_line so they can touch the tokeniser and dictionary:
 *    : ; (def end)   colon definitions
 *    IF ELSE THEN    forward conditionals  (compile-only)
 *    BEGIN UNTIL     backward loops        (compile-only)
 *    VARIABLE        define a named cell pushing its address
 *    SEE             decompile a defined word
 *  Every advertised word is implemented — `words` lists the full set.
 *
 *  Author: F E R M I ∞ H A R T <contact@fermihart.com>
 *  SPDX-License-Identifier: Unlicense
 *  Listening recommended: open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o
 *
 *═══════════════════════════════════════════════════════════════════════════════*/

#include "uz80.h"

/* ── memory carve-out (RAM, after the filesystem) ───────────────────────── */
/* fs.c ends at 0x74D0. Forth starts at 0x8000 and stops below the C stack. */
#define DSTK_BASE UZ_FORTH_DSTACK_BASE /* data stack grows up — 64 cells     */
#define DSTK_END  UZ_FORTH_DSTACK_END
#define RSTK_BASE UZ_FORTH_RSTACK_BASE /* return stack grows up — 64 cells   */
#define RSTK_END  UZ_FORTH_RSTACK_END
#define DICT_BASE UZ_FORTH_DICT_BASE   /* user dictionary — ~24 KiB room     */
#define DICT_END  UZ_FORTH_DICT_END

typedef char forth_layout_is_ordered[
    (DSTK_BASE < DSTK_END && DSTK_END == RSTK_BASE
     && RSTK_BASE < RSTK_END && RSTK_END == DICT_BASE
     && DICT_BASE < DICT_END) ? 1 : -1
];

#define FW_NAMEMAX 12

/* ── pointers into the above ────────────────────────────────────────────── */
typedef int16_t cell_t;

static cell_t   *DSP;             /* points to top-of-stack cell             */
static cell_t   *RSP;
static uint8_t  *HERE;            /* next-free byte in the user dictionary   */
static uint8_t  *LATEST;          /* head of dictionary linked list          */
static uint8_t   STATE;           /* 0 = interpret, 1 = compile             */

/* ── primitive opcodes (the inner-interpreter dispatch keys) ────────────── */
enum {
    P_LIT = 1,     /* push the next cell as literal       */
    P_PLUS, P_MINUS, P_MUL, P_DIV, P_MOD, P_NEG,
    P_AND, P_OR, P_XOR, P_INVERT,
    P_EQ, P_LT, P_GT,
    P_DUP, P_DROP, P_SWAP, P_OVER, P_ROT, P_TUCK, P_NIP,
    P_FETCH, P_STORE, P_CFETCH, P_CSTORE,
    P_DOT, P_DOTS, P_CR, P_EMIT, P_SPACE,
    P_BRANCH, P_ZBRANCH,
    P_TORS, P_FROMRS, P_RFETCH,
    P_WORDS, P_BYE,
    P_EXIT,
    /* one-byte sentinel for "this colon-def body ends here" — same as EXIT */
    P_LAST
};

/* ── stack ops with bounds-checking ─────────────────────────────────────── */
static void push(cell_t v) { if (DSP < (cell_t *)DSTK_END) *DSP++ = v; }
static cell_t pop(void)
{
    if (DSP > (cell_t *)DSTK_BASE) return *--DSP;
    tty_puts("stack underflow\n");
    return 0;
}
static void rpush(cell_t v) { if (RSP < (cell_t *)RSTK_END) *RSP++ = v; }
static cell_t rpop(void)
{ return (RSP > (cell_t *)RSTK_BASE) ? *--RSP : 0; }

/* ── string helpers (no libc) ───────────────────────────────────────────── */
static uint8_t fstr_eq(const char *a, const char *b, uint8_t n)
{
    while (n--) {
        char ca = *a++, cb = *b++;
        /* fold to lower so the user can type either case */
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return 1;
}

/* ── built-in primitives table (name + opcode) ──────────────────────────── */
typedef struct { const char *name; uint8_t op; uint8_t imm; } prim_t;
static const prim_t PRIMS[] = {
    /* arithmetic — both Forth-symbolic and alpha names, because the
     * Spectrum keyboard makes typing `+` / `*` etc. fiddly. */
    { "+",      P_PLUS,    0 },
    { "add",    P_PLUS,    0 },
    { "-",      P_MINUS,   0 },
    { "sub",    P_MINUS,   0 },
    { "*",      P_MUL,     0 },
    { "mul",    P_MUL,     0 },
    { "/",      P_DIV,     0 },
    { "div",    P_DIV,     0 },
    { "mod",    P_MOD,     0 },
    { "negate", P_NEG,     0 },
    { "and",    P_AND,     0 },
    { "or",     P_OR,      0 },
    { "xor",    P_XOR,     0 },
    { "invert", P_INVERT,  0 },
    { "=",      P_EQ,      0 },
    { "<",      P_LT,      0 },
    { ">",      P_GT,      0 },
    /* stack */
    { "dup",    P_DUP,     0 },
    { "drop",   P_DROP,    0 },
    { "swap",   P_SWAP,    0 },
    { "over",   P_OVER,    0 },
    { "rot",    P_ROT,     0 },
    { "tuck",   P_TUCK,    0 },
    { "nip",    P_NIP,     0 },
    /* memory */
    { "@",      P_FETCH,   0 },
    { "!",      P_STORE,   0 },
    { "c@",     P_CFETCH,  0 },
    { "c!",     P_CSTORE,  0 },
    /* I/O */
    { ".",      P_DOT,     0 },
    { "print",  P_DOT,     0 },
    { ".s",     P_DOTS,    0 },
    { "stack",  P_DOTS,    0 },
    { "cr",     P_CR,      0 },
    { "emit",   P_EMIT,    0 },
    { "space",  P_SPACE,   0 },
    /* return-stack */
    { ">r",     P_TORS,    0 },
    { "r>",     P_FROMRS,  0 },
    { "r@",     P_RFETCH,  0 },
    /* misc */
    { "words",  P_WORDS,   0 },
    { "bye",    P_BYE,     0 },
    { "exit",   P_EXIT,    0 },
    { 0, 0, 0 }
};

/* Compile-only / interpreter-side immediate words.  These are handled
 * directly by the outer interpreter (interpret_line) rather than by the
 * primitive dispatch, because they consume input or rewrite the
 * dictionary.  Listed here so `words` can advertise them honestly. */
static const char * const IMMS[] = {
    ":", ";", "def", "end", "if", "else", "then", "begin", "until",
    "variable", "see", 0
};

/* Compile-time control-flow stack: holds the dictionary addresses of the
 * branch offset fields awaiting back-patching by ELSE / THEN / UNTIL.
 * Offsets are stored relative to the offset field itself, matching the
 * `ip += off` convention in execute_body (ip points at the offset when the
 * branch opcode is decoded). */
#define CF_MAX 16
static uint8_t *CFSTK[CF_MAX];
static uint8_t  CFSP;
static void     cf_push(uint8_t *p) { if (CFSP < CF_MAX) CFSTK[CFSP++] = p; }
static uint8_t *cf_pop(void)        { return CFSP ? CFSTK[--CFSP] : 0; }

/* ── dictionary search ──────────────────────────────────────────────────── */
/* Header layout: link[2] flags[1] nlen[1] name[nlen] body... */
#define HDR_LINK(p)  ((uint8_t *)(*(uint16_t *)(p)))
#define HDR_FLAGS(p) ((p)[2])
#define HDR_NLEN(p)  ((p)[3])
#define HDR_NAME(p)  ((char *)((p) + 4))
#define HDR_BODY(p)  ((p) + 4 + HDR_NLEN(p))
#define FLG_IMM      0x01

static uint8_t *find_word(const char *name, uint8_t len)
{
    uint8_t *p = LATEST;
    while (p) {
        if (HDR_NLEN(p) == len && fstr_eq(HDR_NAME(p), name, len))
            return p;
        p = HDR_LINK(p);
    }
    return 0;
}

/* ── input: a tokeniser working off a global line buffer ────────────────── */
static const char *INP;
static char        token[FW_NAMEMAX + 1];
static uint8_t     tlen;
static uint8_t next_token(void)
{
    while (*INP == ' ' || *INP == '\t') INP++;
    tlen = 0;
    while (*INP && *INP != ' ' && *INP != '\t' && tlen < FW_NAMEMAX) {
        token[tlen++] = *INP++;
    }
    token[tlen] = 0;
    return tlen;
}

/* ── integer parser ─────────────────────────────────────────────────────── */
static uint8_t try_number(const char *s, uint8_t n, cell_t *out)
{
    cell_t  v = 0;
    uint8_t i = 0, neg = 0;
    if (n == 0) return 0;
    if (s[0] == '-') { neg = 1; i = 1; if (n == 1) return 0; }
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = (cell_t)(v * 10 + (s[i] - '0'));
    }
    *out = (cell_t)(neg ? -v : v);
    return 1;
}

/* ── dictionary writers (compile-mode) ──────────────────────────────────── */
static void comma_b(uint8_t v) { if (HERE < (uint8_t *)DICT_END) *HERE++ = v; }
static void comma_c(cell_t v)
{ comma_b((uint8_t)(v & 0xFF)); comma_b((uint8_t)((v >> 8) & 0xFF)); }

static uint8_t *create_header(const char *name, uint8_t nlen)
{
    uint8_t *h = HERE;
    comma_c((cell_t)(uintptr_t)LATEST);
    comma_b(0);                     /* flags */
    comma_b(nlen);
    {
        uint8_t i;
        for (i = 0; i < nlen; i++) comma_b((uint8_t)name[i]);
    }
    LATEST = h;
    return h;
}

/* ── output helpers ─────────────────────────────────────────────────────── */
static void put_num(cell_t v)
{
    if (v < 0) { tty_putc('-'); v = (cell_t)(-v); }
    tty_putu((uint16_t)v);
}

/* ── inner interpreter: execute a body starting at `ip` ─────────────────── */
static uint8_t QUIT;          /* set by `bye`                             */

/* Forward — primitives we call recursively from .s and SEE                */
static void execute_body(uint8_t *ip);

static void run_prim(uint8_t op)
{
    cell_t a, b;
    switch (op) {
    case P_PLUS:   b = pop(); a = pop(); push((cell_t)(a + b)); break;
    case P_MINUS:  b = pop(); a = pop(); push((cell_t)(a - b)); break;
    case P_MUL:    b = pop(); a = pop(); push((cell_t)(a * b)); break;
    case P_DIV:    b = pop(); a = pop(); push((cell_t)(b ? a / b : 0)); break;
    case P_MOD:    b = pop(); a = pop(); push((cell_t)(b ? a % b : 0)); break;
    case P_NEG:    a = pop(); push((cell_t)(-a)); break;
    case P_AND:    b = pop(); a = pop(); push((cell_t)(a & b)); break;
    case P_OR:     b = pop(); a = pop(); push((cell_t)(a | b)); break;
    case P_XOR:    b = pop(); a = pop(); push((cell_t)(a ^ b)); break;
    case P_INVERT: a = pop(); push((cell_t)(~a)); break;
    case P_EQ:     b = pop(); a = pop(); push((cell_t)(a == b ? -1 : 0)); break;
    case P_LT:     b = pop(); a = pop(); push((cell_t)(a <  b ? -1 : 0)); break;
    case P_GT:     b = pop(); a = pop(); push((cell_t)(a >  b ? -1 : 0)); break;
    case P_DUP:    a = pop(); push(a); push(a); break;
    case P_DROP:   (void)pop(); break;
    case P_SWAP:   b = pop(); a = pop(); push(b); push(a); break;
    case P_OVER:   b = pop(); a = pop(); push(a); push(b); push(a); break;
    case P_ROT:  { cell_t c = pop(); b = pop(); a = pop();
                   push(b); push(c); push(a); break; }
    case P_TUCK:   b = pop(); a = pop(); push(b); push(a); push(b); break;
    case P_NIP:    b = pop(); (void)pop(); push(b); break;
    case P_FETCH:  a = pop(); push(*(cell_t *)(uintptr_t)a); break;
    case P_STORE:  a = pop(); b = pop();
                   *(cell_t *)(uintptr_t)a = b; break;
    case P_CFETCH: a = pop(); push(*(uint8_t *)(uintptr_t)a); break;
    case P_CSTORE: a = pop(); b = pop();
                   *(uint8_t *)(uintptr_t)a = (uint8_t)b; break;
    case P_DOT:    a = pop(); put_num(a); tty_putc(' '); break;
    case P_DOTS:   { cell_t *p; tty_putc('[');
                     for (p = (cell_t *)DSTK_BASE; p < DSP; p++) {
                         tty_putc(' '); put_num(*p);
                     } tty_puts(" ]\n"); break; }
    case P_CR:     tty_putc('\n'); break;
    case P_EMIT:   a = pop(); tty_putc((char)a); break;
    case P_SPACE:  tty_putc(' '); break;
    case P_TORS:   rpush(pop()); break;
    case P_FROMRS: push(rpop()); break;
    case P_RFETCH: push(*(RSP - 1)); break;
    case P_BYE:    QUIT = 1; break;
    case P_WORDS:  { uint8_t *p = LATEST; uint8_t col = 0;
                     while (p) {
                         uint8_t n = HDR_NLEN(p);
                         if (col + n + 1 > COLS) { tty_putc('\n'); col = 0; }
                         { uint8_t i; for (i = 0; i < n; i++)
                               tty_putc(HDR_NAME(p)[i]); }
                         tty_putc(' '); col = (uint8_t)(col + n + 1);
                         p = HDR_LINK(p);
                     }
                     if (col) tty_putc('\n');
                     /* and the primitives */
                     col = 0;
                     {
                         uint8_t k;
                         for (k = 0; PRIMS[k].name; k++) {
                             uint8_t n = 0; while (PRIMS[k].name[n]) n++;
                             if (col + n + 1 > COLS) { tty_putc('\n'); col = 0; }
                             tty_puts(PRIMS[k].name); tty_putc(' ');
                             col = (uint8_t)(col + n + 1);
                         }
                         if (col) tty_putc('\n');
                     }
                     /* and the immediate / control-flow words */
                     col = 0;
                     {
                         uint8_t k;
                         for (k = 0; IMMS[k]; k++) {
                             uint8_t n = 0; while (IMMS[k][n]) n++;
                             if (col + n + 1 > COLS) { tty_putc('\n'); col = 0; }
                             tty_puts(IMMS[k]); tty_putc(' ');
                             col = (uint8_t)(col + n + 1);
                         }
                         if (col) tty_putc('\n');
                     }
                     break; }
    default: break;
    }
}

/* Inner interpreter.  We model the classic Forth return-stack pattern:
 * CALL pushes the post-instruction IP onto RSP and jumps to the target;
 * EXIT pops one frame.  When EXIT is hit at the outermost level (RSP back
 * at the base we entered with) the inner loop is done. */
static void execute_body(uint8_t *ip)
{
    cell_t *rsp_floor = RSP;        /* depth on entry */
    for (;;) {
        uint8_t op = *ip++;
        if (op == P_EXIT) {
            if (RSP > rsp_floor) { ip = (uint8_t *)(uintptr_t)rpop(); continue; }
            return;
        }
        if (op == P_LIT)  { cell_t v = *(cell_t *)ip; ip += 2; push(v); continue; }
        if (op == P_BRANCH)  { int16_t off = *(int16_t *)ip; ip += off; continue; }
        if (op == P_ZBRANCH) {
            int16_t off = *(int16_t *)ip;
            if (pop() == 0) ip += off;
            else            ip += 2;
            continue;
        }
        if (op == 0xFF) {                  /* CALL into another user-word */
            uint8_t *target = *(uint8_t **)(ip + 0);
            ip += 2;
            rpush((cell_t)(uintptr_t)ip);
            ip = target;
            continue;
        }
        run_prim(op);
        if (QUIT) return;
    }
}

/* ── outer interpreter: one line ────────────────────────────────────────── */
static void interpret_line(const char *line);

static void interpret_token(void)
{
    uint8_t *w;
    cell_t   n;
    uint8_t  i;

    /* user-defined word? */
    w = find_word(token, tlen);
    if (w) {
        if (STATE && !(HDR_FLAGS(w) & FLG_IMM)) {
            /* compile a call */
            comma_b(0xFF);
            comma_c((cell_t)(uintptr_t)HDR_BODY(w));
        } else {
            execute_body(HDR_BODY(w));
        }
        return;
    }

    /* primitive? */
    for (i = 0; PRIMS[i].name; i++) {
        uint8_t k = 0; while (PRIMS[i].name[k]) k++;
        if (k == tlen && fstr_eq(PRIMS[i].name, token, tlen)) {
            if (STATE && !PRIMS[i].imm) comma_b(PRIMS[i].op);
            else                        run_prim(PRIMS[i].op);
            return;
        }
    }

    /* literal number? */
    if (try_number(token, tlen, &n)) {
        if (STATE) { comma_b(P_LIT); comma_c(n); }
        else       push(n);
        return;
    }

    tty_puts(token); tty_puts(" ?\n");
}

/* ── token / keyword match (case-insensitive, length-exact) ─────────────── */
static uint8_t tok_is(const char *kw)
{
    uint8_t n = 0; while (kw[n]) n++;
    return (n == tlen) && fstr_eq(kw, token, tlen);
}

/* reverse opcode → name (first match wins, e.g. "+" before "add") */
static const char *prim_name(uint8_t op)
{
    uint8_t i;
    for (i = 0; PRIMS[i].name; i++)
        if (PRIMS[i].op == op) return PRIMS[i].name;
    return 0;
}

/* ── SEE: decompile a colon body back to readable tokens ────────────────── */
static void see_word(uint8_t *body)
{
    uint8_t *ip = body;
    for (;;) {
        uint8_t op = *ip++;
        if (op == P_EXIT)  { tty_puts(";\n"); return; }
        if (op == P_LIT)   { cell_t v = *(cell_t *)ip; ip += 2;
                             put_num(v); tty_putc(' '); continue; }
        if (op == P_BRANCH){ int16_t o = *(int16_t *)ip; ip += 2;
                             tty_puts("branch("); put_num(o); tty_puts(") ");
                             continue; }
        if (op == P_ZBRANCH){int16_t o = *(int16_t *)ip; ip += 2;
                             tty_puts("?branch("); put_num(o); tty_puts(") ");
                             continue; }
        if (op == 0xFF)    { uint8_t *t = *(uint8_t **)ip; ip += 2;
                             uint8_t *p = LATEST;
                             const char *nm = 0; uint8_t nl = 0;
                             while (p) { if (HDR_BODY(p) == t) {
                                 nm = HDR_NAME(p); nl = HDR_NLEN(p); break; }
                                 p = HDR_LINK(p); }
                             if (nm) { uint8_t i; for (i = 0; i < nl; i++)
                                           tty_putc(nm[i]); }
                             else tty_puts("?call");
                             tty_putc(' '); continue; }
        { const char *nm = prim_name(op);
          if (nm) tty_puts(nm); else tty_putc('?');
          tty_putc(' '); }
    }
}

/* The compile-mode immediates (':' / ';' / IF / ELSE / THEN / BEGIN / UNTIL)
 * and the defining/inspecting words (VARIABLE / SEE) are handled inline here
 * rather than through the primitive table so they have access to the
 * tokeniser, the dictionary, and the compile-time control-flow stack. */
static void interpret_line(const char *line)
{
    INP = line;
    while (next_token()) {
        /* ':' (or alphabetic alias `def`) starts a colon definition */
        if (tok_is(":") || tok_is("def")) {
            if (!next_token()) { tty_puts(": expects a name\n"); return; }
            create_header(token, tlen);
            STATE = 1;
            continue;
        }
        if (tok_is(";") || tok_is("end")) {
            comma_b(P_EXIT);
            STATE = 0;
            continue;
        }

        /* ── forward conditional: IF ... [ELSE ...] THEN ── compile-only ── */
        if (tok_is("if")) {
            if (!STATE) { tty_puts("if: compile-only\n"); return; }
            comma_b(P_ZBRANCH);
            cf_push(HERE);          /* offset field awaiting THEN/ELSE     */
            comma_c(0);
            continue;
        }
        if (tok_is("else")) {
            uint8_t *zslot;
            if (!STATE) { tty_puts("else: compile-only\n"); return; }
            comma_b(P_BRANCH);      /* jump over the false-branch          */
            { uint8_t *bslot = HERE; comma_c(0);
              zslot = cf_pop();     /* the IF's ?branch                    */
              if (zslot) *(int16_t *)zslot = (int16_t)(HERE - zslot);
              cf_push(bslot); }     /* THEN will patch this BRANCH         */
            continue;
        }
        if (tok_is("then")) {
            uint8_t *slot;
            if (!STATE) { tty_puts("then: compile-only\n"); return; }
            slot = cf_pop();
            if (slot) *(int16_t *)slot = (int16_t)(HERE - slot);
            continue;
        }

        /* ── backward loop: BEGIN ... UNTIL ── compile-only ───────────── */
        if (tok_is("begin")) {
            if (!STATE) { tty_puts("begin: compile-only\n"); return; }
            cf_push(HERE);          /* loop-top target                     */
            continue;
        }
        if (tok_is("until")) {
            uint8_t *top;
            if (!STATE) { tty_puts("until: compile-only\n"); return; }
            comma_b(P_ZBRANCH);     /* false → jump back to BEGIN          */
            { uint8_t *slot = HERE; comma_c(0);
              top = cf_pop();
              if (top) *(int16_t *)slot = (int16_t)(top - slot); }
            continue;
        }

        /* ── VARIABLE name ── define a word that pushes its cell address ─ */
        if (tok_is("variable")) {
            uint8_t *slot;
            if (!next_token()) { tty_puts("variable: needs a name\n"); return; }
            create_header(token, tlen);
            comma_b(P_LIT);
            slot = HERE; comma_c(0);            /* address, patched below  */
            comma_b(P_EXIT);
            *(int16_t *)slot = (int16_t)(uintptr_t)HERE;  /* data cell      */
            comma_c(0);                          /* the cell, initial 0     */
            continue;
        }

        /* ── SEE name ── decompile a defined word ───────────────────────── */
        if (tok_is("see")) {
            uint8_t *w;
            if (!next_token()) { tty_puts("see: needs a name\n"); return; }
            w = find_word(token, tlen);
            if (!w) { tty_puts("see: not a defined word\n"); return; }
            tty_puts(": ");
            { uint8_t i; for (i = 0; i < HDR_NLEN(w); i++)
                  tty_putc(HDR_NAME(w)[i]); }
            tty_putc(' ');
            see_word(HDR_BODY(w));
            continue;
        }

        interpret_token();
    }
}


/* ── one-shot init + REPL ───────────────────────────────────────────────── */
static uint8_t did_init;
static void forth_init(void)
{
    DSP    = (cell_t *)DSTK_BASE;
    RSP    = (cell_t *)RSTK_BASE;
    HERE   = (uint8_t *)DICT_BASE;
    LATEST = 0;
    STATE  = 0;
    CFSP   = 0;
    did_init = 1;
}

void forth_repl(void)
{
    static char line[64];
    if (!did_init) forth_init();
    QUIT = 0;
    tty_puts("uforth - jupiter ace would be proud.\n");
    tty_puts("type 'words' or 'bye'.\n");
    while (!QUIT) {
        tty_puts(STATE ? "...> " : "ok> ");
        tty_readline(line, sizeof line);
        tty_putc('\n');
        interpret_line(line);
    }
    tty_puts("bye.\n");
}

/* Public one-shot evaluator — runs a Forth script from C (used by the
 * `forthdemo` builtin to prove uForth without going through the keyboard,
 * since the Spectrum matrix doesn't ship every symbol the language uses). */
void forth_eval(const char *script)
{
    if (!did_init) forth_init();
    QUIT = 0;
    interpret_line(script);
}
