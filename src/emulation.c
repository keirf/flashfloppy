/* Common application API; only the boot-selected engine is initialised.
 * IRQ vectors point directly to that engine, preserving ISR latency. */

uint8_t emulation_mode;
static const struct emulation_ops *active;

#if MCU == MCU_at32f435
#define NR_VECTORS (16 + 90)
#else
#define NR_VECTORS (16 + 68)
#endif
static uint32_t emulation_vectors[NR_VECTORS] aligned(512);

uint32_t motor_chgrst_exti_mask;

void emulation_select(uint8_t mode)
{
    /* Called only at boot, before enabling any interface IRQ or DMA. */
    uint32_t oldpri = IRQ_save(TIMER_IRQ_PRI);
    emulation_mode = (mode == EMULATION_QD) ? EMULATION_QD : EMULATION_FDD;
    active = emulation_is_qd() ? &qd_emulation_ops : &fdd_emulation_ops;
    memcpy(emulation_vectors, vector_table, sizeof(emulation_vectors));
    active->bind_irqs(emulation_vectors);
    scb->vtor = (uint32_t)emulation_vectors;
    cpu_sync();
    IRQ_restore(oldpri);
}

void floppy_init(void)
{
    active->init();
    motor_chgrst_exti_mask = *active->exti_mask;
}

bool_t floppy_ribbon_is_reversed(void)
{
    return active->ribbon_is_reversed();
}

void floppy_insert(unsigned int unit, struct slot *slot)
{
    active->insert(unit, slot);
}

void floppy_cancel(void)
{
    active->cancel();
}

bool_t floppy_handle(void)
{
    return active->handle();
}

void floppy_set_cyl(uint8_t unit, uint8_t cyl)
{
    if (active->set_cyl)
        active->set_cyl(unit, cyl);
}

void floppy_get_track(struct track_info *ti)
{
    active->get_track(ti);
}

void floppy_set_fintf_mode(void)
{
    active->set_fintf_mode();
}

void floppy_set_max_cyl(void)
{
    active->set_max_cyl();
}

void motor_chgrst_setup_exti(void)
{
    active->setup_exti();
    motor_chgrst_exti_mask = *active->exti_mask;
}
