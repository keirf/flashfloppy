RPATH = ../../src/gotek

OBJS += board.o
OBJS += lcd.o
OBJS += led_3dig.o
OBJS += usb_bsp.o
OBJS += usbh_msc_fatfs.o

SUBDIRS += stm32_usbh_msc

usb%.o: CFLAGS += -I$(ROOT)/src/gotek/stm32_usbh_msc/inc/ -include usbh_conf.h
