#include "pins.h"
#include "macro.h"
#include "spi.h"

#ifndef RADIO_H
#define RADIO_H

// NRF24L01, SPI mode 0, CSN active low, CE active high

// commands
#define RADIO_R_REGISTER        0x00    // | register
#define RADIO_W_REGISTER        0x20    // | register
#define RADIO_REGISTER_MASK     0x1F
#define RADIO_R_RX_PAYLOAD      0x61
#define RADIO_W_TX_PAYLOAD      0xA0
#define RADIO_FLUSH_TX          0xE1
#define RADIO_FLUSH_RX          0xE2
#define RADIO_NOP               0xFF

// registers
#define RADIO_CONFIG            0x00
#define RADIO_EN_AA             0x01
#define RADIO_EN_RXADDR         0x02
#define RADIO_SETUP_AW          0x03
#define RADIO_SETUP_RETR        0x04
#define RADIO_RF_CH             0x05
#define RADIO_RF_SETUP          0x06
#define RADIO_STATUS            0x07
#define RADIO_OBSERVE_TX        0x08
#define RADIO_RPD               0x09
#define RADIO_RX_ADDR_P0        0x0A
#define RADIO_TX_ADDR           0x10
#define RADIO_RX_PW_P0          0x11
#define RADIO_FIFO_STATUS       0x17

#define RADIO_RF_CH_DEFAULT     0x02

// NRF24L01 needs 100 ms after power on before the first command
void init_radio(void);
uint8_t radio_command(uint8_t command);
uint8_t radio_read_register(uint8_t reg);
uint8_t radio_write_register(uint8_t reg, uint8_t value);
void radio_read_buffer(uint8_t reg, uint8_t *buf, uint8_t len);
void radio_write_buffer(uint8_t reg, const uint8_t *buf, uint8_t len);
uint8_t radio_is_present(void);

#endif
