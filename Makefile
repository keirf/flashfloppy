OBJS += console.o
OBJS += vectors.o
#OBJS += led.o
OBJS += main.o
OBJS += sd_spi.o
OBJS += string.o
OBJS += stm32f10x.o
#OBJS += usb_hcd.o
OBJS += util.o
OBJS += fatfs/ff.o
OBJS += fatfs/ccsbcs.o

include Rules.mk

PROJ = gotek

.PHONY: all flash serial

all: $(PROJ).elf $(PROJ).bin $(PROJ).hex

flash: $(PROJ).bin
	sudo ~/stm32flash/stm32flash -S 0x08000000 -g 0x08000000 \
	-w $< /dev/ttyUSB0

start:
	sudo ~/stm32flash/stm32flash -g 0x08000000 /dev/ttyUSB0

serial:
	sudo miniterm.py --baud=3000000 /dev/ttyUSB0

clean::
	rm -f fatfs/*~ fatfs/*.o
