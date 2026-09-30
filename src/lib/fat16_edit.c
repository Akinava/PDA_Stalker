#include <string.h>
#include "fat16_edit.h"

#define FAT_FREE                0x0000
#define FAT_CHAIN_END           0xFFFF
// "." and ".." are the first records of subdirectory
#define DOT_OFFSET              0
#define DOTDOT_OFFSET           FAT16_RECORD_SIZE
// ".." of the root directory child has cluster 0
#define ROOT_PARENT_CLUSTER     0
// protection against loops of broken ".." chain
#define MAX_PATH_DEPTH          64

// date and time of the record (there is no clock)
#define RECORD_CREATE_DATE      16
#define RECORD_ACCESS_DATE      18
#define RECORD_WRITE_DATE       24
// FAT date: (year - 1980) << 9 | month << 5 | day
#define DEFAULT_DATE            ((2020 - 1980) << 9 | 1 << 5 | 1)
#define ATTR_ARCHIVE            0x20    // usual file

// the first free cluster is searched from here
static uint16_t alloc_hint = 2;

// shared by recursive delete / copy to keep the stack small:
// they are not used after the recursive call
static fat16_entry_t child;
static uint8_t record[FAT16_RECORD_SIZE];

static uint16_t get16(const uint8_t *buf, uint16_t offset){
  return buf[offset] | ((uint16_t)buf[offset + 1] << 8);
}

static void put16(uint8_t *buf, uint16_t offset, uint16_t value){
  buf[offset] = value;
  buf[offset + 1] = value >> 8;
}

static void put32(uint8_t *buf, uint16_t offset, uint32_t value){
  put16(buf, offset, value);
  put16(buf, offset + 2, value >> 16);
}

static uint8_t is_chain_cluster(uint16_t cluster){
  return cluster >= 2 && cluster <= fat16_volume.max_cluster;
}

/********************************** FAT **************************************/

static uint32_t fat_sector(uint16_t cluster){
  // 256 records of 2 bytes in FAT sector
  return fat16_volume.fat_sector + (cluster >> 8);
}

static uint8_t fat_get(uint16_t cluster, uint16_t *value, uint8_t *buf){
  if(!sd_read_sector(fat_sector(cluster), buf)) return FAT16_ERROR_IO;
  *value = get16(buf, (cluster & 0xFF) * 2);
  return FAT16_OK;
}

// write the record to all FAT copies
static uint8_t fat_set(uint16_t cluster, uint16_t value, uint8_t *buf){
  uint32_t sector = fat_sector(cluster);
  if(!sd_read_sector(sector, buf)) return FAT16_ERROR_IO;
  put16(buf, (cluster & 0xFF) * 2, value);
  for(uint8_t i = 0; i < fat16_volume.num_fats; i++){
    if(!sd_write_sector(sector + (uint32_t)i * fat16_volume.fat_sectors, buf)) return FAT16_ERROR_IO;
  }
  return FAT16_OK;
}

static uint8_t free_chain(uint16_t cluster, uint8_t *buf){
  while(is_chain_cluster(cluster)){
    uint16_t next;
    if(fat_get(cluster, &next, buf)) return FAT16_ERROR_IO;
    if(fat_set(cluster, FAT_FREE, buf)) return FAT16_ERROR_IO;
    if(cluster < alloc_hint) alloc_hint = cluster;
    cluster = next;
  }
  return FAT16_OK;
}

