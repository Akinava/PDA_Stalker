#include <string.h>
#include "fat16_dir.h"

#define MBR_PARTITION_START     0x1C6
#define SIGNATURE_OFFSET        0x1FE

// boot sector (BPB)
#define BPB_BYTES_PER_SECTOR    11
#define BPB_CLUSTER_SECTORS     13
#define BPB_RESERVED_SECTORS    14
#define BPB_NUM_FATS            16
#define BPB_ROOT_ENTRIES        17
#define BPB_TOTAL_SECTORS_16    19
#define BPB_FAT_SECTORS         22
#define BPB_TOTAL_SECTORS_32    32

fat16_volume_t fat16_volume;

static uint16_t get16(const uint8_t *buf, uint16_t offset){
  return buf[offset] | ((uint16_t)buf[offset + 1] << 8);
}

static uint32_t get32(const uint8_t *buf, uint16_t offset){
  return get16(buf, offset) | ((uint32_t)get16(buf, offset + 2) << 16);
}

static uint8_t has_signature(const uint8_t *buf){
  return buf[SIGNATURE_OFFSET] == 0x55 && buf[SIGNATURE_OFFSET + 1] == 0xAA;
}

// find FAT16 partition, returns 1 on success
uint8_t fat16_mount(uint8_t *buf){
  if(!sd_read_sector(0, buf) || !has_signature(buf)) return 0;
  uint32_t start = get32(buf, MBR_PARTITION_START);

  if(!sd_read_sector(start, buf) || !has_signature(buf)) return 0;
  // FAT32 has 0 in 16 bit FAT size
  uint16_t fat_sectors = get16(buf, BPB_FAT_SECTORS);
  if(get16(buf, BPB_BYTES_PER_SECTOR) != SD_SECTOR_SIZE || !fat_sectors) return 0;

  fat16_volume_t *v = &fat16_volume;
  v->cluster_sectors = buf[BPB_CLUSTER_SECTORS];
  v->num_fats = buf[BPB_NUM_FATS];
  v->fat_sectors = fat_sectors;
  v->fat_sector = start + get16(buf, BPB_RESERVED_SECTORS);
  v->root_sector = v->fat_sector + (uint32_t)v->num_fats * fat_sectors;
  v->root_sectors = get16(buf, BPB_ROOT_ENTRIES) * FAT16_RECORD_SIZE / SD_SECTOR_SIZE;
  v->data_sector = v->root_sector + v->root_sectors;
  if(!v->cluster_sectors) return 0;

  uint32_t total = get16(buf, BPB_TOTAL_SECTORS_16);
  if(!total) total = get32(buf, BPB_TOTAL_SECTORS_32);
  // clusters are numbered from 2
  v->max_cluster = (total - (v->data_sector - start)) / v->cluster_sectors + 1;
  return 1;
}

uint32_t fat16_cluster_sector(uint16_t cluster){
  return fat16_volume.data_sector + (uint32_t)(cluster - 2) * fat16_volume.cluster_sectors;
}

// next cluster of the chain, 0 at the end
uint16_t fat16_next_cluster(uint16_t cluster, uint8_t *buf){
  // 256 records of 2 bytes in FAT sector
  if(!sd_read_sector(fat16_volume.fat_sector + (cluster >> 8), buf)) return 0;
  uint16_t next = get16(buf, (cluster & 0xFF) * 2);
  if(next < 2 || next >= FAT16_END_OF_CHAIN) return 0;
  return next;
}

// "." and ".." records
static uint8_t is_dot(const uint8_t *record){
  return record[0] == '.';
}

// go through all records of directory, the end record included
void fat16_dir_walk(uint16_t dir_cluster, uint8_t *buf, fat16_visit_t visit, void *ctx){
  uint16_t cluster = dir_cluster;
  uint32_t sector;
  uint16_t sectors;
  if(cluster == FAT16_ROOT_CLUSTER){
    sector = fat16_volume.root_sector;
    sectors = fat16_volume.root_sectors;
  }else{
    sector = fat16_cluster_sector(cluster);
    sectors = fat16_volume.cluster_sectors;
  }

  while(1){
    for(; sectors; sectors--, sector++){
      if(!sd_read_sector(sector, buf)) return;
      for(uint16_t offset = 0; offset < SD_SECTOR_SIZE; offset += FAT16_RECORD_SIZE){
        if(!visit(buf + offset, sector, offset, ctx)) return;
      }
    }
    // root directory has fixed size, subdirectory goes by FAT chain
    if(cluster == FAT16_ROOT_CLUSTER) return;
    cluster = fat16_next_cluster(cluster, buf);
    if(!cluster) return;
    sector = fat16_cluster_sector(cluster);
    sectors = fat16_volume.cluster_sectors;
  }
}

