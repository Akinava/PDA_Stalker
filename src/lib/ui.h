#include <avr/pgmspace.h>
#include "pins.h"
#include "macro.h"

#ifndef UI_H
#define UI_H

// text mode screens of applications: the text in flash is "_P" (PSTR),
// RAM text (file names) may be NULL

#define UI_POLL_INTERVAL_MS     20
#define UI_NONE                 0xFF

uint8_t ui_wait_key(void);
void ui_wait_release(void);
void ui_message(const char *l0, const char *ram_l1);
uint8_t ui_sure(const char *l0, const char *ram_l1);
void ui_show_error(uint8_t error);
uint8_t ui_menu(const char *items, uint8_t item_size, uint8_t count);
uint8_t ui_exit_confirm(void);
void ui_mount(uint8_t *buf);

#endif
