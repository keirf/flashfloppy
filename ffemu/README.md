# ffemu

Runs the FlashFloppy user interface in a terminal, with no Gotek hardware. Windows (Git Bash / Cygwin / MSYS2) and Linux are supported. The firmware's own code (`src/main.c`, the display driver, FatFS, the image handlers) is compiled for the host and draws on an emulated OLED display, reads an emulated USB drive, and takes the keyboard for its buttons and rotary encoder. It is intended to help UI/UX development of FlashFloppy - working on menus, display layout and fonts without flashing a device.

Nothing is connected to the floppy interface: an image can be selected and is opened, so a bad image is reported as on the device, but no data flows and the track number stays at 0.

## Building

    make -C ffemu [O=<path>]

The program is `out/ffemu/ffemu` under `<path>`, which defaults to the top of the source tree. It needs a C compiler, `objcopy`, Python 3 (for the font and configuration generators of the firmware build) and the wide-character "ncurses" library with its headers: `ncurses-devel` on Cygwin/MSYS2, `libncurses-dev` on Debian and Ubuntu. On Cygwin/MSYS2 everything but the runtime DLL is linked statically, so the program built there also runs in any Git Bash.

## Running

    ffemu [<directory> | <image> | <disk>]

The USB drive holds the files and folders of `<directory>`, by default the current one, as a FAT32 volume. File contents are read from the host when the firmware asks for them. Alternatively, the drive is read from an image file of a whole drive (as `dd` makes it, with or without a partition table, FAT12, FAT16 or FAT32), or from a disk itself, such as a real USB drive: `/dev/sdb` on Linux, or `/dev/sdb` for `\\.\PhysicalDrive1` on Cygwin/MSYS2. Reading a disk needs `sudo` on Linux, which leaves the settings and the log in the home of the user who ran it, and a terminal started as administrator on Windows; ffemu says so when it is denied.

The firmware does write to the drive (it keeps the last selected image in `IMAGE_A.CFG`). Such writes never reach the host files, the image or the disk: they go to a temporary file and last until the drive is removed. They survive a power cycle, as on a real drive, provided that the files of the directory, or the image, have not changed in between. Status names the `FF.CFG` that the firmware reads: the one in folder `FF` if the drive has that folder, even when that one is missing, else the one in the root; names match in any letter case.

A drive made from a directory is meant to be what the firmware would find on a real drive with those files copied to it, so the FAT structures follow the system that ffemu runs on: Windows when built for Cygwin/MSYS2, Linux otherwise.

- Short (8.3) names: Windows writes them in the OEM code page of the system (`GetOEMCP()`, e.g. 866 on a Russian Windows, or UTF-8 where "Use Unicode UTF-8 for worldwide language support" is on), with the tails `~1` to `~4` and then a hash of the long name, as its FAT driver does. Linux writes them in code page 437, the default of vfat, with the tails `~1` to `~9` and then a random number, which ffemu replaces with a hash of the long name so that the volume is the same on every start.
- Long names: Windows writes none for a name that is a valid short name in one letter case per part, such as `readme.txt` or `README.txt`, and keeps the case in the entry instead; Linux writes one for any such name that is not all in capitals. A firmware that cannot convert a long name to its code page falls back to the short name, so the short names matter for non-ASCII names.
- Attributes: read-only from the host file (on Windows its R attribute, on Linux no write permission for anyone, which `cp -p` and copying file managers carry over to vfat, plain `cp` not), and on Windows also hidden and system. The firmware hides hidden files and does not write to read-only images.
- Left out, with a message in the console: names that the system would not copy (trailing spaces, and on Windows trailing dots, which Linux drops instead), names that differ from another one in the folder only in letter case (only possible on Linux; the first in byte order stays), special files, files of 4 GB or larger, and anything beyond 50'000 entries or 12 levels of folders.
- Not followed: the order of the entries in a folder, which on a real drive is the order of copying; ffemu sorts them by name.

The emulated device is an SFRKC30.AT2 board (AT32F415, 32 KB RAM) with an OLED display: an SSD1306 or an SH1106 controller, on a panel of 128x32 or 128x64, chosen in the TUI. As on the device, the firmware tells the controller by itself, and takes the height of the panel from `display-type` in its settings, e.g. `display-type = oled-128x64`; when the display is changed, ffemu offers to set that in the flash memory to match, and when `display-type` is changed in the Flash mem dialog, it offers to fit the display that suits it. When the firmware drives another display than the one fitted, for instance because `FF.CFG` on the drive says so, the display's frame shows "display type mismatch".

The app presents a TUI similar in look to Turbo Vision, mostly self-documented. The keys that act like Gotek controls or its state changers are shown in the Controls pane, other keys are shown in the bottom line of the terminal, which ends in a green `>` when the window is too narrow for all of it. Ctrl+L repaints the screen, for a terminal that has lost part of it, say after the window was maximized; a change of the rendering repaints it too. The Gotek display is rendered via different pseudo-graphical characters - choose the rendering which looks right for your terminal and font. There is also a fallback ASCII-only mode for the whole app.

