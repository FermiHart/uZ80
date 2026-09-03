# Origin: the detour that became uZ80

> This is the historical development account. The Bear-linked emulator and
> Bear/blibc implementation discussed here remain private artifacts. They are
> not distributed, built, or certified by the public uZ80 repository.

## Day 0: "I just want to compile an old emulator"

I had a 17-year-old tree on disk: `qemu-z80`, Stuart Brady's 2009 branch of
QEMU 0.10.x that teaches it a Zilog Z80 target. It can pretend to be a ZX
Spectrum 48K/128K, a SAM Coupe, or an MSX. I typed `make`. Apple clang said no:

```text
error: register 'r14' unsuitable for global register variables on this target
```

QEMU 0.10 pins the CPU state pointer to a hardware register:
`register CPUZ80State *env asm("r14")`. GCC allows that; modern clang does not.
A 2009 codebase would not build with the host toolchain, and the blocker was the
compiler. Fine. Detour.

## Day 1: the detour eats the project

I had this other thing: **Bear Libcs**, a C standard library I am writing from
scratch, security-first and compatible with the Linux syscall ABI. Instead of
fighting clang, what if `qemu-z80` were linked against Bear rather than glibc?

- Bear speaks the Linux syscall ABI, so the work moved into a Linux VM.
- `qemu-system-z80` referenced 197 libc symbols. Bear covered about half by
  public name, while much of the remaining surface existed under `bear_*`
  names.
- In the private development tree, a small shim and a static `-nostdlib` link
  against `libbear.a` reached zero undefined symbols.
- The result ran: roughly 22.9 million Z80 instructions in three seconds, with
  registers advancing and real Z80 disassembly.

## Day 2: evolving the libc

A shim that papers over a libc is not interesting. The genuinely missing
surface moved into Bear's core: networking aliases, formatted input, file and
vector I/O, parts of libm, `popen`, and a condition-variable timed wait backed
by futex. The QEMU shim shrank to a zlib stub; zlib is not libc.

The most useful bug was in `sigaction`. Bear expected a kernel-shaped structure,
while software compiled with stock Linux headers supplied the userspace ABI
layout. QEMU's `sigfillset` poisoned bytes Bear interpreted as flags, the kernel
rejected the call, and QEMU's `SIGALRM` handler silently never installed.
Correcting the ABI layout fixed the class of failure rather than patching QEMU.

A misleading clue along the way was `-d cpu`: it logged only 17 blocks and made
the machine look stuck. Chained translation blocks simply do not re-log. GDB
showed the generated Z80 code continuing to execute. Measure the thing, not a
proxy for the thing.

By the end of the day, the private `qemu-system-z80` booted a ZX Spectrum while
linked against Bear Libcs. Feeding it the original 48K Spectrum ROM produced
the familiar 1982 Sinclair screen.

## Day 3: put something new inside it

Once the private emulator worked, I wanted my own code running on the Z80.
uZ80 became a bare-metal ZX Spectrum ROM written in C and assembly, compiled by
SDCC to a fixed 16 KiB image.

There is no operating system or libc under the target. The processor resets to
`0x0000`, initializes its own RAM and interrupt path, drives the ULA directly,
and enters the monitor shell. Bear and QEMU were the workbench; the ROM is an
independent Z80 artifact.

Return to the [uZ80 overview](../README.md).
