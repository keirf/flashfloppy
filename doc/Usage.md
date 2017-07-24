# Usage

As of the v0.1a pre-release, the firmware requires to be configured
via the HxC-style HXCSDFE.CFG binary file which is updated via host
selector software. A version is supplied for Amiga (along with config
file) in the Amiga/ subfolder of the release archive. The files
therein should be copied to the root of your USB stick. For other
systems with an HxC selector image (AUTOBOOT.HFE) that should be
copied to the root of your USB stick instead, along with a suitable
HXCSDFE.CFG file.

Configuration with the host selector software is straightforward:
assign files from your USB stick to drive A "slots". On reboot, these
slots are accessible via the up and down buttons on the front of the
Gotek. Holding a button will cycle faster through the populated
slots. Pressing both buttons will take you immediately to slot 0
(AUTOBOOT.HFE).
