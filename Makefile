include Rules.mk

OBJS += console.o
OBJS += vectors.o
OBJS += led.o
OBJS += main.o
OBJS += string.o
OBJS += stm32f10x.o

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
	sudo miniterm.py --baud=3000000 /dev/ttyUSB0
