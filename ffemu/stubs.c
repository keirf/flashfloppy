/*
 * stubs.c
 *
 * Stand-ins for the parts of the firmware that are tied to the MCU or that
 * ffemu leaves out: MCU bring-up and delays, the serial console, the
 * configuration flash page, the heap, the USB host stack, and the floppy
 * interface itself. Each keeps the interface that the rest of the firmware
 * expects of the file it replaces.
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
    printk("Config: Written to Flash\n");
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
    printk("Config: ");
    if (found) {
        unsigned int sz = min_t(unsigned int, f.size, ff_cfg.size);
        printk("Flash (ver %u, size %u)\n", f.version, sz);
        /* Copy over all options that are present in Flash. */
        if (sz > offsetof(struct ff_cfg, interface))
            memcpy(&ff_cfg.interface, &f.interface,
                   sz - offsetof(struct ff_cfg, interface));
    } else {
        printk("Factory Defaults\n");
    }
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

static const struct {
    const char *name;
    uint16_t offset, size;
} option[OPT_nr] = {
#define x(n,o,v) { #n, offsetof(struct ff_cfg, o), sizeof(ff_cfg.o) },
#include "ff_cfg_defaults.h"
#undef x
};

/* The value names of the options that have them, as src/main.c parses them:
 * the index is the value. */
static const char * const yes_no[] = { "no", "yes" };
static const char * const interface_name[] = {
    [FINTF_SHUGART] = "shugart", [FINTF_IBMPC] = "ibmpc",
    [FINTF_IBMPC_HDOUT] = "ibmpc-hdout", [FINTF_JPPC_HDOUT] = "jppc-hdout",
    [FINTF_AMIGA] = "amiga", [FINTF_JPPC] = "jppc", [FINTF_JC] = "jc"
};
static const char * const host_name[] = {
    [HOST_unspecified] = "unspecified", [HOST_akai] = "akai",
    [HOST_gem] = "gem", [HOST_ensoniq] = "ensoniq", [HOST_acorn] = "acorn",
    [HOST_ti99] = "ti99", [HOST_memotech] = "memotech", [HOST_uknc] = "uknc",
    [HOST_pc98] = "pc98", [HOST_pc_dos] = "pc-dos", [HOST_msx] = "msx",
    [HOST_dec] = "dec", [HOST_tandy_coco] = "tandy-coco",
    [HOST_fluke] = "fluke", [HOST_nascom] = "nascom", [HOST_casio] = "casio",
    [HOST_ibm_3174] = "ibm-3174"
};
static const char * const pin_name[] = {
    [PIN_auto] = "auto", [PIN_high] = "high", [PIN_low] = "low",
    [PIN_rdy] = "rdy", [PIN_nrdy] = "nrdy", [PIN_dens] = "dens",
    [PIN_ndens] = "ndens", [PIN_chg] = "chg", [PIN_nchg] = "nchg"
};
static const char * const track_change_name[] = {
    [TRKCHG_instant] = "instant", [TRKCHG_realtime] = "realtime"
};
static const char * const write_drain_name[] = {
    [WDRAIN_instant] = "instant", [WDRAIN_realtime] = "realtime",
    [WDRAIN_eot] = "eot"
};
static const char * const image_on_startup_name[] = {
    [IMGS_last] = "last", [IMGS_static] = "static", [IMGS_init] = "init"
};
static const char * const folder_sort_name[] = {
    [SORT_never] = "never", [SORT_always] = "always", [SORT_small] = "small"
};
static const char * const sort_priority_name[] = {
    [SORTPRI_folders] = "folders", [SORTPRI_files] = "files",
    [SORTPRI_none] = "none"
};
static const char * const nav_mode_name[] = {
    [NAVMODE_default] = "default", [NAVMODE_indexed] = "indexed",
    [NAVMODE_native] = "native"
};
static const char * const twobutton_name[] = {
    [TWOBUTTON_zero] = "zero", [TWOBUTTON_eject] = "eject",
    [TWOBUTTON_rotary] = "rotary", [TWOBUTTON_rotary_fast] = "rotary-fast",
    [TWOBUTTON_htu] = "htu"
};
static const char * const rotary_name[] = {
    [ROT_none] = "none", [ROT_full] = "full", [ROT_quarter] = "quarter",
    [ROT_half] = "half", [ROT_trackball] = "trackball",
    [ROT_buttons] = "buttons"
};
static const char * const display_on_name[] = {
    [DISPON_no] = "no", [DISPON_yes] = "yes", [DISPON_sel] = "sel"
};
static const char * const font_name[] = {
    [FONT_6x13] = "6x13", [FONT_8x16] = "8x16"
};

/* The name of value @v in @names of @nr, or NULL if it has none. */
static const char *named(unsigned int v, const char * const *names,
                         unsigned int nr)
{
    return (v < nr) ? names[v] : NULL;
}
#define NAMED(v, names) named(v, names, ARRAY_SIZE(names))

