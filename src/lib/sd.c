#include <util/delay.h>
#include "sd.h"

// ACMD41 init takes up to 1 s
#define SD_INIT_TIMEOUT_MS      1000
#define SD_RESPONSE_RETRY       10
#define SD_READ_RETRY           50000
// ~32 us per byte at 250 kHz SPI: ~1.6 s, write takes up to 250 ms
#define SD_WRITE_RETRY          50000
#define SD_CSD_SIZE             16

static uint8_t card_type = SD_TYPE_NONE;

static void sd_select(void){
  spi_set_mode(SPI_MODE0);
  SET_LOW(SD_PORT, SD_CS);
}

static void sd_unselect(void){
  SET_HIGH(SD_PORT, SD_CS);
  // card releases MISO only on the next clock, the bus is shared
  spi_send(0xFF);
}

// send command, return R1 (bit 7 set = no response)
static uint8_t sd_command(uint8_t cmd, uint32_t arg){
  // CRC is checked only for CMD0 and CMD8 before init
  uint8_t crc = 0xFF;
  if(cmd == SD_CMD0) crc = 0x95;
  if(cmd == SD_CMD8) crc = 0x87;

  spi_send(0xFF);
  spi_send(cmd | 0x40);
  for(int8_t shift = 24; shift >= 0; shift -= 8){
    spi_send(arg >> shift);
  }
  spi_send(crc);

  uint8_t r1;
  uint8_t retry = SD_RESPONSE_RETRY;
  do{
    r1 = spi_transfer(0xFF);
  }while((r1 & 0x80) && --retry);
  return r1;
}

static uint8_t sd_app_command(uint8_t cmd, uint32_t arg){
  sd_command(SD_CMD55, 0);
  return sd_command(cmd, arg);
}

void init_sd(void){
  // CS high: card is not selected
  SET_DDR_OUT(SD_DDR, SD_CS);
  SET_HIGH(SD_PORT, SD_CS);
  init_spi();
}

// init card, SPI clock must be <= 400 kHz; returns SD_TYPE_*
uint8_t sd_init_card(void){
  uint8_t r1;
  card_type = SD_TYPE_NONE;

  // 80 clocks with CS high: card goes to native mode
  spi_set_mode(SPI_MODE0);
  SET_HIGH(SD_PORT, SD_CS);
  for(uint8_t i = 0; i < 10; i++){
    spi_send(0xFF);
  }

  sd_select();

  // reset, card goes to SPI mode
  if(sd_command(SD_CMD0, 0) != SD_R1_IDLE) goto error;

  // check voltage 2.7-3.6 V, pattern 0xAA; SD v1 does not know CMD8
  uint8_t type;
  uint32_t acmd41_arg;
  r1 = sd_command(SD_CMD8, 0x1AA);
  if(r1 & SD_R1_ILLEGAL_COMMAND){
    type = SD_TYPE_V1;
    acmd41_arg = 0;
  }else{
    uint8_t r7[4];
    for(uint8_t i = 0; i < 4; i++){
      r7[i] = spi_transfer(0xFF);
    }
    if(r7[2] != 0x01 || r7[3] != 0xAA) goto error;
    type = SD_TYPE_V2;
    // host supports high capacity
    acmd41_arg = 0x40000000;
  }

  // wait until card leaves idle state
  for(uint16_t ms = 0; ; ms++){
    if(sd_app_command(SD_ACMD41, acmd41_arg) == SD_R1_READY) break;
    if(ms == SD_INIT_TIMEOUT_MS) goto error;
    _delay_ms(1);
  }

  if(type == SD_TYPE_V2){
    // OCR bit 30 (CCS): high capacity card
    if(sd_command(SD_CMD58, 0) != SD_R1_READY) goto error;
    uint8_t ocr = spi_transfer(0xFF);
    for(uint8_t i = 0; i < 3; i++){
      spi_send(0xFF);
    }
    if(ocr & 0x40) type = SD_TYPE_SDHC;
  }

  // byte addressing cards: sector size 512
  if(type != SD_TYPE_SDHC){
    if(sd_command(SD_CMD16, SD_SECTOR_SIZE) != SD_R1_READY) goto error;
  }

  sd_unselect();
  card_type = type;
  return type;

error:
  sd_unselect();
  return SD_TYPE_NONE;
}

