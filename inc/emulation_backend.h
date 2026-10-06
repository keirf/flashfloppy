/* Private names for the two engines in a combined firmware. Included before
 * declarations so that prototypes and definitions use the same names. */
#if defined(DUAL_FDD) || defined(DUAL_QD)
#undef TARGET
#ifdef DUAL_FDD
#define TARGET TARGET_shugart
#define backend_name(n) fdd_##n
#else
#define TARGET TARGET_quickdisk
#define backend_name(n) qd_##n
#endif

#define floppy_init backend_name(floppy_init)
#define floppy_ribbon_is_reversed backend_name(floppy_ribbon_is_reversed)
#define floppy_insert backend_name(floppy_insert)
#define floppy_cancel backend_name(floppy_cancel)
#define floppy_handle backend_name(floppy_handle)
#define floppy_set_cyl backend_name(floppy_set_cyl)
#define floppy_get_track backend_name(floppy_get_track)
#define floppy_set_fintf_mode backend_name(floppy_set_fintf_mode)
#define floppy_set_max_cyl backend_name(floppy_set_max_cyl)
#define motor_chgrst_exti_mask backend_name(motor_chgrst_exti_mask)
#define motor_chgrst_setup_exti backend_name(motor_chgrst_setup_exti)

/* Hardware aliases remain direct ISR entry points, with no runtime dispatch. */
#define IRQ_6 backend_name(IRQ_6)
#define IRQ_7 backend_name(IRQ_7)
#define IRQ_10 backend_name(IRQ_10)
#define IRQ_12 backend_name(IRQ_12)
#define IRQ_13 backend_name(IRQ_13)
#define IRQ_23 backend_name(IRQ_23)
#define IRQ_28 backend_name(IRQ_28)
#define IRQ_40 backend_name(IRQ_40)
#define IRQ_43 backend_name(IRQ_43)
#define IRQ_57 backend_name(IRQ_57)
#define IRQ_58 backend_name(IRQ_58)
#define IRQ_85 backend_name(IRQ_85)
#endif