/* The value that @s names in @names of @nr, or -1. */
static int lookup(const char *s, const char * const *names, unsigned int nr)
{
    unsigned int i;

    for (i = 0; i < nr; i++)
        if ((names[i] != NULL) && !strcmp(s, names[i]))
            return i;
    return -1;
}
#define LOOKUP(s, names) lookup(s, names, ARRAY_SIZE(names))

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
    if (sh < 16)
        *v |= 0x7777 << sh;
    return TRUE;
}

/* A list of values separated by commas: one of @names, which replaces the
 * part @mask of the value, or flags. */
static bool_t parse_list(char *s, uint32_t *v, uint32_t mask,
                         const char * const *names, unsigned int nr,
                         const char *flag1, uint32_t bit1,
                         const char *flag2, uint32_t bit2)
{
    char *next;
    int n;

    for (; s != NULL; s = next) {
        next = next_token(s, ',');
        if (flag1 && !strcmp(s, flag1))
            *v |= bit1;
        else if (flag2 && !strcmp(s, flag2))
            *v |= bit2;
        else if ((n = lookup(s, names, nr)) >= 0)
            *v = (*v & ~mask) | n;
        else
            return FALSE;
    }
    return TRUE;
}

static bool_t parse_notify_volume(char *s, uint32_t *v)
{
    uint32_t n;
    char *next;

    *v = 0;
    for (; s != NULL; s = next) {
        next = next_token(s, ',');
        if (!strcmp(s, "slotnr"))
            *v |= NOTIFY_slotnr;
        else if (parse_number(s, &n) && (n <= NOTIFY_volume_mask))
            *v = (*v & ~NOTIFY_volume_mask) | n;
        else
            return FALSE;
    }
    return TRUE;
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
    int k = -1;

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

    switch (i) {
    case OPT_interface:
        k = !strcmp(s, "akai-s950") ? FINTF_JPPC_HDOUT
            : LOOKUP(s, interface_name);
        break;
    case OPT_host: k = LOOKUP(s, host_name); break;
    case OPT_pin02: case OPT_pin34:
        k = !strcmp(s, "nc") ? PIN_nc : LOOKUP(s, pin_name);
        break;
    case OPT_write_protect: case OPT_index_suppression:
    case OPT_ejected_on_startup: case OPT_nav_loop: case OPT_extend_image:
        k = LOOKUP(s, yes_no);
        break;
    case OPT_track_change: k = LOOKUP(s, track_change_name); break;
    case OPT_write_drain: k = LOOKUP(s, write_drain_name); break;
    case OPT_image_on_startup: k = LOOKUP(s, image_on_startup_name); break;
    case OPT_folder_sort: k = LOOKUP(s, folder_sort_name); break;
    case OPT_sort_priority: k = LOOKUP(s, sort_priority_name); break;
    case OPT_nav_mode: k = LOOKUP(s, nav_mode_name); break;
    case OPT_display_on_activity: k = LOOKUP(s, display_on_name); break;
    case OPT_oled_font: k = LOOKUP(s, font_name); break;
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
    case OPT_twobutton_action:
        v = TWOBUTTON_zero;
        if (!parse_list(s, &v, TWOBUTTON_mask, twobutton_name,
                        ARRAY_SIZE(twobutton_name), "reverse",
                        TWOBUTTON_reverse, NULL, 0))
            return FALSE;
        goto store;
    case OPT_rotary:
        v = ROT_full;
        if (!parse_list(s, &v, ROT_typemask, rotary_name,
                        ARRAY_SIZE(rotary_name), "reverse", ROT_reverse,
                        "v2", ROT_v2))
            return FALSE;
        goto store;
    case OPT_notify_volume:
        if (!parse_notify_volume(s, &v))
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

    /* A named value. */
    if (k < 0)
        return FALSE;
    v = k;

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

    switch (i) {
    case OPT_interface: name = NAMED(v, interface_name); break;
    case OPT_host: name = NAMED(v, host_name); break;
    case OPT_pin02: case OPT_pin34: name = NAMED(v, pin_name); break;
    case OPT_write_protect: case OPT_index_suppression:
    case OPT_ejected_on_startup: case OPT_nav_loop: case OPT_extend_image:
        name = NAMED(v, yes_no);
        break;
    case OPT_track_change: name = NAMED(v, track_change_name); break;
    case OPT_write_drain: name = NAMED(v, write_drain_name); break;
    case OPT_image_on_startup: name = NAMED(v, image_on_startup_name); break;
    case OPT_folder_sort: name = NAMED(v, folder_sort_name); break;
    case OPT_sort_priority: name = NAMED(v, sort_priority_name); break;
    case OPT_nav_mode: name = NAMED(v, nav_mode_name); break;
    case OPT_display_on_activity: name = NAMED(v, display_on_name); break;
    case OPT_oled_font: name = NAMED(v, font_name); break;
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
    case OPT_twobutton_action:
        snprintf(buf, size, "%s%s",
                 NAMED(v & TWOBUTTON_mask, twobutton_name) ?: "?",
                 (v & TWOBUTTON_reverse) ? ",reverse" : "");
        break;
    case OPT_rotary:
        snprintf(buf, size, "%s%s%s",
                 NAMED(v & ROT_typemask, rotary_name) ?: "?",
                 (v & ROT_v2) ? ",v2" : "",
                 (v & ROT_reverse) ? ",reverse" : "");
        break;
    case OPT_notify_volume:
        snprintf(buf, size, "%u%s", (unsigned int)v & NOTIFY_volume_mask,
                 (v & NOTIFY_slotnr) ? ",slotnr" : "");
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
    return usb_disk_io(emu_usb_read(buff, sector, count));
}

static DRESULT usb_disk_write(
    BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !count)
        return RES_PARERR;
    if (dstatus & STA_NOINIT)
        return RES_NOTRDY;
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
 * Floppy interface: src/floppy.c and what it includes. No host computer is
 * attached. An image is opened when selected, so that a bad image is
 * reported as on the real device, but no data ever flows.
 */

static struct image *image;
static uint8_t cur_cyl;

uint32_t motor_chgrst_exti_mask;

void motor_chgrst_setup_exti(void)
{
}

/* EXTI lines of the rotary encoder: PA6 and PA15 on the KC30 header. */
static void IRQ_exti_rotary(void)
{
    uint32_t pr = exti->pr;
    __sync_fetch_and_and(&emu_exti.pr, ~pr);
    if (pr & board_rotary_exti_mask)
        IRQ_rotary();
}
void IRQ_23(void) __attribute__((alias("IRQ_exti_rotary"))); /* EXTI9_5 */
void IRQ_40(void) __attribute__((alias("IRQ_exti_rotary"))); /* EXTI15_10 */

void floppy_init(void)
{
    exti->rtsr = 0xffff;
    exti->ftsr = 0xffff;
    IRQx_set_prio(23, FLOPPY_IRQ_WGATE_PRI);
    IRQx_set_prio(40, TIMER_IRQ_PRI);
    IRQx_enable(23);
    IRQx_enable(40);
}

bool_t floppy_ribbon_is_reversed(void)
{
    return FALSE;
}

void floppy_set_fintf_mode(void)
{
}

void floppy_set_max_cyl(void)
{
    if (cur_cyl > ff_cfg.max_cyl)
        cur_cyl = ff_cfg.max_cyl;
}

/* As floppy_mount(), less the cluster table and the flux buffers. */
void floppy_insert(unsigned int unit, struct slot *slot)
{
    struct image *im;
    FSIZE_t sz;

    do {

        arena_init();

        im = arena_alloc(sizeof(*im));
        memset(im, 0, sizeof(*im));

        fatfs_from_slot(&im->fp, slot, FA_READ);
        sz = f_size(&im->fp);

        im->write_bc_window = ~0;

        im->bufs.write_bc.len = 8*1024;
        im->bufs.write_bc.p = arena_alloc(im->bufs.write_bc.len);
        im->bufs.read_bc.len = im->bufs.write_bc.len / 2;
        im->bufs.read_bc.p = (char *)im->bufs.write_bc.p
            + im->bufs.read_bc.len;
        im->bufs.write_data.len = arena_avail();
        im->bufs.write_data.p = arena_alloc(im->bufs.write_data.len);
        im->bufs.read_data = im->bufs.write_data;

        /* Mount the image file. */
        image_open(im, slot, NULL);
        if (!im->disk_handler->write_track || volume_readonly())
            slot->attributes |= AM_RDO;
        if (slot->attributes & AM_RDO) {
            printk("Image is R/O\n");
        } else {
            image_extend(im);
        }

    } while (f_size(&im->fp) != sz);

    im->fp.dir_ptr = NULL;
    im->fp.dir_sect = 0;

    image = im;
}

void floppy_cancel(void)
{
    image = NULL;
}

bool_t floppy_handle(void)
{
    /* Called from the loop that runs while an image is mounted. */
    emu_idle_ns(1000000);
    return FALSE;
}

void floppy_set_cyl(uint8_t unit, uint8_t cyl)
{
    if (unit == 0)
        cur_cyl = cyl;
}

void floppy_get_track(struct track_info *ti)
{
    memset(ti, 0, sizeof(*ti));
    ti->cyl = cur_cyl;
    ti->in_da_mode = image ? in_da_mode(image, cur_cyl) : FALSE;
}

const char *emu_fw_version(void)
{
    return fw_ver;
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
