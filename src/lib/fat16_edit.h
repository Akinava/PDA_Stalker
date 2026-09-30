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

// raw name: 8 + 3 chars with spaces, no dot
#define FAT16_RAW_NAME_SIZE     (FAT16_NAME_SIZE + FAT16_EXT_SIZE)

void fat16_raw_name(const fat16_entry_t *entry, uint8_t *raw);
uint8_t fat16_entry_valid(const fat16_entry_t *entry, uint8_t *buf);
uint8_t fat16_delete(const fat16_entry_t *entry, uint8_t *buf);
uint8_t fat16_move(const fat16_entry_t *entry, uint16_t dir_cluster, uint8_t *buf);
uint8_t fat16_copy(const fat16_entry_t *entry, uint16_t dir_cluster, uint8_t *buf);
uint8_t fat16_rename(const fat16_entry_t *entry, const uint8_t *raw, uint8_t *buf);

#endif
