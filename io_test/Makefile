RPATH = ../src

OBJS += io_test.o
OBJS += fpec_$(mcu).o
OBJS += build_info.o
OBJS += crc.o
OBJS += vectors.o
OBJS += string.o
OBJS += cortex.o mcu_$(mcu).o
OBJS += time.o
OBJS += timer.o
OBJS += util.o
OBJS += flash_cfg.o

OBJS-$(debug) += console.o

SUBDIRS += display
SUBDIRS += gotek

.PHONY: $(RPATH)/build_info.c
build_info.o: CFLAGS += -DFW_VER="\"$(FW_VER)\""
