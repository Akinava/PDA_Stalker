#=======================================

# Name of the project
TARGET = hw_test
# file name on SD card: ../../sd/BIN/$(APP_NAME).BIN
APP_NAME = HWTEST

# tipe of MK
MCU  = atmega328p
# cpu MK in Hz
F_CPU   = 16000000

# programmer
AVRDUDE_PROGRAMMER = usbasp

LIB_DIR = ../lib
BOOT_DIR = ../bootloader
SD_BIN_DIR = ../../sd/BIN
BUILD_DIR = build

SOURCES = main.c
SOURCES += $(LIB_DIR)/loader.c
SOURCES += $(LIB_DIR)/menu.c
SOURCES += $(LIB_DIR)/spi.c
SOURCES += $(LIB_DIR)/display.c
SOURCES += $(LIB_DIR)/keys.c
SOURCES += $(LIB_DIR)/led.c
SOURCES += $(LIB_DIR)/vibro.c
SOURCES += $(LIB_DIR)/mic.c
SOURCES += $(LIB_DIR)/speaker.c
SOURCES += $(LIB_DIR)/radio.c
SOURCES += $(LIB_DIR)/sd.c
#=======================================

OBJECTS = $(addprefix $(BUILD_DIR)/, $(notdir $(SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(SOURCES)))

CDEFS = -DF_CPU=$(F_CPU)UL

CFLAGS = -Os
CFLAGS += -Wall
CFLAGS += -std=gnu99
CFLAGS += $(CDEFS)
CFLAGS += -I$(LIB_DIR)
# generate header dependencies
CFLAGS += -MMD -MP

CC = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
SIZE = avr-size
AVRDUDE = avrdude

COMPILE = $(CC) $(CFLAGS) -mmcu=$(MCU)

AVRDUDE_FLAGS = -p $(MCU) -c $(AVRDUDE_PROGRAMMER)

#============================================================================

all: $(TARGET).hex

help:
	@echo "make all ....... build $(TARGET).hex"
	@echo "make program ... flash $(TARGET).hex together with bootloader"
	@echo "make bin ....... copy binary to SD card dir as $(APP_NAME).BIN"
	@echo "make clean ..... delete build files"

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(COMPILE) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(COMPILE) -o $@ $(OBJECTS)
	$(OBJDUMP) -D $@ > $(TARGET).asm

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@
	$(SIZE) -C --mcu=$(MCU) $<

# chip erase before flashing removes the bootloader,
# so application and bootloader are flashed as one hex
$(BOOT_DIR)/boot.hex:
	$(MAKE) -C $(BOOT_DIR) build

$(TARGET)_boot.hex: $(TARGET).hex $(BOOT_DIR)/boot.hex
	head -n -1 $(TARGET).hex > $@
	cat $(BOOT_DIR)/boot.hex >> $@

program: $(TARGET)_boot.hex
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:w:$<:i

bin: $(TARGET).hex
	$(OBJCOPY) -I ihex -O binary $< $(SD_BIN_DIR)/$(APP_NAME).BIN

clean:
	rm -rf $(BUILD_DIR) $(TARGET).elf $(TARGET).hex $(TARGET)_boot.hex $(TARGET).asm

.PHONY: all help program bin clean

-include $(OBJECTS:.o=.d)
