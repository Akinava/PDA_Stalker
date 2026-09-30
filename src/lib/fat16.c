#include <string.h>
#include "fat16.h"

// MBR partition entry
#define MBR_PARTITION_ENTRY     0x1BE
#define MBR_CHS_LBA_ONLY        0xFE, 0xFF, 0xFF    // CHS is not used, LBA only
#define SIGNATURE_OFFSET        0x1FE
// root directory record
#define DIR_ATTR_OFFSET         11
#define DIR_ATTR_VOLUME_ID      0x08

static void put16(uint8_t *buf, uint16_t offset, uint16_t value){
  buf[offset] = value;
  buf[offset + 1] = value >> 8;
}

static void put32(uint8_t *buf, uint16_t offset, uint32_t value){
  put16(buf, offset, value);
  put16(buf, offset + 2, value >> 16);
}

static void put_signature(uint8_t *buf){
  buf[SIGNATURE_OFFSET] = 0x55;
  buf[SIGNATURE_OFFSET + 1] = 0xAA;
}

// cluster size by volume size (Microsoft FAT specification table)
static uint8_t cluster_sectors(uint32_t sectors){
  if(sectors <= 32680UL) return 2;
  if(sectors <= 262144UL) return 4;
  if(sectors <= 524288UL) return 8;
  if(sectors <= 1048576UL) return 16;
  if(sectors <= 2097152UL) return 32;
  return 64;
}

// calculate partition layout for the card, returns 0 if card is too small
uint8_t fat16_layout(uint32_t card_sectors, fat16_layout_t *layout){
  if(card_sectors <= FAT16_PARTITION_START) return 0;

  uint32_t sectors = card_sectors - FAT16_PARTITION_START;
  if(sectors > FAT16_MAX_SECTORS) sectors = FAT16_MAX_SECTORS;
  uint8_t spc = cluster_sectors(sectors);

  // FAT size (Microsoft FAT specification formula)
  uint32_t data = sectors - FAT16_RESERVED_SECTORS - FAT16_ROOT_SECTORS;
  uint32_t per_fat_sector = 256UL * spc + FAT16_NUM_FATS;
  uint32_t fat_sectors = (data + per_fat_sector - 1) / per_fat_sector;
  uint32_t clusters = (data - FAT16_NUM_FATS * fat_sectors) / spc;

  if(clusters < FAT16_MIN_CLUSTERS || clusters > FAT16_MAX_CLUSTERS) return 0;

  layout->start = FAT16_PARTITION_START;
  layout->sectors = sectors;
  layout->fat_sectors = fat_sectors;
  layout->clusters = clusters;
  layout->cluster_sectors = spc;
  return 1;
}

static void make_boot_sector(const fat16_layout_t *layout, uint8_t *buf){
  memset(buf, 0, SD_SECTOR_SIZE);
  // jump to boot code
  buf[0] = 0xEB;
  buf[1] = 0x3C;
  buf[2] = 0x90;
  memcpy(buf + 3, "MSWIN4.1", 8);
  put16(buf, 11, SD_SECTOR_SIZE);
  buf[13] = layout->cluster_sectors;
  put16(buf, 14, FAT16_RESERVED_SECTORS);
  buf[16] = FAT16_NUM_FATS;
  put16(buf, 17, FAT16_ROOT_ENTRIES);
  if(layout->sectors < 0x10000UL){
    put16(buf, 19, layout->sectors);
  }else{
    put32(buf, 32, layout->sectors);
  }
  buf[21] = FAT16_MEDIA;
  put16(buf, 22, layout->fat_sectors);
  put16(buf, 24, 63);       // sectors per track
  put16(buf, 26, 255);      // heads
  put32(buf, 28, layout->start);
  buf[36] = 0x80;           // drive number
  buf[38] = 0x29;           // extended boot signature
  put32(buf, 39, FAT16_VOLUME_ID);
  memcpy(buf + 43, FAT16_VOLUME_LABEL, 11);
  memcpy(buf + 54, "FAT16   ", 8);
  put_signature(buf);
}

static void make_mbr(const fat16_layout_t *layout, uint8_t *buf){
  static const uint8_t chs[] = {MBR_CHS_LBA_ONLY};
  uint8_t *entry = buf + MBR_PARTITION_ENTRY;

  memset(buf, 0, SD_SECTOR_SIZE);
  entry[0] = 0x00;          // not bootable
  memcpy(entry + 1, chs, sizeof(chs));
  entry[4] = layout->sectors < 0x10000UL ? FAT16_TYPE_SMALL : FAT16_TYPE;
  memcpy(entry + 5, chs, sizeof(chs));
  put32(entry, 8, layout->start);
  put32(entry, 12, layout->sectors);
  put_signature(buf);
}

// format card, buf is 512 byte work buffer, progress may be NULL.
// MBR and boot sector are written last: card is not valid until the end
uint8_t fat16_format(const fat16_layout_t *layout, uint8_t *buf, fat16_progress_t progress){
  uint32_t fat_start = layout->start + FAT16_RESERVED_SECTORS;
  uint32_t root_start = fat_start + (uint32_t)FAT16_NUM_FATS * layout->fat_sectors;
  uint16_t total = FAT16_NUM_FATS * layout->fat_sectors + FAT16_ROOT_SECTORS + 2;
  uint16_t done = 0;
  uint8_t percent = 0xFF;

  // FATs and root directory: empty, so all written sectors are zero
  // except the first FAT sector with media and end of chain records
  for(uint32_t sector = fat_start; sector < root_start + FAT16_ROOT_SECTORS; sector++){
    memset(buf, 0, SD_SECTOR_SIZE);
    if(sector < root_start && (sector - fat_start) % layout->fat_sectors == 0){
      put16(buf, 0, 0xFF00 | FAT16_MEDIA);
      put16(buf, 2, 0xFFFF);
    }
    // volume label is the first record of the root directory
    if(sector == root_start){
      memcpy(buf, FAT16_VOLUME_LABEL, 11);
      buf[DIR_ATTR_OFFSET] = DIR_ATTR_VOLUME_ID;
    }
    if(!sd_write_sector(sector, buf)) return 0;

    done++;
    if(progress && (uint8_t)(done * 100UL / total) != percent){
      percent = done * 100UL / total;
      progress(percent);
    }
  }

  make_boot_sector(layout, buf);
  if(!sd_write_sector(layout->start, buf)) return 0;

  make_mbr(layout, buf);
  if(!sd_write_sector(0, buf)) return 0;

  if(progress) progress(100);
  return 1;
}
