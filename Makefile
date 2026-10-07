
PROJ := flashfloppy
VER := 3.45-dual

export FW_VER := $(VER)

PYTHON := python3

export ROOT := $(CURDIR)

# Build products are placed in $(O)/out. Downloads are cached in $(O)/ext.
# Specify O=<path> for an out-of-tree build.
O ?= $(ROOT)
OUT := $(abspath $(O))/out
EXT := $(abspath $(O))/ext

.PHONY: FORCE

.DEFAULT_GOAL := all

prod-%: FORCE
	$(MAKE) target mcu=$* target=bootloader level=prod
	$(MAKE) target mcu=$* target=shugart level=prod
	$(MAKE) target mcu=$* target=apple2 level=prod
	$(MAKE) target mcu=$* target=quickdisk level=prod
	$(MAKE) target mcu=$* target=dual level=prod
	$(MAKE) target mcu=$* target=bl_update level=prod
	$(MAKE) target mcu=$* target=io_test level=prod

debug-%: FORCE
	$(MAKE) target mcu=$* target=bootloader level=debug
	$(MAKE) target mcu=$* target=shugart level=debug
	$(MAKE) target mcu=$* target=apple2 level=debug
	$(MAKE) target mcu=$* target=quickdisk level=debug
	$(if $(filter stm32f105,$*),,$(MAKE) target mcu=$* target=dual level=debug)
	$(MAKE) target mcu=$* target=bl_update level=debug
	$(MAKE) target mcu=$* target=io_test level=debug

logfile-%: FORCE
	$(MAKE) target mcu=$* target=bootloader level=logfile
	$(MAKE) target mcu=$* target=shugart level=logfile
	$(MAKE) target mcu=$* target=apple2 level=logfile
	$(MAKE) target mcu=$* target=quickdisk level=logfile
	$(if $(filter stm32f105,$*),,$(MAKE) target mcu=$* target=dual level=logfile)

# The 128kB devices fit the combined production firmware. Additional debug
# logging does not fit; their standalone debug/logfile builds remain available.
dual-%: FORCE
	$(MAKE) target mcu=$* target=bootloader level=prod
	$(MAKE) target mcu=$* target=dual level=prod

apple2-bootloader-%: FORCE
	$(MAKE) target mcu=$* target=apple2-bootloader level=prod

all-%: FORCE prod-% debug-% logfile-% ;

all: FORCE all-stm32f105 all-at32f435 apple2-bootloader-stm32f105;

clean: FORCE
	rm -rf $(OUT)

mrproper: FORCE clean
	rm -rf $(EXT)

out: FORCE
	+mkdir -p $(OUT)/$(mcu)/$(level)/$(target)

target: FORCE out
	$(MAKE) -C $(OUT)/$(mcu)/$(level)/$(target) -f $(ROOT)/Rules.mk target.bin target.hex target.dfu

HXC_FF_URL := https://www.github.com/keirf/flashfloppy-hxc-file-selector
HXC_FF_URL := $(HXC_FF_URL)/releases/download
HXC_FF_VER := v9-FF

_legacy_dist: PROJ := FF_Gotek
_legacy_dist: FORCE
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/$(PROJ)-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/shugart/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/bootloader/$(PROJ)-bootloader-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/bl_update/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/io-test/$(PROJ)-io-test-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/io_test/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/logfile/$(PROJ)-logfile-$(VER).upd \
	  $(OUT)/$(mcu)/logfile/shugart/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/apple2/$(PROJ)-apple2-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/apple2/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/apple2/logfile/$(PROJ)-apple2-logfile-$(VER).upd \
	  $(OUT)/$(mcu)/logfile/apple2/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/quickdisk/$(PROJ)-quickdisk-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/quickdisk/target.bin & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/quickdisk/logfile/$(PROJ)-quickdisk-logfile-$(VER).upd \
	  out/$(mcu)/logfile/quickdisk/target.bin & \
	if [ "$(level)" = prod ]; then \
	$(PYTHON) $(ROOT)/scripts/mk_update.py old \
	  $(t)/alt/dual/$(PROJ)-dual-$(VER).upd \
	  out/$(mcu)/$(level)/dual/target.bin; fi & \
	wait

