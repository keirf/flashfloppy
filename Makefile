
PROJ := flashfloppy
VER := $(shell git rev-parse --short HEAD)

export FW_VER := $(VER)

PYTHON := python3

export ROOT := $(CURDIR)

.PHONY: FORCE

.DEFAULT_GOAL := all

prod-%: FORCE
	$(MAKE) target mcu=$* target=bootloader level=prod
	$(MAKE) target mcu=$* target=shugart level=prod
	$(MAKE) target mcu=$* target=apple2 level=prod
	$(MAKE) target mcu=$* target=quickdisk level=prod
	$(MAKE) target mcu=$* target=bl_update level=prod
	$(MAKE) target mcu=$* target=io_test level=prod

debug-%: FORCE
	$(MAKE) target mcu=$* target=bootloader level=debug
	$(MAKE) target mcu=$* target=shugart level=debug
	$(MAKE) target mcu=$* target=apple2 level=debug
	$(MAKE) target mcu=$* target=quickdisk level=debug
	$(MAKE) target mcu=$* target=bl_update level=debug
	$(MAKE) target mcu=$* target=io_test level=debug

logfile-%: FORCE
	$(MAKE) target mcu=$* target=bootloader level=logfile
	$(MAKE) target mcu=$* target=shugart level=logfile
	$(MAKE) target mcu=$* target=apple2 level=logfile
	$(MAKE) target mcu=$* target=quickdisk level=logfile

apple2-bootloader-%: FORCE
	$(MAKE) target mcu=$* target=apple2-bootloader level=prod

all-%: FORCE prod-% debug-% logfile-% ;

all: FORCE all-stm32f105 all-at32f435 apple2-bootloader-stm32f105;

clean: FORCE
	rm -rf out

mrproper: FORCE clean
	rm -rf ext

out: FORCE
	+mkdir -p out/$(mcu)/$(level)/$(target)

target: FORCE out
	$(MAKE) -C out/$(mcu)/$(level)/$(target) -f $(ROOT)/Rules.mk target.bin target.hex target.dfu

HXC_FF_URL := https://www.github.com/keirf/flashfloppy-hxc-file-selector
HXC_FF_URL := $(HXC_FF_URL)/releases/download
HXC_FF_VER := v9-FF

_legacy_dist: PROJ := FF_Gotek
_legacy_dist: FORCE
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/$(PROJ)-$(VER).upd \
	  out/$(mcu)/$(level)/shugart/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/bootloader/$(PROJ)-bootloader-$(VER).upd \
	  out/$(mcu)/$(level)/bl_update/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/io-test/$(PROJ)-io-test-$(VER).upd \
	  out/$(mcu)/$(level)/io_test/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/logfile/$(PROJ)-logfile-$(VER).upd \
	  out/$(mcu)/logfile/shugart/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/apple2/$(PROJ)-apple2-$(VER).upd \
	  out/$(mcu)/$(level)/apple2/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/apple2/logfile/$(PROJ)-apple2-logfile-$(VER).upd \
	  out/$(mcu)/logfile/apple2/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/quickdisk/$(PROJ)-quickdisk-$(VER).upd \
	  out/$(mcu)/$(level)/quickdisk/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/quickdisk/logfile/$(PROJ)-quickdisk-logfile-$(VER).upd \
	  out/$(mcu)/logfile/quickdisk/target.bin & \
	wait

