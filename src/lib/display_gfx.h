#include <avr/pgmspace.h>
#include "display.h"

#ifndef DISPLAY_GFX_H
#define DISPLAY_GFX_H

// ST7920 128x64 graphics: text rows with 5x7 font in 6x8 cells.
// No frame buffer: every text row is rendered directly to GDRAM.
// Text mode functions of display.h must not be used between
// display_gfx_on and display_gfx_off (extended instruction set is on)

#define DISPLAY_GFX_ROWS        8
#define DISPLAY_GFX_COLS        21
#define DISPLAY_GFX_CELL_WIDTH  6
#define DISPLAY_GFX_CELL_HEIGHT 8
#define DISPLAY_GFX_NO_CURSOR   0xFF

// special glyphs of the font
#define DISPLAY_GFX_NEWLINE     0x80    // '\n'
#define DISPLAY_GFX_TAB         0x81    // '\t'
#define DISPLAY_GFX_CR          0x82    // '\r'
#define DISPLAY_GFX_UNKNOWN     0x83    // other not printable byte
// cyrillic А..Я, а..я (U+0410..U+044F), then Ё, ё
#define DISPLAY_GFX_CYRILLIC    0x84
#define DISPLAY_GFX_CYRILLIC_LETTERS 64
#define DISPLAY_GFX_YO_UPPER    (DISPLAY_GFX_CYRILLIC + DISPLAY_GFX_CYRILLIC_LETTERS)
#define DISPLAY_GFX_YO_LOWER    (DISPLAY_GFX_YO_UPPER + 1)
#define DISPLAY_GFX_LAST        DISPLAY_GFX_YO_LOWER

void display_gfx_on(void);
void display_gfx_off(void);
void display_gfx_clear(void);
uint8_t display_gfx_glyph(uint16_t code);
// one pixel line of 128 pixels (16 bytes, the high bit is the left pixel)
void display_gfx_line(uint8_t y, const uint8_t *bytes);
void display_gfx_row(uint8_t row, const char *text, uint8_t len, uint8_t cursor, uint8_t invert);

#endif
