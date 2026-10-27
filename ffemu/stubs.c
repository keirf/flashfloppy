/*
 * stubs.c
 *
 * Stand-ins for the parts of the firmware that are tied to the MCU or that
 * ffemu leaves out: MCU bring-up and delays, the serial console, the
 * configuration flash page, the heap and the USB host stack, each keeping the
 * interface that the rest of the firmware expects of the file it replaces;
 * and wrappers of the firmware's floppy interface code.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/*
 * MCU bring-up, delays and reset: src/mcu_stm32f105.c, src/cortex.c.
 */

unsigned int sysclk_mhz = 144;
unsigned int apb1_mhz = 72;
bool_t is_artery_mcu = TRUE;
unsigned int flash_page_size = FLASH_PAGE_SIZE;
unsigned int ram_kb = 32;

volatile uint32_t _reset_flag;
uint32_t _thread_stackbottom[1], _irq_stackbottom[1];

/* Section bounds from the linker script: main() uses them to load .data and
 * to clear .bss, which the host has done already. All are the same address,
 * so that it copies and clears nothing. */
char _sdat[4];
extern char _edat[4] __attribute__((alias("_sdat")));
extern char _ldat[4] __attribute__((alias("_sdat")));
extern char _sbss[4] __attribute__((alias("_sdat")));
extern char _ebss[4] __attribute__((alias("_sdat")));

void stm32_init(void)
{
    emu_hw_init();
}

void delay_ticks(unsigned int ticks)
{
    emu_idle_ns((uint64_t)ticks * 1000 / STK_MHZ);
}

void delay_ns(unsigned int ns)
{
    delay_ticks((ns * STK_MHZ) / 1000u);
}

void delay_us(unsigned int us)
{
    delay_ticks(us * STK_MHZ);
}

void delay_ms(unsigned int ms)
{
    delay_ticks(ms * 1000u * STK_MHZ);
}

void system_reset(void)
{
    printk("Resetting...\n");
    emu_reset();
}

void gpio_configure_pin(GPIO gpio, unsigned int pin, unsigned int mode)
{
    gpio_write_pin(gpio, pin, mode >> 4);
    mode &= 0xfu;
    if (pin >= 8) {
        pin -= 8;
        gpio->crh = (gpio->crh & ~(0xfu<<(pin<<2))) | (mode<<(pin<<2));
    } else {
        gpio->crl = (gpio->crl & ~(0xfu<<(pin<<2))) | (mode<<(pin<<2));
    }
}

void _exti_route(unsigned int px, unsigned int pin)
{
    unsigned int n = pin >> 2;
    unsigned int s = (pin & 3) << 2;
    uint32_t exticr = afio->exticr[n];
    ASSERT(!in_exception()); /* no races please */
    exticr &= ~(0xf << s);
    exticr |= px << s;
    afio->exticr[n] = exticr;
}

/*
 * Serial console: src/console.c. Output goes to the log pane.
 */

int vprintk(const char *format, va_list ap)
{
    char str[128];
    int n = vsnprintf(str, sizeof(str), format, ap);
    emu_log(str);
    return n;
}

int printk(const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = vprintk(format, ap);
    va_end(ap);

    return n;
}

void console_init(void)
{
}

void console_sync(void)
{
}

void console_crash_on_input(void)
{
}

/*
 * Configuration cached in flash: src/flash_cfg.c. One slot, held by the host
 * half, which keeps it in the settings file.
 */

const struct ff_cfg dfl_ff_cfg = {
    .version = FFCFG_VERSION,
    .size = sizeof(struct ff_cfg),
#define x(n,o,v) .o = v,
#include "ff_cfg_defaults.h"
#undef x
};

struct ff_cfg ff_cfg;

void flash_ff_cfg_update(void *scratch)
{
    struct ff_cfg old;

    if ((emu_flash_load(&old, sizeof(old)) == sizeof(old))
        && !memcmp(&old, &ff_cfg, sizeof(ff_cfg)))
        return;

    emu_flash_save(&ff_cfg, sizeof(ff_cfg));
    printk("CFG: Written to flash\n");
}

void flash_ff_cfg_erase(void)
{
    emu_flash_save(&ff_cfg, 0);
}

void flash_ff_cfg_read(void)
{
    struct ff_cfg f;
    bool_t found = (emu_flash_load(&f, sizeof(f)) == sizeof(f))
        && (f.version == dfl_ff_cfg.version);

    ff_cfg = dfl_ff_cfg;
    printk("CFG: ");
    if (found) {
        unsigned int sz = min_t(unsigned int, f.size, ff_cfg.size);
        printk("Flash v%u %u bytes\n", f.version, sz);
        /* Copy over all options that are present in Flash. */
        if (sz > offsetof(struct ff_cfg, interface))
            memcpy(&ff_cfg.interface, &f.interface,
                   sz - offsetof(struct ff_cfg, interface));
    } else {
        printk("Factory Defaults\n");
    }
}

