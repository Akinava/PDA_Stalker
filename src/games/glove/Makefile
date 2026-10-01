#=======================================

# Name of the project
TARGET = glove
# file name on SD card: ../../../sd/GAMES/$(APP_NAME).BIN
APP_NAME = GLOVE

# tipe of MK
MCU  = atmega328p
# cpu MK in Hz
F_CPU   = 16000000

# programmer
AVRDUDE_PROGRAMMER = usbasp

LIB_DIR = ../../lib
ARDUBOY_DIR = $(LIB_DIR)/arduboy
BOOT_DIR = ../../bootloader
SD_GAMES_DIR = ../../../sd/GAMES
BUILD_DIR = build

# game sources from Arduboy (glove.ino is main.cpp)
CPP_SOURCES = main.cpp
CPP_SOURCES += badguy.cpp
CPP_SOURCES += bullet.cpp
CPP_SOURCES += exit.cpp
CPP_SOURCES += explorer.cpp
CPP_SOURCES += game.cpp
CPP_SOURCES += globals.cpp
CPP_SOURCES += key.cpp
CPP_SOURCES += mainmenu.cpp
CPP_SOURCES += options.cpp
CPP_SOURCES += physics.cpp
CPP_SOURCES += prompt.cpp
CPP_SOURCES += records.cpp
CPP_SOURCES += save.cpp
CPP_SOURCES += spawner.cpp
CPP_SOURCES += title.cpp
CPP_SOURCES += treasure.cpp
CPP_SOURCES += wall.cpp
CPP_SOURCES += world.cpp
CPP_SOURCES += $(ARDUBOY_DIR)/Arduboy.cpp

SOURCES = $(LIB_DIR)/spi.c
SOURCES += $(LIB_DIR)/display.c
SOURCES += $(LIB_DIR)/display_gfx.c
SOURCES += $(LIB_DIR)/keys.c
SOURCES += $(LIB_DIR)/led.c
SOURCES += $(LIB_DIR)/speaker.c
SOURCES += $(LIB_DIR)/radio.c
SOURCES += $(LIB_DIR)/mic.c
SOURCES += $(LIB_DIR)/loader.c
#=======================================

OBJECTS = $(addprefix $(BUILD_DIR)/, $(notdir $(SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/, $(notdir $(CPP_SOURCES:.cpp=.o)))
vpath %.c $(sort $(dir $(SOURCES)))
vpath %.cpp $(sort $(dir $(CPP_SOURCES)))

CDEFS = -DF_CPU=$(F_CPU)UL

# flags of C and C++
FLAGS = -Os
FLAGS += -Wall
FLAGS += $(CDEFS)
FLAGS += -I$(LIB_DIR)
FLAGS += -I$(ARDUBOY_DIR)
# generate header dependencies
FLAGS += -MMD -MP
# every function and data in own section: the linker drops unused ones
FLAGS += -ffunction-sections -fdata-sections

CFLAGS = $(FLAGS) -std=gnu99
CXXFLAGS = $(FLAGS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics
# the Arduboy code is kept as it is
CXXFLAGS += -Wno-unused-variable -Wno-unused-function -Wno-sign-compare
# as Arduino IDE
CXXFLAGS += -fpermissive -Wno-narrowing -Wno-switch -Wno-char-subscripts

CC = avr-gcc
CXX = avr-g++
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
SIZE = avr-size
AVRDUDE = avrdude

LDFLAGS = -Wl,--gc-sections

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

# the game sources go first: a game file can have the name of a library one
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -mmcu=$(MCU) -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -mmcu=$(MCU) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -mmcu=$(MCU) $(LDFLAGS) -o $@ $(OBJECTS)
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
	mkdir -p $(SD_GAMES_DIR)
	$(OBJCOPY) -I ihex -O binary $< $(SD_GAMES_DIR)/$(APP_NAME).BIN

clean:
	rm -rf $(BUILD_DIR) $(TARGET).elf $(TARGET).hex $(TARGET)_boot.hex $(TARGET).asm

.PHONY: all help program bin clean

-include $(OBJECTS:.o=.d)
