#include "pins.h"
#include "macro.h"
#include "spi.h"

#ifndef DISPLAY_H
#define DISPLAY_H

// ST7920 128x64, serial mode (PSB = GND)
// RS  -> CS   (LCD_CS, active high)
// R/W -> MOSI (SID)
// E   -> SCK  (SCLK)

// text mode: 4 lines x 16 chars (8x16 font)
#define DISPLAY_ROWS            4
#define DISPLAY_COLS            16

// serial sync bytes: 11111 RW RS 0
#define DISPLAY_SYNC_COMMAND    0xF8    // RW = 0, RS = 0
#define DISPLAY_SYNC_DATA       0xFA    // RW = 0, RS = 1

// basic instruction set
#define DISPLAY_CLEAR           0x01    // Clear screen, address counter = 0
#define DISPLAY_HOME            0x02    // Address counter = 0
#define DISPLAY_ENTRY_MODE      0x06    // Cursor moves right, no shift
#define DISPLAY_ON              0x0C    // Display on, cursor off, blink off
#define DISPLAY_OFF             0x08    // Display off
#define DISPLAY_BASIC_FUNCTION  0x30    // 8 bit, basic instruction set
#define DISPLAY_EXTEND_FUNCTION 0x34    // 8 bit, extended instruction set
#define DISPLAY_SET_DDRAM       0x80    // Set DDRAM address (text position)

// DDRAM addresses of text lines
#define DISPLAY_LINE_0          0x80
#define DISPLAY_LINE_1          0x90
#define DISPLAY_LINE_2          0x88
#define DISPLAY_LINE_3          0x98

void init_display(void);
void display_send_command(uint8_t command);
void display_send_data(uint8_t data);
void display_clear(void);
void display_home(void);
void display_set_cursor(uint8_t row, uint8_t col);
void display_putc(char c);
void display_print(const char *str);
void display_print_at(uint8_t row, uint8_t col, const char *str);

#endif