/* The slots of the flash area, as flash_cfg.c lays them out: 64 words
 * each, the last but one 0xffff unless the slot is deleted, the last a CRC
 * over the whole slot. */
#define SLOTW_NR 64
#define SLOTW_DEAD (SLOTW_NR - 2)
#define SLOTW_CRC (SLOTW_NR - 1)
union cfg_slot {
    struct ff_cfg ff_cfg;
    uint16_t words[SLOTW_NR];
};
#define SLOT_NR (FLASH_PAGE_SIZE / sizeof(union cfg_slot))

/* The area that the firmware writes to FLASH.DMP: the configuration in
 * flash, if it holds one, in the first slot, the others blank. */
const void *flash_ff_cfg_area(void)
{
    static union cfg_slot area[SLOT_NR];
    union cfg_slot *slot = &area[0];
    uint16_t crc;

    memset(area, 0xff, sizeof(area));
    memset(slot, 0, sizeof(*slot));
    if (emu_flash_load(&slot->ff_cfg, sizeof(slot->ff_cfg)) == 0) {
        memset(slot, 0xff, sizeof(*slot));
        emu_flash_dumped(0);
        return area;
    }
    slot->words[SLOTW_DEAD] = 0xffff;
    crc = crc16_ccitt(slot, sizeof(*slot) - 2, 0xffff);
    slot->words[SLOTW_CRC] = htobe16(crc);
    emu_flash_dumped(1);
    return area;
}

/* The flag that the main menu sets for the dump, static in main.c, which
 * ffemu's build makes global (Makefile). */
extern bool_t is_dump_flash_cfg_needed;

int emu_flash_dump_pending(void)
{
    return is_dump_flash_cfg_needed;
}

/* The configuration in flash, or FALSE if it holds none. */
static bool_t flash_cfg(struct ff_cfg *f)
{
    return (emu_flash_load(f, sizeof(*f)) == sizeof(*f))
        && (f->version == dfl_ff_cfg.version);
}

/* The options, in the order of ff_cfg_defaults.h, and where they are. */
enum {
#define x(n,o,v) OPT_##o,
#include "ff_cfg_defaults.h"
#undef x
    OPT_nr
};
_Static_assert(OPT_nr <= EMU_OPTIONS_MAX, "EMU_OPTIONS_MAX is too small");

static const struct {
    const char *name;
    uint16_t offset, size;
} option[OPT_nr] = {
#define x(n,o,v) { #n, offsetof(struct ff_cfg, o), sizeof(ff_cfg.o) },
#include "ff_cfg_defaults.h"
#undef x
};

/* The values of the options that FF.CFG names, as src/main.c parses them,
 * in FF.CFG's order: a name after another of the same value is an alias,
 * which is read but never written. */
