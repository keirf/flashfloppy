OBJS += display.o
OBJS += lcd_$(mcu).o
OBJS += oled_font_6x16.o
OBJS += led_7seg.o

# The bootloader shows only its own messages: its font holds ASCII alone.
ifeq ($(target),bootloader)
lcd_$(mcu).o: CFLAGS += -DOLED_FONT_ASCII_ONLY=1
oled_font_6x16.c: font_extent := ascii
endif

ifneq ($(target),bootloader)
lcd_$(mcu).o: CFLAGS += -Dfont_extra=1
# To save the flash size for the logfile and debug builds for
# STM32F105/AT32F415 (the most flash-hungry build types), we don't hardcode the
# 8x16 font in favor of building its glyphs at runtime as a bold (shifted-by-1)
# version of the 6-pixel font.
oled_font_8x16_synth ?= $(debug)$(logfile)
ifeq ($(mcu)$(oled_font_8x16_synth),stm32f105y)
lcd_$(mcu).o: CFLAGS += -DOLED_FONT_8x16_SYNTH=1
else
OBJS += oled_font_8x16.o
endif
endif

oled_font_%.c: $(ROOT)/fonts/%.png $(ROOT)/scripts/mk_font.py
	$(PYTHON) $(ROOT)/scripts/mk_font.py $< oled_font_$* $(font_extent)