// wait for data token and read len bytes of data block, CS must be low
static uint8_t sd_read_data(uint8_t *buf, uint16_t len){
  uint8_t token;
  uint16_t retry = SD_READ_RETRY;
  do{
    token = spi_transfer(0xFF);
  }while(token == 0xFF && --retry);
  if(token != SD_DATA_START_BLOCK) return 0;

  for(uint16_t i = 0; i < len; i++){
    buf[i] = spi_transfer(0xFF);
  }
  // CRC is not used
  spi_send(0xFF);
  spi_send(0xFF);
  return 1;
}

// byte addressing for not SDHC cards
static uint32_t sd_address(uint32_t sector){
  return card_type == SD_TYPE_SDHC ? sector : sector * SD_SECTOR_SIZE;
}

// read 512 bytes of sector to buf, returns 1 on success
uint8_t sd_read_sector(uint32_t sector, uint8_t *buf){
  if(card_type == SD_TYPE_NONE) return 0;

  sd_select();
  uint8_t ok = sd_command(SD_CMD17, sd_address(sector)) == SD_R1_READY
            && sd_read_data(buf, SD_SECTOR_SIZE);
  sd_unselect();
  return ok;
}

// CMD17 and data token, returns 1 on success (the card is selected then)
uint8_t sd_stream_open(uint32_t sector){
  if(card_type == SD_TYPE_NONE) return 0;

  sd_select();
  if(sd_command(SD_CMD17, sd_address(sector)) != SD_R1_READY) goto error;

  uint8_t token;
  uint16_t retry = SD_READ_RETRY;
  do{
    token = spi_transfer(0xFF);
  }while(token == 0xFF && --retry);
  if(token != SD_DATA_START_BLOCK) goto error;
  return 1;

error:
  sd_unselect();
  return 0;
}

uint8_t sd_stream_read(void){
  return spi_transfer(0xFF);
}

void sd_stream_close(uint16_t left){
  // the rest of the data and CRC (not used)
  for(left += 2; left; left--){
    spi_send(0xFF);
  }
  sd_unselect();
}

// write 512 bytes of buf to sector, returns 1 on success
uint8_t sd_write_sector(uint32_t sector, const uint8_t *buf){
  if(card_type == SD_TYPE_NONE) return 0;

  sd_select();
  if(sd_command(SD_CMD24, sd_address(sector)) != SD_R1_READY) goto error;

  spi_send(0xFF);
  spi_send(SD_DATA_START_BLOCK);
  for(uint16_t i = 0; i < SD_SECTOR_SIZE; i++){
    spi_send(buf[i]);
  }
  // CRC is not used
  spi_send(0xFF);
  spi_send(0xFF);

  if((spi_transfer(0xFF) & SD_DATA_RESPONSE_MASK) != SD_DATA_ACCEPTED) goto error;

  // card holds MISO low while it is busy with writing
  uint16_t retry = SD_WRITE_RETRY;
  while(spi_transfer(0xFF) != 0xFF){
    if(!--retry) goto error;
  }

  sd_unselect();
  return 1;

error:
  sd_unselect();
  return 0;
}

// card size in 512 byte sectors from CSD register, 0 on error
uint32_t sd_get_sectors(void){
  uint8_t csd[SD_CSD_SIZE];
  if(card_type == SD_TYPE_NONE) return 0;

  sd_select();
  uint8_t ok = sd_command(SD_CMD9, 0) == SD_R1_READY
            && sd_read_data(csd, SD_CSD_SIZE);
  sd_unselect();
  if(!ok) return 0;

  if((csd[0] >> 6) == 1){
    // CSD v2 (SDHC / SDXC): size = (C_SIZE + 1) * 512 KB
    uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint16_t)csd[8] << 8) | csd[9];
    return (c_size + 1) << 10;
  }
  // CSD v1: size = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN
  uint8_t read_bl_len = csd[5] & 0x0F;
  uint16_t c_size = ((uint16_t)(csd[6] & 0x03) << 10) | ((uint16_t)csd[7] << 2) | (csd[8] >> 6);
  uint8_t c_size_mult = ((csd[9] & 0x03) << 1) | (csd[10] >> 7);
  return (uint32_t)(c_size + 1) << (c_size_mult + 2 + read_bl_len - 9);
}
