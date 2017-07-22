# FlashFloppy

A retro floppy emulator for the ubiquitous Gotek hardware.

If you are installing a binary release then please skip to
Installation/Update instructions below.

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
 # make dist
```

## Initial Installation

The first installation of this firmware onto Gotek requires a USB-TTL
serial adapter (such as [the CAB-12977 from SparkFun](https://www.sparkfun.com/products/12977)), with the Gotek jumpered into system-bootloader
mode. This process is described on the Cortex firmware webpage
[here](https://cortexamigafloppydrive.wordpress.com).

The file to be programmed is called `flashfloppy_fw/FF_Gotek-<ver>.hex`.
This can be programmed in Linux with the stm32flash software:

```
 # sudo stm32flash -w flashfloppy_fw/FF_Gotek*.hex /dev/ttyUSB0
```

## Updates

If you have previously flashed the full firmware, you can make future updates
via USB stick:
```
 # make dist
 # rm /path/to/usb/FF_Gotek*.upd
 # cp flashfloppy_fw/FF_Gotek*.upd /path/to/usb/
```

Now press both Gotek buttons while powering on. The updater will be
entered and will automatically detect and apply the update file. Note
that update will fail if there is more than one update file on the USB
drive!

Errors during update are reported on the LED display:
- **E01** No update file found
- **E02** More than one update file found
- **E03** Update file is invalid (bad signature or size)
- **E04** Update file is corrupt (bad CRC)
- **E05** Flash programming error
- **Fxx** FatFS error (probably bad USB drive or filesystem)
