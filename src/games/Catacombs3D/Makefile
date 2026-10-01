#=======================================

# Name of the project
TARGET = catacombs
# file name on SD card: ../../../sd/GAMES/$(APP_NAME).BIN (FAT16: 8 chars)
APP_NAME = CATACOMB

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

# game sources from Arduboy (Catacombs3D.ino is main.cpp)
CPP_SOURCES = main.cpp
CPP_SOURCES += Draw.cpp
CPP_SOURCES += Enemy.cpp
CPP_SOURCES += Entity.cpp
CPP_SOURCES += FixedMath.cpp
CPP_SOURCES += Font.cpp
CPP_SOURCES += Game.cpp
CPP_SOURCES += Map.cpp
CPP_SOURCES += MapGenerator.cpp
CPP_SOURCES += Menu.cpp
CPP_SOURCES += Particle.cpp
CPP_SOURCES += Player.cpp
CPP_SOURCES += Projectile.cpp
CPP_SOURCES += Sounds.cpp
CPP_SOURCES += $(ARDUBOY_DIR)/Arduboy.cpp
CPP_SOURCES += $(ARDUBOY_DIR)/Sprites.cpp

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
# the 3D view changes the whole screen every frame
CDEFS += -DARDUBOY_NO_LINE_HASH

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
CXXFLAGS += -fpermissive -Wno-narrowing -Wno-switch

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
