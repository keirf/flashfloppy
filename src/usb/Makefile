OBJS += usb_bsp.o
OBJS += usbh_msc_fatfs.o

SUBDIRS += stm32_usbh_msc

usb%.o: CFLAGS += -I$(SRCDIR)/stm32_usbh_msc/inc/ -include usbh_conf.h
