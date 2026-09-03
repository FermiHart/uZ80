# Screenshot provenance

The current JPEG gallery contains historical captures from the author's private
qemu-z80 development environment. The images show genuine uZ80 sessions, but
their exact source commit, emulator digest, capture date, and command transcript
were not recorded at capture time.

They are therefore **product evidence, not reproducible proof artifacts**. The
public `Source-to-ROM proof` workflow does not create or certify them. All ten
files are 320x240 JPEGs first committed in
[`4316f1e`](https://github.com/FermiHart/uZ80/commit/4316f1e5e14b418fccc5e9d90d4c87f5c841b7f1);
that is their first known repository appearance, not their capture provenance.

| Capture | Visible scenario |
|---|---|
| [`cat-motd.jpg`](cat-motd.jpg) | Boot, status line, and message of the day |
| [`help.jpg`](help.jpg) | Generated builtin command help |
| [`forthdemo.jpg`](forthdemo.jpg) | uForth scripted demonstration |
| [`forth-repl.jpg`](forth-repl.jpg) | Interactive uForth session |
| [`history.jpg`](history.jpg) | Numbered shell history |
| [`ls.jpg`](ls.jpg) | RAM filesystem listing |
| [`cowsay.jpg`](cowsay.jpg) | `cowsay uz80` |
| [`bear.jpg`](bear.jpg) | Bear Libcs tribute banner |
| [`fortune.jpg`](fortune.jpg) | LFSR-selected quotation |
| [`uptime.jpg`](uptime.jpg) | Frame-derived uptime |

The checked-in [`SHA256SUMS`](SHA256SUMS) manifest records the current image
bytes and is verified by the public-surface test.

Future captures should be lossless PNGs generated through a publicly
distributable emulator path. Each capture record must include:

- uZ80 source commit;
- emulator name, version, source/package reference, and digest;
- exact command or input sequence;
- capture command and timestamp;
- image dimensions and SHA-256;
- whether CI can reproduce it.

Until those fields exist, gallery images must remain labelled historical.
