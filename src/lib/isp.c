#include <util/delay.h>
#include "isp.h"

// instructions (ATmega328P datasheet, Serial Programming Instruction Set)
#define ISP_CHIP_ERASE          0xAC, 0x80
#define ISP_POLL_READY          0xF0, 0x00
#define ISP_READ_SIGNATURE      0x30, 0x00
#define ISP_LOAD_LOW_BYTE       0x40, 0x00
#define ISP_LOAD_HIGH_BYTE      0x48, 0x00
#define ISP_WRITE_PAGE          0x4C
#define ISP_READ_LOW_BYTE       0x20
#define ISP_READ_HIGH_BYTE      0x28
#define ISP_ECHO                0x53

// positive RESET pulse: >= 2 target clocks (128 kHz clock: 16 us)
#define ISP_RESET_PULSE_US      50
// wait after the pulse before Programming Enable
#define ISP_ENTER_DELAY_MS      20
#define ISP_ENTER_RETRY         4
// ~256 us per poll at 125 kHz: chip erase 9 ms, page / fuse 4.5 ms
#define ISP_READY_RETRY         1000

// read, write commands of fuses: high, low, ext
static const uint8_t fuse_read[ISP_FUSES][2] = {{0x58, 0x08}, {0x50, 0x00}, {0x50, 0x08}};
static const uint8_t fuse_write[ISP_FUSES] = {0xA8, 0xA0, 0xA4};

// 4 byte instruction, returns the 4th byte of the answer
static uint8_t isp_command(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3){
  spi_send(b0);
  spi_send(b1);
  spi_send(b2);
  return spi_transfer(b3);
}

// RESET high for a moment: target leaves programming mode, but does not
// start the program because of its reset time-out
static void reset_pulse(void){
  SET_HIGH(ISP_RESET_PORT, ISP_RESET_PIN);
  _delay_us(ISP_RESET_PULSE_US);
  SET_LOW(ISP_RESET_PORT, ISP_RESET_PIN);
}

static uint8_t wait_ready(void){
  for(uint16_t retry = 0; retry < ISP_READY_RETRY; retry++){
    if(!(isp_command(ISP_POLL_READY, 0x00, 0x00) & 1)) return 1;
  }
  return 0;
}

// hold the target in reset, SPI is slow for any target clock:
// SCK must be < target clock / 4, new chip runs at 1 MHz
void init_isp(void){
  init_spi();
  spi_set_mode(SPI_MODE0);
  // fosc/128 = 125 kHz
  SPSR &= ~_BV(SPI2X);
  SET_LOW(ISP_RESET_PORT, ISP_RESET_PIN);
  SET_DDR_OUT(ISP_RESET_DDR, ISP_RESET_PIN);
}

// release the target: it starts its program
void isp_release(void){
  SET_HIGH(ISP_RESET_PORT, ISP_RESET_PIN);
  // SPI speed of init_spi
  SPSR |= _BV(SPI2X);
}

// enter programming mode, returns 0 if the target does not answer
uint8_t isp_enter(void){
  spi_set_mode(SPI_MODE0);
  for(uint8_t retry = 0; retry < ISP_ENTER_RETRY; retry++){
    // SCK is low: pulse on RESET synchronizes the target
    reset_pulse();
    _delay_ms(ISP_ENTER_DELAY_MS);
    spi_send(0xAC);
    spi_send(0x53);
    uint8_t echo = spi_transfer(0x00);
    spi_send(0x00);
    if(echo == ISP_ECHO) return 1;
  }
  return 0;
}

// leave programming mode, target stays in reset: other devices on SPI bus
// can be used, the target does not take their traffic as instructions
void isp_pause(void){
  reset_pulse();
}

uint8_t isp_check_signature(void){
  static const uint8_t signature[] = {ISP_SIGNATURE_0, ISP_SIGNATURE_1, ISP_SIGNATURE_2};
  for(uint8_t i = 0; i < sizeof(signature); i++){
    if(isp_command(ISP_READ_SIGNATURE, i, 0x00) != signature[i]) return 0;
  }
  return 1;
}

// erase flash and EEPROM, lock bits
uint8_t isp_chip_erase(void){
  isp_command(ISP_CHIP_ERASE, 0x00, 0x00);
  return wait_ready();
}

uint8_t isp_read_fuse(uint8_t fuse){
  return isp_command(fuse_read[fuse][0], fuse_read[fuse][1], 0x00, 0x00);
}

uint8_t isp_write_fuse(uint8_t fuse, uint8_t value){
  isp_command(0xAC, fuse_write[fuse], 0x00, value);
  return wait_ready();
}

// word to the page buffer, low byte first
void isp_load_word(uint8_t word_in_page, uint16_t word){
  isp_command(ISP_LOAD_LOW_BYTE, word_in_page, word);
  isp_command(ISP_LOAD_HIGH_BYTE, word_in_page, word >> 8);
}

// write the page buffer to the page with byte_address
uint8_t isp_write_page(uint16_t byte_address){
  uint16_t word_address = byte_address >> 1;
  isp_command(ISP_WRITE_PAGE, word_address >> 8, word_address, 0x00);
  return wait_ready();
}

uint8_t isp_read_flash(uint16_t byte_address){
  uint16_t word_address = byte_address >> 1;
  uint8_t command = byte_address & 1 ? ISP_READ_HIGH_BYTE : ISP_READ_LOW_BYTE;
  return isp_command(command, word_address >> 8, word_address, 0x00);
}
