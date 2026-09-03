# ╔═══════════════════════════════════════════════════════════════════════════╗
# ║                                                                           ║
# ║   ╦ ╦╔═╗╔═╗ ╔═╗     UZ80 · a Z80 monitor, compiled C ──▶ ROM               ║
# ║   ║ ║╔═╝╠═╣ ║ ║     build · run · shot · demo                              ║
# ║   ╚═╝╚═╝╩ ╩ ╚═╝     built with SDCC · optional external emulator           ║
# ║                                                                           ║
# ╚═══════════════════════════════════════════════════════════════════════════╝
#
#   C source ──sdcc──▶ .ihx (Intel HEX) ──makebin──▶ 16 KiB raw ROM
#
#   make         build uz80.rom
#   make check   run host tests, build validation, and determinism proof
#   make run     build, then boot it in an explicitly configured qemu-z80
#   make shot    build, boot headless, grab a VNC screenshot
#   make demo    build, boot, type a command at the prompt, screenshot
#   make clean   remove the build directory
#   make help    this message
#
#   Build output defaults to ./build.  Override BUILD for a read-only checkout,
#   or set QEMU / KEYMAPS / VNC / DEMO for external emulator automation:
#   make demo DEMO="h e l p ret"
#
# Author: F E R M I ∞ H A R T <contact@fermihart.com>
# SPDX-License-Identifier: Unlicense
# Listening recommended: open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o

SHELL  := /bin/sh
PYTHON ?= python3
HOST_CC ?= cc
Z80_CC ?= sdcc
Z80_AS ?= sdasz80
MAKEBIN ?= makebin

# This Makefile's own directory — sources are read from here.
SRCDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# Build paths are checkout-local by default. Override BUILD for read-only trees.
BUILD  ?= $(SRCDIR)/build
BUILD  := $(abspath $(BUILD))
DATA   := $(BUILD)/data
BUILD_MARKER := $(BUILD)/.uz80-build
STAGED_ROM := $(DATA)/zx-rom.bin

# Emulator artifacts are external and intentionally have no private defaults.
QEMU    ?=
KEYMAPS ?=
VNC     ?= 48
VNC_HOST ?= 127.0.0.1
MONPORT ?= 55580

# `make demo` types these keys at the prompt (qemu monitor key names).
DEMO   ?= b e a r ret

ROMSIZE := 16384
ROM     := $(BUILD)/uz80.rom
ROM_TMP := $(BUILD)/uz80.rom.tmp
IHX     := $(BUILD)/uz80.ihx
MAP     := $(BUILD)/uz80.map
HOST_TEST := $(BUILD)/host_core_test

# sdcc emits intermediates next to its CWD, so every tool runs inside BUILD.
CODELOC := 0x0048
DATALOC := 0x6010

