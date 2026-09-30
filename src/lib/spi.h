#include "pins.h"
#include "macro.h"

#ifndef SPI_H
#define SPI_H

// SPR1 SPR0  SPI2X Freq
//   0    0     1   fosc/2
//   0    0     0   fosc/4
//   0    1     1   fosc/8
//   0    1     0   fosc/16
//   1    0     1   fosc/32
//   1    0     0   fosc/64
//   1    1     1   fosc/64
//   1    1     0   fosc/128

#define SPI_SET SET_LOW
#define SPI_UNSET SET_HIGH

// SPI modes (CPOL CPHA): devices on the bus need different modes,
// so set the mode before each transaction
#define SPI_MODE0 0                         // NRF24L01, SD card
#define SPI_MODE1 _BV(CPHA)
#define SPI_MODE2 _BV(CPOL)
#define SPI_MODE3 (_BV(CPOL)|_BV(CPHA))     // ST7920

void init_spi(void);
void spi_send(uint8_t data);
uint8_t spi_transfer(uint8_t data);
void spi_set_mode(uint8_t mode);

#endif