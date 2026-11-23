OBJS += display.o
OBJS += lcd_$(mcu).o
OBJS += oled_font_6x16.o
OBJS += led_7seg.o

# The utility targets show only their own messages: their font holds ASCII.
ifneq ($(filter $(target),bootloader bl_update io_test),)
lcd_$(mcu).o: CFLAGS += -DOLED_FONT_ASCII_ONLY=1
oled_font_6x16.c: font_extent := ascii
endif

# A minimal image shows the fallback glyph for names outside ASCII.
ifeq ($(minimal_ui),y)
lcd_$(mcu).o: CFLAGS += -DOLED_FONT_ASCII_ONLY=1
oled_font_6x16.c: font_extent := ascii
endif

# Small-MCU firmware keeps Cyrillic and maps accented Latin to ASCII.
ifeq ($(mcu),stm32f105)
ifneq ($(minimal_ui),y)
ifeq ($(filter $(target),bootloader bl_update io_test),)
lcd_$(mcu).o: CFLAGS += -DOLED_FONT_CYR_ONLY=1
oled_font_6x16.c oled_font_8x16.c: font_extent := cyr
endif
endif
endif

ifneq ($(target),bootloader)
# STM32F105/AT32F415 logfile and debug builds have room for 6x16 alone.
oled_font_6x16_only ?= $(debug)$(logfile)
ifneq ($(mcu)$(oled_font_6x16_only),stm32f105y)
lcd_$(mcu).o: CFLAGS += -Dfont_extra=1
OBJS += oled_font_8x16.o oled_font_7x8.o oled_font_8x8.o
oled_font_7x8.c oled_font_8x8.c: font_extent := cyr
endif
endif

oled_font_%.c: $(ROOT)/fonts/%.png $(ROOT)/scripts/mk_font.py
	$(PYTHON) $(ROOT)/scripts/mk_font.py $< oled_font_$* $(font_extent)
