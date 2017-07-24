# Installation & Update

## Initial Installation

The first installation of this firmware onto Gotek can be done either
by [serial](#serial-installation) or [USB](#usb-installation) link to
a host PC.

### Serial Installation

This method requires a USB-TTL serial adapter, which are readily
available on Ebay or from project webstores:
- [CAB-12977 from SparkFun](https://www.sparkfun.com/products/12977)
- Search "PL2303HX usb ttl adapter" on Ebay, available for around one dollar.

The Gotek is then jumpered in system-bootloader mode and programmed
from the host PC. This process is described, along with suitable
Windows software, on the
[Cortex firmware webpage](https://cortexamigafloppydrive.wordpress.com).
Of course, rather than using the Cortex HEX file, use the HEX file
contained in the FlashFloppy release archive.

If programming on Linux, you can follow the Cortex instructions to
physically set up your serial connection and bootstrap the Gotek, and
then use stm32flash to do the programming:

```
 # sudo stm32flash -w flashfloppy_fw/FF_Gotek*.hex /dev/ttyUSB0
```

### USB Installation

See this [Youtube video](https://www.youtube.com/watch?v=yUOyZB9cro4@feature=youtu.be)
for detailed instructions on this method. You will require a USB-A to
USB-A cable, and you should program the HEX file contained in the
FlashFloppy release archive.

## Updates

If you have previously flashed the full firmware, you can make future
updates via USB stick. Copy the UPD file from the release archive to
the root of your USB stick. Insert the USB stick into the Gotek and
then press both Gotek buttons while powering on. The updater will be
entered and will automatically detect and apply the update file. Note
that update will fail if there is more than one update file on the USB
drive.

Errors during update are reported on the LED display:
- **E01** No update file found
- **E02** More than one update file found
- **E03** Update file is invalid (bad signature or size)
- **E04** Update file is corrupt (bad CRC)
- **E05** Flash programming error
- **Fxx** FatFS error (probably bad USB drive or filesystem)
