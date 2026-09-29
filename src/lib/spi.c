#include "spi.h"

void init_spi(void){
  // Set MOSI, SCK as Output
  SD_DDR |= _BV(MOSI)|_BV(SCK);

  // Clock Frequency: f_OSC/4
  SPCR &= ~(_BV(SPR1)|_BV(SPR0));
  // fosc/128
  SPCR |= _BV(SPR1)|_BV(SPR0);

  // Most significant bit first
  SPCR &= ~(_BV(DORD))
  // Least significant bit first
//  SPCR |= _BV(DORD)

  // Enable SPI, Set as Master
  SPCR &= _BV(SPE)|_BV(MSTR)

  // Doubled Clock Frequency
   SPSR |= _BV(SPI2X); // x2
//   SPSR &= ~(_BV(SPI2X));
}

void spi_send(uint8_t data){
  SPDR = data;
  while(!(SPSR & (1<<SPIF)));
}