/*═══════════════════════════════════════════════════════════════════════════════
 *
 *  UZ80 · fs.c — a tiny in-RAM filesystem
 *
 *  Sixteen slots, fixed-size names (12 chars) and contents (256 bytes).
 *  Everything lives in a single static array; there is no directory tree,
 *  no permissions, no metadata.  Lives in RAM, dies on power-off.
 *
 *  Boot pre-populates:
 *    /motd       message of the day
 *    /readme     pointer to docs
 *    /fortune    a database of quotes (one per blank line)
 *    /license    "this code is in the public domain"
 *
 *  Author: F E R M I ∞ H A R T <contact@fermihart.com>
 *  SPDX-License-Identifier: Unlicense
 *  Listening recommended: open.spotify.com/playlist/6flrLsdYxQZvGNRkdohL7o
 *
 *═══════════════════════════════════════════════════════════════════════════════*/

#include "uz80.h"

/* Place the filesystem at 0x6100 so it survives across resets in emulation
 * (and so cold-boot puts it in known RAM on real hardware). */
static fs_file_t *const FS = (fs_file_t *)0x6100;

/* ── small string helpers (no libc) ──────────────────────────────────────── */
static uint8_t streq_n(const char *a, const char *b, uint8_t n)
{
    while (n--) { if (*a != *b) return 0; if (!*a) return 1; a++; b++; }
    return 1;
}
static uint8_t strlen_n(const char *s, uint8_t cap)
{
    uint8_t i = 0; while (i < cap && s[i]) i++; return i;
}
static void strcpy_n(char *d, const char *s, uint8_t cap)
{
    uint8_t i = 0;
    while (i < cap - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

/* ── slot lookup ─────────────────────────────────────────────────────────── */
fs_file_t *fs_find(const char *name)
{
    uint8_t i;
    for (i = 0; i < FS_FILEMAX; i++) {
        if (!FS[i].name[0]) continue;
        if (streq_n(FS[i].name, name, FS_NAMEMAX)) return &FS[i];
    }
    return 0;
}

fs_file_t *fs_iter(uint8_t i)
{
    if (i >= FS_FILEMAX) return 0;
    return FS[i].name[0] ? &FS[i] : 0;
}

fs_file_t *fs_create(const char *name)
{
    uint8_t i;
    if (!name || !name[0] || strlen_n(name, FS_NAMEMAX) >= FS_NAMEMAX)
        return 0;
    if (fs_find(name)) return fs_find(name);
    for (i = 0; i < FS_FILEMAX; i++) {
        if (!FS[i].name[0]) {
            strcpy_n(FS[i].name, name, FS_NAMEMAX);
            FS[i].size = 0;
            return &FS[i];
        }
    }
    return 0;
}

int8_t fs_delete(const char *name)
{
    fs_file_t *f = fs_find(name);
    if (!f) return -1;
    f->name[0] = 0;
    f->size    = 0;
    return 0;
}

void fs_write(fs_file_t *f, const char *text)
{
    uint8_t i = 0;
    while (i < FS_DATAMAX - 1 && text[i]) { f->data[i] = (uint8_t)text[i]; i++; }
    f->data[i] = 0;
    f->size    = i;
}

/* ── pre-populated content ────────────────────────────────────────────────── */
static const char MOTD[] =
    "uz80 v2 - 16k of unix-flavour\n"
    "type 'help' for commands.\n"
    "fortune. cat motd. play boot.\n";

static const char FORTUNES[] =
    "those who do not understand unix\n"
    "are condemned to reinvent it.\n"
    "  -- henry spencer\n"
    "\n"
    "when in doubt, use brute force.\n"
    "  -- ken thompson\n"
    "\n"
    "simplicity is the ultimate\n"
    "sophistication.\n"
    "  -- leonardo (and pike)\n"
    "\n"
    "controlling complexity is the\n"
    "essence of computer programming.\n"
    "  -- brian kernighan\n"
    "\n"
    "premature optimization is the\n"
    "root of all evil.\n"
    "  -- donald knuth\n"
    "\n"
    "unix is simple. it just takes a\n"
    "genius to understand its simpli-\n"
    "city. -- dennis ritchie\n"
    "\n"
    "strong by default. lean by\n"
    "design. private by nature.\n"
    "  -- bear libcs\n";

static const char README[] =
    "uz80 is a z80 boot rom written\n"
    "in c.  no os, no libc - just\n"
    "the bare metal.\n"
    "\n"
    "the rom hosts a tiny shell with\n"
    "an in-ram filesystem.  files\n"
    "live until reset.\n";

static const char LICENSE[] =
    "the unlicense.\n"
    "public domain. no warranty.\n"
    "take it, burn it, ship it.\n";

void fs_init(void)
{
    uint8_t i;
    /* zap every slot first */
    for (i = 0; i < FS_FILEMAX; i++) {
        FS[i].name[0] = 0;
        FS[i].size    = 0;
    }
    fs_write(fs_create("motd"),    MOTD);
    fs_write(fs_create("readme"),  README);
    fs_write(fs_create("fortune"), FORTUNES);
    fs_write(fs_create("license"), LICENSE);
}
