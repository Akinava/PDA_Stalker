#include "pins.h"
#include "macro.h"
#include "fat16_dir.h"

#ifndef FAT16_EDIT_H
#define FAT16_EDIT_H

// changing of FAT16 directories: delete, move, copy, rename.
// FAT16 must be mounted, all FAT copies are updated,
// buf is 512 byte work buffer in all functions

// max depth of nested directories for delete / copy
#define FAT16_EDIT_DEPTH        8

// results
#define FAT16_OK                0
#define FAT16_ERROR_IO          1       // SD read / write error
#define FAT16_ERROR_FULL        2       // no free clusters
#define FAT16_ERROR_DIR_FULL    3       // no free records in the root directory
#define FAT16_ERROR_EXISTS      4       // the name is used in the directory
#define FAT16_ERROR_SAME_DIR    5       // move to the same directory
#define FAT16_ERROR_INSIDE      6       // directory to itself or to its subdirectory
#define FAT16_ERROR_DEPTH       7       // too deep directories
#define FAT16_ERROR_NOT_DIR     8       // a file has the name of the directory

// raw name: 8 + 3 chars with spaces, no dot
#define FAT16_RAW_NAME_SIZE     (FAT16_NAME_SIZE + FAT16_EXT_SIZE)

void fat16_raw_name(const fat16_entry_t *entry, uint8_t *raw);
uint8_t fat16_entry_valid(const fat16_entry_t *entry, uint8_t *buf);
uint8_t fat16_delete(const fat16_entry_t *entry, uint8_t *buf);
uint8_t fat16_move(const fat16_entry_t *entry, uint16_t dir_cluster, uint8_t *buf);
uint8_t fat16_copy(const fat16_entry_t *entry, uint16_t dir_cluster, uint8_t *buf);
uint8_t fat16_rename(const fat16_entry_t *entry, const uint8_t *raw, uint8_t *buf);
uint8_t fat16_mkdir(uint16_t dir_cluster, const uint8_t *raw, uint8_t *buf);
uint8_t fat16_write_file(fat16_entry_t *entry, const uint8_t *data, uint32_t size, uint8_t *buf);
uint8_t fat16_create_file(uint16_t dir_cluster, const uint8_t *raw, const uint8_t *data,
                          uint32_t size, fat16_entry_t *entry, uint8_t *buf);
uint8_t fat16_find(uint16_t dir_cluster, const uint8_t *raw, fat16_entry_t *entry, uint8_t *buf);
uint8_t fat16_name_exists(uint16_t dir_cluster, const uint8_t *raw, uint8_t *buf);
// directory /TMP for temp files, it is created if there is none
uint8_t fat16_tmp_dir(uint16_t *cluster, uint8_t *buf);

// big files: data by sectors to a new chain, then the chain goes to a file
typedef struct {
  uint16_t first;             // 0 - nothing is written
  uint16_t cluster;
  uint8_t sector;             // in the cluster
} fat16_writer_t;

void fat16_writer_open(fat16_writer_t *writer);
uint8_t fat16_writer_prepare(fat16_writer_t *writer, uint8_t *buf);
uint8_t fat16_writer_sector(fat16_writer_t *writer, const uint8_t *data, uint8_t *buf);
uint8_t fat16_free(uint16_t first, uint8_t *buf);
uint8_t fat16_chain_append(uint16_t last, uint16_t *cluster, uint8_t *buf);
uint8_t fat16_set_data(fat16_entry_t *entry, uint16_t first, uint32_t size,
                       uint16_t *old, uint8_t *buf);
uint8_t fat16_replace_data(fat16_entry_t *entry, uint16_t first, uint32_t size, uint8_t *buf);
uint8_t fat16_add_file(uint16_t dir_cluster, const uint8_t *raw, uint16_t first,
                       uint32_t size, fat16_entry_t *entry, uint8_t *buf);

#endif