// take a free cluster as the end of chain, link it after prev (0 - new chain)
static uint8_t alloc_cluster(uint16_t prev, uint16_t *cluster, uint8_t *buf){
  uint16_t max = fat16_volume.max_cluster;
  uint16_t candidate = alloc_hint >= 2 && alloc_hint <= max ? alloc_hint : 2;
  uint32_t loaded = 0;

  for(uint16_t left = max - 1; left; left--){
    uint32_t sector = fat_sector(candidate);
    if(sector != loaded){
      if(!sd_read_sector(sector, buf)) return FAT16_ERROR_IO;
      loaded = sector;
    }
    if(get16(buf, (candidate & 0xFF) * 2) == FAT_FREE){
      if(fat_set(candidate, FAT_CHAIN_END, buf)) return FAT16_ERROR_IO;
      if(prev && fat_set(prev, candidate, buf)) return FAT16_ERROR_IO;
      alloc_hint = candidate + 1;
      *cluster = candidate;
      return FAT16_OK;
    }
    candidate = candidate < max ? candidate + 1 : 2;
  }
  return FAT16_ERROR_FULL;
}

static uint8_t zero_cluster(uint16_t cluster, uint8_t *buf){
  uint32_t sector = fat16_cluster_sector(cluster);
  memset(buf, 0, SD_SECTOR_SIZE);
  for(uint8_t i = 0; i < fat16_volume.cluster_sectors; i++){
    if(!sd_write_sector(sector + i, buf)) return FAT16_ERROR_IO;
  }
  return FAT16_OK;
}

// copy data of the chain to a new chain, first - its first cluster (0 if empty)
static uint8_t copy_chain(uint16_t cluster, uint16_t *first, uint8_t *buf){
  uint16_t prev = 0;
  *first = 0;
  while(is_chain_cluster(cluster)){
    uint16_t copy;
    uint8_t error = alloc_cluster(prev, &copy, buf);
    if(error) return error;
    if(!*first) *first = copy;

    uint32_t src = fat16_cluster_sector(cluster);
    uint32_t dst = fat16_cluster_sector(copy);
    for(uint8_t i = 0; i < fat16_volume.cluster_sectors; i++){
      if(!sd_read_sector(src + i, buf) || !sd_write_sector(dst + i, buf)) return FAT16_ERROR_IO;
    }
    prev = copy;
    if(fat_get(cluster, &cluster, buf)) return FAT16_ERROR_IO;
  }
  return FAT16_OK;
}

/******************************** records ************************************/

void fat16_raw_name(const fat16_entry_t *entry, uint8_t *raw){
  const char *p = entry->name;
  memset(raw, ' ', FAT16_RAW_NAME_SIZE);
  for(uint8_t i = 0; *p && *p != '.' && i < FAT16_NAME_SIZE; i++){
    raw[i] = *p++;
  }
  memcpy(raw + FAT16_NAME_SIZE, entry->ext, strlen(entry->ext));
}

static uint8_t read_record(const fat16_entry_t *entry, uint8_t *dst, uint8_t *buf){
  if(!sd_read_sector(entry->record_sector, buf)) return FAT16_ERROR_IO;
  memcpy(dst, buf + entry->record_offset, FAT16_RECORD_SIZE);
  return FAT16_OK;
}

// long name records are before the short one in the same sector
static void delete_long_name(uint8_t *buf, uint16_t offset){
  while(offset){
    offset -= FAT16_RECORD_SIZE;
    if(buf[offset + FAT16_ATTR_OFFSET] != FAT16_ATTR_LFN) return;
    if(buf[offset] == FAT16_RECORD_DELETED) return;
    buf[offset] = FAT16_RECORD_DELETED;
  }
}

static uint8_t delete_record(uint32_t sector, uint16_t offset, uint8_t *buf){
  if(!sd_read_sector(sector, buf)) return FAT16_ERROR_IO;
  buf[offset] = FAT16_RECORD_DELETED;
  delete_long_name(buf, offset);
  if(!sd_write_sector(sector, buf)) return FAT16_ERROR_IO;
  return FAT16_OK;
}

typedef struct {
  const uint8_t *raw;
  uint32_t skip_sector;
  uint16_t skip_offset;
  uint8_t found;
} find_ctx_t;

