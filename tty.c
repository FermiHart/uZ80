/*═══════════════════════════════════════════════════════════════════════════════
 *
 *  UZ80 · tty.c — a scrolling terminal on the ZX Spectrum bitmap
 *
 *  We treat the bitmap as a 32x24 grid of 8x8 cells.  Row 0 is the status
 *  bar (uptime, never scrolls); rows 1..ROWS-1 are the terminal viewport.
 *  When we run off the bottom we scroll the whole bitmap up one cell row.
 *
 *  Line editor:
 *    - prints printable ASCII at the cursor, advances
 *    - BACKSPACE deletes the previous character (CAPS SHIFT + 0)
 *    - UP/DOWN cycle the command history          (CAPS SHIFT + 7 / 6)
 *    - ENTER returns, NUL-terminating the buffer
 *    - cursor blinks at 50 Hz / 16 — a 1.5 Hz wink
 *
 *  Spectrum framebuffer reminder:
 *    addr = 0x4000
 *         | ((y & 0xC0) << 5)   // which screen-third
 *         | ((y & 0x07) << 8)   // pixel row inside the cell
 *         | ((y & 0x38) << 2)   // cell row inside the third
 *         | x                   // byte column (0..31)
 *
 *  Author: F E R M I ∞ H A R T <contact@fermihart.com>
 *  SPDX-License-Identifier: Unlicense
 *  Listening recommended: open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o
 *
 *═══════════════════════════════════════════════════════════════════════════════*/

#include "uz80.h"

/* ── cursor / viewport ────────────────────────────────────────────────────── */
#define VIEW_TOP   1        /* row 0 = status bar                            */
#define VIEW_BOT   23

static uint8_t cx, cy;       /* current cursor cell (column, row)            */

/* ── history (circular, newest at head) ──────────────────────────────────── */
#define HIST_LINE 32
static char    hist[HIST_DEPTH][HIST_LINE];
static uint8_t hist_n;       /* count, saturates at HIST_DEPTH                */
static uint8_t hist_head;    /* next slot to write                            */

/* ── primitive: byte address of (col, pixel-row) in the bitmap ───────────── */
static uint8_t *bitp(uint8_t col, uint8_t py)
{
    uint16_t a = 0x4000u
               | ((uint16_t)(py & 0xC0) << 5)
               | ((uint16_t)(py & 0x07) << 8)
               | ((uint16_t)(py & 0x38) << 2)
               |  col;
    return (uint8_t *)a;
}

/* ── primitive: draw one 8x8 glyph at cell (col, row) ────────────────────── */
static void blit(uint8_t col, uint8_t row, uint8_t gi)
{
    uint8_t  r;
    uint8_t  py = (uint8_t)(row * 8);
    for (r = 0; r < 8; r++)
        *bitp(col, (uint8_t)(py + r)) = FONT[gi][r];
}

static uint8_t gi_of(char c)
{
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7F) return 0;  /* space */
    return (uint8_t)(c - 0x20);
}

/* ── viewport ops ─────────────────────────────────────────────────────────── */
void tty_clear(void)
{
    uint16_t a;
    for (a = 0x4000u; a < 0x5800u; a++) *(volatile uint8_t *)a = 0;
    /* attributes: bright green ink on black paper, no flash */
    for (a = 0x5800u; a < 0x5B00u; a++) *(volatile uint8_t *)a = 0x44;
    cx = 0; cy = VIEW_TOP;
}

/* Scroll rows VIEW_TOP..VIEW_BOT up by one cell row.  We move whole 8-line
 * "thirds" by `memcpy`-style loops; the Z80's LDIR would be a few %
 * faster but we don't need it. */
static void scroll_one(void)
{
    uint8_t  py, c;
    /* shift rows VIEW_TOP+1..VIEW_BOT into VIEW_TOP..VIEW_BOT-1 */
    for (py = VIEW_TOP * 8; py < VIEW_BOT * 8; py++)
        for (c = 0; c < COLS; c++)
            *bitp(c, py) = *bitp(c, (uint8_t)(py + 8));
    /* clear the freshly-revealed bottom row */
    for (py = VIEW_BOT * 8; py < (uint8_t)(VIEW_BOT * 8 + 8); py++)
        for (c = 0; c < COLS; c++)
            *bitp(c, py) = 0;
}

