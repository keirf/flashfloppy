
PROJ = FlashFloppy

SUBDIRS += src

.PHONY: all clean flash start serial

ifneq ($(RULES_MK),y)
export ROOT := $(CURDIR)
all:
	$(MAKE) -C src -f $(ROOT)/Rules.mk $(PROJ).elf $(PROJ).bin $(PROJ).hex

clean:
	$(MAKE) -f $(ROOT)/Rules.mk $@

gotek: export gotek=y
gotek: all

touch: export touch=y
touch: all

dist:
	rm -rf flashfloppy_fw*
	mkdir -p flashfloppy_fw
	$(MAKE) clean
	$(MAKE) gotek
	cp -a src/FlashFloppy.bin flashfloppy_fw/FF_Gotek.bin
	cp -a src/FlashFloppy.hex flashfloppy_fw/FF_Gotek.hex
	cp -a src/FlashFloppy.elf flashfloppy_fw/FF_Gotek.elf
	$(MAKE) clean
	$(MAKE) touch
	cp -a src/FlashFloppy.bin flashfloppy_fw/FF_Touch.bin
	cp -a src/FlashFloppy.hex flashfloppy_fw/FF_Touch.hex
	cp -a src/FlashFloppy.elf flashfloppy_fw/FF_Touch.elf
	$(MAKE) clean
	zip -r flashfloppy_fw flashfloppy_fw

mrproper: clean
	rm -rf flashfloppy_fw*

endif

FLASH=0x8000000
BAUD=921600

flash: all
	sudo ~/stm32flash/stm32flash -S $(FLASH) -g $(FLASH) \
	-b $(BAUD) -v -w src/$(PROJ).hex /dev/ttyUSB0

start:
	sudo ~/stm32flash/stm32flash -b $(BAUD) -g $(FLASH) /dev/ttyUSB0

serial:
	sudo miniterm.py /dev/ttyUSB0 3000000
