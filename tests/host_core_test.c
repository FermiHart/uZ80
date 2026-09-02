/* Host regression harness for shell/filesystem behavior that needs no Z80.
 * SPDX-License-Identifier: Unlicense */

#include "uz80.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile uint16_t uz_host_frames;
volatile uint8_t uz_host_kbd_rows[8];

static char output[4096];
static size_t output_len;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

void tty_clear(void) { output_len = 0; output[0] = 0; }

void tty_putc(char c)
{
    if (output_len + 1 >= sizeof output) fail("captured output overflow");
    output[output_len++] = c;
    output[output_len] = 0;
}

void tty_puts(const char *text)
{
    while (*text) tty_putc(*text++);
}

void tty_putu(uint16_t value)
{
    char digits[6];
    size_t index = sizeof digits;
    digits[--index] = 0;
    do {
        digits[--index] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    tty_puts(&digits[index]);
}

uint8_t tty_hist_count(void) { return 0; }
const char *tty_hist_get(uint8_t back) { (void)back; return ""; }
void forth_repl(void) {}
void forth_eval(const char *script) { (void)script; }
void beep_tone(uint16_t cycles, uint16_t half)
{
    (void)cycles;
    (void)half;
}

static void dispatch(const char *command)
{
    char line[FS_DATAMAX + 32];
    size_t length = strlen(command);
    if (length >= sizeof line) fail("test command is too long");
    memcpy(line, command, length + 1);
    cmd_dispatch(line);
}

static void test_move_to_self_preserves_file(void)
{
    fs_file_t *before;
    fs_file_t *after;
    uint8_t size;
    uint8_t data[FS_DATAMAX];

    fs_init();
    before = fs_find("motd");
    if (!before) fail("motd fixture is missing");
    size = before->size;
    memcpy(data, before->data, size);

    dispatch("mv motd motd");

    after = fs_find("motd");
    if (!after) fail("mv FILE FILE deleted the file");
    if (after->size != size || memcmp(after->data, data, size) != 0)
        fail("mv FILE FILE changed file contents");
}

static void test_name_capacity_is_explicit(void)
{
    fs_init();
    if (!fs_create("12345678901")) fail("11-character name was rejected");
    if (fs_create("123456789012")) fail("12-character name was truncated");
}

static void test_oversized_write_is_failure_atomic(void)
{
    fs_file_t *file;
    char exact[FS_DATAMAX];
    char oversized[FS_DATAMAX + 1];
    uint16_t index;

    fs_init();
    file = fs_create("limit");
    if (!file || !fs_write(file, "safe")) fail("could not seed limit file");

    for (index = 0; index < FS_DATAMAX - 1; index++) exact[index] = 'x';
    exact[FS_DATAMAX - 1] = 0;
    if (!fs_write(file, exact) || file->size != FS_DATAMAX - 1)
        fail("maximum text payload was rejected");

    for (index = 0; index < FS_DATAMAX; index++) oversized[index] = 'y';
    oversized[FS_DATAMAX] = 0;
    if (fs_write(file, oversized)) fail("oversized text payload was accepted");
    if (file->size != FS_DATAMAX - 1 || file->data[0] != 'x')
        fail("rejected write changed the file");
}

static void test_echo_redirection_reports_success_honestly(void)
{
    fs_file_t *file;
    fs_init();
    output_len = 0;
    output[0] = 0;

    dispatch("echo hello > note");

    file = fs_find("note");
    if (!file || file->size != 5 || memcmp(file->data, "hello", 5) != 0)
        fail("echo redirection did not publish the file");
    if (output_len) fail("successful echo redirection printed an error");
}

static void test_failed_redirection_does_not_publish_an_empty_file(void)
{
    char command[FS_DATAMAX + 32];
    uint16_t index;

    memcpy(command, "echo ", 5);
    for (index = 5; index < 5 + FS_DATAMAX; index++) command[index] = 'x';
    memcpy(command + index, " > huge", 8);

    fs_init();
    output_len = 0;
    output[0] = 0;
    dispatch(command);

    if (fs_find("huge")) fail("failed redirection published an empty file");
    if (!strstr(output, "file too large"))
        fail("failed redirection did not explain the size limit");
}

static void test_builtin_fortunes_are_complete_records(void)
{
    fs_file_t *file;
    fs_init();
    file = fs_find("fortune");
    if (!file || !strstr((const char *)file->data, "sophistication"))
        fail("fortune fixture lost a complete advertised record");
    if (file->size > 1 && file->data[file->size - 1] == '\n'
            && file->data[file->size - 2] == '\n')
        fail("fortune fixture contains an empty trailing record");
}

static void release_keys(void)
{
    size_t row;
    for (row = 0; row < 8; row++) uz_host_kbd_rows[row] = 0x1F;
}

static void press_key(uint8_t row, uint8_t bit)
{
    uz_host_kbd_rows[row] &= (uint8_t)~(1u << bit);
}

static void test_keyboard_shift_layers(void)
{
    static const struct {
        uint8_t row;
        uint8_t bit;
        char expected;
    } symbols[] = {
        {3, 0, '!'}, {3, 1, '@'}, {3, 2, '#'}, {3, 3, '$'}, {3, 4, '%'},
        {4, 0, '_'}, {4, 1, ')'}, {4, 2, '('}, {4, 3, '\''}, {4, 4, '&'},
        {5, 0, '"'}, {5, 1, ';'}, {5, 3, ']'}, {5, 4, '['},
        {2, 3, '<'}, {2, 4, '>'},
        {6, 1, '='}, {6, 2, '+'}, {6, 3, '-'}, {6, 4, '^'},
        {0, 1, ':'}, {0, 3, '?'}, {0, 4, '/'},
        {7, 2, '.'}, {7, 3, ','}, {7, 4, '*'},
    };
    size_t index;

    for (index = 0; index < sizeof symbols / sizeof symbols[0]; index++) {
        release_keys();
        press_key(7, 1);
        press_key(symbols[index].row, symbols[index].bit);
        if (keyboard_decode() != symbols[index].expected)
            fail("SYMBOL layer mapping is wrong");
    }

    release_keys();
    press_key(7, 1); /* SYMBOL SHIFT */
    press_key(2, 4); /* T */
    if (keyboard_decode() != '>') fail("SYMBOL+T did not produce >");

    release_keys();
    press_key(7, 1); /* SYMBOL SHIFT */
    press_key(6, 3); /* J */
    if (keyboard_decode() != '-') fail("SYMBOL+J did not produce -");

    release_keys();
    press_key(7, 1); /* SYMBOL SHIFT */
    press_key(7, 2); /* M */
    if (keyboard_decode() != '.') fail("SYMBOL+M did not produce .");

    release_keys();
    press_key(7, 1); /* SYMBOL SHIFT */
    press_key(5, 4); /* Y */
    if (keyboard_decode() != '[') fail("SYMBOL+Y did not produce [");

    release_keys();
    press_key(7, 1); /* SYMBOL SHIFT */
    press_key(5, 3); /* U */
    if (keyboard_decode() != ']') fail("SYMBOL+U did not produce ]");

    release_keys();
    press_key(0, 0); /* CAPS SHIFT */
    press_key(1, 0); /* A */
    if (keyboard_decode() != 'A') fail("CAPS+A did not produce uppercase A");
}

static void test_editor_reserves_a_bounded_cursor_cell(void)
{
    if (editor_line_limit(2, 48) != 29)
        fail("shell prompt line limit is wrong");
    if (editor_line_limit(4, 64) != 27)
        fail("Forth prompt line limit is wrong");
    if (editor_line_limit(31, 64) != 0)
        fail("last-column prompt has writable text space");
    if (editor_line_limit(0, 0) != 0)
        fail("zero-capacity buffer has writable text space");
    if (!editor_start_needs_wrap(31) || !editor_start_needs_wrap(32))
        fail("editor did not wrap an unsafe cursor start");
    if (editor_start_needs_wrap(30))
        fail("editor wrapped a safe cursor start");
}

int main(void)
{
    test_move_to_self_preserves_file();
    test_name_capacity_is_explicit();
    test_oversized_write_is_failure_atomic();
    test_echo_redirection_reports_success_honestly();
    test_failed_redirection_does_not_publish_an_empty_file();
    test_builtin_fortunes_are_complete_records();
    test_keyboard_shift_layers();
    test_editor_reserves_a_bounded_cursor_cell();
    puts("host core tests: PASS");
    return 0;
}
