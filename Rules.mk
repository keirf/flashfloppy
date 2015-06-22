TOOL_PREFIX = arm-none-eabi-
CC = $(TOOL_PREFIX)gcc
OBJCOPY = $(TOOL_PREFIX)objcopy
LD = $(TOOL_PREFIX)ld

ifneq ($(VERBOSE),1)
TOOL_PREFIX := @$(TOOL_PREFIX)
endif

FLAGS  = -g -Os -nostdlib -std=gnu99 -iquote $(ROOT)/inc
FLAGS += -Wall -Werror -Wno-format -Wdeclaration-after-statement
FLAGS += -Wstrict-prototypes -Wredundant-decls -Wnested-externs
FLAGS += -fno-common -fno-exceptions -fno-strict-aliasing
FLAGS += -mlittle-endian -mthumb -mcpu=cortex-m3 -mfloat-abi=soft

FLAGS += -MMD -MF .$(@F).d
DEPS = .*.d

CFLAGS += $(FLAGS) -include decls.h
AFLAGS += $(FLAGS) -D__ASSEMBLY__
LDFLAGS += $(FLAGS) -Wl,--gc-sections

RULES_MK := y

include Makefile

OBJS += $(patsubst %,%/build.o,$(SUBDIRS))

# Force execution of pattern rules (for which PHONY cannot be directly used).
.PHONY: FORCE
FORCE:

.PHONY: clean

.SECONDARY:

build.o: $(OBJS)
	$(LD) -r -o $@ $^

%/build.o: FORCE
	$(MAKE) -f $(ROOT)/Rules.mk -C $* build.o

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

clean:: $(addprefix _clean_,$(SUBDIRS))
	rm -f *~ *.o *.elf *.hex *.bin *.ld $(DEPS)
_clean_%: FORCE
	$(MAKE) -f $(ROOT)/Rules.mk -C $* clean

-include $(DEPS)
