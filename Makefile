
PROJ = FlashFloppy
VER = v0.2.1a

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
	rm -rf flashfloppy_*
	mkdir -p flashfloppy_$(VER)/doc
	$(MAKE) clean
	$(MAKE) gotek
	cp -a FF.upd flashfloppy_$(VER)/FF_Gotek-$(VER).upd
	cp -a FF.hex flashfloppy_$(VER)/FF_Gotek-$(VER).hex
	$(MAKE) clean
#	$(MAKE) touch
#	cp -a FF.upd flashfloppy_$(VER)/FF_Touch-$(VER).upd
#	cp -a FF.hex flashfloppy_$(VER)/FF_Touch-$(VER).hex
#	$(MAKE) clean
	cp -a COPYING flashfloppy_$(VER)/
	cp -a README.md flashfloppy_$(VER)/
	cp -a doc/*.md flashfloppy_$(VER)/doc/
	zip -r flashfloppy_$(VER).zip flashfloppy_$(VER)

mrproper: clean
	rm -rf flashfloppy_*

endif

BAUD=115200

flash:
	sudo stm32flash -b $(BAUD) -w FF.hex /dev/ttyUSB0

start:
	sudo stm32flash -b $(BAUD) -g 0 /dev/ttyUSB0

serial:
	sudo miniterm.py /dev/ttyUSB0 3000000
