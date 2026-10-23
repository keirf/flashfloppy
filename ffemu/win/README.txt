ffemu for Windows

ffemu runs the user interface of the FlashFloppy firmware for Gotek
floppy drive emulators in a terminal, with no Gotek hardware. This
folder runs on Windows with nothing else installed, in the bundled
mintty terminal (from MSYS2) with the recommended colors and fonts.

- ffemu-shugart.cmd - runs the firmware for Shugart-like (TR-DOS, IBM
  PC) floppy drives (bin\ffemu-shugart.exe)
- ffemu-apple2.cmd - runs the firmware for Apple2 (Apple, Pravetz,
  Agat) floppy drives (bin\ffemu-apple2.exe)

Start any of these .cmd scripts by a double click to take the USB drive
attached to the PC, or drop a folder with the floppy images or a USB
drive image onto the .cmd script.

If you have Git Bash, Cygwin or MSYS2, you can run the .exe files
directly.

On first run, each program writes its settings, says where they, its log
and its temporary files are, and waits for a key.

See ffemu/README.md in the GitHub sources for the details.

FlashFloppy, ffemu included, is released into the public domain.
msys-2.0.dll is the MSYS2 runtime, licensed under the LGPL version 3 or
later (runtime\usr\share\doc\Cygwin\), its source is at
https://github.com/msys2/msys2-runtime. mintty.exe is licensed under the
GPL version 3 or later (runtime\usr\share\licenses\mintty\), its source
is at https://github.com/mintty/mintty. The programs include ncurses,
licensed under the MIT license (runtime\usr\share\licenses\ncurses\).
