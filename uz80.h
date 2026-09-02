/*═══════════════════════════════════════════════════════════════════════════════
 *
 *  UZ80 · uz80.h — kernel-wide types, layout, contracts
 *
 *  Memory map of a stock 48K Spectrum:
 *
 *    [0x0000,0x4000)  ROM         our code lives here, 16 KiB
 *    [0x4000,0x5800)  bitmap      256x192, 1bpp, thirds-interleaved
 *    [0x5800,0x5B00)  attributes  one byte per 8x8 cell
 *    [0x5B00,0x5C00)  ULA scratch (printer buffer area, we reuse it)
 *    [0x5C00,0x6000)  system vars (we ignore them — no BASIC)
 *    [0x6000,0x10000) free RAM    40 KiB for us
 *
 *  We carve our scratch out of the free RAM:
 *
 *    [0x6000,0x6008)  keyboard half-rows         (kbd_scan, crt0.s)
 *    [0x6008,0x600A)  16-bit frame counter       (IM 1 ISR, crt0.s)
 *    [0x600A,0x6010)  free (was beeper scratch — beeper now in C)
 *    [0x6010,0x6400)  SDCC C statics (_DATA), grows up   (--data-loc 0x6010)
 *                     measured 438 B, [0x6010,0x61C6), per uz80.map; crt0
 *                     zeroes this whole window so BSS reads zero at boot.
 *    [0x6400,0x74D0)  filesystem block           (fs.c)
 *    [0x74D0,0x8000)  free
 *    [0x8000,0xE000)  uForth stacks and dictionary
 *    [0xE000,0xFF00)  native C stack reserve; SP starts at 0xFF00, grows down
 *    [0xFF00,0x10000) unused above the initial SP
 *
 *  Memory-layout note (was a collision — now resolved & verified on QEMU):
 *    _DATA starts at 0x6010 and grows up.  The FS used to sit at 0x6100,
 *    leaving only 240 B for statics; the 456 B _DATA overran the /motd file
 *    by 216 B (visible corruption on a real boot).  Fix: FS moved to 0x6400
 *    (570 B headroom) and crt0's clear extended to [0x6000,0x6400), so the
 *    larger _DATA stays zero-initialised. `make check` enforces
 *    s__DATA + l__DATA <= 0x6400 from the generated linker map.
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
#ifdef UZ80_HOST_TEST
extern volatile uint8_t uz_host_kbd_rows[8];
extern volatile uint16_t uz_host_frames;
#define UZ_KBD_ROW(r) uz_host_kbd_rows[(r)]
#define UZ_FRAMES16 uz_host_frames
#else
#define UZ_KBD_ROW(r) (*(volatile uint8_t *)(0x6000 + (r)))
#define UZ_FRAMES16 (*(volatile uint16_t *)0x6008)
#endif

#define UZ_FS_BASE             0x6400u
#define UZ_FORTH_DSTACK_BASE   0x8000u
#define UZ_FORTH_DSTACK_END    0x8080u
#define UZ_FORTH_RSTACK_BASE   0x8080u
#define UZ_FORTH_RSTACK_END    0x8100u
#define UZ_FORTH_DICT_BASE     0x8100u
#define UZ_FORTH_DICT_END      0xE000u

/* ── geometry ─────────────────────────────────────────────────────────────── */
#define COLS  32
#define ROWS  24

/* ── crt0.s exports ───────────────────────────────────────────────────────── */
void kbd_scan(void);
/* Square-wave on the ULA speaker for `cycles` periods; `half` controls pitch
 * (smaller = higher).  Both blocking and approximately calibrated by ear. */
void beep_tone(uint16_t cycles, uint16_t half);

/* ── keyboard.c ──────────────────────────────────────────────────────────── */
char keyboard_decode(void);            /* current matrix -> ASCII/control key */

/* ── editor.c ────────────────────────────────────────────────────────────── */
uint8_t editor_line_limit(uint8_t start_x, uint8_t capacity);
uint8_t editor_start_needs_wrap(uint8_t start_x);

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

/* Read one display row into `buf`, bounded by both cap-1 and the cells left
 * before the reserved cursor column. Echoes, handles BACKSPACE / history,
 * blinks a cursor and returns on ENTER. Drives the keyboard via kbd_scan(). */
void tty_readline(char *buf, uint8_t cap);

/* ── fs.c ─────────────────────────────────────────────────────────────────── */
#define FS_NAMEMAX 12
#define FS_FILEMAX 16
#define FS_DATAMAX 256

typedef struct {
    char    name[FS_NAMEMAX];          /* "" = free slot                    */
    uint8_t size;                      /* 0..FS_DATAMAX-1                   */
    uint8_t data[FS_DATAMAX];
} fs_file_t;

void          fs_init(void);
fs_file_t    *fs_find(const char *name);     /* NULL if missing             */
fs_file_t    *fs_create(const char *name);   /* NULL if full / bad name     */
int8_t        fs_delete(const char *name);   /* 0 ok, -1 missing            */
fs_file_t    *fs_iter(uint8_t i);            /* returns slot i or NULL      */
uint8_t       fs_write(fs_file_t *f, const char *text);    /* 1 ok, 0 invalid */

/* ── cmd.c ────────────────────────────────────────────────────────────────── */
void cmd_dispatch(char *line);                /* mutates line (tokenising)  */

/* ── forth.c ──────────────────────────────────────────────────────────────── */
void forth_repl(void);                        /* the uForth interpreter     */
void forth_eval(const char *script);          /* one-shot scripted eval     */

/* ── history (tty.c) ──────────────────────────────────────────────────────── */
#define HIST_DEPTH 8

/* Read-only view of the command history for the `history` builtin.
 * tty_hist_count() returns how many entries are stored (0..HIST_DEPTH);
 * tty_hist_get(back) returns entry `back` where 1 = most recent, or "". */
uint8_t     tty_hist_count(void);
const char *tty_hist_get(uint8_t back);

#endif /* UZ80_H */