static uint8_t find_visit(const uint8_t *rec, uint32_t sector, uint16_t offset, void *ctx){
  find_ctx_t *find = ctx;
  uint8_t attr = rec[FAT16_ATTR_OFFSET];
  if(rec[0] == FAT16_RECORD_END) return 0;
  if(rec[0] == FAT16_RECORD_DELETED || attr == FAT16_ATTR_LFN || (attr & FAT16_ATTR_VOLUME_ID)) return 1;
  if(sector == find->skip_sector && offset == find->skip_offset) return 1;
  if(!memcmp(rec, find->raw, FAT16_RAW_NAME_SIZE)){
    find->found = 1;
    return 0;
  }
  return 1;
}

// is the name used in directory, the record at skip place is not checked
static uint8_t name_exists(uint16_t dir, const uint8_t *raw, uint32_t skip_sector,
                           uint16_t skip_offset, uint8_t *buf){
  find_ctx_t find = {raw, skip_sector, skip_offset, 0};
  fat16_dir_walk(dir, buf, find_visit, &find);
  return find.found;
}

typedef struct {
  uint32_t sector;
  uint16_t offset;
  uint8_t found;
} free_ctx_t;

static uint8_t free_visit(const uint8_t *rec, uint32_t sector, uint16_t offset, void *ctx){
  free_ctx_t *free_record = ctx;
  if(rec[0] != FAT16_RECORD_END && rec[0] != FAT16_RECORD_DELETED) return 1;
  free_record->sector = sector;
  free_record->offset = offset;
  free_record->found = 1;
  return 0;
}

// write the record (not in buf) to a free place of directory,
// subdirectory gets a new cluster if it is full.
// place (may be NULL) gets the sector and offset of the record
static uint8_t dir_add(uint16_t dir, const uint8_t *rec, uint8_t *buf, free_ctx_t *place){
  free_ctx_t free_record = {0, 0, 0};
  fat16_dir_walk(dir, buf, free_visit, &free_record);

  if(free_record.found){
    if(!sd_read_sector(free_record.sector, buf)) return FAT16_ERROR_IO;
    memcpy(buf + free_record.offset, rec, FAT16_RECORD_SIZE);
    if(!sd_write_sector(free_record.sector, buf)) return FAT16_ERROR_IO;
    if(place) *place = free_record;
    return FAT16_OK;
  }
  if(dir == FAT16_ROOT_CLUSTER) return FAT16_ERROR_DIR_FULL;

  // the last cluster of directory
  uint16_t last = dir;
  uint16_t next;
  while(1){
    if(fat_get(last, &next, buf)) return FAT16_ERROR_IO;
    if(!is_chain_cluster(next)) break;
    last = next;
  }
  uint16_t cluster;
  uint8_t error = alloc_cluster(last, &cluster, buf);
  if(error) return error;
  if(zero_cluster(cluster, buf)) return FAT16_ERROR_IO;
  // buf is zero after zero_cluster
  memcpy(buf, rec, FAT16_RECORD_SIZE);
  if(!sd_write_sector(fat16_cluster_sector(cluster), buf)) return FAT16_ERROR_IO;
  if(place){
    place->sector = fat16_cluster_sector(cluster);
    place->offset = 0;
    place->found = 1;
  }
  return FAT16_OK;
}

// is dir the ancestor or its subdirectory: go up by ".." records
static uint8_t is_inside(uint16_t dir, uint16_t ancestor, uint8_t *buf){
  for(uint8_t depth = 0; depth < MAX_PATH_DEPTH; depth++){
    if(dir == ancestor) return 1;
    if(dir == FAT16_ROOT_CLUSTER) return 0;
    if(!sd_read_sector(fat16_cluster_sector(dir), buf)) return 1;
    if(buf[DOTDOT_OFFSET] != '.' || buf[DOTDOT_OFFSET + 1] != '.') return 1;
    dir = get16(buf, DOTDOT_OFFSET + FAT16_CLUSTER_OFFSET);
  }
  // broken chain: do not risk
  return 1;
}

