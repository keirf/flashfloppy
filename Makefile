
PROJ = FlashFloppy

SUBDIRS += src

.PHONY: all clean flash start serial

ifneq ($(RULES_MK),y)
export ROOT := $(CURDIR)
all:
	$(MAKE) -C src -f $(ROOT)/Rules.mk $(PROJ).elf $(PROJ).bin $(PROJ).hex
clean:
	$(MAKE) -f $(ROOT)/Rules.mk $@
endif

FLASH=0x8000000
BAUD=921600

flash: all
	sudo ~/stm32flash/stm32flash -S $(FLASH) -g $(FLASH) \
	-b $(BAUD) -v -w src/$(PROJ).bin /dev/ttyUSB0

start:
	sudo ~/stm32flash/stm32flash -b $(BAUD) -g $(FLASH) /dev/ttyUSB0

serial:
	sudo miniterm.py /dev/ttyUSB0 3000000
