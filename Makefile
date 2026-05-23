# ╔═══════════════════════════════════════════════════════════════════════════╗
# ║                                                                           ║
# ║   ╦ ╦╔═╗╔═╗ ╔═╗     UZ80 · a z80 micro-kernel, compiled C ──▶ ROM          ║
# ║   ║ ║╔═╝╠═╣ ║ ║     build · run · shot · demo                              ║
# ║   ╚═╝╚═╝╩ ╩ ╚═╝     hosted on Bear Libcs                                   ║
# ║                                                                           ║
# ╚═══════════════════════════════════════════════════════════════════════════╝
#
#   C source ──sdcc──▶ .ihx (Intel HEX) ──makebin──▶ 16 KiB raw ROM
#
#   make         build uz80.rom
#   make run     build, then boot it in qemu-system-z80 (Bear-hosted)
#   make shot    build, boot headless, grab a VNC screenshot
#   make demo    build, boot, type a command at the prompt, screenshot
#   make clean   remove the build directory
#   make help    this message
#
#   Builds out-of-tree (into $(BUILD)) so the source tree may be read-only
#   — e.g. a Lima mount.  Override BUILD / QEMU / VNC / DEMO on the command
#   line:  make demo DEMO="h e l p ret"
#
# Author: F E R M I ∞ H A R T <contact@fermihart.com>
# SPDX-License-Identifier: Unlicense
# Listening recommended: open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o

SHELL  := /bin/sh

# This Makefile's own directory — sources are read from here.
SRCDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# Everything we generate goes here (keeps a read-only source tree clean).
BUILD  ?= $(HOME)/.uz80
DATA   := $(BUILD)/data

# The Bear-hosted emulator and the VNC display number it should serve.
QEMU    ?= $(HOME)/work/qemu-z80/z80-softmmu/qemu-system-z80-bear
KEYMAPS ?= $(HOME)/work/qemu-z80/pc-bios/keymaps
VNC     ?= 48
MONPORT ?= 55580

# `make demo` types these keys at the prompt (qemu monitor key names).
DEMO   ?= b e a r ret

ROMSIZE := 16384
ROM     := $(BUILD)/uz80.rom

# sdcc emits intermediates next to its CWD, so every tool runs inside BUILD.
CODELOC := 0x0048
DATALOC := 0x6010

G := \033[1;32m
D := \033[0;36m
Z := \033[0m

.PHONY: all run shot demo clean help
.DEFAULT_GOAL := all

# ── build ────────────────────────────────────────────────────────────────────
all: $(ROM)

CSRC := kernel.c tty.c fs.c cmd.c font.c forth.c

$(ROM): $(SRCDIR)/crt0.s $(SRCDIR)/uz80.h $(addprefix $(SRCDIR)/,$(CSRC))
	@mkdir -p $(DATA)
	@printf '$(D)  AS$(Z)    crt0.s\n'
	@cd $(BUILD) && sdasz80 -o crt0.rel $(SRCDIR)/crt0.s
	@for f in $(CSRC); do \
	    printf '$(D)  CC$(Z)    %s\n' $$f; \
	    ( cd $(BUILD) && sdcc -mz80 -c \
	          -I$(SRCDIR) $(SRCDIR)/$$f ) || exit 1; \
	done
	@printf '$(D)  LD$(Z)    link to .ihx\n'
	@cd $(BUILD) && sdcc -mz80 --no-std-crt0 \
	      --code-loc $(CODELOC) --data-loc $(DATALOC) \
	      crt0.rel $(CSRC:.c=.rel) -o uz80.ihx
	@printf '$(D)  ROM$(Z)   makebin -> %d bytes\n' $(ROMSIZE)
	@cd $(BUILD) && makebin -s 65536 uz80.ihx uz80.img \
	      && head -c $(ROMSIZE) uz80.img > uz80.rom && rm -f uz80.img
	@USED=$$(awk '$$2=="l__CODE"{printf "%d", strtonum("0x" $$1)}' $(BUILD)/uz80.map); \
	 printf '$(G)  OK$(Z)    %s  (%d / %d bytes, %d%% used)\n' \
	        "$(ROM)" "$$USED" $(ROMSIZE) "$$(( USED * 100 / $(ROMSIZE) ))"

# ── deploy: stage the ROM (+ qemu keymaps) where qemu-system-z80 looks ───────
$(DATA)/zx-rom.bin: $(ROM)
	@mkdir -p $(DATA)/keymaps
	@cp $(ROM) $@
	@cp $(KEYMAPS)/* $(DATA)/keymaps/ 2>/dev/null || true

# ── run: boot UZ80 interactively (foreground; Ctrl-C to stop) ────────────────
run: $(DATA)/zx-rom.bin
	@test -x $(QEMU) || { printf 'qemu not found: %s\n' "$(QEMU)"; exit 1; }
	@printf '$(G)  RUN$(Z)   UZ80 booting — VNC on localhost:%d  (Ctrl-C to stop)\n' \
	        $$(( 5900 + $(VNC) ))
	@$(QEMU) -M zxspec48 -vnc :$(VNC) -L $(DATA)

# ── shot: boot headless and capture the screen ──────────────────────────────
shot: $(DATA)/zx-rom.bin
	@test -x $(QEMU) || { printf 'qemu not found: %s\n' "$(QEMU)"; exit 1; }
	@$(QEMU) -M zxspec48 -vnc :$(VNC) -L $(DATA) >/dev/null 2>&1 & \
	   echo $$! > $(BUILD)/qemu.pid
	@sleep 4
	@vncsnapshot -quiet localhost:$(VNC) $(BUILD)/uz80.jpg >/dev/null 2>&1 || true
	@kill -9 $$(cat $(BUILD)/qemu.pid) 2>/dev/null; rm -f $(BUILD)/qemu.pid
	@printf '$(G)  SHOT$(Z)  %s\n' "$(BUILD)/uz80.jpg"

# ── demo: boot, type a command at the prompt, screenshot the result ─────────
demo: $(DATA)/zx-rom.bin
	@test -x $(QEMU) || { printf 'qemu not found: %s\n' "$(QEMU)"; exit 1; }
	@$(QEMU) -M zxspec48 -vnc :$(VNC) -L $(DATA) \
	         -monitor tcp:127.0.0.1:$(MONPORT),server,nowait >/dev/null 2>&1 & \
	   echo $$! > $(BUILD)/qemu.pid
	@sleep 4
	@printf '$(D)  KEYS$(Z)  typing at the prompt: $(DEMO)\n'
	@( for k in $(DEMO); do printf 'sendkey %s\n' "$$k"; sleep 0.4; done; \
	   sleep 1 ) | nc -w1 127.0.0.1 $(MONPORT) >/dev/null 2>&1 || true
	@sleep 1
	@vncsnapshot -quiet localhost:$(VNC) $(BUILD)/uz80-demo.jpg >/dev/null 2>&1 || true
	@kill -9 $$(cat $(BUILD)/qemu.pid) 2>/dev/null; rm -f $(BUILD)/qemu.pid
	@printf '$(G)  DEMO$(Z)  %s\n' "$(BUILD)/uz80-demo.jpg"

clean:
	@rm -rf $(BUILD)
	@printf '$(G)  CLEAN$(Z) %s removed\n' "$(BUILD)"

help:
	@sed -n '2,22p' $(SRCDIR)/Makefile | sed 's/^# \{0,1\}//'
