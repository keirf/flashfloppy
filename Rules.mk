TOOL_PREFIX = arm-none-eabi-
CC = $(TOOL_PREFIX)gcc
OBJCOPY = $(TOOL_PREFIX)objcopy

ifneq ($(VERBOSE),1)
TOOL_PREFIX := @$(TOOL_PREFIX)
endif

FLAGS  = -g -Os -nostdlib -std=gnu99 -iquote inc
FLAGS += -Wall -Werror -Wno-format -Wdeclaration-after-statement
FLAGS += -Wstrict-prototypes -Wredundant-decls -Wnested-externs
FLAGS += -fno-common -fno-exceptions -fno-strict-aliasing
FLAGS += -mlittle-endian -mthumb -mcpu=cortex-m3 -mfloat-abi=soft

FLAGS += -MMD -MF .$(@F).d
DEPS = .*.d

CFLAGS += $(FLAGS) -include decls.h
AFLAGS += $(FLAGS) -D__ASSEMBLY__
LDFLAGS += $(FLAGS) -Wl,--gc-sections

.DEFAULT_GOAL := all

.PHONY: clean

.SECONDARY:

%.o: %.c Makefile
	@echo CC $@
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S Makefile
	@echo AS $@
	$(CC) $(AFLAGS) -c $< -o $@

%.ld: %.ld.S Makefile
	@echo CPP $@
	$(CC) -P -E $(AFLAGS) $< -o $@

%.elf: $(OBJS) %.ld Makefile
	@echo LD $@
	$(CC) $(LDFLAGS) -T$(*F).ld $(OBJS) -o $@

%.hex: %.elf
	@echo OBJCOPY $@
	$(OBJCOPY) -O ihex $< $@

%.bin: %.elf
	@echo OBJCOPY $@
	$(OBJCOPY) -O binary $< $@

clean::
	rm -f *~ *.o *.elf *.hex *.bin *.ld $(DEPS)

-include $(DEPS)