static const struct emu_named yes_no[] = { { "yes", 1 }, { "no", 0 } };
static const struct emu_named interface_names[] = {
    { "jc", FINTF_JC }, { "shugart", FINTF_SHUGART },
    { "ibmpc", FINTF_IBMPC }, { "ibmpc-hdout", FINTF_IBMPC_HDOUT },
    { "jppc", FINTF_JPPC }, { "jppc-hdout", FINTF_JPPC_HDOUT },
    { "akai-s950", FINTF_JPPC_HDOUT }, { "amiga", FINTF_AMIGA }
};
static const struct emu_named host_names[] = {
    { "acorn", HOST_acorn }, { "akai", HOST_akai }, { "casio", HOST_casio },
    { "dec", HOST_dec }, { "ensoniq", HOST_ensoniq },
    { "fluke", HOST_fluke }, { "gem", HOST_gem },
    { "ibm-3174", HOST_ibm_3174 }, { "memotech", HOST_memotech },
    { "msx", HOST_msx }, { "nascom", HOST_nascom }, { "pc98", HOST_pc98 },
    { "pc-dos", HOST_pc_dos }, { "tandy-coco", HOST_tandy_coco },
    { "ti99", HOST_ti99 }, { "uknc", HOST_uknc },
    { "unspecified", HOST_unspecified }
};
/* An output not connected is one held high, so nc reads back as high. */
static const struct emu_named pin_names[] = {
    { "auto", PIN_auto }, { "low", PIN_low }, { "high", PIN_high },
    { "nc", PIN_nc }, { "rdy", PIN_rdy }, { "nrdy", PIN_nrdy },
    { "dens", PIN_dens }, { "ndens", PIN_ndens }, { "chg", PIN_chg },
    { "nchg", PIN_nchg }
};
static const struct emu_named track_change_names[] = {
    { "instant", TRKCHG_instant }, { "realtime", TRKCHG_realtime }
};
static const struct emu_named write_drain_names[] = {
    { "instant", WDRAIN_instant }, { "realtime", WDRAIN_realtime },
    { "eot", WDRAIN_eot }
};
static const struct emu_named image_on_startup_names[] = {
    { "last", IMGS_last }, { "static", IMGS_static }, { "init", IMGS_init }
};
static const struct emu_named folder_sort_names[] = {
    { "always", SORT_always }, { "never", SORT_never },
    { "small", SORT_small }
};
static const struct emu_named sort_priority_names[] = {
    { "folders", SORTPRI_folders }, { "files", SORTPRI_files },
    { "none", SORTPRI_none }
};
static const struct emu_named nav_mode_names[] = {
    { "native", NAVMODE_native }, { "indexed", NAVMODE_indexed },
    { "default", NAVMODE_default }
};
static const struct emu_named twobutton_names[] = {
    { "zero", TWOBUTTON_zero }, { "eject", TWOBUTTON_eject },
    { "htu", TWOBUTTON_htu }, { "rotary", TWOBUTTON_rotary },
    { "rotary-fast", TWOBUTTON_rotary_fast }
};
static const struct emu_named twobutton_flags[] = {
    { "reverse", TWOBUTTON_reverse }
};
static const struct emu_named rotary_names[] = {
    { "none", ROT_none }, { "quarter", ROT_quarter }, { "half", ROT_half },
    { "full", ROT_full }, { "trackball", ROT_trackball },
    { "buttons", ROT_buttons }
};
static const struct emu_named rotary_flags[] = {
    { "v2", ROT_v2 }, { "reverse", ROT_reverse }
};
static const struct emu_named display_on_names[] = {
    { "yes", DISPON_yes }, { "sel", DISPON_sel }, { "no", DISPON_no }
};
static const struct emu_named show_filename_ext_names[] = {
    { "no", SHOWEXT_no }, { "yes", SHOWEXT_yes }, { "auto", SHOWEXT_auto }
};
static const struct emu_named font_names[] = {
    { "6x13", FONT_6x13 }, { "8x16", FONT_8x16 }
};
static const struct emu_named notify_flags[] = {
    { "slotnr", NOTIFY_slotnr }
};

/* What each option takes. One not here takes text that code of its own
 * parses, as display-type does, or else a number. */
#define VALUES(a) .values = a, .nr_values = ARRAY_SIZE(a)
#define FLAGS(a) .flags = a, .nr_flags = ARRAY_SIZE(a)
#define YES_NO { .kind = EMU_KIND_yes_no, VALUES(yes_no) }
#define NAMED(a) { .kind = EMU_KIND_named, VALUES(a) }
#define NUMBER { .kind = EMU_KIND_number }
#define STRING { .kind = EMU_KIND_string }
static const struct emu_option_info info[OPT_nr] = {
    [OPT_interface] = NAMED(interface_names),
    [OPT_host] = NAMED(host_names),
    [OPT_pin02] = NAMED(pin_names),
    [OPT_pin34] = NAMED(pin_names),
    [OPT_write_protect] = YES_NO,
    [OPT_max_cyl] = NUMBER,
    [OPT_side_select_glitch_filter] = NUMBER,
    [OPT_track_change] = NAMED(track_change_names),
    [OPT_write_drain] = NAMED(write_drain_names),
    [OPT_index_suppression] = YES_NO,
    [OPT_head_settle_ms] = NUMBER,
    [OPT_ejected_on_startup] = YES_NO,
    [OPT_image_on_startup] = NAMED(image_on_startup_names),
    [OPT_display_probe_ms] = NUMBER,
    [OPT_autoselect_file_secs] = NUMBER,
    [OPT_autoselect_folder_secs] = NUMBER,
    [OPT_folder_sort] = NAMED(folder_sort_names),
    [OPT_sort_priority] = NAMED(sort_priority_names),
    [OPT_nav_mode] = NAMED(nav_mode_names),
    [OPT_nav_loop] = YES_NO,
    [OPT_twobutton_action] = {
        .kind = EMU_KIND_named, VALUES(twobutton_names),
        FLAGS(twobutton_flags), .mask = TWOBUTTON_mask,
        .start = TWOBUTTON_zero },
    [OPT_rotary] = {
        .kind = EMU_KIND_named, VALUES(rotary_names), FLAGS(rotary_flags),
        .mask = ROT_typemask, .start = ROT_full },
    [OPT_indexed_prefix] = STRING,
    [OPT_oled_font] = NAMED(font_names),
    [OPT_oled_contrast] = NUMBER,
    [OPT_osd_columns] = NUMBER,
    [OPT_display_off_secs] = NUMBER,
    [OPT_display_on_activity] = NAMED(display_on_names),
    [OPT_show_filename_ext] = NAMED(show_filename_ext_names),
    [OPT_display_scroll_rate] = NUMBER,
    [OPT_display_scroll_pause] = NUMBER,
    [OPT_nav_scroll_rate] = NUMBER,
    [OPT_nav_scroll_pause] = NUMBER,
    [OPT_hfe_step] = NUMBER,
    [OPT_hfe_rpm] = NUMBER,
    [OPT_step_volume] = NUMBER,
    [OPT_notify_volume] = {
        .kind = EMU_KIND_number, FLAGS(notify_flags),
        .mask = NOTIFY_volume_mask },
    [OPT_da_report_version] = STRING,
    [OPT_extend_image] = YES_NO
};
#undef VALUES
#undef FLAGS
#undef YES_NO
#undef NAMED
#undef NUMBER
#undef STRING