static void newline(void)
{
    cx = 0;
    if (cy < VIEW_BOT) { cy++; return; }
    scroll_one();
}

void tty_putc(char c)
{
    if (c == '\n') { newline(); return; }
    if (cx >= COLS) newline();
    blit(cx, cy, gi_of(c));
    cx++;
}

void tty_puts(const char *s)        { while (*s) tty_putc(*s++); }

void tty_putu(uint16_t n)
{
    char    buf[6];
    uint8_t i = 6;
    buf[--i] = 0;
    do { buf[--i] = (char)('0' + n % 10); n /= 10; } while (n && i);
    tty_puts(&buf[i]);
}

static const char HEX[] = "0123456789ABCDEF";
void tty_putx(uint8_t n)
{
    tty_putc(HEX[(n >> 4) & 0xF]);
    tty_putc(HEX[n & 0xF]);
}

void tty_at(uint8_t cxv, uint8_t cyv) { cx = cxv; cy = cyv; }

static uint8_t saved_cx, saved_cy;
void tty_save(void)    { saved_cx = cx; saved_cy = cy; }
void tty_restore(void) { cx = saved_cx; cy = saved_cy; }

void tty_clear_status(void)
{
    uint8_t col, py;
    for (py = 0; py < 8; py++)
        for (col = 0; col < COLS; col++)
            *bitp(col, py) = 0;
}

void tty_init(void)
{
    tty_clear();
    hist_n = hist_head = 0;
}

/* ── keyboard ─────────────────────────────────────────────────────────────── */
/* Spectrum matrix: 8 half-rows of 5 keys.  0 = a modifier; we read CAPS
 * SHIFT and SYMBOL SHIFT explicitly. */
static const char KEYMAP[40] = {
    0 ,'z','x','c','v',          /* 0xFEFE   CAPS,Z,X,C,V       */
   'a','s','d','f','g',          /* 0xFDFE                      */
   'q','w','e','r','t',          /* 0xFBFE                      */
   '1','2','3','4','5',          /* 0xF7FE                      */
   '0','9','8','7','6',          /* 0xEFFE                      */
   'p','o','i','u','y',          /* 0xDFFE                      */
    13,'l','k','j','h',          /* 0xBFFE   ENTER at [0]       */
   ' ', 0 ,'m','n','b',          /* 0x7FFE   SPACE, SYM-SHIFT   */
};

/* Special CAPS-SHIFT combos (CAPS = row 0 bit 0).
 *   CAPS+0 = BACKSPACE   (row 4 bit 0 → '0')
 *   CAPS+5 = LEFT        (row 3 bit 4 → '5')      [not yet wired]
 *   CAPS+6 = DOWN        (row 4 bit 4 → '6')
 *   CAPS+7 = UP          (row 4 bit 3 → '7')
 *   CAPS+8 = RIGHT       (row 4 bit 2 → '8')      [not yet wired]
 *   CAPS+1 = EDIT/clear  (row 3 bit 0 → '1')      [we map to ^U: clear line]
 *   SYM+P  = '"'         (row 7 bit 1 → 'm')      [not yet wired]
 */
#define K_BS    8
#define K_UP    11
#define K_DOWN  10
#define K_KILL  21    /* ^U */

/* Decode one freshly-scanned matrix into a single keycode (or 0).  We
 * track edge-detection in a global so holding a key yields one event. */
static uint8_t kb_down;

static char decode(void)
{
    uint8_t r, b, caps;
    char    base = 0;

    caps = !(UZ_KBD_ROW(0) & 0x01);            /* CAPS SHIFT held? */

    for (r = 0; r < 8; r++) {
        uint8_t v = UZ_KBD_ROW(r);
        for (b = 0; b < 5; b++) {
            if (((v >> b) & 1) == 0) {         /* 0 = pressed */
                char k = KEYMAP[r * 5 + b];
                if (k) base = k;
            }
        }
    }
    if (!base) return 0;

    if (caps) {
        if (base == '0') return K_BS;
        if (base == '6') return K_DOWN;
        if (base == '7') return K_UP;
        if (base == '1') return K_KILL;
        /* other caps-combos: ignore the shift, return the base */
    }
    return base;
}

