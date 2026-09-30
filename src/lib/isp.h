#include "pins.h"
#include "macro.h"
#include "spi.h"

#ifndef ISP_H
#define ISP_H

// AVR serial programming (ISP) of the other board through the connector:
// MOSI, MISO, SCK of the SPI bus and ISP_RESET_PIN as RESET of the target

// target: ATmega328P
#define ISP_SIGNATURE_0         0x1E
#define ISP_SIGNATURE_1         0x95
#define ISP_SIGNATURE_2         0x0F
#define ISP_PAGE_SIZE           128     // bytes
#define ISP_FLASH_SIZE          0x8000  // bytes

// fuses, index for isp_read_fuse / isp_write_fuse
#define ISP_FUSE_HIGH           0
#define ISP_FUSE_LOW            1
#define ISP_FUSE_EXT            2
#define ISP_FUSES               3

void isp_begin(void);
void isp_end(void);
uint8_t isp_enter(void);
void isp_pause(void);
uint8_t isp_check_signature(void);
uint8_t isp_chip_erase(void);
uint8_t isp_read_fuse(uint8_t fuse);
uint8_t isp_write_fuse(uint8_t fuse, uint8_t value);
void isp_load_word(uint8_t word_in_page, uint16_t word);
uint8_t isp_write_page(uint16_t byte_address);
uint8_t isp_read_flash(uint16_t byte_address);

#endif
