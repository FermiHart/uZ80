/* UZ80 keyboard matrix decoding, kept separate from the line editor for tests.
 * SPDX-License-Identifier: Unlicense */

#include "uz80.h"

#define K_BS    8
#define K_UP    11
#define K_DOWN  10
#define K_KILL  21

static const char KEYMAP[40] = {
     0 ,'z','x','c','v',          /* CAPS,Z,X,C,V       */
    'a','s','d','f','g',
    'q','w','e','r','t',
    '1','2','3','4','5',
    '0','9','8','7','6',
    'p','o','i','u','y',
     13,'l','k','j','h',          /* ENTER,L,K,J,H       */
    ' ', 0 ,'m','n','b',          /* SPACE,SYMBOL,M,N,B  */
};

static char symbol_of(char base)
{
    switch (base) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '0': return '_';
    case '9': return ')';
    case '8': return '(';
    case '7': return '\'';
    case '6': return '&';
    case 'p': return '"';
    case 'o': return ';';
    case 'u': return ']';
    case 'y': return '[';
    case 'r': return '<';
    case 't': return '>';
    case 'l': return '=';
    case 'k': return '+';
    case 'j': return '-';
    case 'h': return '^';
    case 'z': return ':';
    case 'c': return '?';
    case 'v': return '/';
    case 'b': return '*';
    case 'n': return ',';
    case 'm': return '.';
    default:  return 0;
    }
}

char keyboard_decode(void)
{
    uint8_t r, b;
    uint8_t caps = !(UZ_KBD_ROW(0) & 0x01);
    uint8_t symbol = !(UZ_KBD_ROW(7) & 0x02);
    char base = 0;

    for (r = 0; r < 8; r++) {
        uint8_t value = UZ_KBD_ROW(r);
        for (b = 0; b < 5; b++) {
            if (((value >> b) & 1) == 0) {
                char key = KEYMAP[r * 5 + b];
                if (key) base = key;
            }
        }
    }
    if (!base) return 0;

    if (symbol) return symbol_of(base);
    if (caps) {
        if (base == '0') return K_BS;
        if (base == '6') return K_DOWN;
        if (base == '7') return K_UP;
        if (base == '1') return K_KILL;
        if (base >= 'a' && base <= 'z') return (char)(base - ('a' - 'A'));
    }
    return base;
}
