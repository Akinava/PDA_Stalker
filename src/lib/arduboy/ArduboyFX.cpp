#include <util/delay.h>
#include "ArduboyFX.h"

// libraries of the PDA are C
extern "C" {
#include "pins.h"
#include "macro.h"
#include "spi.h"
#include "sd.h"
#include "fat16_dir.h"
#include "display.h"
#include "display_gfx.h"
#include "keys.h"
#include "loader.h"
}

// SPI clock of the card while reading: fosc/2 = 8 MHz
#define FX_SPI_FAST()   do{ \
    SPCR &= ~(_BV(SPR1)|_BV(SPR0)); \
    SPSR |= _BV(SPI2X); \
  }while(0)

#define FX_DIR_NAME     "GAMES      "
#define FX_NAME_SIZE    (FAT16_NAME_SIZE + FAT16_EXT_SIZE)
#define FX_POLL_MS      20

namespace {

uint32_t first_sector;      // the data file is in one piece from this sector
uint32_t current_sector;
uint16_t position;          // next byte in the current sector
bool is_open;

uint16_t get16(const uint8_t *p){
  return p[0] | ((uint16_t)p[1] << 8);
}

uint32_t get32(const uint8_t *p){
  return get16(p) | ((uint32_t)get16(p + 2) << 16);
}

struct find_ctx_t {
  const char *name;
  uint8_t directory;
  uint16_t cluster;
  uint32_t size;
};

// look for the record by 8.3 name, returns 0 to stop the walk
uint8_t find_visit(const uint8_t *record, uint32_t sector, uint16_t offset, void *ctx){
  find_ctx_t *find = (find_ctx_t *)ctx;
  if(record[0] == FAT16_RECORD_END) return 0;
  if(record[0] == FAT16_RECORD_DELETED) return 1;
  uint8_t attr = record[FAT16_ATTR_OFFSET];
  if(attr == FAT16_ATTR_LFN || (attr & FAT16_ATTR_VOLUME_ID)) return 1;
  if(((attr & FAT16_ATTR_DIRECTORY) != 0) != find->directory) return 1;
  if(memcmp(record, find->name, FX_NAME_SIZE)) return 1;
  find->cluster = get16(record + FAT16_CLUSTER_OFFSET);
  find->size = get32(record + FAT16_SIZE_OFFSET);
  return 0;
}

uint8_t find(uint16_t dir_cluster, const char *name, uint8_t directory, find_ctx_t *ctx, uint8_t *buf){
  ctx->name = name;
  ctx->directory = directory;
  ctx->cluster = 0;
  fat16_dir_walk(dir_cluster, buf, find_visit, ctx);
  return ctx->cluster != 0;
}

// the data can not be read: message in text mode, C loads the file manager
void error(const char *line1, const char *line2){
  display_gfx_off();
  arduboy_lcd_spi();
  display_print_screen_P(PSTR("FX data:"), line1, line2, PSTR("C - exit"));
  while(1){
    if(keys_read() == C_KEY_PRESSED && loader_is_present()){
      init_spi();
      loader_load_default_app();
    }
    _delay_ms(FX_POLL_MS);
  }
}

void open_sector(uint32_t sector){
  FX_SPI_FAST();
  current_sector = sector;
  position = 0;
  is_open = sd_stream_open(sector);
  if(!is_open) arduboy_lcd_spi();
}

}  // namespace

void FX::release(void){
  if(!is_open) return;
  sd_stream_close(SD_SECTOR_SIZE - position);
  is_open = false;
  arduboy_lcd_spi();
}

// the frame buffer is the work buffer of FAT16 functions (it is cleared then)
void FX::begin(uint16_t dataPage, uint16_t savePage){
  uint8_t *buf = Arduboy::getBuffer();
  find_ctx_t dir, file;

  // card init needs SPI clock <= 400 kHz (init_spi: fosc/64)
  init_sd();
  if(sd_init_card() == SD_TYPE_NONE || !fat16_mount(buf)){
    error(PSTR("no SD card"), PSTR(""));
  }
  if(!find(FAT16_ROOT_CLUSTER, FX_DIR_NAME, 1, &dir, buf)
      || !find(dir.cluster, ARDUBOY_FX_NAME, 0, &file, buf)){
    error(PSTR("no file /GAMES/"), PSTR(ARDUBOY_FX_NAME));
  }

  // the clusters of the file must go one by one
  uint32_t cluster_bytes = (uint32_t)fat16_volume.cluster_sectors * SD_SECTOR_SIZE;
  uint16_t clusters = (file.size + cluster_bytes - 1) / cluster_bytes;
  uint16_t cluster = file.cluster;
  for(uint16_t i = 1; i < clusters; i++){
    uint16_t next = fat16_next_cluster(cluster, buf);
    if(next != cluster + 1) error(PSTR("file is fragmen-"), PSTR("ted, copy again"));
    cluster = next;
  }
  first_sector = fat16_cluster_sector(file.cluster);

  Arduboy::clear();
  arduboy_lcd_spi();
  arduboy_bus_release = release;
}

