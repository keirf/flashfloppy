OBJS += board.o
OBJS += lcd.o
OBJS += oled_font_6x13.o
OBJS += led_7seg.o
OBJS += speaker.o
OBJS += usb_bsp.o
OBJS += usbh_msc_fatfs.o

lcd.o: CFLAGS += -Dfont_extra=1
OBJS += oled_font_8x16.o

SUBDIRS += stm32_usbh_msc

usb%.o: CFLAGS += -Istm32_usbh_msc/inc/ -include usbh_conf.h
