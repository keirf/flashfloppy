/*
 * hxc.h
 * 
 * Structure definitions for HxC compatibility.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* HXCSDFE.CFG file header. */
struct __packed hxcsdfe_cfg {
    char     signature[16]; /* "HXCFECFGVx.y" */
    uint8_t  step_sound;
    uint8_t  ihm_sound;
    uint8_t  back_light_tmr;
    uint8_t  standby_tmr;
    uint8_t  disable_drive_select;
    uint8_t  buzzer_duty_cycle;
    uint8_t  number_of_slot;
    uint8_t  slot_index;
    uint16_t update_cnt;
    uint8_t  load_last_floppy;
    uint8_t  buzzer_step_duration;
    uint8_t  lcd_scroll_speed;
    uint8_t  startup_mode;
    uint8_t  enable_drive_b;
    uint8_t  index_mode;
    struct __packed {
        uint8_t  cfg_from_cfg;
        uint8_t  interfacemode;
        uint8_t  pin02_cfg;
        uint8_t  pin34_cfg;
    } drive[2];
    uint8_t  drive_b_as_motor_on;
    uint8_t  pad[23];
    uint32_t slots_map_position;
    uint32_t max_slot_number;
    uint32_t slots_position;
    uint32_t number_of_drive_per_slot;
    uint32_t cur_slot_number;
    uint32_t ihm_mode;
};

#define HXCSTARTUP_slot0   0x04
#define HXCSTARTUP_ejected 0x10

/* HXCFECFGV1.x slots start at offset 0x400: */
struct __packed v1_slot {
    char     name[12];
    uint8_t  attributes;
    uint32_t firstCluster;
    uint32_t size;
    char     longName[17];
};

/* HXCFECFGV2.x slots start at sector offset 'slots_position': */
struct __packed v2_slot {
    char     type[3];
    uint8_t  attributes;
    uint32_t firstCluster;
    uint32_t size;
    char     name[52];
};

#define short_slot v2_slot

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