typedef struct {
  fat16_visit_t visit;
  void *ctx;
} visible_ctx_t;

// skip deleted, long names, volume label, dots
static uint8_t visible_visit(const uint8_t *record, uint32_t sector, uint16_t offset, void *ctx){
  visible_ctx_t *visible = ctx;
  uint8_t attr = record[FAT16_ATTR_OFFSET];
  if(record[0] == FAT16_RECORD_END) return 0;
  if(record[0] == FAT16_RECORD_DELETED) return 1;
  if(attr == FAT16_ATTR_LFN || (attr & FAT16_ATTR_VOLUME_ID)) return 1;
  if(is_dot(record)) return 1;
  return visible->visit(record, sector, offset, visible->ctx);
}

// go through files and directories
static void dir_walk(uint16_t dir_cluster, uint8_t *buf, fat16_visit_t visit, void *ctx){
  visible_ctx_t visible = {visit, ctx};
  fat16_dir_walk(dir_cluster, buf, visible_visit, &visible);
}

static uint8_t count_visit(const uint8_t *record, uint32_t sector, uint16_t offset, void *ctx){
  (*(uint16_t *)ctx)++;
  return 1;
}

// number of files and directories in directory
uint16_t fat16_dir_count(uint16_t dir_cluster, uint8_t *buf){
  uint16_t count = 0;
  dir_walk(dir_cluster, buf, count_visit, &count);
  return count;
}

typedef struct {
  uint16_t dir_cluster;
  uint16_t index;
  uint16_t first;
  fat16_entry_t *entries;
  uint8_t count;
  uint8_t read;
} read_ctx_t;

// copy name without trailing spaces
static char *copy_name(char *dst, const uint8_t *src, uint8_t size){
  uint8_t len = size;
  while(len && src[len - 1] == ' ') len--;
  memcpy(dst, src, len);
  dst += len;
  *dst = '\0';
  return dst;
}

void fat16_entry_from_record(fat16_entry_t *entry, const uint8_t *record){
  char *end = copy_name(entry->name, record, FAT16_NAME_SIZE);
  copy_name(entry->ext, record + FAT16_NAME_SIZE, FAT16_EXT_SIZE);
  if(entry->ext[0]){
    *end++ = '.';
    strcpy(end, entry->ext);
  }
  entry->attr = record[FAT16_ATTR_OFFSET];
  entry->cluster = get16(record, FAT16_CLUSTER_OFFSET);
  entry->size = get32(record, FAT16_SIZE_OFFSET);
}

static uint8_t read_visit(const uint8_t *record, uint32_t sector, uint16_t offset, void *ctx){
  read_ctx_t *read_ctx = ctx;
  if(read_ctx->index++ < read_ctx->first) return 1;

  fat16_entry_t *entry = &read_ctx->entries[read_ctx->read++];
  fat16_entry_from_record(entry, record);
  entry->dir_cluster = read_ctx->dir_cluster;
  entry->record_sector = sector;
  entry->record_offset = offset;

  return read_ctx->read < read_ctx->count;
}

// read up to count entries starting from entry number first,
// returns number of read entries
uint8_t fat16_dir_read(uint16_t dir_cluster, uint16_t first,
                       fat16_entry_t *entries, uint8_t count, uint8_t *buf){
  read_ctx_t ctx = {dir_cluster, 0, first, entries, count, 0};
  if(count) dir_walk(dir_cluster, buf, read_visit, &ctx);
  return ctx.read;
}

uint8_t fat16_is_dir(const fat16_entry_t *entry){
  return entry->attr & FAT16_ATTR_DIRECTORY;
}

void fat16_file_open(fat16_file_t *file, uint16_t cluster, uint32_t size){
  file->cluster = cluster;
  file->sector = fat16_cluster_sector(cluster);
  file->cluster_sector = 0;
  file->left = size;
}

// read next sector of file to buf, len = valid bytes in buf (0 at the end).
// returns 0 on read error or broken FAT chain
uint8_t fat16_file_read(fat16_file_t *file, uint8_t *buf, uint16_t *len){
  *len = 0;
  if(!file->left) return 1;

  // cluster is over: go to the next one by FAT chain
  if(file->cluster_sector == fat16_volume.cluster_sectors){
    file->cluster = fat16_next_cluster(file->cluster, buf);
    if(!file->cluster) return 0;
    file->sector = fat16_cluster_sector(file->cluster);
    file->cluster_sector = 0;
  }

  if(!sd_read_sector(file->sector, buf)) return 0;
  file->sector++;
  file->cluster_sector++;

  *len = file->left < SD_SECTOR_SIZE ? file->left : SD_SECTOR_SIZE;
  file->left -= *len;
  return 1;
}
