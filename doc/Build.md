# Building from source

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
 # make dist
```