_dist: FORCE
	cd out/$(mcu)/$(level)/shugart; \
	  cp -a target.dfu $(t)/dfu/$(PROJ)-$(n)-$(VER).dfu; \
	  cp -a target.hex $(t)/hex/$(PROJ)-$(n)-$(VER).hex
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/$(PROJ)-$(VER).upd \
	  out/$(mcu)/$(level)/shugart/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/bootloader/$(PROJ)-bootloader-$(VER).upd \
	  out/$(mcu)/$(level)/bl_update/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/io-test/$(PROJ)-io-test-$(VER).upd \
	  out/$(mcu)/$(level)/io_test/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/logfile/$(PROJ)-logfile-$(VER).upd \
	  out/$(mcu)/logfile/shugart/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/apple2/$(PROJ)-apple2-$(VER).upd \
	  out/$(mcu)/$(level)/apple2/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/apple2/logfile/$(PROJ)-apple2-logfile-$(VER).upd \
	  out/$(mcu)/logfile/apple2/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/quickdisk/$(PROJ)-quickdisk-$(VER).upd \
	  out/$(mcu)/$(level)/quickdisk/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/quickdisk/logfile/$(PROJ)-quickdisk-logfile-$(VER).upd \
	  out/$(mcu)/logfile/quickdisk/target.bin $(mcu) & \
	wait

_dist_apple2_at2_bootloader: f := $(t)/alt/apple2/at2-bootloader
_dist_apple2_at2_bootloader: n := $(PROJ)-apple2-at2-bootloader-$(VER)
_dist_apple2_at2_bootloader: FORCE
	mkdir -p $(f)
	cd out/stm32f105/prod/apple2-bootloader; \
	  cp -a target.dfu $(f)/$(n).dfu; \
	  cp -a target.hex $(f)/$(n).hex

dist: level := prod
dist: t := $(ROOT)/out/$(PROJ)-$(VER)
dist: FORCE all
	rm -rf out/$(PROJ)-*
	mkdir -p $(t)/hex
	mkdir -p $(t)/dfu
	mkdir -p $(t)/alt/bootloader
	mkdir -p $(t)/alt/logfile
	mkdir -p $(t)/alt/io-test
	mkdir -p $(t)/alt/apple2/logfile
	mkdir -p $(t)/alt/quickdisk/logfile
	$(MAKE) _legacy_dist mcu=stm32f105 level=$(level) t=$(t)
	$(MAKE) _dist mcu=stm32f105 n=at415-st105 level=$(level) t=$(t)
	$(MAKE) _dist mcu=at32f435 n=at435 level=$(level) t=$(t)
	$(MAKE) _dist_apple2_at2_bootloader level=$(level) t=$(t)
	$(PYTHON) scripts/mk_qd.py --window=6.5 $(t)/alt/quickdisk/Blank.qd
	cp -a COPYING $(t)/
	cp -a README $(t)/
	cp -a RELEASE_NOTES $(t)/
	cp -a examples $(t)/
	# Clive Drive is particularly fussy about QD timings.
	$(PYTHON) scripts/mk_qd.py --window=6.4 --total=7.5 --round $(t)/examples/Host/Sinclair_ZX_Spectrum/Clive_Drive/CliveDrive_Blank.qd
	[ -e ext/HxC_Compat_Mode-$(HXC_FF_VER).zip ] || \
	(mkdir -p ext ; cd ext ; wget -q --show-progress $(HXC_FF_URL)/$(HXC_FF_VER)/HxC_Compat_Mode-$(HXC_FF_VER).zip ; rm -rf index.html)
	(cd $(t) && unzip -q ../../ext/HxC_Compat_Mode-$(HXC_FF_VER).zip)
	mkdir -p $(t)/scripts
	cp -a scripts/edsk* $(t)/scripts/
	cp -a scripts/mk_hfe.py $(t)/scripts/
	cd out && zip -r $(PROJ)-$(VER).zip $(PROJ)-$(VER)

BAUD=115200
DEV=/dev/ttyUSB0
SUDO=sudo
STM32FLASH=stm32flash
T=out/$(target)/target.hex

ocd: FORCE all
	$(PYTHON) scripts/openocd/flash.py $(T)

flash: FORCE all
	$(SUDO) $(STM32FLASH) -b $(BAUD) -w $(T) $(DEV)

start: FORCE
	$(SUDO) $(STM32FLASH) -b $(BAUD) -g 0 $(DEV)

serial: FORCE
	$(SUDO) miniterm.py $(DEV) 3000000