static char poll_key(void)
{
    char c;
    kbd_scan();
    c = decode();
    if (c) {
        if (kb_down) return 0;
        kb_down = 1;
        return c;
    }
    kb_down = 0;
    return 0;
}

/* ── line editor ──────────────────────────────────────────────────────────── */

/* Draw or erase the cursor block at the current (cx, cy) */
static void cursor(uint8_t on)
{
    blit(cx, cy, on ? 0x5F : 0);     /* glyph 0x5F = solid block (0x7F-0x20) */
}

static void redraw_line(const char *buf, uint8_t len,
                        uint8_t start_x, uint8_t start_y)
{
    uint8_t i;
    cx = start_x; cy = start_y;
    for (i = 0; i < len && cx < COLS; i++) {
        blit(cx, cy, gi_of(buf[i]));
        cx++;
    }
    /* erase any trailing chars from a previous longer history entry */
    while (cx < COLS) { blit(cx, cy, 0); cx++; }
    cx = start_x + len;
    cy = start_y;
}

static void hist_push(const char *line)
{
    uint8_t i;
    if (!line[0]) return;                    /* don't store empty lines */
    for (i = 0; i < HIST_LINE - 1 && line[i]; i++)
        hist[hist_head][i] = line[i];
    hist[hist_head][i] = 0;
    hist_head = (uint8_t)((hist_head + 1) % HIST_DEPTH);
    if (hist_n < HIST_DEPTH) hist_n++;
}

/* Fetch history entry `back` (1 = most recent), or empty string. */
static const char *hist_get(uint8_t back)
{
    if (back == 0 || back > hist_n) return "";
    /* head is the next-write slot; newest is head-1, then head-2... */
    return hist[(uint8_t)((hist_head + HIST_DEPTH - back) % HIST_DEPTH)];
}

void tty_readline(char *buf, uint8_t cap)
{
    uint8_t  start_x = cx, start_y = cy;
    uint8_t  len = 0;
    uint8_t  hi  = 0;                        /* 0 = current edit; 1..n = history */
    uint8_t  i;
    uint16_t last_blink = 0xFFFF;

    buf[0] = 0;

    for (;;) {
        char     k;
        uint16_t bf = UZ_FRAMES16 >> 4;       /* ~3 Hz blink phase */
        if (bf != last_blink) {
            cursor((uint8_t)(bf & 1));
            last_blink = bf;
        }

        k = poll_key();
        if (!k) continue;

        cursor(0);                            /* erase before any edit */

        if (k == 13) {                        /* ENTER */
            buf[len] = 0;
            hist_push(buf);
            return;
        }
        if (k == K_BS) {
            if (len) { len--; buf[len] = 0;
                       cx = start_x + len; cy = start_y;
                       blit(cx, cy, 0); }
            continue;
        }
        if (k == K_KILL) {
            for (i = 0; i < len; i++) {
                cx = start_x + i; cy = start_y; blit(cx, cy, 0);
            }
            len = 0; buf[0] = 0;
            cx = start_x; cy = start_y;
            continue;
        }
        if (k == K_UP || k == K_DOWN) {
            const char *h;
            if (k == K_UP)        { if (hi < hist_n) hi++; }
            else /* K_DOWN */     { if (hi)          hi--; }
            h = hist_get(hi);
            for (i = 0; i < cap - 1 && h[i]; i++) buf[i] = h[i];
            buf[i] = 0; len = i;
            redraw_line(buf, len, start_x, start_y);
            continue;
        }
        if (k >= 0x20 && k < 0x7F && len < cap - 1
                                  && (uint8_t)(start_x + len) < COLS - 1) {
            cx = start_x + len; cy = start_y;
            blit(cx, cy, gi_of(k));
            buf[len++] = k;
            buf[len]   = 0;
        }
    }
}
