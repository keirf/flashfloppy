
PROJ = FlashFloppy
VER = v0.9.27a.256KB

SUBDIRS += src bootloader reloader

.PHONY: all clean flash start serial gotek

ifneq ($(RULES_MK),y)

.DEFAULT_GOAL := gotek
export ROOT := $(CURDIR)

all:
	$(MAKE) -f $(ROOT)/Rules.mk all

clean:
	rm -f *.hex *.upd *.rld *.dfu *.html
	$(MAKE) -f $(ROOT)/Rules.mk $@

gotek: export gotek=y
gotek: all
	mv FF.dfu FF_Gotek-$(VER).dfu
	mv FF.upd FF_Gotek-$(VER).upd
	mv FF.hex FF_Gotek-$(VER).hex
	mv BL.rld FF_Gotek-Bootloader-$(VER).rld
	mv RL.upd FF_Gotek-Reloader-$(VER).upd

HXC_FF_URL := https://www.github.com/keirf/HxC_FF_File_Selector
HXC_FF_URL := $(HXC_FF_URL)/releases/download
HXC_FF_VER := v1.71-ff

dist:
	rm -rf flashfloppy_*
	mkdir -p flashfloppy_$(VER)/reloader
	$(MAKE) clean
	$(MAKE) gotek
	cp -a FF_Gotek-$(VER).dfu flashfloppy_$(VER)/
	cp -a FF_Gotek-$(VER).upd flashfloppy_$(VER)/
	cp -a FF_Gotek-$(VER).hex flashfloppy_$(VER)/
	cp -a FF_Gotek-Reloader-$(VER).upd flashfloppy_$(VER)/reloader/
	cp -a FF_Gotek-Bootloader-$(VER).rld flashfloppy_$(VER)/reloader/
	$(MAKE) clean
	cp -a COPYING flashfloppy_$(VER)/
	cp -a README.md flashfloppy_$(VER)/
	cp -a RELEASE_NOTES flashfloppy_$(VER)/
	cp -a examples flashfloppy_$(VER)/
	[ -e HxC_Compat_Mode-$(HXC_FF_VER).zip ] || \
	wget -q --show-progress $(HXC_FF_URL)/$(HXC_FF_VER)/HxC_Compat_Mode-$(HXC_FF_VER).zip
	rm -rf index.html
	unzip -q HxC_Compat_Mode-$(HXC_FF_VER).zip
	mv HxC_Compat_Mode flashfloppy_$(VER)
	zip -r flashfloppy_$(VER).zip flashfloppy_$(VER)

mrproper: clean
	rm -rf flashfloppy_*
	rm -rf HxC_Compat_Mode-$(HXC_FF_VER).zip

else

all:
	$(MAKE) -C src -f $(ROOT)/Rules.mk $(PROJ).elf $(PROJ).bin $(PROJ).hex
	bootloader=y $(MAKE) -C bootloader -f $(ROOT)/Rules.mk \
		Bootloader.elf Bootloader.bin Bootloader.hex
	reloader=y $(MAKE) -C reloader -f $(ROOT)/Rules.mk \
		Reloader.elf Reloader.bin Reloader.hex
	srec_cat bootloader/Bootloader.hex -Intel src/$(PROJ).hex -Intel \
	-o FF.hex -Intel
	$(PYTHON) ./scripts/mk_update.py src/$(PROJ).bin FF.upd
	$(PYTHON) ./scripts/mk_update.py bootloader/Bootloader.bin BL.rld
	$(PYTHON) ./scripts/mk_update.py reloader/Reloader.bin RL.upd
	$(PYTHON) ./scripts/dfu-convert.py -i FF.hex FF.dfu

endif

BAUD=115200

flash:
	sudo stm32flash -b $(BAUD) -w FF_Gotek-$(VER).hex /dev/ttyUSB0

start:
	sudo stm32flash -b $(BAUD) -g 0 /dev/ttyUSB0

serial:
	sudo miniterm.py /dev/ttyUSB0 3000000
