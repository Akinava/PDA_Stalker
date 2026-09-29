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

void init_spi(void);
void spi_send(uint8_t data);

#endif