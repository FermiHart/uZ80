# UZ80

**A Z80 micro-kernel I compiled into a 16 KiB ROM — and the rabbit hole I fell
into to get there.**

This is a lab diary. It is written in the order things actually happened,
including the parts where I was wrong. If you want the short version: I have a
ZX Spectrum boot ROM, written in C, that runs an interactive shell on a Z80 —
and the emulator it boots in is itself linked against a libc I wrote by hand,
with not one byte of glibc. None of that was the plan.

```
                  UZ80
           Z80 MICRO-KERNEL
         HOSTED ON BEAR LIBCS

        F E R M I  ∞  H A R T

    aufer@fermihart:~$ _
```

---

## Day 0 — "I just want to compile an old emulator"

I had a 17-year-old tree on disk: `qemu-z80`, Stuart Brady's 2009 branch of
QEMU 0.10.x that teaches it a Zilog Z80 target (it can pretend to be a ZX
Spectrum 48K/128K, a SAM Coupé, an MSX). I typed `make`. Apple clang said no:

```
error: register 'r14' unsuitable for global register variables on this target
```

QEMU 0.10 pins the CPU state pointer to a hardware register —
`register CPUZ80State *env asm("r14")`. GCC has always allowed that. Modern
clang does not. So a 2009 codebase will not build with a 2026 toolchain, and
the blocker is the *compiler*, not me. Annoying. Fine. Detour.

## Day 1 — the detour eats the project

I have this *other* thing: **Bear Libcs**, a C standard library I am writing
from scratch — public-domain, security-first, byte-compatible with the Linux
syscall ABI. Idle thought: instead of fighting clang, what if `qemu-z80` were
linked against **Bear** instead of glibc?

- Bear speaks the *Linux* syscall ABI, so this only makes sense for a Linux
  binary. I build everything from here on inside a **Lima** VM (a real Linux,
  on my Mac). GCC there, not clang — and GCC is fine with the register pinning.
- I measured the gap first. `qemu-system-z80` references **197** libc symbols.
  Bears covers about half by name — but most of the "missing" half already
  exists inside Bear under `bear_*` names, just not exported as POSIX. So the
  real maturity was ~80%.
- I wrote a tiny shim, relinked `qemu-system-z80` static, `-nostdlib`, against
  `libbear.a`. **It linked. Zero undefined symbols. No glibc.**
- Then it *ran*. I logged the Z80: **~22.9 million instructions in 3 seconds**,
  registers advancing, real Z80 disassembly. A full system emulator, hosted on
  my hand-rolled libc.

## Day 2 — evolving the libc, and the bug I'm proudest of finding

A shim that papers over a libc is not interesting. Migrating the gaps *into*
the libc is. So I moved the genuinely-missing surface into Bear's core:
networking aliases, the `scanf` family, `open`/`fcntl`/`writev`/`pread`…, a
chunk of libm, `popen`, a real `pthread_cond_timedwait` backed by a timed
futex. The shim shrank to a single file: a zlib stub (zlib is not libc).

The headline was a real bug. Bear's `sigaction` expected a *kernel*-shaped
`struct sigaction`. Anything compiled with a stock `<signal.h>` — QEMU, bash,
all of it — hands it the *glibc* layout instead. The two disagree on where
`sa_flags` lives. QEMU's `sigfillset` then poisoned exactly the bytes Bear
misread as flags, the kernel rejected the call, and QEMU's `SIGALRM` handler
**silently never installed**. The VM was being killed by its own timer. Fixing
`struct bear_sigaction` to mirror the glibc ABI fixed it for *every* program,
not just QEMU.

(Red herring of the week: `-d cpu` logged only 17 blocks and I was sure it had
livelocked. It hadn't — chained translation blocks just don't re-log. `gdb`
caught it happily executing JIT'd Z80 code. The lesson is always "measure the
thing, not a proxy for the thing.")

End of Day 2: a `qemu-system-z80` that boots a ZX Spectrum, links 100% against
Bear Libcs, and runs the CPU. I fed it the real 48K Spectrum ROM and watched
`© 1982 Sinclair Research Ltd` appear. Goosebumps, honestly.

## Day 3 — UZ80: putting something of *mine* inside it

I had an emulator on my Bear Libcs. I wanted my own code running *on the Z80*. So I
wrote a kernel.

**UZ80** is a bare-metal ZX Spectrum boot ROM, written in C, compiled with
**SDCC** to a 16 KiB image. There is no operating system under it and no libc
beside it — the Z80 resets to `0x0000` and runs *this*:

- `crt0.s` — the reset vector. Sets the stack, zeroes RAM scratch, installs
  `IM 1` so the ULA's 50 Hz frame interrupt vectors through `0x0038`, calls
  `main()`. It also has the keyboard-matrix scan routine.
- `kernel.c` — a 5×7 font I hand-drew bit by bit (A–Z, 0–9, lowercase, an `∞`
  glyph that spent its first build looking like an asterisk); the ZX Spectrum
  screen routines (that gloriously deranged thirds-interleaved framebuffer
  layout); a keyboard decoder over the real `IN (0xFE)` half-row matrix; and a
  tiny shell.

What it does when you boot it:

- paints a banner and a prompt — `aufer@fermihart:~$`
- **reads the keyboard** straight off the Spectrum matrix
- **blinks the cursor** off the 50 Hz hardware interrupt — a real clock, not a
  delay loop
- keeps a **live uptime counter** in the corner, incremented by that same ISR
- runs built-in commands: `HELP`, `INFO`, `TIME`, `BEAR`, `CLEAR`, with
  backspace (CAPS SHIFT + 0, the Spectrum's DELETE)

It currently uses **~2.1 KiB of the 16 KiB ROM**. There is a *lot* of room
left.

---


Every interface UZ80 touches is genuine ZX Spectrum hardware: the 16 KiB ROM at `0x0000`, 
the framebuffer at `0x4000`, the `0xFE` border/keyboard port, the `IM 1` interrupt at `0x0038`. 
SDCC emits genuine Z80 machine code. The `uz80.rom` file contains nothing emulator-specific 
— Bear and QEMU are only the workbench.

Burn `uz80.rom` onto a 16 KiB EPROM (a 27C128), drop it in place of a real
Spectrum's ROM, power on: it would boot. Honest caveat — I have verified it in
emulation only, not yet on physical silicon. The interfaces it uses are simple
and standard enough that I expect it to just work.

## Build it

Needs `sdcc` (compiler) and a Linux host. Builds out-of-tree, so a read-only
source checkout is fine.

```sh
make            # C ──sdcc──▶ .ihx ──makebin──▶ uz80.rom  (16 KiB)
make run        # build + boot in qemu-system-z80, VNC on :5948
make shot       # build + boot headless + screenshot
make demo       # build + boot + type a command + screenshot
make help
```

`make demo DEMO="h e l p ret"` types a different command at the prompt.

## Layout

```
crt0.s     reset vector · 50 Hz IM 1 ISR · keyboard scan   (Z80 asm)
kernel.c   font · screen · keyboard decode · shell         (C, SDCC)
Makefile   C ▶ ROM pipeline, plus run/shot/demo automation
```

## Credits & license

- `qemu-z80` — QEMU by Fabrice Bellard; the Z80 target by Stuart Brady (2009).
  My change to it was four lines in `configure` (a macOS linker case).
- **Bear Libcs** — my own libc; the QEMU port and the bug fixes live in its
  `ports/qemu/` tree.
- UZ80 itself: public domain. Unlicense. Take it, burn it, break it.
- Listening recommended: [open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o](https://open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o)

— F E R M I ∞ H A R T
