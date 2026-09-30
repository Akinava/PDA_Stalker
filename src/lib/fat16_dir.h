#include "pins.h"
#include "macro.h"
#include "sd.h"

#ifndef FAT16_DIR_H
#define FAT16_DIR_H

// reading directories of FAT16 partition (MBR, first partition)

#define FAT16_ROOT_CLUSTER      0       // root directory has no cluster

// directory record
#define FAT16_RECORD_SIZE       32
#define FAT16_NAME_SIZE         8
#define FAT16_EXT_SIZE          3
#define FAT16_ATTR_OFFSET       11
#define FAT16_CLUSTER_OFFSET    26
#define FAT16_SIZE_OFFSET       28
#define FAT16_RECORD_END        0x00    // first byte: no more records
#define FAT16_RECORD_DELETED    0xE5

// attributes
#define FAT16_ATTR_VOLUME_ID    0x08
#define FAT16_ATTR_DIRECTORY    0x10
#define FAT16_ATTR_LFN          0x0F    // long file name record

#define FAT16_END_OF_CHAIN      0xFFF8

typedef struct {
  char name[FAT16_NAME_SIZE + 1 + FAT16_EXT_SIZE + 1];  // "NAME.EXT"
  char ext[FAT16_EXT_SIZE + 1];
  uint8_t attr;
  uint16_t cluster;
  uint32_t size;
} fat16_entry_t;

// sequential reading of file by FAT chain
typedef struct {
  uint16_t cluster;
  uint32_t sector;
  uint8_t cluster_sector;     // sector number in the current cluster
  uint32_t left;              // bytes left to read
} fat16_file_t;

// buf is 512 byte work buffer in all functions
uint8_t fat16_mount(uint8_t *buf);
uint16_t fat16_dir_count(uint16_t dir_cluster, uint8_t *buf);
uint8_t fat16_dir_read(uint16_t dir_cluster, uint16_t first,
                       fat16_entry_t *entries, uint8_t count, uint8_t *buf);
uint8_t fat16_is_dir(const fat16_entry_t *entry);
void fat16_file_open(fat16_file_t *file, uint16_t cluster, uint32_t size);
uint8_t fat16_file_read(fat16_file_t *file, uint8_t *buf, uint16_t *len);

#endif
