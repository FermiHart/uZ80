/*═══════════════════════════════════════════════════════════════════════════════
 *
 *  UZ80 · fs.c — a tiny in-RAM filesystem
 *
 *  Sixteen slots, 11-character names and 255-byte text payloads. Storage uses
 *  a 12-byte NUL-terminated name field and a 256-byte data field per slot.
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

/* Place the filesystem at 0x6400 — safely above the SDCC _DATA segment.
 * _DATA starts at --data-loc (0x6010) and currently measures 438 bytes
 * ([0x6010,0x61C6) per uz80.map); 0x6400 leaves 570 bytes for the
 * C statics to grow before they could reach the FS.  Verified against the
 * build map — see the memory layout + the resolved collision note in
 * uz80.h. FS spans [0x6400,0x74D0) (16 * 269 B), clear of uForth @0x8000. */
#ifdef UZ80_HOST_TEST
static fs_file_t HOST_FS[FS_FILEMAX];
static fs_file_t *const FS = HOST_FS;
#else
static fs_file_t *const FS = (fs_file_t *)UZ_FS_BASE;
#endif

typedef char fs_layout_fits_before_forth[
    (UZ_FS_BASE + sizeof(fs_file_t) * FS_FILEMAX <= UZ_FORTH_DSTACK_BASE)
        ? 1 : -1
];

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
    fs_file_t *existing;
    if (!name || !name[0] || strlen_n(name, FS_NAMEMAX) >= FS_NAMEMAX)
        return 0;
    existing = fs_find(name);
    if (existing) return existing;
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

uint8_t fs_write(fs_file_t *f, const char *text)
{
    uint16_t length = 0;
    uint16_t i;
    if (!f || !text) return 0;
    while (length < FS_DATAMAX && text[length]) length++;
    if (length == FS_DATAMAX) return 0;
    for (i = 0; i < length; i++) f->data[i] = (uint8_t)text[i];
    f->data[length] = 0;
    f->size = (uint8_t)length;
    return 1;
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
    "  -- leonardo (and pike)\n";

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

typedef char motd_fits_in_file[(sizeof MOTD <= FS_DATAMAX) ? 1 : -1];
typedef char fortunes_fit_in_file[(sizeof FORTUNES <= FS_DATAMAX) ? 1 : -1];
typedef char readme_fits_in_file[(sizeof README <= FS_DATAMAX) ? 1 : -1];
typedef char license_fits_in_file[(sizeof LICENSE <= FS_DATAMAX) ? 1 : -1];

void fs_init(void)
{
    uint8_t i;
    /* zap every slot first */
    for (i = 0; i < FS_FILEMAX; i++) {
        FS[i].name[0] = 0;
        FS[i].size    = 0;
    }
    (void)fs_write(fs_create("motd"),    MOTD);
    (void)fs_write(fs_create("readme"),  README);
    (void)fs_write(fs_create("fortune"), FORTUNES);
    (void)fs_write(fs_create("license"), LICENSE);
}
