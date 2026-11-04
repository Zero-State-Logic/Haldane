# HALDANE - ATmega32 firmware with plain avr-gcc / avrdude.
#   make            build haldane.hex
#   make size       flash / RAM usage
#   make flash      program over USBasp
#   make fuses      lfuse 0x3F (16 MHz crystal, BOD 4.0 V) / hfuse 0xC9 (JTAG off)
MCU     = atmega32
F_CPU   = 16000000UL
CC      = avr-gcc
OBJCOPY = avr-objcopy
SIZE    = avr-size
CORE    = ../core
SRC     = $(wildcard *.c) $(wildcard $(CORE)/*.c)
CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=gnu99 -Wall -Wextra \
          -I$(CORE) -DHD_ROM=__flash -ffunction-sections -fdata-sections
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections

all: haldane.hex

haldane.elf: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

haldane.hex: haldane.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: haldane.elf
	$(SIZE) -A $<
	$(SIZE) $<

flash: haldane.hex
	avrdude -c usbasp -p m32 -U flash:w:haldane.hex:i

fuses:
	avrdude -c usbasp -p m32 -U lfuse:w:0x3F:m -U hfuse:w:0xC9:m

clean:
	rm -f haldane.elf haldane.hex

.PHONY: all size flash fuses clean