/* A decimal number, or a hexadecimal one after "0x", into @v. */
static bool_t parse_number(const char *s, uint32_t *v)
{
    unsigned int base = 10, d;
    uint32_t n = 0;

    if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
        base = 16;
        s += 2;
    }
    if (*s == '\0')
        return FALSE;
    for (; *s != '\0'; s++) {
        if ((*s >= '0') && (*s <= '9'))
            d = *s - '0';
        else if ((base == 16) && (*s >= 'a') && (*s <= 'f'))
            d = *s - 'a' + 10;
        else if ((base == 16) && (*s >= 'A') && (*s <= 'F'))
            d = *s - 'A' + 10;
        else
            return FALSE;
        if (n > (0xffffffffu - d) / base)
            return FALSE;
        n = n * base + d;
    }
    *v = n;
    return TRUE;
}

/* Cuts @s at the first @sep, and returns what follows it, or NULL. */
static char *next_token(char *s, char sep)
{
    for (; *s != '\0'; s++) {
        if (*s == sep) {
            *s = '\0';
            return s + 1;
        }
    }
    return NULL;
}

static bool_t parse_display_type(char *s, uint32_t *v)
{
    char *next, *x;
    uint32_t w, h;

    *v = DISPLAY_auto;
    for (; s != NULL; s = next) {
        next = next_token(s, '-');
        if (!strcmp(s, "auto") && (*v == DISPLAY_auto)) {
            continue;
        } else if (!strcmp(s, "lcd")) {
            *v = DISPLAY_lcd;
        } else if (!strcmp(s, "oled")) {
            *v = DISPLAY_oled;
        } else if ((x = next_token(s, 'x')) != NULL) {
            if (!parse_number(s, &w) || !parse_number(x, &h))
                return FALSE;
            if (*v & DISPLAY_oled) {
                if ((w != 128) || ((h != 32) && (h != 64)))
                    return FALSE;
                if (h == 64)
                    *v |= DISPLAY_oled_64;
            } else if (*v & DISPLAY_lcd) {
                if ((w > 63) || (h > 31))
                    return FALSE;
                *v |= DISPLAY_lcd_columns(w) | DISPLAY_lcd_rows(h);
            } else {
                return FALSE;
            }
        } else if (*v & DISPLAY_oled) {
            if (!strcmp(s, "rotate"))
                *v |= DISPLAY_rotate;
            else if (!strcmp(s, "narrow"))
                *v |= DISPLAY_narrow;
            else if (!strcmp(s, "narrower"))
                *v |= DISPLAY_narrower;
            else if (!strcmp(s, "inverse"))
                *v |= DISPLAY_inverse;
            else if (!strcmp(s, "ztech"))
                *v |= DISPLAY_ztech;
            else if (!strcmp(s, "slow"))
                *v |= DISPLAY_slow;
            else if (!strcmp(s, "hflip"))
                *v |= DISPLAY_hflip;
            else
                return FALSE;
        } else {
            return FALSE;
        }
    }
    return TRUE;
}

static bool_t parse_display_order(char *s, uint32_t *v)
{
    unsigned int sh = 0;
    char *next;

    if (!strcmp(s, "default")) {
        *v = DORD_default;
        return TRUE;
    }
    *v = 0;
    for (; s != NULL; s = next) {
        next = next_token(s, ',');
        if ((sh >= 16) || (s[0] < '0') || (s[0] > '7')
            || ((s[1] != '\0') && ((s[1] != 'd') || (s[2] != '\0'))))
            return FALSE;
        *v |= ((s[0] - '0') | ((s[1] == 'd') ? DORD_double : 0)) << sh;
        sh += DORD_shift;
    }
    /* Blank rows for the rest, within the 16 bits that the option has. */
    if (sh < 16)
        *v |= (0x7777u << sh) & 0xffff;
    return TRUE;
}

/* Value @s of option @o: one of its names, or for a number option a
 * number; then its flags after commas, in any order, as src/main.c takes
 * them, a list of flags alone starting from @o's start. */
