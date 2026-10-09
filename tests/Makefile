# Unit tests: firmware sources compiled for a PC, as ffemu compiles them,
# driven against a USB drive in RAM.
#
# Usage: make -C tests [O=<path>] [run | all | clean]
#
# The default, run, builds $(O)/out/fftest/fftest and runs it; it takes
# names of tests to run, as in "make -C tests run T=img_pc". all builds it
# and runs it only when it has changed since the tests last passed, so that a
# build that leaves it alone costs nothing and a failing test stops the
# build. EXTRA_CFLAGS
# goes to every compile and to the link, as in
# EXTRA_CFLAGS="-fsanitize=address,undefined -Wno-maybe-uninitialized", the
# latter for a false warning that the sanitizers bring out in img.c.

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
TESTS := $(ROOT)/tests
EMU := $(ROOT)/ffemu

O ?= $(ROOT)
OUT := $(abspath $(O))/out/fftest

CC ?= cc
PYTHON ?= $(firstword $(shell command -v python3 python py 2>/dev/null))

ifneq ($(VERBOSE),1)
Q := @
endif

# The firmware half: the sources under test, as they are, with ffemu's
# host-build headers.
FW_SRCS := fs.c volume.c cache.c crc.c string.c util.c
FW_SRCS += fatfs/ff.c fatfs/ffunicode.c
FW_SRCS += image/image.c image/adf.c image/dsk.c image/hfe.c image/img.c
FW_SRCS += image/da.c image/dummy.c image/mfm.c
FW_SRCS += floppy.c
TEST_SRCS := harness.c floppy_env.c
TEST_SRCS += test_floppy.c test_flux.c test_img.c test_util.c

# The host half: the runner.
HOST_SRCS := ft_main.c

FW_OBJS := $(patsubst %.c,$(OUT)/fw/%.o,$(FW_SRCS))
FW_OBJS += $(patsubst %.c,$(OUT)/test/%.o,$(TEST_SRCS))
HOST_OBJS := $(patsubst %.c,$(OUT)/host/%.o,$(HOST_SRCS))

# As in ffemu/Makefile.
FW_CFLAGS := -std=gnu99 -O1 -g -funsigned-char -fno-builtin
FW_CFLAGS += -fno-common -fno-strict-aliasing
FW_CFLAGS += -fno-tree-loop-distribute-patterns
FW_CFLAGS += -Wall -Werror -Wno-format -Wno-unused-value
FW_CFLAGS += -iquote $(ROOT)/inc -iquote $(EMU) -iquote $(TESTS)
FW_CFLAGS += -iquote $(OUT)
FW_CFLAGS += -include $(EMU)/decls.h
# The multi target, which has the shugart target's image handlers, the
# Apple2 ones and the detection of the host.
FW_CFLAGS += -DMCU=MCU_stm32f105 -DTARGET=TARGET_multi -DLEVEL=LEVEL_debug
FW_CFLAGS += -MMD -MP $(EXTRA_CFLAGS)

HOST_CFLAGS := -std=gnu11 -O1 -g -Wall -Werror -MMD -MP $(EXTRA_CFLAGS)

.PHONY: all run clean

run: $(OUT)/fftest
	$(OUT)/fftest $(T)

all: $(OUT)/passed

$(OUT)/passed: $(OUT)/fftest
	$(Q)$(OUT)/fftest
	@touch $@

$(OUT)/fftest: $(FW_OBJS) $(HOST_OBJS)
	@echo LD $(notdir $@)
	$(Q)$(CC) -g $(EXTRA_CFLAGS) -o $@ $^

$(OUT)/ff_cfg_defaults.h: $(ROOT)/examples/FF.CFG $(ROOT)/scripts/mk_config.py
	@mkdir -p $(dir $@)
	$(Q)$(PYTHON) $(ROOT)/scripts/mk_config.py $< $@

$(OUT)/test/harness.o: $(OUT)/ff_cfg_defaults.h

# As in ffemu/Makefile: the floppy code of the logfile level keeps the Apple2
# stepper phases on the release's pins.
$(OUT)/fw/floppy.o: FW_CFLAGS += -ULEVEL -DLEVEL=LEVEL_logfile

$(OUT)/fw/%.o: $(ROOT)/src/%.c
	@echo CC $*.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(FW_CFLAGS) -c $< -o $@

$(OUT)/test/%.o: $(TESTS)/%.c
	@echo CC tests/$*.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(FW_CFLAGS) -c $< -o $@

$(OUT)/host/%.o: $(TESTS)/%.c
	@echo CC tests/$*.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(HOST_CFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT)

-include $(FW_OBJS:.o=.d) $(HOST_OBJS:.o=.d)
