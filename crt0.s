;; UZ80 · crt0.s — reset vector, IM 1 ISR, keyboard scan
;;
;; The z80 begins at 0x0000 on reset.  We point the stack at the top of
;; RAM, zero our scratch region, install IM 1 so the ULA's 50 Hz frame
;; interrupt vectors through 0x0038, and hand control to main().
;;
;; Shared with C through fixed RAM:
;;   0x6000..0x6007  keyboard half-rows  (kbd_scan fills)
;;   0x6008..0x6009  16-bit frame counter (ISR bumps)
;;
;; Assembled with sdasz80.
;;
;; Author: F E R M I ∞ H A R T <contact@fermihart.com>
;; SPDX-License-Identifier: Unlicense
;; Listening recommended: open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o

        .module crt0
        .globl  _main
        .globl  _kbd_scan

        .area   _HEADER (ABS)

        ;; ---- reset vector ------------------------------------------------
        .org    0x0000
        di
        jp      init

        ;; ---- IM 1 interrupt vector: 16-bit 50 Hz frame counter -----------
        .org    0x0038
        push    af
        push    hl
        ld      hl, (0x6008)
        inc     hl
        ld      (0x6008), hl
        pop     hl
        pop     af
        ei
        reti

        .area   _CODE
init:
        ld      sp, #0xff00             ; stack at top of RAM, below 0xFF00

        ;; Zero [0x6000,0x6400): kbd buffer, frame counter, and the ENTIRE
        ;; SDCC _DATA segment (statics/BSS).  SDCC does not zero BSS on z80,
        ;; so this clear is what guarantees zero-initialised statics actually
        ;; read zero at boot (e.g. forth.c's did_init).  _DATA currently ends
        ;; at 0x61C6 exclusive (uz80.map); clearing to the FS base covers
        ;; it with 570 B of headroom and stops short of the filesystem.
        ld      hl, #0x6000
        ld      de, #0x6001
        ld      bc, #0x03ff
        ld      (hl), #0x00
        ldir

        im      1                       ; ULA frame IRQ -> RST 0x38

        ;; Run the SDCC global initialiser chain.  Each C file with
        ;; runtime-initialised globals appends code into the _GSINIT area;
        ;; _GSFINAL closes the chain with a single RET.  Sandwiching them
        ;; here (and declaring the areas now) guarantees they get glued to
        ;; _CODE — i.e. land inside the ROM image, not in RAM.
        call    gsinit
        call    _main
halt$:  halt
        jr      halt$

        .area   _GSINIT
gsinit::

        .area   _GSFINAL
        ret

        ;; ─────────────────────────────────────────────────────────────────
        ;; kbd_scan: strobe the 8 keyboard half-rows into 0x6000..
        ;; The ULA decodes A8..A15 as a one-cold row select.  Bit = 0 means
        ;; "key down".  Order matches KEYMAP[] in tty.c.
        ;; ─────────────────────────────────────────────────────────────────
_kbd_scan:
        ld      hl, #0x6000
        ld      c,  #0xfe
        ld      b,  #0xfe
        in      a,(c)
        ld      (hl), a
        inc     hl
        ld      b,  #0xfd
        in      a,(c)
        ld      (hl), a
        inc     hl
        ld      b,  #0xfb
        in      a,(c)
        ld      (hl), a
        inc     hl
        ld      b,  #0xf7
        in      a,(c)
        ld      (hl), a
        inc     hl
        ld      b,  #0xef
        in      a,(c)
        ld      (hl), a
        inc     hl
        ld      b,  #0xdf
        in      a,(c)
        ld      (hl), a
        inc     hl
        ld      b,  #0xbf
        in      a,(c)
        ld      (hl), a
        inc     hl
        ld      b,  #0x7f
        in      a,(c)
        ld      (hl), a
        ret

        ;; ─────────────────────────────────────────────────────────────────
        ;; beep_tone() lives in C (see kernel.c): SDCC owns the stack frame,
        ;; the bit-flip + OUT (0xFE) are the only inline asm there.  Nothing
        ;; about the beeper remains in crt0 — no scratch byte, no equ.
        ;; ─────────────────────────────────────────────────────────────────

        .area   _DATA
