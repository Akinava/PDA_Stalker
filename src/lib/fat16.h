#include "pins.h"
#include "macro.h"
#include "sd.h"

#ifndef FAT16_H
#define FAT16_H

// FAT16 on SD card: MBR with one partition, 512 byte sectors

#define FAT16_PARTITION_START   2048    // 1 MB from the card start
#define FAT16_RESERVED_SECTORS  1       // boot sector only
#define FAT16_NUM_FATS          2
#define FAT16_ROOT_ENTRIES      512
#define FAT16_ROOT_SECTORS      (FAT16_ROOT_ENTRIES * 32 / SD_SECTOR_SIZE)
// FAT16 limit: 65524 clusters of 32 KB, bigger cards get a ~2 GB partition
#define FAT16_MAX_SECTORS       0x3FF000UL
#define FAT16_MIN_CLUSTERS      4085
#define FAT16_MAX_CLUSTERS      65524

// partition types in MBR
#define FAT16_TYPE_SMALL        0x04    // < 32 MB
#define FAT16_TYPE              0x06

#define FAT16_MEDIA             0xF8    // fixed disk
#define FAT16_VOLUME_ID         0x50444100UL
#define FAT16_VOLUME_LABEL      "PDA        "   // 11 chars

typedef struct {
  uint32_t start;             // first sector of the partition
  uint32_t sectors;           // partition size
  uint16_t fat_sectors;       // size of one FAT
  uint16_t clusters;
  uint8_t cluster_sectors;
} fat16_layout_t;

// progress of formatting 0..100 %
typedef void (*fat16_progress_t)(uint8_t percent);

uint8_t fat16_layout(uint32_t card_sectors, fat16_layout_t *layout);
uint8_t fat16_format(const fat16_layout_t *layout, uint8_t *buf, fat16_progress_t progress);

#endif