static uint8_t set_parent(uint16_t dir, uint16_t parent, uint8_t *buf){
  uint32_t sector = fat16_cluster_sector(dir);
  if(!sd_read_sector(sector, buf)) return FAT16_ERROR_IO;
  if(buf[DOTDOT_OFFSET] != '.' || buf[DOTDOT_OFFSET + 1] != '.') return FAT16_OK;
  put16(buf, DOTDOT_OFFSET + FAT16_CLUSTER_OFFSET, parent);
  if(!sd_write_sector(sector, buf)) return FAT16_ERROR_IO;
  return FAT16_OK;
}

// the shared record: new name, attributes and date, no data
static void init_record(const uint8_t *raw, uint8_t attr){
  memset(record, 0, FAT16_RECORD_SIZE);
  memcpy(record, raw, FAT16_RAW_NAME_SIZE);
  record[FAT16_ATTR_OFFSET] = attr;
  put16(record, RECORD_CREATE_DATE, DEFAULT_DATE);
  put16(record, RECORD_ACCESS_DATE, DEFAULT_DATE);
  put16(record, RECORD_WRITE_DATE, DEFAULT_DATE);
}

// new chain with size bytes of data (in RAM, not in buf), first is 0 if empty
static uint8_t write_chain(const uint8_t *data, uint32_t size, uint16_t *first, uint8_t *buf){
  uint16_t prev = 0;
  uint32_t done = 0;
  *first = 0;

  while(done < size){
    uint16_t cluster;
    uint8_t error = alloc_cluster(prev, &cluster, buf);
    if(error) return error;
    if(!*first) *first = cluster;

    uint32_t sector = fat16_cluster_sector(cluster);
    for(uint8_t i = 0; i < fat16_volume.cluster_sectors && done < size; i++){
      const uint8_t *src = data + done;
      uint32_t len = size - done;
      if(len >= SD_SECTOR_SIZE){
        len = SD_SECTOR_SIZE;
      }else{
        // the last sector: the rest is zero
        memset(buf, 0, SD_SECTOR_SIZE);
        memcpy(buf, src, len);
        src = buf;
      }
      if(!sd_write_sector(sector + i, src)) return FAT16_ERROR_IO;
      done += len;
    }
    prev = cluster;
  }
  return FAT16_OK;
}

/******************************** public *************************************/

// the record of entry is still on its place with the same name and data
uint8_t fat16_entry_valid(const fat16_entry_t *entry, uint8_t *buf){
  uint8_t raw[FAT16_RAW_NAME_SIZE];
  fat16_raw_name(entry, raw);
  if(!sd_read_sector(entry->record_sector, buf)) return 0;
  const uint8_t *rec = buf + entry->record_offset;
  return !memcmp(rec, raw, FAT16_RAW_NAME_SIZE)
      && get16(rec, FAT16_CLUSTER_OFFSET) == entry->cluster;
}

static uint8_t delete_entry(const fat16_entry_t *entry, uint8_t *buf, uint8_t depth){
  // entry may be the shared child: save it before the recursion
  uint16_t cluster = entry->cluster;
  uint32_t sector = entry->record_sector;
  uint16_t offset = entry->record_offset;

  if(fat16_is_dir(entry)){
    if(depth == FAT16_EDIT_DEPTH) return FAT16_ERROR_DEPTH;
    // the first entry every time: deleted ones are skipped
    while(fat16_dir_read(cluster, 0, &child, 1, buf)){
      uint8_t error = delete_entry(&child, buf, depth + 1);
      if(error) return error;
    }
  }
  if(free_chain(cluster, buf)) return FAT16_ERROR_IO;
  return delete_record(sector, offset, buf);
}

// delete file or directory with all its content
uint8_t fat16_delete(const fat16_entry_t *entry, uint8_t *buf){
  return delete_entry(entry, buf, 0);
}