static bool_t parse_named(const struct emu_option_info *o, char *s,
                         uint32_t *v)
{
    uint32_t mask = o->mask ? o->mask : ~0u, n;
    unsigned int j;
    char *next;

    *v = o->start;
    for (; s != NULL; s = next) {
        next = o->nr_flags ? next_token(s, ',') : NULL;
        for (j = 0; j < o->nr_flags; j++)
            if (!strcmp(s, o->flags[j].name))
                break;
        if (j < o->nr_flags) {
            *v |= o->flags[j].value;
            continue;
        }
        if (o->kind == EMU_KIND_number) {
            if (!parse_number(s, &n) || (n & ~mask))
                return FALSE;
        } else {
            for (j = 0; j < o->nr_values; j++)
                if (!strcmp(s, o->values[j].name))
                    break;
            if (j == o->nr_values)
                return FALSE;
            n = o->values[j].value;
        }
        *v = (*v & ~mask) | n;
    }
    return TRUE;
}

/* Value @v of option @o as FF.CFG has it, into @buf: its first name, or
 * its number, then its flags; "?" for a value that has no name. */
static void format_named(const struct emu_option_info *o, uint32_t v,
                         char *buf, unsigned int size)
{
    uint32_t mask = o->mask ? o->mask : ~0u;
    unsigned int j, n;

    if (o->kind == EMU_KIND_number) {
        n = snprintf(buf, size, "%u", (unsigned int)(v & mask));
    } else {
        for (j = 0; j < o->nr_values; j++)
            if (o->values[j].value == (v & mask))
                break;
        n = snprintf(buf, size, "%s",
                     (j < o->nr_values) ? o->values[j].name : "?");
    }
    for (j = 0; (j < o->nr_flags) && (n < size); j++)
        if (v & o->flags[j].value)
            n += snprintf(buf + n, size - n, ",%s", o->flags[j].name);
}

/* Sets option @i of @f from @text, as FF.CFG has it or as a raw number after
 * "0x". Returns FALSE, leaving @f as it is, if the value makes no sense or
 * does not fit. */
static bool_t parse_option(unsigned int i, struct ff_cfg *f, const char *text)
{
    uint8_t *p = (uint8_t *)f + option[i].offset;
    unsigned int sz = option[i].size, n;
    char s[64];
    uint32_t v = 0;

    if (sz > 4) {
        /* A string: quoted, or as FF.CFG takes it unquoted. */
        n = strlen(text);
        if ((text[0] == '"') && (n >= 2) && (text[n-1] == '"')) {
            text++;
            n -= 2;
        }
        if (n >= sz)
            return FALSE;
        memset(p, 0, sz);
        memcpy(p, text, n);
        return TRUE;
    }

    n = strlen(text);
    if ((n == 0) || (n >= sizeof(s)))
        return FALSE;
    memcpy(s, text, n + 1);

    if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
        if (!parse_number(s, &v))
            return FALSE;
        goto store;
    }

    switch (info[i].kind) {
    case EMU_KIND_yes_no: case EMU_KIND_named: case EMU_KIND_number:
        if (!parse_named(&info[i], s, &v))
            return FALSE;
        goto store;
    }

    switch (i) {
    case OPT_motor_delay:
        if (!strcmp(s, "ignore")) {
            v = MOTOR_ignore;
        } else {
            if (!parse_number(s, &v))
                return FALSE;
            v = (v + 9) / 10;
        }
        goto store;
    case OPT_chgrst:
        if (!strcmp(s, "step"))
            v = CHGRST_step;
        else if (!strcmp(s, "pa14"))
            v = CHGRST_pa14;
        else if (strncmp(s, "delay-", 6) || !parse_number(s + 6, &v))
            return FALSE;
        goto store;
    case OPT_display_type:
        if (!parse_display_type(s, &v))
            return FALSE;
        goto store;
    case OPT_display_order: case OPT_osd_display_order:
        if (!parse_display_order(s, &v))
            return FALSE;
        goto store;
    default:
        /* A plain number. */
        if (!parse_number(s, &v))
            return FALSE;
        goto store;
    }

store:
    if ((sz < 4) && (v >> (8 * sz)))
        return FALSE;
    memcpy(p, &v, sz);
    return TRUE;
}

/* A display order: its rows, less the blank ones that the parser adds. */
static void format_display_order(char *buf, unsigned int size, uint16_t v)
{
    unsigned int nr = 4, i, n = 0;

    if (v == DORD_default) {
        snprintf(buf, size, "default");
        return;
    }
    while ((nr > 1) && (((v >> ((nr - 1) * DORD_shift)) & 15) == DORD_row))
        nr--;
    buf[0] = '\0';
    for (i = 0; i < nr; i++) {
        unsigned int row = (v >> (i * DORD_shift)) & 15;
        n += snprintf(buf + n, size - n, "%s%u%s", i ? "," : "",
                      row & DORD_row, (row & DORD_double) ? "d" : "");
    }
}

