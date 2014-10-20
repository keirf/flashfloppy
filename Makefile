include Rules.mk

OBJS += main.o head.o string.o console.o

PROJ = gotek

.PHONY: all flash serial

all: $(PROJ).elf

%.elf: $(OBJS) %.ld Makefile
	$(CC) $(LDFLAGS) -T$(*F).ld $(OBJS) -o $@ 
	$(OBJCOPY) -O ihex $@ $(*F).hex
	$(OBJCOPY) -O binary $@ $(*F).bin

flash: all
	sudo ~/stm32flash/stm32flash -S 0x20004000 -g 0x20004000 -v \
	-w $(PROJ).bin /dev/ttyUSB0

serial:
	sudo miniterm.py --baud=460800 /dev/ttyUSB0