G := \033[1;32m
D := \033[0;36m
Z := \033[0m

.PHONY: all check test host-test reproducible check-tools check-emulator \
	prepare-build stage run shot demo clean help
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

# ── build ────────────────────────────────────────────────────────────────────
all: $(ROM)
	@"$(PYTHON)" -B "$(SRCDIR)/tools/validate_build.py" \
	      --map "$(MAP)" --ihx "$(IHX)" --rom "$(ROM)"

CSRC := kernel.c keyboard.c editor.c tty.c fs.c cmd.c font.c forth.c

prepare-build:
	@build="$(BUILD)"; src="$(SRCDIR)"; \
	 case "$$build" in ""|/|"$(HOME)"|"$$src") \
	     printf 'unsafe BUILD path: %s\n' "$$build" >&2; exit 1;; \
	 esac; \
	 if [ -e "$$build" ]; then \
	     test -d "$$build" || { printf 'BUILD is not a directory: %s\n' "$$build" >&2; exit 1; }; \
	     test -f "$(BUILD_MARKER)" || { printf 'refusing unowned BUILD directory: %s\n' "$$build" >&2; exit 1; }; \
	     IFS= read -r owner < "$(BUILD_MARKER)"; \
	     test "$$owner" = "$$src" || { printf 'BUILD belongs to another checkout: %s\n' "$$owner" >&2; exit 1; }; \
	 else \
	     mkdir -p "$$build"; \
	     printf '%s\n' "$$src" > "$(BUILD_MARKER)"; \
	 fi

check-tools:
	@for tool in "$(Z80_AS)" "$(Z80_CC)" "$(MAKEBIN)" "$(PYTHON)"; do \
	    command -v "$$tool" >/dev/null 2>&1 || { \
	        printf 'required tool not found: %s\n' "$$tool" >&2; exit 1; \
	    }; \
	done

check-emulator:
	@test -n "$(QEMU)" || { \
	    printf 'compatible qemu-z80 emulator is not bundled; set QEMU=/path/to/qemu-system-z80\n' >&2; \
	    exit 1; \
	}
	@command -v "$(QEMU)" >/dev/null 2>&1 || { \
	    printf 'configured qemu executable not found: %s\n' "$(QEMU)" >&2; exit 1; \
	}

$(ROM): $(SRCDIR)/crt0.s $(SRCDIR)/uz80.h $(addprefix $(SRCDIR)/,$(CSRC)) \
		$(SRCDIR)/Makefile $(SRCDIR)/tools/validate_build.py | prepare-build check-tools
	@mkdir -p "$(DATA)"
	@printf '$(D)  AS$(Z)    crt0.s\n'
	@cd "$(BUILD)" && "$(Z80_AS)" -o crt0.rel "$(SRCDIR)/crt0.s"
	@for f in $(CSRC); do \
	    printf '$(D)  CC$(Z)    %s\n' $$f; \
	    ( cd "$(BUILD)" && "$(Z80_CC)" -mz80 -c \
	          -I"$(SRCDIR)" "$(SRCDIR)/$$f" ) || exit 1; \
	done
	@printf '$(D)  LD$(Z)    link to .ihx\n'
	@cd "$(BUILD)" && "$(Z80_CC)" -mz80 --no-std-crt0 \
	      --code-loc $(CODELOC) --data-loc $(DATALOC) \
	      crt0.rel $(CSRC:.c=.rel) -o uz80.ihx
	@printf '$(D)  ROM$(Z)   makebin -> %d bytes\n' $(ROMSIZE)
	@rm -f -- "$(ROM_TMP)"
	@cd "$(BUILD)" && "$(MAKEBIN)" -s $(ROMSIZE) uz80.ihx "$(notdir $(ROM_TMP))"
	@"$(PYTHON)" -B "$(SRCDIR)/tools/validate_build.py" \
	      --map "$(MAP)" --ihx "$(IHX)" --rom "$(ROM_TMP)"
	@mv -f -- "$(ROM_TMP)" "$(ROM)"
	@printf '$(G)  OK$(Z)    %s\n' "$(ROM)"

$(HOST_TEST): $(SRCDIR)/tests/host_core_test.c $(SRCDIR)/fs.c \
		$(SRCDIR)/cmd.c $(SRCDIR)/keyboard.c $(SRCDIR)/editor.c \
		$(SRCDIR)/uz80.h $(SRCDIR)/Makefile | prepare-build
	@command -v "$(HOST_CC)" >/dev/null 2>&1 || { \
	    printf 'required host compiler not found: %s\n' "$(HOST_CC)" >&2; exit 1; \
	}
	@"$(HOST_CC)" -std=c11 -Wall -Wextra -Werror -pedantic \
	      -fsanitize=address,undefined -fno-sanitize-recover=all \
	      -DUZ80_HOST_TEST -I"$(SRCDIR)" \
	      "$(SRCDIR)/tests/host_core_test.c" "$(SRCDIR)/fs.c" \
	      "$(SRCDIR)/cmd.c" "$(SRCDIR)/keyboard.c" \
	      "$(SRCDIR)/editor.c" -o "$@"

host-test: $(HOST_TEST)
	@"$(HOST_TEST)"

test: host-test
	@"$(PYTHON)" -B -m unittest discover -s "$(SRCDIR)/tests" -p 'test_*.py' -v

reproducible: all check-tools
	@set -e; root=$$(mktemp -d); one="$$root/one"; two="$$root/two"; \
	 cleanup() { rm -rf -- "$$root"; }; \
	 trap cleanup EXIT HUP INT TERM; \
	 $(MAKE) --no-print-directory BUILD="$$one" all >/dev/null; \
	 $(MAKE) --no-print-directory BUILD="$$two" all >/dev/null; \
	 cmp "$(ROM)" "$$one/uz80.rom"; \
	 cmp "$$one/uz80.rom" "$$two/uz80.rom"; \
	 printf '$(G)  REPRO$(Z) %s\n' "$$(sha256sum "$$one/uz80.rom" | cut -d ' ' -f 1)"

check: test all reproducible

# ── deploy: validate and stage ROM (+ keymaps) for qemu-system-z80 ──────────
stage: all
	@test -n "$(KEYMAPS)" || { \
	    printf 'qemu keymaps are not bundled; set KEYMAPS=/path/to/qemu/keymaps\n' >&2; \
	    exit 1; \
	}
	@test -d "$(KEYMAPS)" || { printf 'qemu keymaps not found: %s\n' "$(KEYMAPS)" >&2; exit 1; }
	@mkdir -p "$(DATA)/keymaps"
	@set -e; tmp="$(STAGED_ROM).tmp.$$$$"; \
	 trap 'rm -f -- "$$tmp"' EXIT HUP INT TERM; \
	 cp "$(ROM)" "$$tmp"; cmp "$(ROM)" "$$tmp"; \
	 mv -f -- "$$tmp" "$(STAGED_ROM)"; trap - EXIT HUP INT TERM
	@cp "$(KEYMAPS)"/* "$(DATA)/keymaps/"

# ── run: boot UZ80 interactively (foreground; Ctrl-C to stop) ────────────────
run: check-emulator stage
	@printf '$(G)  RUN$(Z)   UZ80 booting — VNC on localhost:%d  (Ctrl-C to stop)\n' \
	        $$(( 5900 + $(VNC) ))
	@"$(QEMU)" -M zxspec48 -vnc "$(VNC_HOST):$(VNC)" -L "$(DATA)"

# ── shot: boot headless and capture the screen ──────────────────────────────
shot: check-emulator stage
	@command -v vncsnapshot >/dev/null 2>&1 || { printf 'vncsnapshot not found\n' >&2; exit 1; }
	@set -e; out="$(BUILD)/uz80.jpg"; tmp="$(BUILD)/uz80.jpg.tmp.$$$$"; \
	 log="$(BUILD)/qemu.log"; pid=; \
	 stop_qemu() { \
	     if [ -n "$$pid" ] && kill -0 "$$pid" 2>/dev/null; then \
	         kill "$$pid" 2>/dev/null || true; tries=0; \
	         while kill -0 "$$pid" 2>/dev/null && [ "$$tries" -lt 5 ]; do \
	             sleep 1; tries=$$((tries + 1)); \
	         done; \
	         if kill -0 "$$pid" 2>/dev/null; then kill -9 "$$pid" 2>/dev/null || true; fi; \
	         wait "$$pid" 2>/dev/null || true; \
	     fi; \
	 }; \
	 cleanup() { rm -f -- "$$tmp"; stop_qemu; }; \
	 trap 'exit 129' HUP; trap 'exit 130' INT; trap 'exit 143' TERM; \
	 trap cleanup EXIT; rm -f -- "$$out" "$$tmp"; \
	 "$(QEMU)" -M zxspec48 -vnc "$(VNC_HOST):$(VNC)" -L "$(DATA)" >"$$log" 2>&1 & pid=$$!; \
	 sleep 4; kill -0 "$$pid"; \
	 vncsnapshot -quiet "$(VNC_HOST):$(VNC)" "$$tmp" >/dev/null; \
	 test -s "$$tmp"; mv -f -- "$$tmp" "$$out"; \
	 printf '$(G)  SHOT$(Z)  %s\n' "$$out"

# ── demo: boot, type a command at the prompt, screenshot the result ─────────
demo: check-emulator stage
	@command -v vncsnapshot >/dev/null 2>&1 || { printf 'vncsnapshot not found\n' >&2; exit 1; }
	@command -v nc >/dev/null 2>&1 || { printf 'nc not found\n' >&2; exit 1; }
	@set -e; out="$(BUILD)/uz80-demo.jpg"; tmp="$(BUILD)/uz80-demo.jpg.tmp.$$$$"; \
	 log="$(BUILD)/qemu.log"; pid=; \
	 stop_qemu() { \
	     if [ -n "$$pid" ] && kill -0 "$$pid" 2>/dev/null; then \
	         kill "$$pid" 2>/dev/null || true; tries=0; \
	         while kill -0 "$$pid" 2>/dev/null && [ "$$tries" -lt 5 ]; do \
	             sleep 1; tries=$$((tries + 1)); \
	         done; \
	         if kill -0 "$$pid" 2>/dev/null; then kill -9 "$$pid" 2>/dev/null || true; fi; \
	         wait "$$pid" 2>/dev/null || true; \
	     fi; \
	 }; \
	 cleanup() { rm -f -- "$$tmp"; stop_qemu; }; \
	 trap 'exit 129' HUP; trap 'exit 130' INT; trap 'exit 143' TERM; \
	 trap cleanup EXIT; rm -f -- "$$out" "$$tmp"; \
	 "$(QEMU)" -M zxspec48 -vnc "$(VNC_HOST):$(VNC)" -L "$(DATA)" \
	     -monitor tcp:127.0.0.1:$(MONPORT),server,nowait >"$$log" 2>&1 & pid=$$!; \
	 sleep 4; kill -0 "$$pid"; \
	 printf '$(D)  KEYS$(Z)  typing at the prompt: $(DEMO)\n'; \
	 ( for k in $(DEMO); do printf 'sendkey %s\n' "$$k"; sleep 0.4; done; sleep 1; ) \
	     | nc -w1 127.0.0.1 $(MONPORT) >/dev/null; \
	 sleep 1; vncsnapshot -quiet "$(VNC_HOST):$(VNC)" "$$tmp" >/dev/null; \
	 test -s "$$tmp"; mv -f -- "$$tmp" "$$out"; \
	 printf '$(G)  DEMO$(Z)  %s\n' "$$out"

clean:
	@build="$(BUILD)"; src="$(SRCDIR)"; \
	 case "$$build" in ""|/|"$(HOME)"|"$$src") \
	     printf 'refusing unsafe BUILD path: %s\n' "$$build" >&2; exit 1;; \
	 esac; \
	 if [ ! -e "$$build" ]; then \
	     printf '$(G)  CLEAN$(Z) %s does not exist\n' "$$build"; exit 0; \
	 fi; \
	 test -d "$$build" || { printf 'BUILD is not a directory: %s\n' "$$build" >&2; exit 1; }; \
	 marker="$$build/.uz80-build"; \
	 test -f "$$marker" || { printf 'refusing unowned BUILD directory: %s\n' "$$build" >&2; exit 1; }; \
	 IFS= read -r owner < "$$marker"; \
	 test "$$owner" = "$$src" || { printf 'BUILD belongs to another checkout: %s\n' "$$owner" >&2; exit 1; }; \
	 rm -rf -- "$$build"; \
	 printf '$(G)  CLEAN$(Z) %s removed\n' "$$build"

help:
	@sed -n '2,23p' "$(SRCDIR)/Makefile" | sed 's/^# \{0,1\}//'
