# Usage

- [Interface Mode](#interface-mode)
- [Modes of operation](#modes-of-operation)

## Interface Mode

By default FlashFloppy will emulate the Shugart floppy interface. This
is compatible with a broad range of systems including Amiga, Atari ST,
Amstrad CPC, and many other devices. Shugart-compatible systems will
typically expect the Gotek to respond as 'unit 0'. Therefore place the
selection jumper at location S0 at the rear of the Gotek.

IBM PC compatibles use a slightly modified interface which places the
disk-changed signal on a different pin. To select this interface mode
place a jumper at location JC at the rear of the Gotek. The host
system may expect the Gotek to respond as 'unit 1': in this case place
the selection jumper at location S1 at the rear of the Gotek.

## Modes of operation

FlashFloppy supports three different modes of operation:
- Config-less mode. No need for any config files. Cycles between
  all valid image files in the root of the USB stick.
- HxC Autoboot mode, configured via HxC host selector software (AUTOBOOT.HFE)
  into the HXCSDFE.CFG file in the root of the USB stick.
- HxC Index mode, switching between image names of the form DSKA0000 and
  so on. Requires a special HXCSDFE.CFG file. No need for
  AUTOBOOT.HFE.

### Config-less mode

In this mode you need no configuration files or selector
software. FlashFloppy will automatically assign all valid images in
the root folder of your USB stick to slots which you can switch
between using the Gotek buttons.

### HxC Autoboot mode

This mode requires HXCSDFE.CFG and an AUTOBOOT.HFE image compatible
with your system. These files are available for Amiga, Atari ST
and Amstrad CPC from the [HxC project](http://hxc2001.com/).

The Gotek buttons cycle between the assigned slots in the config
file. To reassign slots boot the AUTOBOOT.HFE disk: this is
immediately accessible in slot 0 by pressing both Gotek buttons at any
time. Configuration with the host selector software is straightforward:
assign files from your USB stick to drive A "slots". On reboot, these
slots are accessible via the up and down buttons on the front of the
Gotek. Holding a button will cycle faster through the populated
slots. Pressing both buttons will take you immediately to slot 0
(AUTOBOOT.HFE).

### HxC Index mode

This mode requires only a special HXCSDFE.CFG file, available from the
[HxC project](http://hxc2001.com/). FlashFloppy will switch between
images with names of the form DSKA0000.HFE, DSKA0001.HFE, and so on,
which will be automatically assigned to the corresponding numbered
slot. Note that any supported image type can be used in place of HFE
in this example.