static void format_display_type(char *buf, unsigned int size, uint16_t v)
{
    /* Bit 0 is lcd on its own, but narrower with oled. */
    if (v == DISPLAY_auto) {
        snprintf(buf, size, "auto");
    } else if (v & DISPLAY_oled) {
        snprintf(buf, size, "oled-128x%u%s%s%s%s%s%s%s",
                 (v & DISPLAY_oled_64) ? 64 : 32,
                 (v & DISPLAY_rotate) ? "-rotate" : "",
                 (v & DISPLAY_hflip) ? "-hflip" : "",
                 (v & DISPLAY_narrow) ? "-narrow" : "",
                 (v & DISPLAY_narrower) ? "-narrower" : "",
                 (v & DISPLAY_inverse) ? "-inverse" : "",
                 (v & DISPLAY_ztech) ? "-ztech" : "",
                 (v & DISPLAY_slow) ? "-slow" : "");
    } else {
        snprintf(buf, size, "lcd-%ux%02u", (v >> _DISPLAY_lcd_columns) & 63,
                 v >> _DISPLAY_lcd_rows);
    }
}

/* Option @i of @f as FF.CFG would set it, into @buf, as far as FF.CFG can:
 * whatever does not read back the same is a raw number, or for a string,
 * nothing. Returns EMU_OPT_raw or EMU_OPT_hex_only then, else 0. */
static unsigned int format_option(unsigned int i, const struct ff_cfg *f,
                                  char *buf, unsigned int size)
{
    const uint8_t *p = (const uint8_t *)f + option[i].offset;
    unsigned int sz = option[i].size, j;
    const char *name = NULL;
    struct ff_cfg t = *f;
    uint32_t v = 0;

    if (sz > 4) {
        /* The firmware's snprintf() has no precision to bound it. */
        char s[32] = "";
        for (j = 0; (j < sz) && (p[j] != '\0'); j++)
            if ((p[j] < ' ') || (p[j] > '~') || (p[j] == '"'))
                break;
        memcpy(s, p, min_t(unsigned int, j, sizeof(s) - 1));
        snprintf(buf, size, "\"%s\"", s);
        goto check;
    }
    memcpy(&v, p, sz);
    buf[0] = '\0';

    switch (info[i].kind) {
    case EMU_KIND_yes_no: case EMU_KIND_named: case EMU_KIND_number:
        format_named(&info[i], v, buf, size);
        goto check;
    }

    switch (i) {
    case OPT_motor_delay:
        if (v == MOTOR_ignore)
            name = "ignore";
        else
            snprintf(buf, size, "%u", (unsigned int)v * 10);
        break;
    case OPT_chgrst:
        if ((v == CHGRST_step) || (v == CHGRST_pa14))
            name = (v == CHGRST_step) ? "step" : "pa14";
        else
            snprintf(buf, size, "delay-%u", (unsigned int)v);
        break;
    case OPT_display_type:
        format_display_type(buf, size, v);
        break;
    case OPT_display_order: case OPT_osd_display_order:
        format_display_order(buf, size, v);
        break;
    default:
        snprintf(buf, size, "%u", (unsigned int)v);
        break;
    }
    if (name != NULL)
        snprintf(buf, size, "%s", name);
    else if (buf[0] == '\0')
        snprintf(buf, size, "?");

check:
    /* What does not read back to the same bytes is not FF.CFG's to say. */
    memset((uint8_t *)&t + option[i].offset, 0xa5, sz);
    if (parse_option(i, &t, buf)
        && !memcmp((uint8_t *)&t + option[i].offset, p, sz))
        return 0;
    if (sz > 4) {
        buf[0] = '\0';
        return EMU_OPT_hex_only;
    }
    snprintf(buf, size, "0x%X", (unsigned int)v);
    return EMU_OPT_raw;
}

unsigned int emu_flash_cfg_size(void)
{
    return sizeof(struct ff_cfg);
}

void emu_flash_cfg_defaults(void *cfg)
{
    memcpy(cfg, &dfl_ff_cfg, sizeof(dfl_ff_cfg));
}

unsigned int emu_flash_nr_options(void)
{
    return OPT_nr;
}

void emu_flash_option_info(unsigned int i, struct emu_option_info *o)
{
    *o = info[i];
    o->size = option[i].size;
}

const char *emu_flash_option(unsigned int i, const void *cfg, char *value,
                             unsigned int size, unsigned int *flags)
{
    const struct ff_cfg *f = cfg;
    unsigned int o = option[i].offset;

    *flags = format_option(i, f, value, size);
    if (memcmp((const uint8_t *)f + o, (const uint8_t *)&dfl_ff_cfg + o,
               option[i].size))
        *flags |= EMU_OPT_changed;
    return option[i].name;
}

int emu_flash_set_option(void *cfg, const char *name, const char *value)
{
    unsigned int i;

    for (i = 0; i < OPT_nr; i++)
        if (!strcmp(name, option[i].name))
            return parse_option(i, cfg, value) ? EMU_SET_ok : EMU_SET_bad;
    return EMU_SET_unknown;
}

