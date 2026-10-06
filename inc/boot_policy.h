/* Bootloader decision shared with the host regression test. */
enum firmware_boot_action {
    BOOT_FIRMWARE, BOOT_MENU, BOOT_UPDATE
};

static inline enum firmware_boot_action firmware_boot_action(
    uint32_t marker, unsigned int buttons, bool_t update_requested)
{
    if (update_requested
        || ((buttons & (B_LEFT|B_RIGHT)) == (B_LEFT|B_RIGHT)))
        return BOOT_UPDATE;
    if (buttons & B_SELECT)
        return (marker == DUAL_FW_MAGIC) ? BOOT_MENU : BOOT_UPDATE;
    return BOOT_FIRMWARE;
}
