#include <util/delay.h>
#include "display.h"

// busy flag can not be read in serial mode, so wait max execution time
#define DISPLAY_COMMAND_DELAY_US 72
#define DISPLAY_CLEAR_DELAY_MS   2

static void display_send(uint8_t sync, uint8_t byte){
  // ST7920 works in SPI mode 3: SCK is high when idle, sample on rising edge
  spi_set_mode(SPI_MODE3);
  SET_HIGH(LCD_PORT, LCD_CS);
  spi_send(sync);
  spi_send(byte & 0xF0);  // high nibble
  spi_send(byte << 4);    // low nibble
  SET_LOW(LCD_PORT, LCD_CS);
  _delay_us(DISPLAY_COMMAND_DELAY_US);
}

void display_send_command(uint8_t command){
  display_send(DISPLAY_SYNC_COMMAND, command);
}

void display_send_data(uint8_t data){
  display_send(DISPLAY_SYNC_DATA, data);
}

void init_display(void){
  SET_DDR_OUT(LCD_DDR, LCD_CS);
  SET_LOW(LCD_PORT, LCD_CS);
  init_spi();

  // wait for display power on
  _delay_ms(100);

  display_send_command(DISPLAY_BASIC_FUNCTION);
  display_send_command(DISPLAY_BASIC_FUNCTION);
  display_send_command(DISPLAY_ON);
  display_clear();
  display_send_command(DISPLAY_ENTRY_MODE);
}

void display_clear(void){
  display_send_command(DISPLAY_CLEAR);
  _delay_ms(DISPLAY_CLEAR_DELAY_MS);
}

void display_home(void){
  display_send_command(DISPLAY_HOME);
  _delay_ms(DISPLAY_CLEAR_DELAY_MS);
}

// one DDRAM address holds 2 chars, so col is rounded down to even
void display_set_cursor(uint8_t row, uint8_t col){
  static const uint8_t lines[DISPLAY_ROWS] = {
    DISPLAY_LINE_0, DISPLAY_LINE_1, DISPLAY_LINE_2, DISPLAY_LINE_3
  };
  if(row >= DISPLAY_ROWS) row = DISPLAY_ROWS - 1;
  if(col >= DISPLAY_COLS) col = DISPLAY_COLS - 1;
  display_send_command(lines[row] + col / 2);
}

void display_putc(char c){
  display_send_data((uint8_t)c);
}

void display_print(const char *str){
  while(*str){
    display_putc(*str++);
  }
}

void display_print_at(uint8_t row, uint8_t col, const char *str){
  display_set_cursor(row, col);
  display_print(str);
}

// print str on the whole line, the rest of the line is filled with spaces
void display_print_line(uint8_t row, const char *str){
  uint8_t col = 0;
  display_set_cursor(row, 0);
  for(; *str && col < DISPLAY_COLS; col++){
    display_putc(*str++);
  }
  for(; col < DISPLAY_COLS; col++){
    display_putc(' ');
  }
}
