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
  // place of the record on the card
  uint16_t dir_cluster;       // parent directory
  uint32_t record_sector;
  uint16_t record_offset;
} fat16_entry_t;

// mounted volume
typedef struct {
  uint32_t fat_sector;        // the first FAT
  uint32_t root_sector;
  uint32_t data_sector;
  uint16_t fat_sectors;       // size of one FAT
  uint16_t root_sectors;
  uint16_t max_cluster;       // the last cluster number
  uint8_t cluster_sectors;
  uint8_t num_fats;
} fat16_volume_t;

extern fat16_volume_t fat16_volume;

// called for every record of directory with its place on the card,
// returns 0 to stop the walk
typedef uint8_t (*fat16_visit_t)(const uint8_t *record, uint32_t sector,
                                 uint16_t offset, void *ctx);

// sequential reading of file by FAT chain
typedef struct {
  uint16_t cluster;
  uint32_t sector;
  uint8_t cluster_sector;     // sector number in the current cluster
  uint32_t left;              // bytes left to read
} fat16_file_t;

// position of the random access to a file: cluster number `index` of the chain
typedef struct {
  uint16_t index;
  uint16_t cluster;           // 0 - not set
} fat16_seek_t;

// buf is 512 byte work buffer in all functions
uint8_t fat16_mount(uint8_t *buf);
uint16_t fat16_dir_count(uint16_t dir_cluster, uint8_t *buf);
uint8_t fat16_dir_read(uint16_t dir_cluster, uint16_t first,
                       fat16_entry_t *entries, uint8_t count, uint8_t *buf);
uint8_t fat16_is_dir(const fat16_entry_t *entry);
uint32_t fat16_cluster_sector(uint16_t cluster);
uint16_t fat16_next_cluster(uint16_t cluster, uint8_t *buf);
void fat16_dir_walk(uint16_t dir_cluster, uint8_t *buf, fat16_visit_t visit, void *ctx);
void fat16_entry_from_record(fat16_entry_t *entry, const uint8_t *record);
void fat16_file_open(fat16_file_t *file, uint16_t cluster, uint32_t size);
uint8_t fat16_file_read(fat16_file_t *file, uint8_t *buf, uint16_t *len);
uint8_t fat16_file_sector(uint16_t first, uint32_t index, fat16_seek_t *seek,
                          uint32_t *sector, uint8_t *buf);

#endif
