/*
 * emu.h
 *
 * Interface between the two halves of ffemu: the firmware half, built with the
 * firmware's own headers, and the host half, built with the host C library.
 * Only plain C types cross this boundary.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#ifndef FFEMU_EMU_H
#define FFEMU_EMU_H

#include <stdint.h>
#include <stddef.h>

/*
 * Host half, called from the firmware half.
 */

/* Interrupt controller (cpu.c). Priorities are 0 (highest) to 15. */
int emu_in_exception(void);
void emu_irq_global(int enable);
unsigned int emu_irq_save(unsigned int newpri);
void emu_irq_restore(unsigned int oldpri);
void emu_irqx_enable(unsigned int x);
void emu_irqx_disable(unsigned int x);
int emu_irqx_is_enabled(unsigned int x);
void emu_irqx_set_pending(unsigned int x);
void emu_irqx_clear_pending(unsigned int x);
int emu_irqx_is_pending(unsigned int x);
void emu_irqx_set_prio(unsigned int x, unsigned int pri);
unsigned int emu_irqx_get_prio(unsigned int x);
/* Runs the handlers of any interrupts that are now deliverable. */
void emu_irq_poll(void);

/* Time (cpu.c). */
uint64_t emu_time_ns(void);
/* Waits @ns, keeping the peripherals and the interrupts running. */
void emu_idle_ns(uint64_t ns);
void emu_relax(void);

/* Process control (cpu.c). */
void emu_illegal(const char *file, int line) __attribute__((noreturn));
void emu_reset(void) __attribute__((noreturn));

/* Firmware console output, shown in the log pane (cpu.c). */
void emu_log(const char *s);

/* Contents of the emulated configuration flash page (cpu.c). */
unsigned int emu_flash_load(void *buf, unsigned int size);
void emu_flash_save(const void *buf, unsigned int size);

/* I2C slave devices (ssd1306.c). */
int emu_i2c_dev_start(unsigned int addr, int rd); /* returns 1 on ACK */
void emu_i2c_dev_write(uint8_t b);
uint8_t emu_i2c_dev_read(void);
void emu_i2c_dev_stop(void);

/* USB flash drive: 512-byte sectors of a FAT volume (fatimg.c). */
int emu_usb_inserted(void);
int emu_usb_read(void *buf, uint32_t sector, unsigned int count);
int emu_usb_write(const void *buf, uint32_t sector, unsigned int count);

/* Front-panel inputs, written by the user interface. */
#define EMU_B_LEFT   1
#define EMU_B_RIGHT  2
#define EMU_B_SELECT 4
extern volatile unsigned int emu_in_buttons;
/* Encoder detents not yet delivered: clockwise is positive. */
extern volatile int emu_in_rotary;

/* Front-panel outputs, read by the user interface. */
/* Counts the pulses sent to the speaker. */
extern volatile unsigned int emu_out_speaker;

/*
 * Firmware half, called from the host half.
 */

/* The firmware's main(). */
int ff_main(void);

/* MCU peripherals (hw.c). */
void emu_hw_init(void);
void emu_hw_sync(void);
int emu_hw_busy(void);
void emu_irq_vector(unsigned int nr);

/* Facts about the emulated device, for the status pane (hw.c, stubs.c). */
const char *emu_board_name(void);
const char *emu_fw_version(void);
unsigned int emu_arena_used(void);
unsigned int emu_arena_size(void);

/* The options of the configuration in flash that differ from the defaults,
 * or all of them if @all, as "name=value" separated by @sep, into @buf; 0 if
 * flash holds none. With @all, each starts with '*' if it differs from its
 * default, else with a space. */
int emu_flash_describe(char *buf, unsigned int size, char sep, int all);

/* The configuration as it is kept in flash: emu_flash_cfg_size() bytes, and
 * the firmware's defaults for them. */
unsigned int emu_flash_cfg_size(void);
void emu_flash_cfg_defaults(void *cfg);
/* Its options, in the firmware's order: the name of option @i, and its value
 * in @cfg as FF.CFG would set it into @value, with what it is in @flags. */
unsigned int emu_flash_nr_options(void);
const char *emu_flash_option(unsigned int i, const void *cfg, char *value,
                             unsigned int size, unsigned int *flags);
#define EMU_OPT_changed  1 /* it differs from the default */
#define EMU_OPT_raw      2 /* a number, which FF.CFG would not give */
#define EMU_OPT_hex_only 4 /* FF.CFG cannot express it: @value is empty */
/* Sets option @name in @cfg from @value, as FF.CFG has it or as a number
 * after "0x". */
int emu_flash_set_option(void *cfg, const char *name, const char *value);
#define EMU_SET_ok      0
#define EMU_SET_unknown 1 /* no such option in this firmware */
#define EMU_SET_bad     2 /* a value that it cannot take: @cfg unchanged */
/* The configuration in flash into @cfg, or the firmware's defaults if flash
 * holds none, which returns 0. */
int emu_flash_get(void *cfg);
/* Whether display-type in @cfg suits an OLED of @rows rows. */
int emu_flash_display_fits(const void *cfg, unsigned int rows);
/* Sets display-type in @cfg for an OLED of @rows rows, keeping what else it
 * says about an OLED. */
void emu_flash_set_oled_rows(void *cfg, unsigned int rows);

#endif /* FFEMU_EMU_H */