int emu_flash_describe(char *buf, unsigned int size, char sep, int all)
{
    struct ff_cfg f;
    char gap[2] = { sep, '\0' }, value[64];
    unsigned int i, n = 0, flags;
    const char *name;

    if (!flash_cfg(&f))
        return 0;

    buf[0] = '\0';
    for (i = 0; (i < OPT_nr) && (n < size); i++) {
        name = emu_flash_option(i, &f, value, sizeof(value), &flags);
        if (!(flags & EMU_OPT_changed) && !all)
            continue;
        n += snprintf(buf + n, size - n, "%s%s%s=%s", n ? gap : "",
                      all ? ((flags & EMU_OPT_changed) ? "*" : " ") : "",
                      name, value);
    }
    return 1;
}

int emu_flash_get(void *cfg)
{
    struct ff_cfg *f = cfg;

    if (flash_cfg(f))
        return 1;
    *f = dfl_ff_cfg;
    return 0;
}

int emu_flash_parse_dump(const void *area, unsigned int len, void *cfg,
                         unsigned int *slot)
{
    union cfg_slot s;
    struct ff_cfg *f = cfg;
    unsigned int i, sz;

    if (len != SLOT_NR * sizeof(s))
        return EMU_DUMP_bad;
    /* As flash_cfg.c finds its slot: the first neither deleted nor blank
     * whose version and CRC are good; a blank one ends the search. */
    for (i = 0; i < SLOT_NR; i++) {
        memcpy(&s, (const uint8_t *)area + i * sizeof(s), sizeof(s));
        if (s.words[SLOTW_DEAD] != 0xffff)
            continue;
        if (s.words[0] == 0xffff)
            break;
        if ((s.ff_cfg.version != dfl_ff_cfg.version)
            || crc16_ccitt(&s, sizeof(s), 0xffff))
            continue;
        /* The options it holds over the defaults, as the firmware takes
         * them at boot. */
        *f = dfl_ff_cfg;
        sz = min_t(unsigned int, s.ff_cfg.size, sizeof(*f));
        if (sz > offsetof(struct ff_cfg, interface))
            memcpy(&f->interface, &s.ff_cfg.interface,
                   sz - offsetof(struct ff_cfg, interface));
        *slot = i;
        return EMU_DUMP_slot;
    }
    return EMU_DUMP_none;
}

int emu_flash_display_fits(const void *cfg, unsigned int rows)
{
    uint16_t t = ((const struct ff_cfg *)cfg)->display_type;

    /* The firmware drives 32 rows when it finds an OLED by itself. */
    if (t == DISPLAY_auto)
        return rows == 32;
    if (!(t & DISPLAY_oled))
        return 0;
    return !!(t & DISPLAY_oled_64) == (rows == 64);
}

void emu_flash_set_oled_rows(void *cfg, unsigned int rows)
{
    struct ff_cfg *f = cfg;
    uint16_t t = (f->display_type & DISPLAY_oled) ? f->display_type
        : DISPLAY_oled;

    f->display_type = (rows == 64) ? (t | DISPLAY_oled_64)
        : (t & ~DISPLAY_oled_64);
}

int emu_flash_lcd_fits(const void *cfg, unsigned int cols, unsigned int rows)
{
    uint16_t t = ((const struct ff_cfg *)cfg)->display_type;
    unsigned int c, r;

    /* As the firmware sizes an LCD: from display-type, clamped, so that one
     * it finds by itself is a 16x2. */
    if (t & DISPLAY_oled)
        return 0;
    c = range_t(unsigned int, (t >> _DISPLAY_lcd_columns) & 63, 16, 40);
    r = range_t(unsigned int, (t >> _DISPLAY_lcd_rows) & 7, 2, 4);
    return (c == cols) && (r == rows);
}

void emu_flash_set_lcd(void *cfg, unsigned int cols, unsigned int rows)
{
    ((struct ff_cfg *)cfg)->display_type = DISPLAY_lcd
        | DISPLAY_lcd_columns(cols) | DISPLAY_lcd_rows(rows);
}

int emu_flash_display_auto(const void *cfg)
{
    return (((const struct ff_cfg *)cfg)->display_type & 3) == DISPLAY_auto;
}

void emu_flash_set_display_auto(void *cfg)
{
    ((struct ff_cfg *)cfg)->display_type = DISPLAY_auto;
}

/*
 * Heap: src/arena.c. The real one is what is left of the 32 kB of RAM above
 * the firmware's own data, 26.5 kB. Structures holding pointers are larger
 * here, so the emulated heap is larger by a similar proportion.
 */

static char heap[32*1024] aligned(8);
static char *heap_p;
#define heap_top (heap + sizeof(heap))

void *arena_alloc(uint32_t sz)
{
    void *p = heap_p;
    heap_p += (sz + 7) & ~7;
    ASSERT(heap_p <= heap_top);
    return p;
}

