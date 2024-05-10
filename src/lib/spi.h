#include "pins.h"
#include "macro.h"

#ifndef SPI_H
#define SPI_H

#define SPI_SET SET_LOW
#define SPI_UNSET SET_HIGH

void init_spi(void);
void spi_send(uint8_t data);

#endif