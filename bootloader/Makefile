RPATH = ../src

OBJS += update.o
OBJS += fpec.o

OBJS += build_info.o
OBJS += cancellation.o
OBJS += crc.o
OBJS += vectors.o
OBJS += fs.o
OBJS += spi.o
OBJS += string.o
OBJS += stm32f10x.o
OBJS += time.o
OBJS += timer.o
OBJS += util.o
OBJS += volume.o
OBJS += flash_cfg.o

OBJS-$(debug) += console.o

SUBDIRS += display
SUBDIRS += fatfs
SUBDIRS += gotek
SUBDIRS += usb

.PHONY: $(RPATH)/build_info.c
build_info.o: CFLAGS += -DFW_VER="\"$(FW_VER)\""
