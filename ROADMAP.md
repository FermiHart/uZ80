# uZ80 engineering roadmap

uZ80 should remain a small, understandable ZX Spectrum ROM. Raising its level
means making every claim executable and every fixed-memory boundary explicit,
not turning it into a large general-purpose operating system.

## Wave 1: proof foundation

Status: complete on `main`; the public CI executes this wave's exit criterion.

- Fix the Docker base digest and direct SDCC package version.
- Validate Intel HEX checksums, ROM extent, RAM extent and final image identity.
- Reject unsupported initialized RAM instead of silently dropping it.
- Prove deterministic ROM output from two independent builds.
- Add sanitizer-backed host seams for shell, filesystem, keyboard and editor
  contracts.
- Make cleanup and emulator capture fail closed; bind VNC to loopback.
- Run the same proof in least-privilege GitHub Actions.

Exit criterion: `make check` passes in the versioned container and generates a
16 KiB ROM, map, and Intel HEX file; GitHub Actions uploads those files as
temporary evidence. Snapshot-pinning all apt dependencies remains release work.

## Wave 2: transactional uForth

- Reserve dictionary writes atomically and report exhaustion.
- Keep incomplete definitions hidden and roll them back on any compile error.
- Tag and validate IF/ELSE/THEN and BEGIN/UNTIL control-flow entries.
- Make data/return-stack operations preflight their complete stack effect.
- Stop and unwind immediately on `bye`; reject overlong tokens as one token.
- Define divide-by-zero and 16-bit arithmetic behavior.
- Introduce a bounded host VM using 16-bit arena offsets, then fuzz malformed
  source and bytecode without touching the target memory model.

Exit criterion: malformed source cannot publish or execute a partial word, and
the Forth demo plus negative corpus pass on host and target builds.

## Wave 3: clock, terminal and input

- Extend the 50 Hz clock beyond the current 16-bit, 21m51s wrap.
- Update the status bar while idle and inside Forth.
- Track key identity rather than a single any-key latch; add deterministic
  debounce/repeat behavior.
- Complete LEFT/RIGHT editing and preserve an in-progress line while browsing
  history.
- Model the Spectrum framebuffer with guard bytes in host tests.

Exit criterion: long-idle, wraparound, fast-key and cross-prompt editor traces
are deterministic and cannot address outside the bitmap.

## Wave 4: shell and filesystem semantics

- Specify one filename/argument grammar and reject trailing or excess tokens.
- Return structured errors for full directory, invalid names and oversized
  writes.
- Decide whether to keep fixed 16 x 255-byte text files or introduce a compact
  variable-length arena without crossing the Forth region.
- Add golden command tests for every builtin; keep help/man driven by the
  existing dispatch table.

Exit criterion: every command has host tests for success, boundary and failure
paths, with no silent truncation or ambiguous mutation.

## Wave 5: emulator and hardware evidence

- Integrate a publicly distributable compatible emulator, pinned by source or
  package version and digest, without exposing the private Bear/blibc tree.
- Keep the private Bear-linked qemu-z80 build outside the public release and
  label its existing screenshots as historical evidence.
- Replace screenshot-only smoke tests with machine-readable framebuffer,
  keyboard and interrupt assertions.
- Attach command, source SHA, toolchain and emulator provenance to gallery
  images.
- Run the ROM on physical 48K-compatible hardware or an EPROM test fixture and
  retain raw capture evidence.

Exit criterion: emulator and physical reports are reproducible, attributable
and clearly separated.

## Release discipline

The first release should wait for Waves 1-3. It should contain the ROM, Intel
HEX, linker map, SHA-256 manifest, toolchain identity, license and a concise
statement of what was tested versus what remains inferred.
