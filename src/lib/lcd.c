#include "lcd.h"


void lcd_send_cmd(char c){
  SET_HIGH(LCD_PORT, LCD_CS);
  SPI.transfer(0xF8);
  SPI.transfer(c & 0xF0);
  SPI.transfer(c << 4);
  SET_LOW(LCD_PORT, LCD_CS);
}

void lcd_send_data_(char c){
  SET_HIGH(LCD_PORT, LCD_CS);
  SPI.transfer(0xFA);
  SPI.transfer(c & 0xF0);
  SPI.transfer(c << 4);
  digitalWrite(LCD_CS, LOW);
  SET_LOW(LCD_PORT, LCD_CS);
}

void init_lcd(void){
    init_spi();
    SET_LOW(LCD_PORT, LCD_CS);
}

void lcd_print(char *str){
  while(*str){
    lcd_send_data_(*str>>8);
    lcd_send_data_(*str&0xff);
    str++;
  }
}