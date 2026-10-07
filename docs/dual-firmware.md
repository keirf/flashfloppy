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
lists `.qd`, `.mzq` and `.qdf` images, while FDD retains its supported formats and
Direct Access mode.

### Independent QuickDisk READY setting

`qd-ready = standard | motor-off | jc` affects QuickDisk only:

- `standard`: Motor-off does not immediately deactivate READY.
- `motor-off`: Motor-off immediately deactivates READY, as required by
  Sharp MZ-800/MZ-1F11 and Roland. This is the example configuration default.
- `jc`: Only the physical JC jumper selects the behaviour (open: standard,
  closed: motor-off).

FDD `interface=jc` still selects Shugart with JC open and IBM PC with JC closed.
Explicit FDD interface values and explicit QD READY values ignore JC. FDD
`interface=ibmpc` has no effect on QD READY. The previous option has been removed;
replace it with `qd-ready` in your USB configuration. Older saved READY bytes
are discarded in favour of the new default, then FF.CFG is applied as usual.
For a Sharp using both modes, select `interface=shugart` and
`qd-ready=motor-off`; JC may remain open.

The QD motor input remains PA0. The universal wiring uses S0 fitted, S1 and MO
open, and host QD /MOTOR connected to Gotek pin 10. FDD uses the same PA0 as
SELA. Traditional QD wiring instead uses /MOTOR on pin 16 and MO fitted; both
routes reach PA0. No firmware pin mapping changes are required.

`qd-motor-volume = 0..20` controls the QD spindle sound independently of FDD
`step-volume`. Both default to 10; 0 silences the respective drive sound.
Insert/eject notifications continue to use the shared `notify-volume` setting.
The OLED/LCD QD status shows `R` while the emulator outputs a read stream
with READY asserted. The interface cannot determine whether the host actually
consumes that stream. `W` means an accepted physical WGATE is active; USB
writeback alone does not show `W`. Read-only images show `*` immediately on
mount and throughout use, including alongside `R`. Percentages are removed.
The seven-segment display uses `rd`, `wrt` and `qd` for read, write and idle;
it cannot render the OLED/LCD asterisk.

### Temporary runtime Settings

With an OLED/LCD, use SELECT on an inserted image, then Eject Menu -> Settings.
The drive has been cancelled before this menu is opened, in both FDD and QD.
LEFT/RIGHT or the rotary encoder select items; SELECT enters a submenu/editor
or accepts the edited value. Back returns to Settings; Exit returns to Eject Menu.

- Sound: FDD Step Volume (0..20), QD Motor Volume (0..20), Notify Volume (0..15,
  preserving the slot-number notification flag).
- Display: OLED Contrast (0..255; applied on refresh without resetting), Display
  Timeout (0=off, 1..254 seconds, 255=always on). Menus remain visible even when
  the configured timeout is off. OLED Contrast has no effect on an HD44780 LCD.
- Drive: FDD Interface (JC, Shugart, IBM PC, IBM PC+HD, Japanese PC,
  Japanese PC+HD, Amiga), QD READY Mode (Standard, Motor Off, JC).

All seven settings are RAM overrides until reset/power-on. They never update
FF.CFG, another settings file, or internal Flash. Normal configuration writes
exclude the RAM overrides, including if FF.CFG is reloaded after USB reconnect.
Overrides survive reconnect and HxC settings cannot replace overridden volume
or timeout values. Reset restores the usual saved/default configuration and
FF.CFG. Drive settings take effect safely before the next insert; boot-only
FDD/QD backend selection and service menu items are unchanged.

### Hardware acceptance checks (Sharp MZ-800/MZ-1F11)

1. Use S0 fitted, S1/MO open, /MOTOR on pin 10; confirm native physical QD
   reads and formats as before. Keep logical QD/MZQ/QDF write-protected.
2. With JC both open and closed, test explicit `qd-ready=standard` and
   `motor-off`. Then test `jc`: open selects standard, closed motor-off.
   Changing FDD `interface=ibmpc` must not change QD READY.
3. In FDD, test `interface=jc` (open Shugart, closed IBM PC) and explicit
   Shugart/IBM PC with either JC state. Check image navigation, encoder,
   head-step sound and eject/reinsert.
4. In both backends, enter SELECT -> Settings; edit every item, return via Back
   and Exit, reinsert and confirm the changes. Check contrast without a reset,
   timeout off/seconds/always-on and notification slot-number preservation.
5. Reconnect USB and confirm overrides remain; reset and confirm FF.CFG values
   return. Compare the USB configuration files before/after Settings.
6. On a writable native QD, test LOAD from the MZ monitor and FORMAT including
   verify. Check `R` during the READY read stream and `W` during WGATE.
   `W` must stop with WGATE, including while USB writeback is pending.
7. Test SAVE and COPY DISK, including multiple separated writes and track wrap.
   Repeat with `write-drain=instant`, `realtime` and `eot`; inspect the handover
   to READ and verify saved data. Compare against the previous working firmware.

Host tests simulate the control and UI paths, not the electrical host signals.
FORMAT/SAVE/COPY and FDD controller compatibility still require these hardware
checks before treating the new build as validated on the Sharp.

### Sharp Quick Disk byte images

The QD image handler recognises formats by their contents:

- Native FlashFloppy `.qd` bitcell images retain read/write support.
- MZQ and legacy logical `.qd` images support reading only. The block count
  and record lengths delimit the data; trailing legacy padding is ignored.
  The reader replaces `CRC` placeholders with the Sharp SIO CRC-16 (reflected
  polynomial `0xa001`, initial zero, including the block marker), inserts sync
  characters and inter-block gaps, and synthesises LSB-first MFM bitcells.
- Full byte-stream `.qdf` images with a 16-byte `-QD format-` header support
  reading only. Existing sync bytes, gaps and CRCs are preserved. Compact emulator
  QDF images beginning with four sync bytes and `CR` trailers are normalised
  like MZQ; their CRCs and physical gaps are generated.

Both logical formats use the existing QD timing, flux generator and UI. Data
is generated in small batches from the USB file; the whole image is never
loaded into RAM and no converted files are written to USB. A record index of
at most 4 KiB and a 512-byte source cache occupy the existing shared I/O buffer.
Logical images are always write-protected, including full/compact QDF. Their
experimental writer was removed after hardware tests continued to fail. No
`write-protect = no` setting can enable writing or formatting these containers.
The handler has no write callback, so mounting immediately sets the effective
read-only flag, asserts the QD write-protect pin and displays `*` on the OLED.
Native physical FlashFloppy QD images retain their existing write/format support,
subject to `write-protect`, the FAT read-only attribute and read-only storage.

Malformed or truncated compact records and images too large for the eight-second
spiral are rejected. Full QDF streams fit at most 93825 data bytes; typical
81920-byte streams retain the usual half-second lead-in and read window.
These formats describe Sharp byte records, not arbitrary Quick Disk flux or
copy protection requiring missing MFM clock cells.

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
python3 tests/check_qd_images.py
```

The QD host harness builds the real image handlers with address/undefined
behaviour sanitizers. It verifies unconditional logical-image write protection, random seeks, bitcell
ring boundaries, cache isolation, and track wrapping. Synthetic tests cover
BASIC records, compact QDF, empty images, 256 records, sync patterns in payload,
and malformed input. If `specification/{FF.qd,QDF.qdf,MZQ.mzq,legacy.qd}` are
present, it also checks the captured QDF is bit-for-bit equivalent to the native
QD track, both compact samples agree, and generated CRCs match the physical QDF.

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
