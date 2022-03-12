TOOL_PREFIX = arm-none-eabi-
CC = $(TOOL_PREFIX)gcc
OBJCOPY = $(TOOL_PREFIX)objcopy
LD = $(TOOL_PREFIX)ld

PYTHON = python3

ifneq ($(VERBOSE),1)
TOOL_PREFIX := @$(TOOL_PREFIX)
endif

FLAGS  = -g -Os -nostdlib -std=gnu99 -iquote $(ROOT)/inc
FLAGS += -Wall -Werror -Wno-format -Wdeclaration-after-statement
FLAGS += -Wstrict-prototypes -Wredundant-decls -Wnested-externs
FLAGS += -fno-common -fno-exceptions -fno-strict-aliasing
FLAGS += -mlittle-endian -mthumb -mfloat-abi=soft
FLAGS += -Wno-unused-value -ffunction-sections

## STM32F105
ifeq ($(mcu),stm32f105)
FLAGS += -mcpu=cortex-m3 -DSTM32F105=1 -DMCU=1
ifeq ($(bootloader),y)
# Debug bootloader doesn't fit in 32kB
override debug=n
override logfile=n
endif

## AT32F435
else ifeq ($(mcu),at32f435)
FLAGS += -mcpu=cortex-m4 -DAT32F435=4 -DMCU=4
endif

ifneq ($(debug),y)
FLAGS += -DNDEBUG
endif

ifeq ($(bootloader),y)
FLAGS += -DBOOTLOADER=1
endif

ifeq ($(logfile),y)
FLAGS += -DLOGFILE=1
endif

ifeq ($(quickdisk),y)
FLAGS += -DQUICKDISK=1
endif

FLAGS += -MMD -MF .$(@F).d
DEPS = .*.d

FLAGS += $(FLAGS-y)

CFLAGS += $(CFLAGS-y) $(FLAGS) -include decls.h
AFLAGS += $(AFLAGS-y) $(FLAGS) -D__ASSEMBLY__
LDFLAGS += $(LDFLAGS-y) $(FLAGS) -Wl,--gc-sections

RPATH := $(shell $(PYTHON) $(ROOT)/scripts/rpath.py $(ROOT) $(CURDIR))
include $(RPATH)/Makefile

SUBDIRS += $(SUBDIRS-y)
OBJS += $(OBJS-y) $(OBJS-^n) $(patsubst %,%/build.o,$(SUBDIRS))

# Force execution of pattern rules (for which PHONY cannot be directly used).
.PHONY: FORCE
FORCE:

.PHONY: clean

.SECONDARY:

build.o: $(OBJS)
	$(LD) -r -o $@ $^

%/build.o: FORCE
	$(MAKE) -f $(ROOT)/Rules.mk -C $* build.o

%.ld: $(RPATH)/%.ld.S $(RPATH)/Makefile
	@echo CPP $@
	$(CC) -P -E $(AFLAGS) $< -o $@

%.elf: $(OBJS) %.ld $(RPATH)/Makefile
	@echo LD $@
	$(CC) $(LDFLAGS) -T$(*F).ld $(OBJS) -o $@
	chmod a-x $@

%.hex: %.elf
	@echo OBJCOPY $@
	$(OBJCOPY) -O ihex $< $@
	chmod a-x $@
ifneq ($(bootloader),y)
	srec_cat ../bootloader/target.hex -Intel $@ -Intel -o $@ -Intel
endif

%.bin: %.elf
	@echo OBJCOPY $@
	$(OBJCOPY) -O binary $< $@
	chmod a-x $@

%.upd: %.bin
	@echo OBJCOPY $@
	$(PYTHON) $(ROOT)/scripts/mk_update.py $< $@

%.dfu: %.hex
	$(PYTHON) $(ROOT)/scripts/dfu-convert.py -i $< $@

%.o: $(RPATH)/%.c $(RPATH)/Makefile
	@echo CC $@
	$(CC) $(CFLAGS) -c $< -o $@

%.o: $(RPATH)/%.S $(RPATH)/Makefile
	@echo AS $@
	$(CC) $(AFLAGS) -c $< -o $@

-include $(DEPS)
