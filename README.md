# FlashFloppy

A retro floppy emulator for STM32F10x-based boards including
the uniquitous Gotek.

Most code is public domain; the rest is MIT/BSD (see the
[COPYING](COPYING) file).

## Building

This project is cross-compiled on an x86 Ubuntu Linux system. However
other similar Linux-base systems (or a Linux virtual environment on
another OS) can likely be made to work quite easily.

The Ubuntu package prerequisites include:
- git
- gcc-arm-none-eabi
- srecord
- stm32flash
- python-crcmod

If the stm32flash package is unavailable on your system then it must
be downloaded from Sourceforge and built locally.

To build the FlashFloppy firmware:
```
 # git clone https://github.com/keirf/FlashFloppy.git
 # cd FlashFloppy
 # make gotek
```

This produces a combined programming file FF.hex, including both the
update bootloader and the main firmware. This can be programmed via a
USB-TTL serial adapter, with the Gotek jumpered into system-bootloader
mode:
```
 # sudo stm32flash -w FF.hex /dev/ttyUSB0
```
