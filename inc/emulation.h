/* Boot-selected emulation. Selection is fixed before either engine starts. */
#define EMULATION_FDD 0
#define EMULATION_QD  1

#if TARGET == TARGET_dual
extern uint8_t emulation_mode;
static inline bool_t emulation_is_qd(void)
{
    return emulation_mode == EMULATION_QD;
}
void emulation_select(uint8_t mode);
#else
static inline bool_t emulation_is_qd(void)
{
    return TARGET == TARGET_quickdisk;
}
#endif

#if TARGET == TARGET_dual || defined(DUAL_FDD) || defined(DUAL_QD)
struct track_info;
struct emulation_ops {
    void (*init)(void);
    bool_t (*ribbon_is_reversed)(void);
    void (*insert)(unsigned int unit, struct slot *slot);
    void (*cancel)(void);
    bool_t (*handle)(void);
    void (*set_cyl)(uint8_t unit, uint8_t cyl);
    void (*get_track)(struct track_info *ti);
    void (*set_fintf_mode)(void);
    void (*set_max_cyl)(void);
    void (*setup_exti)(void);
    uint32_t *exti_mask;
    void (*bind_irqs)(uint32_t *vectors);
};
extern const struct emulation_ops fdd_emulation_ops, qd_emulation_ops;
#endif
