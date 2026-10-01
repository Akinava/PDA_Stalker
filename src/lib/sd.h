#include "pins.h"
#include "macro.h"
#include "spi.h"

#ifndef SD_H
#define SD_H

// SD card in SPI mode, SPI mode 0, CS active low

#define SD_SECTOR_SIZE          512

// commands
#define SD_CMD0                 0       // GO_IDLE_STATE
#define SD_CMD8                 8       // SEND_IF_COND
#define SD_CMD9                 9       // SEND_CSD
#define SD_CMD16                16      // SET_BLOCKLEN
#define SD_CMD17                17      // READ_SINGLE_BLOCK
#define SD_CMD24                24      // WRITE_BLOCK
#define SD_CMD55                55      // APP_CMD
#define SD_CMD58                58      // READ_OCR
#define SD_ACMD41               41      // SD_SEND_OP_COND

// R1 response
#define SD_R1_READY             0x00
#define SD_R1_IDLE              0x01
#define SD_R1_ILLEGAL_COMMAND   0x04

#define SD_DATA_START_BLOCK     0xFE
#define SD_DATA_RESPONSE_MASK   0x1F
#define SD_DATA_ACCEPTED        0x05

// card types, result of sd_init_card
#define SD_TYPE_NONE            0       // no card or init error
#define SD_TYPE_V1              1       // SD v1, byte addressing
#define SD_TYPE_V2              2       // SD v2 standard capacity, byte addressing
#define SD_TYPE_SDHC            3       // SDHC / SDXC, sector addressing

// MBR
#define SD_MBR_PARTITION_TYPE   0x1C2   // type of the first partition
#define SD_MBR_SIGNATURE        0x1FE   // 0x55 0xAA

void init_sd(void);
uint8_t sd_init_card(void);
uint8_t sd_read_sector(uint32_t sector, uint8_t *buf);
uint8_t sd_write_sector(uint32_t sector, const uint8_t *buf);
uint32_t sd_get_sectors(void);

// reading of a sector byte by byte without buffer: the card stays selected
// (SPI bus is busy) from sd_stream_open to sd_stream_close.
// sd_stream_close reads the rest of the sector, left = bytes not read
uint8_t sd_stream_open(uint32_t sector);
uint8_t sd_stream_read(void);
void sd_stream_close(uint16_t left);

#endif