// move file or directory to directory dir_cluster
uint8_t fat16_move(const fat16_entry_t *entry, uint16_t dir_cluster, uint8_t *buf){
  uint8_t raw[FAT16_RAW_NAME_SIZE];
  uint8_t error;

  if(entry->dir_cluster == dir_cluster) return FAT16_ERROR_SAME_DIR;
  if(fat16_is_dir(entry) && is_inside(dir_cluster, entry->cluster, buf)) return FAT16_ERROR_INSIDE;
  fat16_raw_name(entry, raw);
  if(name_exists(dir_cluster, raw, 0, 0, buf)) return FAT16_ERROR_EXISTS;

  if((error = read_record(entry, record, buf))) return error;
  if((error = dir_add(dir_cluster, record, buf, NULL))) return error;
  if(fat16_is_dir(entry) && (error = set_parent(entry->cluster, dir_cluster, buf))) return error;
  return delete_record(entry->record_sector, entry->record_offset, buf);
}

// subdirectory in dir_cluster by the record (name, attributes, date):
// a new cluster with "." and "..", the record gets the cluster
static uint8_t new_dir(uint16_t dir_cluster, uint8_t *rec, uint16_t *cluster, uint8_t *buf){
  uint16_t dir;
  uint8_t error;

  if((error = alloc_cluster(0, &dir, buf))) return error;
  if((error = zero_cluster(dir, buf))){
    free_chain(dir, buf);
    return error;
  }

  // "." and ".." with the date and time of the directory, buf is zero
  memcpy(buf + DOT_OFFSET, rec, FAT16_RECORD_SIZE);
  memset(buf + DOT_OFFSET, ' ', FAT16_RAW_NAME_SIZE);
  buf[DOT_OFFSET] = '.';
  put16(buf, DOT_OFFSET + FAT16_CLUSTER_OFFSET, dir);
  put32(buf, DOT_OFFSET + FAT16_SIZE_OFFSET, 0);
  memcpy(buf + DOTDOT_OFFSET, buf + DOT_OFFSET, FAT16_RECORD_SIZE);
  buf[DOTDOT_OFFSET + 1] = '.';
  put16(buf, DOTDOT_OFFSET + FAT16_CLUSTER_OFFSET,
        dir_cluster == FAT16_ROOT_CLUSTER ? ROOT_PARENT_CLUSTER : dir_cluster);
  if(!sd_write_sector(fat16_cluster_sector(dir), buf)){
    free_chain(dir, buf);
    return FAT16_ERROR_IO;
  }

  put16(rec, FAT16_CLUSTER_OFFSET, dir);
  put32(rec, FAT16_SIZE_OFFSET, 0);
  if((error = dir_add(dir_cluster, rec, buf, NULL))){
    free_chain(dir, buf);
    return error;
  }
  *cluster = dir;
  return FAT16_OK;
}

static uint8_t copy_entry(const fat16_entry_t *entry, uint16_t dir_cluster, uint8_t *buf, uint8_t depth){
  uint16_t src = entry->cluster;
  uint16_t copy;
  uint8_t error;

  if((error = read_record(entry, record, buf))) return error;

  if(!fat16_is_dir(entry)){
    error = copy_chain(src, &copy, buf);
    if(!error){
      put16(record, FAT16_CLUSTER_OFFSET, copy);
      error = dir_add(dir_cluster, record, buf, NULL);
    }
    if(error) free_chain(copy, buf);
    return error;
  }

  if(depth == FAT16_EDIT_DEPTH) return FAT16_ERROR_DEPTH;
  if((error = new_dir(dir_cluster, record, &copy, buf))) return error;

  // entry may be the shared child: only src is used from here
  for(uint16_t i = 0; fat16_dir_read(src, i, &child, 1, buf); i++){
    if((error = copy_entry(&child, copy, buf, depth + 1))) return error;
  }
  return FAT16_OK;
}

