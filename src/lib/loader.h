#include <avr/pgmspace.h>
#include "pins.h"
#include "macro.h"

#ifndef LOADER_H
#define LOADER_H

// SD bootloader shared functions (see shared_func_ in bootloader/boot.c).
// Table of word addresses at the end of flash (SHARED_FUNC_ADDRESS):
#define LOADER_SHARED_FUNC          0x7FFA
#define LOADER_LOAD_APP_BY_CLUSTER  (LOADER_SHARED_FUNC + 0)
#define LOADER_LOAD_APP_BY_NAME     (LOADER_SHARED_FUNC + 2)
#define LOADER_LOAD_DEFAULT_APP     (LOADER_SHARED_FUNC + 4)

// application must not overwrite the bootloader (BOOT_ADDRESS)
#define LOADER_APP_MAX_SIZE         0x7800

// bootloader works with SDHC cards only

uint8_t loader_is_present(void);
// these functions flash the application and reset MCU, they never return
void loader_load_app_by_cluster(uint16_t cluster, uint32_t size) __attribute__((noreturn));
// file_path is in flash (PROGMEM), unix view: "/BIN/APP.BIN"
void loader_load_app_by_name(const char *file_path) __attribute__((noreturn));
void loader_load_default_app(void) __attribute__((noreturn));

#endif
