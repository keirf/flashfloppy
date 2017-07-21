
PROJ = FlashFloppy
VER = v0.1a

SUBDIRS += src bootloader

.PHONY: all clean flash start serial

ifneq ($(RULES_MK),y)
export ROOT := $(CURDIR)
all:
	$(MAKE) -C src -f $(ROOT)/Rules.mk $(PROJ).elf $(PROJ).bin $(PROJ).hex
	$(MAKE) -C bootloader -f $(ROOT)/Rules.mk \
	Bootloader.elf Bootloader.bin Bootloader.hex
	srec_cat bootloader/Bootloader.hex -Intel src/$(PROJ).hex -Intel \
	-o FF.hex -Intel
	python ./scripts/mk_update.py src/$(PROJ).bin FF.upd

clean:
	rm -f *.hex *.upd
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
	cp -a FF.upd flashfloppy_fw/FF_Gotek-$(VER).upd
	cp -a FF.hex flashfloppy_fw/FF_Gotek-$(VER).hex
	$(MAKE) clean
#	$(MAKE) touch
#	cp -a FF.upd flashfloppy_fw/FF_Touch-$(VER).upd
#	cp -a FF.hex flashfloppy_fw/FF_Touch-$(VER).hex
#	$(MAKE) clean
	cp -a README.md flashfloppy_fw/
	zip -r flashfloppy_fw flashfloppy_fw

mrproper: clean
	rm -rf flashfloppy_fw*

endif

BAUD=115200

flash:
	sudo stm32flash -b $(BAUD) -w FF.hex /dev/ttyUSB0

start:
	sudo stm32flash -b $(BAUD) -g 0 /dev/ttyUSB0

serial:
	sudo miniterm.py /dev/ttyUSB0 3000000
