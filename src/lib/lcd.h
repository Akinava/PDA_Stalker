#include "pins.h"
#include "macro.h"
#include "spi.h"

#ifndef LCD_H
#define LCD_H

#define LCD_DATA                1       // Data bit
#define LCD_COMMAND             0       // Command bit
#define LCD_CLEAR_SCREEN        0x01    // Clear screen
#define LCD_ADDRESS_RESET       0x02    // The address counter is reset
#define LCD_BASIC_FUNCTION      0x30    // Basic instruction set
#define LCD_EXTEND_FUNCTION     0x34    // Extended instruction set

void init_lcd(void);
void lcd_print(char*);

#endif