uint32_t arena_total(void)
{
    return sizeof(heap);
}

uint32_t arena_avail(void)
{
    return heap_top - heap_p;
}

void arena_init(void)
{
    heap_p = heap;
}

unsigned int emu_arena_used(void)
{
    return heap_p ? heap_p - heap : 0;
}

unsigned int emu_arena_size(void)
{
    return sizeof(heap);
}

/*
 * USB mass storage: src/usb/. The drive is a FAT volume held in memory by
 * the host half.
 */

static DSTATUS dstatus = STA_NOINIT;

void usbh_msc_init(void)
{
}

void usbh_msc_buffer_set(uint8_t *buf)
{
}

void usbh_msc_process(void)
{
    /* Called from the loop that waits for a drive: no need to spin. */
    emu_idle_ns(1000000);
}

bool_t usbh_msc_inserted(void)
{
    return emu_usb_inserted();
}

static bool_t usbh_msc_connected(void)
{
    return emu_usb_inserted();
}

static bool_t usbh_msc_readonly(void)
{
    return FALSE;
}

static DSTATUS usb_disk_initialize(BYTE pdrv)
{
    if (pdrv)
        return RES_PARERR;
    dstatus = usbh_msc_connected() ? 0 : STA_NOINIT;
    return dstatus;
}

static DSTATUS usb_disk_status(BYTE pdrv)
{
    return pdrv ? STA_NOINIT : dstatus;
}

static DRESULT usb_disk_io(int rc)
{
    if (rc != 0) {
        /* Disallow further disk operations. */
        dstatus |= STA_NOINIT;
        return RES_ERROR;
    }
    return RES_OK;
}

static DRESULT usb_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !count)
        return RES_PARERR;
    if (dstatus & STA_NOINIT)
        return RES_NOTRDY;
    emu_fw_progress();
    return usb_disk_io(emu_usb_read(buff, sector, count));
}

static DRESULT usb_disk_write(
    BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !count)
        return RES_PARERR;
    if (dstatus & STA_NOINIT)
        return RES_NOTRDY;
    emu_fw_progress();
    return usb_disk_io(emu_usb_write(buff, sector, count));
}

static DRESULT usb_disk_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    if (pdrv)
        return RES_PARERR;
    if (dstatus & STA_NOINIT)
        return RES_NOTRDY;
    return (ctrl == CTRL_SYNC) ? RES_OK : RES_PARERR;
}

struct volume_ops usb_ops = {
    .initialize = usb_disk_initialize,
    .status = usb_disk_status,
    .read = usb_disk_read,
    .write = usb_disk_write,
    .ioctl = usb_disk_ioctl,
    .connected = usbh_msc_connected,
    .readonly = usbh_msc_readonly
};

/*
 * Floppy interface: the firmware's own src/floppy.c, with what it includes,
 * runs against the models of hw.c. ffemu renames three of its functions when
 * building it, and wraps them here.
 */

bool_t fw_floppy_handle(void);
void fw_floppy_insert(unsigned int unit, struct slot *slot);
void fw_floppy_cancel(void);

/* The image mounted, for the Status window; "" if none. Its name alone,
 * its extension and its size, for the IMG.CFG window. */
static char image_name[64], image_base[64], image_ext[8];
static unsigned int image_size;

void floppy_insert(unsigned int unit, struct slot *slot)
{
    fw_floppy_insert(unit, slot);
    snprintf(image_name, sizeof(image_name), "%s.%s", slot->name,
             slot->type);
    snprintf(image_base, sizeof(image_base), "%s", slot->name);
    snprintf(image_ext, sizeof(image_ext), "%s", slot->type);
    image_size = slot->size;
}

void floppy_cancel(void)
{
    image_name[0] = '\0';
    fw_floppy_cancel();
}

/* Called over and over while an image is mounted, which the device does at
 * full speed: the host takes a rest each time round. */
bool_t floppy_handle(void)
{
    bool_t rc = fw_floppy_handle();
    emu_idle_ns(1000000);
    return rc;
}

void emu_fdd_status(unsigned int *cyl, unsigned int *side, int *sel,
                    const char **name)
{
    struct track_info ti;

    /* From another thread, which may see a mount half done. */
    floppy_get_track(&ti);
    *cyl = ti.cyl;
    *side = ti.side;
    *sel = ti.sel;
    *name = image_name[0] ? image_name : NULL;
}

int emu_fdd_image(const char **name, const char **type, unsigned int *size)
{
    if (image_name[0] == '\0')
        return FALSE;
    *name = image_base;
    *type = image_ext;
    *size = image_size;
    return TRUE;
}

const char *emu_fw_version(void)
{
    return fw_ver;
}

const char *emu_fw_target(void)
{
#if TARGET == TARGET_apple2
    return "Apple2";
#else
    return "Shugart";
#endif
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
