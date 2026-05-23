/*═══════════════════════════════════════════════════════════════════════════════
 *
 *  UZ80 · uz80.h — kernel-wide types, layout, contracts
 *
 *  Memory map of a stock 48K Spectrum:
 *
 *    0x0000..0x3FFF  ROM         our code lives here, 16 KiB
 *    0x4000..0x57FF  bitmap      256x192, 1bpp, thirds-interleaved
 *    0x5800..0x5AFF  attributes  one byte per 8x8 cell
 *    0x5B00..0x5BFF  ULA scratch (printer buffer area, we reuse it)
 *    0x5C00..0x5FFF  system vars (we ignore them — no BASIC)
 *    0x6000..       free RAM up to 0xFFFF (40 KiB for us)
 *
 *  We carve our scratch out of the free RAM:
 *
 *    0x6000..0x6007   keyboard half-rows         (kbd_scan, crt0.s)
 *    0x6008..0x6009   16-bit frame counter       (IM 1 ISR, crt0.s)
 *    0x6010..0x60FF   reserved
 *    0x6100..0x7FFF   filesystem block           (fs.c, ~8 KiB)
 *    0x8000..0xFEFF   C statics + heap (unused)
 *    0xFF00..0xFFFF   stack
 *
 *  Author: F E R M I ∞ H A R T <contact@fermihart.com>
 *  SPDX-License-Identifier: Unlicense
 *  Listening recommended: open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o
 *
 *═══════════════════════════════════════════════════════════════════════════════*/
#ifndef UZ80_H
#define UZ80_H

#include <stdint.h>

/* ── shared addresses ─────────────────────────────────────────────────────── */
#define UZ_KBD_ROW(r) (*(volatile uint8_t  *)(0x6000 + (r)))
#define UZ_FRAMES16   (*(volatile uint16_t *)0x6008)

/* ── geometry ─────────────────────────────────────────────────────────────── */
#define COLS  32
#define ROWS  24

/* ── crt0.s exports ───────────────────────────────────────────────────────── */
void kbd_scan(void);
/* Square-wave on the ULA speaker for `cycles` periods; `half` controls pitch
 * (smaller = higher).  Both blocking and approximately calibrated by ear. */
void beep_tone(uint16_t cycles, uint16_t half);

/* ── font.c ───────────────────────────────────────────────────────────────── */
/* 8 bytes per cell, ASCII 32..127.  Top 5 bits are pixels; bottom 3 are 0. */
extern const uint8_t FONT[96][8];

/* ── tty.c ────────────────────────────────────────────────────────────────── */
void tty_init(void);
void tty_clear(void);
void tty_putc(char c);                 /* handles \n, scrolls automatically */
void tty_puts(const char *s);
void tty_putu(uint16_t n);             /* unsigned decimal                  */
void tty_putx(uint8_t n);              /* two hex digits                    */
void tty_at(uint8_t cx, uint8_t cy);   /* set cursor for next puts          */
void tty_clear_status(void);           /* clears row 0 (status bar)         */
void tty_save(void);                   /* push cursor                       */
void tty_restore(void);                /* pop cursor                        */

/* Read a full line into `buf` (up to `cap-1` chars + NUL).  Echoes,
 * handles BACKSPACE / history (up/down), blinks a cursor, returns when
 * the user presses ENTER.  Drives the keyboard via kbd_scan(). */
void tty_readline(char *buf, uint8_t cap);

/* ── fs.c ─────────────────────────────────────────────────────────────────── */
#define FS_NAMEMAX 12
#define FS_FILEMAX 16
#define FS_DATAMAX 256

typedef struct {
    char    name[FS_NAMEMAX];          /* "" = free slot                    */
    uint8_t size;                      /* 0..FS_DATAMAX                     */
    uint8_t data[FS_DATAMAX];
} fs_file_t;

void          fs_init(void);
fs_file_t    *fs_find(const char *name);     /* NULL if missing             */
fs_file_t    *fs_create(const char *name);   /* NULL if full / bad name     */
int8_t        fs_delete(const char *name);   /* 0 ok, -1 missing            */
fs_file_t    *fs_iter(uint8_t i);            /* returns slot i or NULL      */
void          fs_write(fs_file_t *f, const char *text);    /* truncate+copy */

/* ── cmd.c ────────────────────────────────────────────────────────────────── */
void cmd_dispatch(char *line);                /* mutates line (tokenising)  */

/* ── forth.c ──────────────────────────────────────────────────────────────── */
void forth_repl(void);                        /* the uForth interpreter     */
void forth_eval(const char *script);          /* one-shot scripted eval     */

/* ── history (tty.c) ──────────────────────────────────────────────────────── */
#define HIST_DEPTH 8

#endif /* UZ80_H */
