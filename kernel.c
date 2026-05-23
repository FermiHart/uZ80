/*═══════════════════════════════════════════════════════════════════════════════
 *
 *  UZ80 · kernel.c — boot splash, status bar, REPL
 *
 *  Everything interesting lives in tty.c, fs.c and cmd.c.  This file is
 *  the wiring: animate a boot sequence, paint the persistent status bar
 *  on row 0, then run the shell forever.
 *
 *  Author: F E R M I ∞ H A R T <contact@fermihart.com>
 *  SPDX-License-Identifier: Unlicense
 *  Listening recommended: open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o
 *
 *═══════════════════════════════════════════════════════════════════════════════*/

#include "uz80.h"

/* ── beep_tone: 1-bit square wave on the ULA speaker (port 0xFE bit 4)
 *
 *  cycles  = number of half-period flips
 *  half    = inner-loop count per half period (lower = higher pitch)
 *
 *  Implemented in C so SDCC owns the frame; the bit-flip and the OUT are
 *  the only assembly.  The inner spin is portable busy-wait. */
static volatile uint8_t  spk_state;
void beep_tone(uint16_t cycles, uint16_t half)
{
    spk_state = 0;
    while (cycles--) {
        uint16_t h = half;
        spk_state ^= 0x10;
        /* fire the speaker bit + a black border */
        __asm__("ld   a,(_spk_state)\n"
                "out  (0xfe),a");
        while (h--) { /* busy wait */ }
    }
    /* leave the speaker line low */
    spk_state = 0;
    __asm__("xor  a\n"
            "out  (0xfe),a");
}

/* ── status bar (row 0): "uz80   bear libcs   up Ns" ────────────────────── */
static void status_redraw(uint16_t secs)
{
    tty_save();
    tty_clear_status();
    tty_at(0,  0);  tty_puts("uz80");
    tty_at(10, 0);  tty_puts("bear libcs");
    tty_at(24, 0);  tty_puts("up ");
    tty_putu(secs);
    tty_putc('s');
    tty_restore();
}

/* ── boot splash: a chip-tune jingle while the banner unfolds ───────────── */
static void splash(void)
{
    static const char *L[] = {
        "uz80 booting...",
        "[ok] z80 cpu",
        "[ok] 16k rom",
        "[ok] 48k ram",
        "[ok] ula @ 50 hz",
        "[ok] keyboard matrix",
        "[ok] filesystem mounted (/)",
        "ready.",
        0,
    };
    uint8_t i;
    tty_clear();
    for (i = 0; L[i]; i++) {
        tty_puts(L[i]);
        tty_putc('\n');
        beep_tone(4, (uint16_t)(260 - (i * 20)));
    }
    /* the famous three-note "ready" stinger */
    beep_tone(16, 240);
    beep_tone(16, 180);
    beep_tone(36, 120);
    tty_putc('\n');
}

/* ── main loop: status bar tick + read a line + dispatch ────────────────── */
void main(void)
{
    static char    line[48];
    uint16_t       last = 0xFFFF;
    fs_file_t     *motd;

    /* Black border via the ULA port. */
    __asm
        xor a
        out (0xfe), a
    __endasm;

    fs_init();
    tty_init();

    __asm ei __endasm;             /* arm the 50 Hz frame IRQ */

    splash();

    /* spit the motd straight out of /motd */
    motd = fs_find("motd");
    if (motd) {
        uint8_t i;
        for (i = 0; i < motd->size; i++) tty_putc((char)motd->data[i]);
        tty_putc('\n');
    }

    for (;;) {
        uint16_t secs = UZ_FRAMES16 / 50;
        if (secs != last) { status_redraw(secs); last = secs; }

        tty_puts("$ ");
        tty_readline(line, sizeof line);
        tty_putc('\n');
        cmd_dispatch(line);
    }
}
