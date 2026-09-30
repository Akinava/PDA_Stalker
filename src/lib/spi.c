#include "spi.h"

void init_spi(void){
  // Set MOSI, SCK as Output
  SET_DDR_OUT(SPI_DDR, MOSI);
  SET_DDR_OUT(SPI_DDR, SCK);

  // SS (PB2, CS SD) must be output, otherwise SPI can drop to slave mode.
  // High level = SD card is not selected
  SET_DDR_OUT(SD_DDR, SD_CS);
  SET_HIGH(SD_PORT, SD_CS);

  // Clock Frequency: f_OSC/4
  SPCR &= ~(_BV(SPR1)|_BV(SPR0));
  // fosc/128
  SPCR |= _BV(SPR1)|_BV(SPR0);

  // Most significant bit first
  SPCR &= ~(_BV(DORD));
  // Least significant bit first
//  SPCR |= _BV(DORD);

  // SPI mode 0: SCK is low when idle, sample on rising edge
  SPCR &= ~(_BV(CPOL)|_BV(CPHA));

  // Enable SPI, Set as Master
  SPCR |= _BV(SPE)|_BV(MSTR);

  // Doubled Clock Frequency
   SPSR |= _BV(SPI2X); // x2
//   SPSR &= ~(_BV(SPI2X));
}

void spi_send(uint8_t data){
  SPDR = data;
  while(!(SPSR & (1<<SPIF)));
}
