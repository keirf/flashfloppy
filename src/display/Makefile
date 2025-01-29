OBJS += display.o
OBJS += lcd_$(mcu).o
OBJS += oled_font_6x13.o
OBJS += led_7seg.o

ifneq ($(target),bootloader)
lcd_$(mcu).o: CFLAGS += -Dfont_extra=1
OBJS += oled_font_8x16.o
endif

oled_font_%.c: $(ROOT)/fonts/%.bdf
	$(PYTHON) $(ROOT)/scripts/mk_font.py $< oled_font_$*