void FX::seekData(uint24_t address){
  uint32_t sector = first_sector + (address >> 9);
  uint16_t offset = address & (SD_SECTOR_SIZE - 1);

  // forward in the open sector: skip bytes, else open the sector
  if(!is_open || sector != current_sector || offset < position){
    release();
    open_sector(sector);
  }
  for(; position < offset; position++){
    sd_stream_read();
  }
}

uint8_t FX::readPendingUInt8(void){
  if(position == SD_SECTOR_SIZE){
    uint32_t next = current_sector + 1;
    release();
    open_sector(next);
  }
  // read error: 0xFF as erased flash
  if(!is_open) return 0xFF;
  position++;
  return sd_stream_read();
}

// FX data is big endian
uint16_t FX::readPendingUInt16(void){
  uint16_t value = (uint16_t)readPendingUInt8() << 8;
  return value | readPendingUInt8();
}

uint16_t FX::readPendingLastUInt16(void){
  return readPendingUInt16();
}

uint24_t FX::readPendingUInt24(void){
  uint24_t value = (uint24_t)readPendingUInt16() << 8;
  return value | readPendingUInt8();
}

uint32_t FX::readPendingUInt32(void){
  uint32_t value = (uint32_t)readPendingUInt16() << 16;
  return value | readPendingUInt16();
}

uint8_t FX::readEnd(void){
  return readPendingUInt8();
}

uint8_t FX::readIndexedUInt8(uint24_t address, uint8_t index){
  seekData(address + index);
  return readPendingUInt8();
}

uint16_t FX::readIndexedUInt16(uint24_t address, uint8_t index){
  seekData(address + (uint16_t)index * 2);
  return readPendingUInt16();
}

uint24_t FX::readIndexedUInt24(uint24_t address, uint8_t index){
  seekData(address + (uint16_t)index * 3);
  return readPendingUInt24();
}

uint32_t FX::readIndexedUInt32(uint24_t address, uint8_t index){
  seekData(address + (uint16_t)index * 4);
  return readPendingUInt32();
}

void FX::drawBitmap(int16_t x, int16_t y, uint24_t address, uint8_t frame, uint8_t mode){
  seekData(address);
  int16_t width = readPendingUInt16();
  int16_t height = readPendingUInt16();
  if(x + width <= 0 || x >= WIDTH || y + height <= 0 || y >= HEIGHT) return;

  bool masked = mode & dbmMasked;
  uint8_t pages = (height + 7) / 8;
  uint8_t bytes = masked ? 2 : 1;
  uint24_t data = address + 4 + (uint24_t)frame * pages * width * bytes;

  uint8_t *buffer = Arduboy::getBuffer();
  uint8_t y_offset = y & 7;
  int8_t first_page = y >> 3;
  // visible columns
  int16_t left = x < 0 ? -x : 0;
  int16_t right = x + width > WIDTH ? WIDTH - x : width;

  for(uint8_t p = 0; p < pages; p++){
    int8_t page = first_page + p;
    if(page >= HEIGHT / 8) break;
    if(page + 1 < 0) continue;

    // the last page: only the rows of the bitmap
    uint8_t rows = height - p * 8;
    uint8_t row_mask = rows >= 8 ? 0xFF : 0xFF >> (8 - rows);

    seekData(data + ((uint24_t)p * width + left) * bytes);
    for(int16_t i = left; i < right; i++){
      uint8_t image = readPendingUInt8();
      uint8_t image_mask = masked ? readPendingUInt8() : 0xFF;
      image_mask &= row_mask;
      image &= image_mask;

      uint16_t image16 = (uint16_t)image << y_offset;
      uint16_t mask16 = (uint16_t)image_mask << y_offset;
      int16_t index = page * WIDTH + x + i;
      if(page >= 0){
        uint8_t *b = &buffer[index];
        *b = (*b & ~(uint8_t)mask16) | (uint8_t)image16;
      }
      if(y_offset && page + 1 < HEIGHT / 8){
        uint8_t *b = &buffer[index + WIDTH];
        *b = (*b & ~(uint8_t)(mask16 >> 8)) | (uint8_t)(image16 >> 8);
      }
    }
  }
}

void FX::display(void){
  release();
  Arduboy::display();
}

void FX::display(bool clear){
  display();
  if(clear) Arduboy::clear();
}