// copy file or directory with all its content to directory dir_cluster
uint8_t fat16_copy(const fat16_entry_t *entry, uint16_t dir_cluster, uint8_t *buf){
  uint8_t raw[FAT16_RAW_NAME_SIZE];

  if(fat16_is_dir(entry) && is_inside(dir_cluster, entry->cluster, buf)) return FAT16_ERROR_INSIDE;
  fat16_raw_name(entry, raw);
  if(name_exists(dir_cluster, raw, 0, 0, buf)) return FAT16_ERROR_EXISTS;
  return copy_entry(entry, dir_cluster, buf, 0);
}

// new raw name (8 + 3 chars with spaces), long name is deleted
uint8_t fat16_rename(const fat16_entry_t *entry, const uint8_t *raw, uint8_t *buf){
  if(name_exists(entry->dir_cluster, raw, entry->record_sector, entry->record_offset, buf)){
    return FAT16_ERROR_EXISTS;
  }
  if(!sd_read_sector(entry->record_sector, buf)) return FAT16_ERROR_IO;
  memcpy(buf + entry->record_offset, raw, FAT16_RAW_NAME_SIZE);
  delete_long_name(buf, entry->record_offset);
  if(!sd_write_sector(entry->record_sector, buf)) return FAT16_ERROR_IO;
  return FAT16_OK;
}

// new empty directory with raw name (8 + 3 chars with spaces) in dir_cluster
uint8_t fat16_mkdir(uint16_t dir_cluster, const uint8_t *raw, uint8_t *buf){
  uint16_t cluster;

  if(name_exists(dir_cluster, raw, 0, 0, buf)) return FAT16_ERROR_EXISTS;

  init_record(raw, FAT16_ATTR_DIRECTORY);
  return new_dir(dir_cluster, record, &cluster, buf);
}

// replace data of the file with size bytes of data (in RAM, not in buf):
// data goes to a new chain, then the record is changed and the old chain
// is freed, so the old data stays if the write fails
uint8_t fat16_write_file(fat16_entry_t *entry, const uint8_t *data, uint32_t size, uint8_t *buf){
  uint16_t first;
  uint8_t error = write_chain(data, size, &first, buf);
  if(error){
    free_chain(first, buf);
    return error;
  }

  if(!sd_read_sector(entry->record_sector, buf)){
    free_chain(first, buf);
    return FAT16_ERROR_IO;
  }
  uint8_t *rec = buf + entry->record_offset;
  uint16_t old = get16(rec, FAT16_CLUSTER_OFFSET);
  put16(rec, FAT16_CLUSTER_OFFSET, first);
  put32(rec, FAT16_SIZE_OFFSET, size);
  if(!sd_write_sector(entry->record_sector, buf)){
    free_chain(first, buf);
    return FAT16_ERROR_IO;
  }

  entry->cluster = first;
  entry->size = size;
  return free_chain(old, buf);
}

// new file with raw name (8 + 3 chars with spaces) in dir_cluster and size
// bytes of data (in RAM, not in buf), entry gets the new file
uint8_t fat16_create_file(uint16_t dir_cluster, const uint8_t *raw, const uint8_t *data,
                          uint32_t size, fat16_entry_t *entry, uint8_t *buf){
  free_ctx_t place;
  uint16_t first;
  uint8_t error;

  if(name_exists(dir_cluster, raw, 0, 0, buf)) return FAT16_ERROR_EXISTS;

  error = write_chain(data, size, &first, buf);
  if(!error){
    init_record(raw, ATTR_ARCHIVE);
    put16(record, FAT16_CLUSTER_OFFSET, first);
    put32(record, FAT16_SIZE_OFFSET, size);
    error = dir_add(dir_cluster, record, buf, &place);
  }
  if(error){
    free_chain(first, buf);
    return error;
  }

  fat16_entry_from_record(entry, record);
  entry->dir_cluster = dir_cluster;
  entry->record_sector = place.sector;
  entry->record_offset = place.offset;
  return FAT16_OK;
}
