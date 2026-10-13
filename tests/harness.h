/*
 * harness.h
 *
 * What the tests have of the firmware's environment: a USB drive in RAM,
 * and an image mounted from it and driven as the floppy code drives it.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "ft.h"

/* Empties the drive, a FAT16 volume, and resets the configuration to the
 * defaults of examples/FF.CFG. */
void drive_reset(void);
/* Adds a file of an 8.3 name to the root folder. */
void drive_add(const char *name, const void *data, uint32_t len);
/* The same, but with no two clusters of the file in a row: returns the
 * number of its fragments. */
unsigned int drive_add_fragmented(const char *name, const void *data,
                                  uint32_t len);
/* Reads a file back into @buf: returns its length. */
uint32_t drive_get(const char *name, void *buf, uint32_t max);

extern struct image *im;

/* Mounts a file of the drive as the floppy code does: FR_OK, or the error
 * that image_open() gave, FR_BAD_IMAGE when no handler took it. */
FRESULT image_mount(const char *name);
/* The same, with the fast-seek table of clusters that the floppy code
 * makes for an image. */
FRESULT image_mount_fast_seek(const char *name);
/* The slot of the file mounted last. */
struct slot *image_slot(void);
/* The bit cells of one revolution of track @track from the index, MSB
 * first, into @out of @max bytes: returns their number in bytes. */
unsigned int image_stream(unsigned int track, void *out, unsigned int max);
/* @nr_bits bit cells of track @track from bit cell @start on, as the floppy
 * code asks for them mid-track: returns the bit cell the handler started
 * at, which it may round down. */
uint32_t image_stream_from(unsigned int track, uint32_t start, void *out,
                           uint32_t nr_bits);
/* The first @nr flux intervals of track @track from the index, as the
 * floppy code times its output by them: each the ticks to a flux reversal,
 * less one. */
void image_flux(unsigned int track, uint16_t *out, unsigned int nr);
/* The flux intervals that bit cells @cells stand for, @ticks_per_cell
 * sixteenths of a tick each, the fractions carried: returns their number.
 * HFE holds its cells LSB first. */
unsigned int flux_of_cells(uint16_t *out, unsigned int max,
                           const uint8_t *cells, uint32_t nr_cells,
                           bool_t lsb_first, uint32_t ticks_per_cell);
/* Writes @nr_bits bit cells, MSB first, to track @track from bit cell
 * @start on, as one write. */
void image_write(unsigned int track, uint32_t start, const void *bits,
                 uint32_t nr_bits);
/* Flushes the image file, so that drive_get() sees what was written. */
void image_sync(void);

/* Whether the firmware's console output since the test began holds @s. */
bool_t log_has(const char *s);

uint32_t crc32(const void *buf, uint32_t len);

/* Bit @i of @p, MSB first. */
unsigned int bit_get(const uint8_t *p, uint32_t i);
/* The @nr_bits bits of @src from bit @start on, around its end. */
void bits_rotate(uint8_t *dst, const uint8_t *src, uint32_t nr_bits,
                 uint32_t start);

/*
 * The board, in floppy_env.c.
 */

/* No image inserted, no timers, no interrupts, the clock at 0, every input
 * high as pulled up, the floppy interface being active low. */
void board_reset(void);
/* An input pin of port A (0) or B (1). */
void pin_set(unsigned int port, unsigned int pin, bool_t high);
/* Called whenever the clock moves on. */
extern void (*clock_hook)(time_t now);
/* Moves the clock on in steps of 0.1 ms, each firing the timers due and
 * running the pending interrupts. */
void clock_run_ms(unsigned int ms);
/* Runs the pending interrupts that are enabled; raises one first. */
void irqs_run(void);
void irq_raise(unsigned int x);
extern unsigned int nr_speaker_pulses;

/* The floppy code booted for a Shugart host, and the drive selected. */
void floppy_boot(void);
/* Inserts a file of the drive into the floppy code: the disk passes its
 * index now, and every 200 ms after. */
void floppy_insert_image(const char *name);
/* The head at cylinder @cyl as the floppy code counts them, and side @head
 * selected. */
void floppy_head(unsigned int cyl, unsigned int head);
/* The host writes @nr_cells bit cells, MSB first, from @start ticks of the
 * WDATA timer past the index: WGATE asserted there, a flux reversal on WDATA
 * for each 1, @ticks a cell and each up to @jitter ticks early or late, and
 * WGATE released after the last cell. The floppy code's main loop runs
 * meanwhile and until the write is in the image, though not before it, so
 * that no read stream moves the index. */
void host_write(uint32_t start, const uint8_t *cells, uint32_t nr_cells,
                unsigned int ticks, unsigned int jitter);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
