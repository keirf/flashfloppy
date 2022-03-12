
export FW_VER := 4.4a

PROJ := FlashFloppy
VER := v$(FW_VER)

PYTHON := python3

export ROOT := $(CURDIR)

.PHONY: FORCE

.DEFAULT_GOAL := all

prod-%: FORCE
	$(eval export mcu := $*)
	$(MAKE) target target=bootloader level=prod
	$(MAKE) target target=floppy level=prod
	$(MAKE) target target=quickdisk level=prod
	$(MAKE) target target=bl_update level=prod
	$(MAKE) target target=io_test level=prod

debug-%: FORCE
	$(eval export mcu := $*)
	$(MAKE) target target=bootloader level=debug
	$(MAKE) target target=floppy level=debug
	$(MAKE) target target=quickdisk level=debug
	$(MAKE) target target=bl_update level=debug
	$(MAKE) target target=io_test level=debug

logfile-%: FORCE
	$(eval export mcu := $*)
	$(MAKE) target target=bootloader level=logfile
	$(MAKE) target target=floppy level=logfile
	$(MAKE) target target=quickdisk level=logfile

all-%: FORCE prod-% debug-% logfile-% ;

all: FORCE all-stm32f105 all-at32f435 ;

clean: FORCE
	rm -rf out

mrproper: FORCE clean
	rm -rf ext

out: FORCE
	mkdir -p out/$(mcu)/$(level)/$(target)
	rsync -a --include="*/" --exclude="*" src/ out/$(mcu)/$(level)/$(target)

target: FORCE out
	$(MAKE) -C out/$(mcu)/$(level)/$(target) -f $(ROOT)/Rules.mk target.bin target.hex target.upd target.dfu $(mcu)=y $(level)=y $(target)=y

HXC_FF_URL := https://www.github.com/keirf/flashfloppy-hxc-file-selector
HXC_FF_URL := $(HXC_FF_URL)/releases/download
HXC_FF_VER := v9-FF

dist: level := prod
dist: FORCE all
	rm -rf out/flashfloppy-*
	$(eval t := out/flashfloppy-$(VER))
	mkdir -p $(t)/alt/bootloader
	mkdir -p $(t)/alt/logfile
	mkdir -p $(t)/alt/io-test
	mkdir -p $(t)/alt/quickdisk/logfile
	$(eval s := out/$(mcu)/$(level)/floppy)
	cp -a $(s)/target.dfu $(t)/FF_Gotek-$(VER).dfu
	cp -a $(s)/target.upd $(t)/FF_Gotek-$(VER).upd
	cp -a $(s)/target.hex $(t)/FF_Gotek-$(VER).hex
	$(eval s := out/$(mcu)/$(level)/bootloader)
	cp -a $(s)/target.upd $(t)/alt/bootloader/FF_Gotek-Bootloader-$(VER).upd
	$(eval s := out/$(mcu)/$(level)/io_test)
	cp -a $(s)/target.upd $(t)/alt/io-test/FF_Gotek-IO-Test-$(VER).upd
	$(eval s := out/$(mcu)/logfile/floppy)
	cp -a $(s)/target.upd $(t)/alt/logfile/FF_Gotek-Logfile-$(VER).upd
	$(eval s := out/$(mcu)/$(level)/quickdisk)
	cp -a $(s)/target.upd $(t)/alt/quickdisk/FF_Gotek-QuickDisk-$(VER).upd
	$(eval s := out/$(mcu)/logfile/quickdisk)
	cp -a $(s)/target.upd $(t)/alt/quickdisk/logfile/FF_Gotek-QuickDisk-Logfile-$(VER).upd
	$(PYTHON) scripts/mk_qd.py --window=6.5 $(t)/alt/quickdisk/Blank.qd
	cp -a COPYING $(t)/
	cp -a README.md $(t)/
	cp -a RELEASE_NOTES $(t)/
	cp -a examples $(t)/
	[ -e ext/HxC_Compat_Mode-$(HXC_FF_VER).zip ] || \
	(mkdir -p ext ; cd ext ; wget -q --show-progress $(HXC_FF_URL)/$(HXC_FF_VER)/HxC_Compat_Mode-$(HXC_FF_VER).zip ; rm -rf index.html)
	(cd $(t) && unzip -q ../../ext/HxC_Compat_Mode-$(HXC_FF_VER).zip)
	mkdir -p $(t)/scripts
	cp -a scripts/edsk* $(t)/scripts/
	cp -a scripts/mk_hfe.py $(t)/scripts/
	(cd out && zip -r flashfloppy-$(VER).zip flashfloppy-$(VER))

BAUD=115200
DEV=/dev/ttyUSB0
SUDO=sudo
STM32FLASH=stm32flash

ocd: FORCE all
	$(PYTHON) scripts/openocd/flash.py $(target)/target.hex

flash: gotek
	$(SUDO) $(STM32FLASH) -b $(BAUD) -w $(target)/target.hex $(DEV)

start:
	$(SUDO) $(STM32FLASH) -b $(BAUD) -g 0 $(DEV)

serial:
	$(SUDO) miniterm.py $(DEV) 3000000
