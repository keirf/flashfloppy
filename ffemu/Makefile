# ffemu: runs the FlashFloppy user interface in a terminal, with no hardware.
#
# Usage: make -C ffemu [O=<path>] [TARGET=shugart|apple2]
#
# The program is built as $(O)/out/ffemu/ffemu, or for the Apple2 firmware
# as $(O)/out/ffemu-apple2/ffemu-apple2. O defaults to the top of the source
# tree, as in the main Makefile.

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
EMU := $(ROOT)/ffemu

TARGET ?= shugart
ifeq ($(TARGET),shugart)
NAME := ffemu
else ifeq ($(TARGET),apple2)
NAME := ffemu-apple2
else
$(error TARGET must be shugart or apple2)
endif

O ?= $(ROOT)
OUT := $(abspath $(O))/out/$(NAME)

CC ?= cc
OBJCOPY ?= objcopy
PYTHON ?= $(firstword $(shell command -v python3 python py 2>/dev/null))
VER ?= $(shell git -C $(ROOT) rev-parse --short HEAD)

ifneq ($(VERBOSE),1)
Q := @
endif

# The firmware half: firmware sources as they are, plus the stand-ins for the
# hardware they need. The model is an AT32F415, which shares the STM32F105
# code, and the build level is "debug" for its assertions and console output.
FW_SRCS := main.c config.c fs.c volume.c cache.c crc.c string.c util.c
FW_SRCS += time.c timer.c build_info.c spi.c sd_spi.c
FW_SRCS += fatfs/ff.c fatfs/ffunicode.c
FW_SRCS += display/display.c display/lcd_stm32f105.c display/led_7seg.c
FW_SRCS += gotek/board.c gotek/speaker.c floppy.c
FW_SRCS += image/image.c image/adf.c image/dsk.c image/hfe.c image/img.c
FW_SRCS += image/da.c image/dummy.c image/mfm.c
EMU_FW_SRCS := hw.c stubs.c
# Display fonts, generated from fonts/ as in src/display/Makefile.
FONTS := 6x13 8x16

# The host half: everything that touches the host operating system.
HOST_SRCS := ffemu.c cpu.c ssd1306.c fatimg.c rc.c tui.c script.c

FW_OBJS := $(patsubst %.c,$(OUT)/fw/%.o,$(FW_SRCS))
FW_OBJS += $(patsubst %.c,$(OUT)/emu/%.o,$(EMU_FW_SRCS))
FW_OBJS += $(patsubst %,$(OUT)/gen/oled_font_%.o,$(FONTS))
HOST_OBJS := $(patsubst %.c,$(OUT)/host/%.o,$(HOST_SRCS))

# -funsigned-char: as on ARM, which the firmware is written for.
FW_CFLAGS := -std=gnu99 -O1 -g -funsigned-char -fno-builtin
FW_CFLAGS += -fno-common -fno-strict-aliasing
# As in src/Makefile: do not turn the loops of util.c into calls to itself.
FW_CFLAGS += -fno-tree-loop-distribute-patterns
FW_CFLAGS += -Wall -Werror -Wno-format -Wno-unused-value
FW_CFLAGS += -iquote $(ROOT)/inc -iquote $(EMU) -iquote $(OUT)
FW_CFLAGS += -include $(EMU)/decls.h
FW_CFLAGS += -DMCU=MCU_stm32f105 -DTARGET=TARGET_$(TARGET) -DLEVEL=LEVEL_debug
FW_CFLAGS += -MMD -MP

HOST_CFLAGS := -std=gnu11 -O2 -g -Wall -Werror -D_GNU_SOURCE
HOST_CFLAGS += -DNCURSES_WIDECHAR=1 -DFFEMU_NAME='"$(NAME)"'
ifeq ($(TARGET),apple2)
HOST_CFLAGS += -DFFEMU_APPLE2=1
endif
HOST_CFLAGS += -MMD -MP

LIBS := $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncursesw)
LIBS += -lpthread

# The Cygwin and MSYS runtime is the one library that cannot be linked in.
# With the rest static, the program runs wherever that runtime is, which
# includes any Git Bash.
ifneq ($(filter CYGWIN% MSYS% MINGW%,$(shell uname -s)),)
LDFLAGS += -static
HOST_CFLAGS += -DNCURSES_STATIC
endif

.PHONY: all clean

# Keep the generated sources.
.SECONDARY:

all: $(OUT)/$(NAME)

$(OUT)/$(NAME): $(FW_OBJS) $(HOST_OBJS)
	@echo LD $(notdir $@)
	$(Q)$(CC) -g $(LDFLAGS) -o $@ $^ $(LIBS)

$(OUT)/ff_cfg_defaults.h: $(ROOT)/examples/FF.CFG $(ROOT)/scripts/mk_config.py
	@mkdir -p $(dir $@)
	$(Q)$(PYTHON) $(ROOT)/scripts/mk_config.py $< $@

$(OUT)/fw/main.o $(OUT)/emu/stubs.o: $(OUT)/ff_cfg_defaults.h

$(OUT)/gen/oled_font_%.c: $(ROOT)/fonts/%.bdf $(ROOT)/scripts/mk_font.py
	@mkdir -p $(dir $@)
	$(Q)cd $(dir $@) && $(PYTHON) $(ROOT)/scripts/mk_font.py $< oled_font_$*

$(OUT)/gen/%.o: $(OUT)/gen/%.c
	@echo CC $*.c
	$(Q)$(CC) $(FW_CFLAGS) -c $< -o $@

# The Apple2 debug build moves two stepper phases onto the rotary encoder
# pins; the board and floppy code of the logfile level keep them where the
# release has them, and ffemu takes the console output itself.
ifeq ($(TARGET),apple2)
$(OUT)/fw/gotek/board.o $(OUT)/fw/floppy.o: \
    FW_CFLAGS += -ULEVEL -DLEVEL=LEVEL_logfile
endif

# As in src/display/Makefile: the main firmware has both fonts.
$(OUT)/fw/display/lcd_stm32f105.o: FW_CFLAGS += -Dfont_extra=1

.PHONY: $(ROOT)/src/build_info.c
$(OUT)/fw/build_info.o: FW_CFLAGS += -DFW_VER="\"$(VER)\""

$(OUT)/fw/%.o: $(ROOT)/src/%.c
	@echo CC $*.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(FW_CFLAGS) -c $< -o $@

# The firmware's main() must not be the entry point of the program.
$(OUT)/fw/main.o: $(ROOT)/src/main.c
	@echo CC main.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(FW_CFLAGS) -c $< -o $@
	$(Q)$(OBJCOPY) --redefine-sym main=ff_main $@

# stubs.c wraps these with what ffemu adds to them.
$(OUT)/fw/floppy.o: $(ROOT)/src/floppy.c
	@echo CC floppy.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(FW_CFLAGS) -c $< -o $@
	$(Q)$(OBJCOPY) --redefine-sym floppy_handle=fw_floppy_handle \
	    --redefine-sym floppy_insert=fw_floppy_insert \
	    --redefine-sym floppy_cancel=fw_floppy_cancel $@

$(OUT)/emu/%.o: $(EMU)/%.c
	@echo CC ffemu/$*.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(FW_CFLAGS) -c $< -o $@

$(OUT)/host/%.o: $(EMU)/%.c
	@echo CC ffemu/$*.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(HOST_CFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT)

-include $(FW_OBJS:.o=.d) $(HOST_OBJS:.o=.d)
