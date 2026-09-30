#include <string.h>
#include <util/delay.h>
#include "display.h"
#include "keys.h"
#include "radio.h"
#include "sd.h"
#include "fat16_dir.h"
#include "file_browser.h"
#include "loader.h"

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20

static uint8_t sector[SD_SECTOR_SIZE];
static uint8_t card_type;

// SD traffic corrupts display RAM, so every screen is drawn completely
// after the SD work
static void draw_screen(const char *l0, const char *l1, const char *l2, const char *l3){
  display_print_line(0, l0);
  display_print_line(1, l1);
  display_print_line(2, l2);
  display_print_line(3, l3);
}

static uint8_t wait_key(void){
  uint8_t key;
  while((key = keys_get_press()) == NOOP){
    _delay_ms(POLL_INTERVAL_MS);
  }
  return key;
}

static void error(const char *l0, const char *l1){
  draw_screen(l0, l1, "", "A - retry");
  while(wait_key() != A_KEY_PRESSED);
}

// init card and FAT16, retry until success
static void mount(void){
  while(1){
    draw_screen("SD init...", "", "", "");
    card_type = sd_init_card();
    if(card_type == SD_TYPE_NONE){
      error("SD init error", "insert the card");
    }else if(!fat16_mount(sector)){
      error("no FAT16", "on the card");
    }else{
      return;
    }
  }
}

static uint8_t is_bin(const fat16_entry_t *entry){
  return !fat16_is_dir(entry) && !strcmp(entry->ext, "BIN");
}

// ask and flash the application, returns only if cancelled or impossible
static void flash_app(const fat16_entry_t *entry){
  if(!loader_is_present()){
    error("no bootloader", "");
    return;
  }
  if(card_type != SD_TYPE_SDHC){
    error("bootloader needs", "SDHC card");
    return;
  }
  if(!entry->size || entry->size > LOADER_APP_MAX_SIZE){
    error(entry->name, "bad size");
    return;
  }

  draw_screen("flash app?", entry->name, "A - yes", "C - no");
  while(1){
    switch(wait_key()){
      case A_KEY_PRESSED:
        draw_screen("flashing...", entry->name, "", "");
        // bootloader writes flash and resets MCU
        loader_load_app_by_cluster(entry->cluster, entry->size);
      case C_KEY_PRESSED:
        return;
    }
  }
}

int main(void){
  fat16_entry_t entry;

  init_keys();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_sd();
  init_display();

  mount();
  file_browser_open(sector);

  while(1){
    // C in the root directory does nothing
    if(file_browser_run(&entry) == FILE_BROWSER_FILE && is_bin(&entry)){
      flash_app(&entry);
    }
  }
}
