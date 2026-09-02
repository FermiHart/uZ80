/* Pure line-geometry contract shared by the target editor and host tests.
 * SPDX-License-Identifier: Unlicense */

#include "uz80.h"

uint8_t editor_start_needs_wrap(uint8_t start_x)
{
    return start_x >= COLS - 1;
}

uint8_t editor_line_limit(uint8_t start_x, uint8_t capacity)
{
    uint8_t screen_room;
    uint8_t buffer_room;

    if (!capacity || editor_start_needs_wrap(start_x)) return 0;
    screen_room = (uint8_t)(COLS - 1 - start_x);
    buffer_room = (uint8_t)(capacity - 1);
    return buffer_room < screen_room ? buffer_room : screen_room;
}
