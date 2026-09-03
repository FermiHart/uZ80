<div align="center">

# uZ80

**A freestanding 16 KiB monitor ROM for the ZX Spectrum 48K.**

Written in C and Z80 assembly, with a UNIX-flavoured shell, an in-RAM
filesystem, and a tiny Forth. No operating system. No target libc.

<img src="docs/screenshots/cat-motd.jpg" width="640" alt="Historical uZ80 emulator capture showing the boot splash, status bar, shell prompt, and motd">

<sub>Historical capture from the private development emulator. Public CI
currently stops at a validated ROM image.</sub>

[![Source-to-ROM proof](https://github.com/FermiHart/uZ80/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/FermiHart/uZ80/actions/workflows/ci.yml)
[![License: Unlicense](https://img.shields.io/badge/license-Unlicense-6f42c1.svg)](LICENSE)
[![Target: ZX Spectrum 48K](https://img.shields.io/badge/target-ZX%20Spectrum%2048K-cf3341.svg)](#architecture)

</div>

<p align="center">
  <a href="#quick-start">Quick start</a> ·
  <a href="#what-fits-in-16-kib">Features</a> ·
  <a href="#architecture">Architecture</a> ·
  <a href="#proof-boundary">Proof</a> ·
  <a href="#gallery">Gallery</a> ·
  <a href="ROADMAP.md">Roadmap</a>
</p>

> [!IMPORTANT]
> **Status: pre-release.** Public CI builds and validates the ROM, runs
> sanitizer-backed host regressions, and proves byte-identical clean builds
> within one toolchain environment.
> Emulator execution is historical evidence; physical hardware remains
> unverified.

## At a glance

| Surface | Current state |
|---|---|
| Target | ZX Spectrum 48K hardware interfaces |
| Image | Exactly 16 KiB, reset vector at `0x0000` |
| Toolchain | SDCC 4.2.0 in a base-image-digest-pinned Ubuntu container; apt indexes remain mutable |
| Runtime | Bare metal; direct framebuffer, keyboard, border, beeper, and IM 1 IRQ |
| Public proof | Host regressions, linker/map gates, ROM identity, same-environment determinism |
| Emulator | Historical captures from a private qemu-z80 build; that emulator is not distributed or run in CI |
| Hardware | Not tested; a 16 KiB image alone does not prove electrical or socket compatibility |
| Release | None yet; CI artifacts are temporary evidence, not releases |

## Quick start

The container path is canonical: it supplies SDCC 4.2.0 and runs the same gate
as GitHub Actions.

```sh
git clone https://github.com/FermiHart/uZ80.git
cd uZ80
docker build -t uz80-toolchain:sdcc-4.2 .
mkdir -p .container-work
docker run --rm --user "$(id -u):$(id -g)" \
  --network none --cap-drop ALL --security-opt no-new-privileges \
  --read-only --tmpfs /tmp:rw,noexec,nosuid,nodev \
  -v "$PWD:/src:ro" -v "$PWD/.container-work:/work" \
  uz80-toolchain:sdcc-4.2 \
  make -C /src BUILD=/work/build check
```

The validated image is written to `.container-work/build/uz80.rom`.

For a native build, install SDCC 4.2.0 (`sdcc`, `sdasz80`, `makebin`), GNU
Make, Python 3, a C compiler, and `sha256sum`:

```sh
make          # build and validate build/uz80.rom
make test     # host ASan/UBSan tests plus proof-pipeline regressions
make check    # tests, ROM/RAM gates, and same-environment determinism
make help
```

## What fits in 16 KiB

- A direct ZX Spectrum framebuffer terminal with a hand-drawn 5x7 font.
- Matrix keyboard input with CAPS SHIFT, SYMBOL SHIFT, deletion, and history.
- A UNIX-flavoured command surface: `ls`, `cat`, `cp`, `mv`, `rm`, `wc`,
  `echo TEXT > FILE`, `help`, `history`, `uptime`, `fortune`, `cowsay`, and
  more.
- A 16-slot RAM filesystem with failure-atomic text writes and 255-byte
  payloads.
- uForth, a Jupiter Ace tribute with definitions, control flow, variables,
  `SEE`, and an interactive REPL.
- A frame-derived clock and blinking cursor driven by the 50 Hz IM 1 interrupt.

The clock is intentionally honest about its current limit: its 16-bit frame
counter wraps after roughly 21 minutes 51 seconds. Deeper uForth failure
semantics and the long-lived clock are scheduled in [ROADMAP.md](ROADMAP.md).

## Architecture

```text
crt0.s + C modules
        |
        v
     SDCC 4.2.0  --->  Intel HEX + linker map
                              |
                              v
                    makebin candidate ROM
                              |
                              v
                    fail-closed validator
                              |
                              v
                   validated 16 KiB uz80.rom
                              |
                              v
       ZX ULA: screen / keyboard / border / beeper / 50 Hz IRQ
```

| Address range | Owner | Contract |
|---|---|---|
| `[0x0000,0x4000)` | ROM | Reset, IM 1 vector, code, constants |
| `[0x4000,0x5B00)` | ZX display | Bitmap and colour attributes |
| `[0x6000,0x6010)` | Runtime scratch | Keyboard rows and frame counter |
| `[0x6010,0x6400)` | SDCC `_DATA` | Zeroed at boot; linker-gated below FS |
| `[0x6400,0x74D0)` | RAM filesystem | 16 fixed-capacity slots |
| `[0x8000,0xE000)` | uForth | Data stack, return stack, dictionary |
| `[0xE000,0xFF00)` | Native stack | Starts at `0xFF00` and grows down |

The ROM contains only Z80 target code. Bear Libcs and QEMU were a private
development workbench, not target dependencies.

## Proof boundary

`make check` is designed to answer a narrow question: did this source produce a
bounded, internally consistent ROM deterministically in the current environment
without violating the declared RAM layout?

| Claim | Evidence | Boundary |
|---|---|---|
| A valid 16 KiB ROM is produced | Intel HEX checksum, extent, vector, and byte-identity checks | Does not execute the ROM |
| Linked memory respects declared regions | Linker map and emitted-byte ownership validation | Layout proof, not runtime semantics |
| Shell, FS, keyboard, and editor seams behave | Host C harness under ASan/UBSan, fail-fast on UB | Host execution is not Z80 execution |
| Output is deterministic in one environment | Primary artifact compared with two clean build directories in one invocation | Cross-time reproducibility is not proven; apt indexes are not snapshot-pinned |
| An unidentified historical build booted | Historical screenshots below | Does not establish current-HEAD execution; private emulator is not public or replayed in CI |
| Physical compatibility | Not yet proven | No hardware capture exists |

The badge, workflow, and uploaded CI artifacts cover only the public
source-to-ROM boundary. They do not compile, link, execute, or certify Bear
Libcs or the private qemu-z80 build.

## External emulator

The optional automation accepts an explicitly supplied compatible qemu-z80
binary and keymap directory:

```sh
make run  QEMU=/path/to/qemu-system-z80 KEYMAPS=/path/to/qemu/keymaps
make shot QEMU=/path/to/qemu-system-z80 KEYMAPS=/path/to/qemu/keymaps
make demo QEMU=/path/to/qemu-system-z80 KEYMAPS=/path/to/qemu/keymaps \
  DEMO="h e l p ret"
```

There are deliberately no private defaults. This repository does not publish
Bear/blibc source, headers, archives, QEMU patches, or a Bear-linked executable.
None of them is required to compile or validate uZ80. `make shot` additionally
needs `vncsnapshot`; `make demo` needs both `vncsnapshot` and `nc`.

## Gallery

These are real captures of historical builds from the private development
emulator. They are product evidence, not current-HEAD or CI proof. See the
[capture provenance policy](docs/screenshots/README.md).

<p align="center">
  <img src="docs/screenshots/forthdemo.jpg" width="640" alt="uZ80 running its uForth demonstration"><br>
  <strong>uForth</strong> - definitions, control flow, variables, and <code>SEE</code>
</p>

<details>
<summary><strong>More historical captures</strong></summary>

| | |
|:---:|:---:|
| <img src="docs/screenshots/help.jpg" width="400" alt="uZ80 help command listing its builtins"><br>Generated command help | <img src="docs/screenshots/cowsay.jpg" width="400" alt="uZ80 cowsay command"><br><code>cowsay uz80</code> |
| <img src="docs/screenshots/history.jpg" width="400" alt="uZ80 numbered command history"><br>Numbered history and recall | <img src="docs/screenshots/forth-repl.jpg" width="400" alt="Interactive uForth REPL"><br>Interactive uForth REPL |
| <img src="docs/screenshots/ls.jpg" width="400" alt="uZ80 RAM filesystem listing"><br>RAM filesystem | <img src="docs/screenshots/fortune.jpg" width="400" alt="uZ80 fortune command"><br>LFSR-selected fortune |
| <img src="docs/screenshots/uptime.jpg" width="400" alt="uZ80 uptime command"><br>Frame-derived uptime | <img src="docs/screenshots/bear.jpg" width="400" alt="uZ80 Bear Libcs tribute banner"><br>Bear Libcs tribute |

</details>

## Project map

```text
crt0.s                    reset, RAM clear, IM 1 ISR, keyboard scan
kernel.c                  boot, status bar, beeper, main loop
keyboard.c / editor.c     pure input decoding and line-capacity contracts
tty.c / font.c            framebuffer terminal, history, 5x7 font
fs.c / cmd.c              RAM filesystem and shell dispatch
forth.c                   tokeniser, compiler, threaded interpreter
uz80.h                    shared hardware, memory, and subsystem contracts
tools/validate_build.py   map/IHX/ROM proof gate
tests/                    host regressions and build-system contracts
Dockerfile                SDCC 4.2.0 build environment
ROADMAP.md                staged engineering plan and explicit limits
docs/origin.md            the Bear Libcs and qemu-z80 development story
```

## Origin

uZ80 began as a detour while reviving a 2009 qemu-z80 tree and experimenting
with a private QEMU build linked against Bear Libcs. The emulator remained the
workbench; the artifact that emerged is an independent bare-metal Z80 ROM.

[Read the development story ->](docs/origin.md)

## Credits and license

- `qemu-z80`: QEMU by Fabrice Bellard; Z80 target by Stuart Brady (2009).
- Bear Libcs: the author's private libc and development environment; not
  distributed or required by uZ80.
- uZ80 is released into the public domain under the [Unlicense](LICENSE).

F E R M I ∞ H A R T · [contact@fermihart.com](mailto:contact@fermihart.com)
