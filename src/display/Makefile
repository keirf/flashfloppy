OBJS += display.o
OBJS += lcd_$(mcu).o
OBJS += oled_font_6x13.o
OBJS += led_7seg.o

ifneq ($(target),bootloader)
lcd_$(mcu).o: CFLAGS += -Dfont_extra=1
# To save the flash size for the logfile and debug builds for
# STM32F105/AT32F415 (the most flash-hungry build types), we don't hardcode the
# 8x16 font in favor of building its glyphs at runtime as a bold (shifted-by-1)
# version of 6x13.
oled_font_8x16_synth ?= $(debug)$(logfile)
ifeq ($(mcu)$(oled_font_8x16_synth),stm32f105y)
lcd_$(mcu).o: CFLAGS += -DOLED_FONT_8x16_SYNTH=1
else
OBJS += oled_font_8x16.o
endif
endif

oled_font_%.c: $(ROOT)/fonts/%.bdf
	$(PYTHON) $(ROOT)/scripts/mk_font.py $< oled_font_$*
