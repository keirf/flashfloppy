
PROJ = FlashFloppy
VER = v0.5.0a

SUBDIRS += src bootloader reloader

.PHONY: all clean flash start serial gotek touch

ifneq ($(RULES_MK),y)
export ROOT := $(CURDIR)
all:
	$(MAKE) -C src -f $(ROOT)/Rules.mk $(PROJ).elf $(PROJ).bin $(PROJ).hex
	$(MAKE) -C bootloader -f $(ROOT)/Rules.mk \
		Bootloader.elf Bootloader.bin Bootloader.hex
	$(MAKE) -C reloader -f $(ROOT)/Rules.mk \
		Reloader.elf Reloader.bin Reloader.hex
	srec_cat bootloader/Bootloader.hex -Intel src/$(PROJ).hex -Intel \
	-o FF.hex -Intel
	python ./scripts/mk_update.py src/$(PROJ).bin FF.upd
	python ./scripts/mk_update.py bootloader/Bootloader.bin BL.rld
	python ./scripts/mk_update.py reloader/Reloader.bin RL.upd

clean:
	rm -f *.hex *.upd *.rld
	$(MAKE) -f $(ROOT)/Rules.mk $@

gotek: export gotek=y
gotek: all
	mv FF.upd FF_Gotek-$(VER).upd
	mv FF.hex FF_Gotek-$(VER).hex
	mv BL.rld FF_Gotek-Bootloader-$(VER).rld
	mv RL.upd FF_Gotek-Reloader-$(VER).upd

touch: export touch=y
touch: all

dist:
	rm -rf flashfloppy_*
	mkdir -p flashfloppy_$(VER)/reloader
	$(MAKE) clean
	$(MAKE) gotek
	cp -a FF_Gotek-$(VER).upd flashfloppy_$(VER)/
	cp -a FF_Gotek-$(VER).hex flashfloppy_$(VER)/
	cp -a FF_Gotek-Reloader-$(VER).upd flashfloppy_$(VER)/reloader/
	cp -a FF_Gotek-Bootloader-$(VER).rld flashfloppy_$(VER)/reloader/
	$(MAKE) clean
	cp -a COPYING flashfloppy_$(VER)/
	cp -a README.md flashfloppy_$(VER)/
	zip -r flashfloppy_$(VER).zip flashfloppy_$(VER)

mrproper: clean
	rm -rf flashfloppy_*

endif

BAUD=115200

flash:
	sudo stm32flash -b $(BAUD) -w FF_Gotek-$(VER).hex /dev/ttyUSB0

start:
	sudo stm32flash -b $(BAUD) -g 0 /dev/ttyUSB0

serial:
	sudo miniterm.py /dev/ttyUSB0 3000000
