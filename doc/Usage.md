# Usage

- [Modes of operation](#modes-of-operation)
- [Speaker](#speaker)
- [LCD Display](#lcd-display)
- [OLED Display](#oled-display)

## Modes of operation

FlashFloppy supports three different modes of operation:
- Slot mode, configured via HxC host selector software (AUTOBOOT.HFE)
  into the HXCSDFE.CFG file in the root of the USB stick.
- Index mode, switching between image names of the form DSKA0000 and
  so on. Requires a special HXCSDFE.CFG file. No need for
  AUTOBOOT.HFE.
- Config-less operation. No need for any config files. Cycles between
  all valid image files in the root of the USB stick.

### Slot mode

This mode requires an AUTOBOOT.HFE image compatible with your
system. These are available for Amiga, Atari ST and Amstrad CPC. For
Amiga only, a special build of AUTOBOOT.HFE is included in the
FlashFloppy distribution: copy the contents of the Config/Amiga/
subfolder to the root of your USB stick. For other platforms, use the
relevant usual AUTOBOOT.HFE from the HxC project.

The Gotek buttons cycle between the assigned slots in the config
file. To reassign slots boot the AUTOBOOT.HFE disk: this is
immediately accessible in slot 0 by pressing both Gotek buttons at any
time. Configuration with the host selector software is straightforward:
assign files from your USB stick to drive A "slots". On reboot, these
slots are accessible via the up and down buttons on the front of the
Gotek. Holding a button will cycle faster through the populated
slots. Pressing both buttons will take you immediately to slot 0
(AUTOBOOT.HFE).

Note that the version number on the selector software
does not need to match the FlashFloppy firmware version.

### Index mode

This mode requires only a special HXCSDFE.CFG file, such as included
in the FlashFloppy distribution: simply copy the contents of the
Config/Index_Mode/ subfolder to the root of your USB
stick. FlashFloppy will switch between images with names of the form
DSKA0000.HFE, DSKA0001.HFE, and so on, which will be automatically
assigned to the corresponding numbered slot. Note that any supported
image type can be used in place of HFE in this example.

As of the v0.1a pre-release, the firmware requires to be configured
via the HxC-style HXCSDFE.CFG binary file which is updated via host
selector software. A version is supplied for Amiga (along with config
file) in the Amiga/ subfolder of the release archive. The files
therein should be copied to the root of your USB stick. For other
systems with an HxC selector image (AUTOBOOT.HFE) that should be
copied to the root of your USB stick instead, along with a suitable
HXCSDFE.CFG file.

### Config-less mode

In this mode you need no configuration files or selector
software. FlashFloppy will automatically assign all valid images in
the root folder of your USB stick to slots which you can switch
between using the Gotek buttons.

## Speaker

A speaker can be attached to the Gotek to sound whenever the floppy
drive heads are stepped. A piezo sounder can be connected directly
between jumper JB and Ground, marked respectively as SPEAKER and GND
in the picture below.
![Piezo speaker](assets/jumpers.jpg)

A magnetic speaker should be buffered via an NPN transistor (eg
2N3904) as follows:
- **Base**: connected to JB (SPEAKER) via a 1k resistor
- **Collector**: connected to one terminal of the speaker (the other
    connected to 5v)
- **Emitter**: connected to Ground (GND)

Pinout for the 2N3904 is shown below (note that the leg arrangement
can differ on other NPN transistors).
![2N3904 legs](assets/2n3904.jpg)

## LCD Display

As an alternative to the Gotek 7-segment display, FlashFloppy supports
the ubiquitous 1602 LCD with I2C backpack board. These are available
from many Ebay sellers. The connections should be made just as for HxC
Gotek firmware, including pullup resistors (if required - see below).

You can locate SCL, SDA, and GND on your Gotek PCB as below. These
connect to the corresponding header pins on your LCD I2C backpack
module.
![LCD data/clock interface](assets/header_closeup.jpg)

VCC (aka 5V) can be found in various places, including just behind the
floppy power connector.
![LCD VCC](assets/jumpers.jpg)

The SCL and SDA lines must be connected to VCC ("pulled up" to VCC)
via 4.7k resistors.  Note that many I2C boards have the pullup
resistors on board and in this case you do not need to attach your own
external pullups. You can confirm this by checking the resistance
between SDA/SCL and VCC. If it is less than 10k you do not need to add
pullups.

If you do require the pullup resistors, these can be soldered to the
backside of the Gotek PCB between VCC and each of SDA and
SCL. Alternatively can be soldered to the back of the I2C module
header as below.
![LCD Pullup Resistors](assets/pullups.jpg)

## OLED Display

Another alternative to the Gotek 7-segment display is a 0.91" 128x32
display, as sold for Arduino projects by many Ebay sellers. You will
require a display with I2C interface: you should see it has a 4-pin
header marked GND, VCC, SCL, SDA.

These displays can simply connect to the 7-segment display's header,
reusing the existing jumper wires, as in the pictires below.

![OLED Display Front](assets/oled1.jpg)

![OLED Display Rear](assets/oled2.jpg)