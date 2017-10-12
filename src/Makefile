OBJS += arena.o
OBJS += cancellation.o
OBJS += config.o
OBJS += crc.o
OBJS += display.o
OBJS += vectors.o
OBJS += floppy.o
OBJS += fs.o
OBJS += main.o
OBJS += spi.o
OBJS += string.o
OBJS += stm32f10x.o
OBJS += timer.o
OBJS += util.o

OBJS-$(debug) += console.o

SUBDIRS += fatfs
SUBDIRS += image
SUBDIRS-$(gotek) += gotek
SUBDIRS-$(touch) += touch

main.o: ff_cfg_defaults.h

ff_cfg_defaults.h: ../examples/FF.CFG
	python ../scripts/mk_config.py $< $@

clean::
	rm -f ff_cfg_defaults.h