A terminal reports key presses but not releases, so a key press holds its virtual button down for a fixed time (150 ms by default), and then auto-repeat kicks in. To simulate long presses, or pressing/holding several keys at once, which is used by the fw to trigger certain functions, use the "latching mode": press Space, then press each key to toggle the virtual button state; Space again to release all the latched virtual buttons.

The firmware needs its timers on time to within a millisecond. Where the host sleeps only in coarser steps, as a virtual machine on a Windows host may (4 to 16 ms), ffemu keeps time by spinning, which keeps one CPU core busy, and says so in the console window.

The firmware's console output (normally available in debug builds on the Gotek's UART pins) is shown at the bottom pane, and also written to `${XDG_STATE_HOME:-~/.local/state}/ffemu/console.log` (all file paths are shown at the bottom of the respective panes). The file is recreated on every start and power cycle; the previous one is kept as `console.bak`, replacing the one before it.

## Settings

The ffemu app settings (not to be confused with the Gotek settings in the flash memory or on the USB drive's `FF.CFG`) are stored in a file: `${XDG_CONFIG_HOME:-~/.config}/ffemu/ffemurc` holds `name = value` lines; a `#` starts a comment line. ffemu writes it with the defaults, and a comment on each setting, when it does not exist, and writes a setting changed from the keyboard back into it, keeping the other lines as they are.

The settings file also holds the state of the Gotek flash memory, where the firmware stores its settings. Those lines are rewritten every time the firmware writes to flash memory. Also there is a TUI dialog to edit those values right in the emulated flash memory. Note that if the USB drive contains `FF.CFG`, settings in it will be read by the firmware on start or restart, and will immediately be written to the flash memory over what was there before.

Those lines are the `[Flash mem]` section at the end of the file: `hex` holds the bytes of the flash memory, and the options follow in `FF.CFG` syntax, so that lines can be copied between the two; a number after `0x` stands for bytes that no `FF.CFG` value gives. On loading, the hex applies first and the options on top, with a warning in the console for an option that differs from the hex, is not in this firmware, or has a value that does not fit. The Flash mem pane shows the same options: those changed from the defaults in yellow, the rest in gray, and the raw numbers in magenta.

## Scripting

When standard input is not a terminal, ffemu reads commands from it and prints to standard output, which is how it can be tested without a terminal:

    printf 'sleep 2500\nkey right\nsleep 500\ndump\nlog\n' | ffemu <directory>

The commands are `key <action>`, the action being `select`, `left`, `right`, `cw`, `ccw`, `remove`, `insert`, `reset`, `quit` or `latch`, `sleep <ms>`, `dump` (the display as text), `log` (the firmware's console output so far), `status`, `flash` (the `[Flash mem]` section as the flash memory is now), and `save <file>`, which writes the USB drive as the firmware sees it, its writes included, to an image file.

## How it works

- `decls.h`, `regs.h`, `hooks.h`: the host counterparts of `inc/decls.h` and `inc/intrinsics.h`. The MCU register blocks become ordinary memory, and interrupt control is routed to the emulated interrupt controller.
- `hw.c`: models of the peripherals behind those registers that the user interface needs: GPIO inputs with their EXTI interrupts, the one-shot timer of `src/timer.c`, and the I2C master with its DMA channel. The I2C bus also runs on a thread of its own, beside the firmware, as the hardware does, so that an interrupt handler that waits for the bus is released whatever the firmware's thread is doing.
- `stubs.c`: stand-ins for MCU bring-up, the serial console, the configuration flash page, the heap, the USB host stack and the floppy interface.
- `cpu.c`: the interrupt controller, a 1 ms tick delivered as a signal to the thread that runs the firmware, cancellable calls, and reset, which re-executes the program.
- `ssd1306.c`: the display controller, an SSD1306 or an SH1106, as an I2C slave.
- `fatimg.c`: the USB drive: the FAT32 volume built from a directory, or an image or a disk read as it is, with the firmware's writes kept aside.
- `tui.c`, `script.c`, `rc.c`, `ffemu.c`: the terminal user interface, the script driver, the preferences and the main program.

The firmware half is built with the firmware's headers and its own subset of the C library, renamed out of the way of the host's; the host half is built with the host C library. Only `emu.h` is shared between the two.

## Not emulated [yet]

- Floppy interface and any data flow.
- AT32F435 boards, which have a display driver of their own, the HD44780 LCD, and the 7-segment LED display.
- FF OSD.
- Getting `IMAGE_A.CFG` written by the firmware.
- FAT16 on a USB drive made from a directory, which is always FAT32; an image or a disk may be FAT16.
- A setting for the code page of short names, in place of the system's.