_dist: FORCE
	cd $(OUT)/$(mcu)/$(level)/shugart; \
	  cp -a target.dfu $(t)/dfu/$(PROJ)-$(n)-$(VER).dfu; \
	  cp -a target.hex $(t)/hex/$(PROJ)-$(n)-$(VER).hex
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/$(PROJ)-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/shugart/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/bootloader/$(PROJ)-bootloader-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/bl_update/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/io-test/$(PROJ)-io-test-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/io_test/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/logfile/$(PROJ)-logfile-$(VER).upd \
	  $(OUT)/$(mcu)/logfile/shugart/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/apple2/$(PROJ)-apple2-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/apple2/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/apple2/logfile/$(PROJ)-apple2-logfile-$(VER).upd \
	  $(OUT)/$(mcu)/logfile/apple2/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/quickdisk/$(PROJ)-quickdisk-$(VER).upd \
	  $(OUT)/$(mcu)/$(level)/quickdisk/target.bin $(mcu) & \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/quickdisk/logfile/$(PROJ)-quickdisk-logfile-$(VER).upd \
	  out/$(mcu)/logfile/quickdisk/target.bin $(mcu) & \
	if [ "$(mcu)" != stm32f105 ] || [ "$(level)" = prod ]; then \
	$(PYTHON) $(ROOT)/scripts/mk_update.py new \
	  $(t)/alt/dual/$(PROJ)-dual-$(VER).upd \
	  out/$(mcu)/$(level)/dual/target.bin $(mcu); fi & \
	wait
	if [ "$(mcu)" != stm32f105 ] || [ "$(level)" = prod ]; then \
	  cp -a out/$(mcu)/$(level)/dual/target.hex $(t)/hex/$(PROJ)-dual-$(n)-$(VER).hex; \
	  cp -a out/$(mcu)/$(level)/dual/target.dfu $(t)/dfu/$(PROJ)-dual-$(n)-$(VER).dfu; \
	fi

_dist_apple2_at2_bootloader: f := $(t)/alt/apple2/at2-bootloader
_dist_apple2_at2_bootloader: n := $(PROJ)-apple2-at2-bootloader-$(VER)
_dist_apple2_at2_bootloader: FORCE
	mkdir -p $(f)
	cd $(OUT)/stm32f105/prod/apple2-bootloader; \
	  cp -a target.dfu $(f)/$(n).dfu; \
	  cp -a target.hex $(f)/$(n).hex

dist: level := prod
dist: t := $(OUT)/$(PROJ)-$(VER)
dist: FORCE all
	rm -rf $(OUT)/$(PROJ)-*
	mkdir -p $(t)/hex
	mkdir -p $(t)/dfu
	mkdir -p $(t)/alt/bootloader
	mkdir -p $(t)/alt/logfile
	mkdir -p $(t)/alt/io-test
	mkdir -p $(t)/alt/apple2/logfile
	mkdir -p $(t)/alt/quickdisk/logfile
	mkdir -p $(t)/alt/dual
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
	[ -e $(EXT)/HxC_Compat_Mode-$(HXC_FF_VER).zip ] || \
	(mkdir -p $(EXT) ; cd $(EXT) ; wget -q --show-progress $(HXC_FF_URL)/$(HXC_FF_VER)/HxC_Compat_Mode-$(HXC_FF_VER).zip ; rm -rf index.html)
	(cd $(t) && unzip -q $(EXT)/HxC_Compat_Mode-$(HXC_FF_VER).zip)
	mkdir -p $(t)/scripts
	cp -a scripts/edsk* $(t)/scripts/
	cp -a scripts/mk_hfe.py $(t)/scripts/
	cd $(OUT) && zip -r $(PROJ)-$(VER).zip $(PROJ)-$(VER)

BAUD=115200
DEV=/dev/ttyUSB0
SUDO=sudo
STM32FLASH=stm32flash
T=$(OUT)/$(target)/target.hex

ocd: FORCE all
	$(PYTHON) scripts/openocd/flash.py $(T)

flash: FORCE all
	$(SUDO) $(STM32FLASH) -b $(BAUD) -w $(T) $(DEV)

start: FORCE
	$(SUDO) $(STM32FLASH) -b $(BAUD) -g 0 $(DEV)

serial: FORCE
	$(SUDO) miniterm.py $(DEV) 3000000
