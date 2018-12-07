OBJS += arena.o
OBJS += build_info.o
OBJS += cancellation.o
OBJS += config.o
OBJS += crc.o
OBJS += flash_cfg.o
OBJS += vectors.o
OBJS += floppy.o
OBJS += fpec.o
OBJS += fs.o
OBJS += main.o
OBJS += sd_spi.o
OBJS += spi.o
OBJS += string.o
OBJS += stm32f10x.o
OBJS += time.o
OBJS += timer.o
OBJS += util.o
OBJS += volume.o

OBJS-$(debug) += console.o

SUBDIRS += display
SUBDIRS += fatfs
SUBDIRS += image
SUBDIRS += gotek
SUBDIRS += usb

.PHONY: build_info.c
build_info.o: CFLAGS += -DFW_VER="\"$(FW_VER)\""

flash_cfg.o: ff_cfg_defaults.h

ff_cfg_defaults.h: $(ROOT)/examples/FF.CFG
	$(PYTHON) $(ROOT)/scripts/mk_config.py $< $@

clean::
	rm -f ff_cfg_defaults.h
