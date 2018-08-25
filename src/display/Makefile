OBJS += display.o
OBJS += lcd.o
OBJS += oled_font_6x13.o
OBJS += led_7seg.o
OBJS += font.o

lcd.o: CFLAGS += -Dfont_extra=1
OBJS += oled_font_8x16.o

oled_font_%.c: $(ROOT)/fonts/%.bdf
	$(PYTHON) $(ROOT)/scripts/mk_font.py $< oled_font_$*

clean::
	rm -f oled_font_*.c
