#include "pins.h"
#include "macro.h"
#include "fat16_dir.h"

#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

// text list of files and directories on the display:
// UP / DOWN - move, A - enter directory or choose file, C - parent directory,
// B - menu for the selected entry

#define FILE_BROWSER_DEPTH      8       // max depth of nested directories
#define FILE_BROWSER_DIR_MARK   '/'

// result of file_browser_run
#define FILE_BROWSER_FILE       1       // file is chosen
#define FILE_BROWSER_EXIT       0       // C in the root directory
#define FILE_BROWSER_MENU       2       // B: entry is selected one, empty name if
                                        // the directory is empty

// FAT16 must be mounted, buf is 512 byte work buffer kept by the browser
void file_browser_open(uint8_t *buf);
uint8_t file_browser_run(fat16_entry_t *entry);
uint16_t file_browser_dir(void);
void file_browser_reload(void);

#endif
