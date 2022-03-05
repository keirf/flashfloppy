OBJS += arena.o
OBJS += build_info.o
OBJS += cache.o
OBJS += cancellation.o
OBJS += config.o
OBJS += crc.o
OBJS += flash_cfg.o
OBJS += vectors.o
OBJS += fpec_$(mcu).o
OBJS += fs.o
OBJS += fs_async.o
OBJS += main.o
OBJS += ring_io.o
OBJS += sd_spi.o
OBJS += spi.o
OBJS += string.o
OBJS += thread.o
OBJS += cortex.o mcu_$(mcu).o
OBJS += time.o
OBJS += timer.o
OBJS += util.o
OBJS += volume.o

OBJS-$(floppy) += floppy.o
OBJS-$(quickdisk) += quickdisk.o
OBJS-$(debug) += console.o
OBJS-$(logfile) += logfile.o

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

main.o flash_cfg.o: ff_cfg_defaults.h

clean::
	rm -f ff_cfg_defaults.h
