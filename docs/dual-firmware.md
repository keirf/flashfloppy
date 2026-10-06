# Combined FDD and QuickDisk firmware

The `dual` target contains the regular Shugart FDD emulator and the QuickDisk
emulator in one application. The USB/storage stack, filesystem, navigation,
display, configuration, image allocation, buffers and write-flux decoder are
shared. The specific drive state machines, electrical interface and timing
remain in separate engines.

## Boot selection

- Power on normally to start the last selected emulation. The default is FDD.
- Hold the encoder push button (SELECT) while powering on to open the boot menu.
- Release the button, rotate the encoder to choose **QuickDisk**, **FDD** or
  **Update FW**, then press it again to confirm. LEFT/RIGHT also navigate.
- LCD/OLED shows the names; a seven-segment display shows `qd`, `Fdd` and `UPd`.
- Choosing an emulation saves it in the existing flash configuration page.
- Choosing Update FW requests the existing USB update bootloader. Insert a USB
  drive with a matching update file; the menu confirmation starts the update.

Only the chosen engine is initialised. A RAM vector table points directly to
its IRQ handlers; there is no mode branch in the time-critical SELECT ISR.
The other engine never enables interface IRQs, DMA or timer callbacks. Mode
changes require a restart. Shared UI/storage settings still apply in both modes.

In native navigation, FDD retains `IMAGE_A.CFG`/`INIT_A.CFG`; QuickDisk uses
`IMAGE_Q.CFG`/`INIT_Q.CFG` to retain a separate last image and folder. QD navigation
only lists `.qd` images, while FDD retains its existing supported formats and
Direct Access mode. Existing QD jumper JC behavior is retained.

## Building and installing

Build the bootloader and combined production application together:

```sh
make -j4 dual-stm32f105
make -j4 dual-at32f435
```

Files are written to `out/<mcu>/prod/dual/`:

- `target.hex` and `target.dfu` contain both the updated bootloader and application.
- `target.bin` contains only the application.

The updated bootloader is required: an older bootloader interprets SELECT at
power-on as a direct update request. Either install the combined HEX/DFU, or
update the bootloader using the existing `bl_update` procedure before installing
the application update. An application `.upd` alone does not update a bootloader.

The new bootloader recognises the combined image through a marker in reserved
vector slot 7. It passes the boot-menu request through its reset, so releasing
SELECT during startup does not lose the request. Standalone images retain the
original SELECT-to-update behavior. Holding LEFT and RIGHT at power-on still
enters recovery update mode, including when the main application is absent.

`make dist` also packages combined production updates under `alt/dual/` and
combined HEX/DFU files with `dual` in their name. Debug and logfile combined
targets are available on AT32F435. The STM32F105 build has a 94 KiB application
budget: the combined production image fits, but combined debug/logfile images
with additional logging do not fit. Its standalone debug/logfile targets remain
part of the normal build matrix. No FDD image formats are removed to make room.

## Validation

The normal build checks the flash limit for every firmware and bootloader image.
The host dispatcher test checks mode selection, invalid saved-mode fallback,
vector-table alignment/copying, configuration IRQ masks, and that every public
operation only calls the chosen engine. It also covers every button combination
for combined/legacy/absent firmware and explicit update requests:

```sh
cc -Wall -Wextra -Werror -Wno-pointer-to-int-cast \
  tests/emulation_test.c -o out/emulation_test
out/emulation_test
cc -Wall -Wextra -Werror -Wno-pointer-to-int-cast -DMCU=4 \
  tests/emulation_test.c -o out/emulation_test_at32
out/emulation_test_at32
python3 tests/check_dual_artifacts.py
```

The artifact check verifies both engine API tables, the boot-menu marker, RAM
vector alignment, flash limits, and that the combined HEX and bootloader updater
contain the current bootloader. It also compares the critical FDD SELECT entry
stub in SRAM with the standalone FDD image byte for byte. Passing a distribution
directory as an argument also verifies the combined production update catalogs
and compares both MCU payloads against their current binaries.

Hardware validation is still required for the boot menu, update flow and both
interfaces, especially read/write timing, RDATA polarity, JC/Roland behavior,
QFN32 pin remapping, and FDD selection/Direct Access. Building and host tests
cannot verify electrical behavior on a Gotek.
