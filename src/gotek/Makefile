OBJS += board.o
OBJS += lcd.o
OBJS += led_3dig.o
OBJS += speaker.o
OBJS += usb_bsp.o
OBJS += usbh_msc_fatfs.o

ifeq ($(font_7x16),y)
OBJS += oled_font_7x16.o
lcd.o: CFLAGS += -Dfont_7x16=1
else
OBJS += oled_font_8x16.o
lcd.o: CFLAGS += -Dfont_8x16=1
endif

SUBDIRS += stm32_usbh_msc

usb%.o: CFLAGS += -Istm32_usbh_msc/inc/ -include usbh_conf.h
