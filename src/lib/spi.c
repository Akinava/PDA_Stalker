#include "spi.h"

void init_spi(void){
  // Set MOSI, SCK as Output
  SD_DDR |= _BV(MOSI)|_BV(SCK);
  // Enable SPI, Set as Master
  SPCR &= _BV(SPE)|_BV(MSTR)

  // Clock Frequency: f_OSC/4
  SPCR &= ~(_BV(SPR1)|_BV(SPR0));
  // Doubled Clock Frequency: f_OSC/2
  SPSR |= _BV(SPI2X);
}

void spi_send(uint8_t data){
  SPDR = data;
  while(!(SPSR & (1<<SPIF)));
}