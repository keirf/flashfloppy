OBJS += arena.o
OBJS += build_info.o
OBJS += cancellation.o
OBJS += config.o
OBJS += crc.o
OBJS += flash_cfg.o
OBJS += vectors.o
OBJS += fpec_$(mcu).o
OBJS += fs.o
OBJS += sd_spi.o
OBJS += spi.o
OBJS += string.o
OBJS += cortex.o mcu_$(mcu).o
OBJS += time.o
OBJS += timer.o
OBJS += util.o
OBJS += volume.o
OBJS-$(debug) += console.o
OBJS-$(logfile) += logfile.o

ifeq ($(bl_update),y)

OBJS += bl_update.o
SUBDIRS += display gotek

else ifeq ($(io_test),y)

OBJS += io_test.o
SUBDIRS += display gotek

else ifeq ($(bootloader),y)

OBJS += fw_update.o
SUBDIRS += display fatfs gotek usb

else

OBJS += main.o
OBJS += cache.o

OBJS-$(apple2) += floppy.o
OBJS-$(floppy) += floppy.o
OBJS-$(quickdisk) += quickdisk.o

SUBDIRS += display
SUBDIRS += fatfs
SUBDIRS += image
SUBDIRS += gotek
SUBDIRS += usb

endif

.PHONY: $(SRCDIR)/build_info.c
build_info.o: CFLAGS += -DFW_VER="\"$(FW_VER)\""

# Avoid infinite loops due to GCC noticing code that can be replaced by a call
# to a standard library function... within our implementation of that function.
util.o: CFLAGS += -fno-tree-loop-distribute-patterns

ff_cfg_defaults.h: $(ROOT)/examples/FF.CFG
	$(PYTHON) $(ROOT)/scripts/mk_config.py $< $@

main.o flash_cfg.o: ff_cfg_defaults.h
main.o flash_cfg.o: CFLAGS += -iquote .
