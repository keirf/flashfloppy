OBJS += board.o
OBJS += lcd.o
OBJS += led_7seg.o
OBJS += speaker.o
OBJS += usb_bsp.o
OBJS += usbh_msc_fatfs.o

OBJS += oled_font_7x16.o
lcd.o: CFLAGS += -Dfont_7x16=1
OBJS += oled_font_8x16.o

SUBDIRS += stm32_usbh_msc

usb%.o: CFLAGS += -Istm32_usbh_msc/inc/ -include usbh_conf.h
