#include "loader.h"

typedef void (*load_by_cluster_t)(uint16_t cluster, uint32_t size);
typedef void (*load_by_name_t)(const char *file_path);
typedef void (*load_default_t)(void);

// empty flash reads as 0xFFFF: there is no bootloader or no table
static uint16_t loader_func(uint16_t table_address){
  return pgm_read_word(table_address);
}

uint8_t loader_is_present(void){
  return loader_func(LOADER_LOAD_APP_BY_CLUSTER) != 0xFFFF
      && loader_func(LOADER_LOAD_APP_BY_NAME) != 0xFFFF
      && loader_func(LOADER_LOAD_DEFAULT_APP) != 0xFFFF;
}

// function pointers in avr-gcc are word addresses, the same as in the table
void loader_load_app_by_cluster(uint16_t cluster, uint32_t size){
  ((load_by_cluster_t)loader_func(LOADER_LOAD_APP_BY_CLUSTER))(cluster, size);
  while(1);
}

void loader_load_app_by_name(const char *file_path){
  ((load_by_name_t)loader_func(LOADER_LOAD_APP_BY_NAME))(file_path);
  while(1);
}

void loader_load_default_app(void){
  ((load_default_t)loader_func(LOADER_LOAD_DEFAULT_APP))();
  while(1);
}
