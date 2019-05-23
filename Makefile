
export FW_VER := 2.11a

PROJ := FlashFloppy
VER := v$(FW_VER)

SUBDIRS += src bootloader bl_update

.PHONY: all upd clean flash start serial gotek

ifneq ($(RULES_MK),y)

.DEFAULT_GOAL := gotek
export ROOT := $(CURDIR)

all:
	$(MAKE) -f $(ROOT)/Rules.mk all

clean:
	rm -f *.hex *.upd *.dfu *.html
	$(MAKE) -f $(ROOT)/Rules.mk $@

gotek: all
	mv FF.dfu FF_Gotek-$(VER).dfu
	mv FF.upd FF_Gotek-$(VER).upd
	mv FF.hex FF_Gotek-$(VER).hex
	mv BL.upd FF_Gotek-Bootloader-$(VER).upd

HXC_FF_URL := https://www.github.com/keirf/HxC_FF_File_Selector
HXC_FF_URL := $(HXC_FF_URL)/releases/download
HXC_FF_VER := v5-FF

dist:
	rm -rf flashfloppy-*
	mkdir -p flashfloppy-$(VER)/alt/bootloader
	mkdir -p flashfloppy-$(VER)/alt/logfile
	$(MAKE) clean
	$(MAKE) gotek
	cp -a FF_Gotek-$(VER).dfu flashfloppy-$(VER)/
	cp -a FF_Gotek-$(VER).upd flashfloppy-$(VER)/
	cp -a FF_Gotek-$(VER).hex flashfloppy-$(VER)/
	cp -a FF_Gotek-Bootloader-$(VER).upd flashfloppy-$(VER)/alt/bootloader/
	$(MAKE) clean
	debug=n logfile=y $(MAKE) -f $(ROOT)/Rules.mk upd
	mv FF.upd flashfloppy-$(VER)/alt/logfile/FF_Gotek-Logfile-$(VER).upd
	$(MAKE) clean
	cp -a COPYING flashfloppy-$(VER)/
	cp -a README.md flashfloppy-$(VER)/
	cp -a RELEASE_NOTES flashfloppy-$(VER)/
	cp -a examples flashfloppy-$(VER)/
	[ -e HxC_Compat_Mode-$(HXC_FF_VER).zip ] || \
	wget -q --show-progress $(HXC_FF_URL)/$(HXC_FF_VER)/HxC_Compat_Mode-$(HXC_FF_VER).zip
	rm -rf index.html
	unzip -q HxC_Compat_Mode-$(HXC_FF_VER).zip
	mv HxC_Compat_Mode flashfloppy-$(VER)
	mkdir -p flashfloppy-$(VER)/scripts
	cp -a scripts/edsk* flashfloppy-$(VER)/scripts/
	cp -a scripts/mk_hfe.py flashfloppy-$(VER)/scripts/
	zip -r flashfloppy-$(VER).zip flashfloppy-$(VER)

mrproper: clean
	rm -rf flashfloppy-*
	rm -rf HxC_Compat_Mode-$(HXC_FF_VER).zip

else

upd:
	$(MAKE) -C src -f $(ROOT)/Rules.mk $(PROJ).elf $(PROJ).bin $(PROJ).hex
	$(PYTHON) ./scripts/mk_update.py src/$(PROJ).bin FF.upd

all:
	$(MAKE) -C src -f $(ROOT)/Rules.mk $(PROJ).elf $(PROJ).bin $(PROJ).hex
	bootloader=y $(MAKE) -C bootloader -f $(ROOT)/Rules.mk \
		Bootloader.elf Bootloader.bin Bootloader.hex
	logfile=n $(MAKE) -C bl_update -f $(ROOT)/Rules.mk \
		BL_Update.elf BL_Update.bin BL_Update.hex
	srec_cat bootloader/Bootloader.hex -Intel src/$(PROJ).hex -Intel \
	-o FF.hex -Intel
	$(PYTHON) ./scripts/mk_update.py src/$(PROJ).bin FF.upd
	$(PYTHON) ./scripts/mk_update.py bl_update/BL_Update.bin BL.upd
	$(PYTHON) ./scripts/dfu-convert.py -i FF.hex FF.dfu

endif

BAUD=115200
DEV=/dev/ttyUSB0

flash:
	sudo stm32flash -b $(BAUD) -w FF_Gotek-$(VER).hex $(DEV)

start:
	sudo stm32flash -b $(BAUD) -g 0 $(DEV)

serial:
	sudo miniterm.py $(DEV) 3000000
