/* Included after either engine; not a standalone translation unit. */

#if defined(DUAL_FDD) || defined(DUAL_QD)
static void backend_bind_irqs(uint32_t *vectors)
{
    vectors[16 + 6] = (uint32_t)IRQ_6;
    vectors[16 + 7] = (uint32_t)IRQ_7;
    vectors[16 + 23] = (uint32_t)IRQ_23;
    vectors[16 + 28] = (uint32_t)IRQ_28;
    vectors[16 + 40] = (uint32_t)IRQ_40;
#if MCU == MCU_stm32f105
    vectors[16 + DMA1_CH2_IRQ] = (uint32_t)IRQ_12;
    vectors[16 + DMA1_CH3_IRQ] = (uint32_t)IRQ_13;
#else
    vectors[16 + DMA1_CH2_IRQ] = (uint32_t)IRQ_57;
    vectors[16 + DMA1_CH3_IRQ] = (uint32_t)IRQ_58;
#endif
#ifdef DUAL_FDD
    vectors[16 + 10] = (uint32_t)IRQ_10;
#if MCU == MCU_stm32f105
    vectors[16 + SOFTIRQ_0] = (uint32_t)IRQ_43;
#else
    vectors[16 + SOFTIRQ_0] = (uint32_t)IRQ_85;
#endif
#endif
}

const struct emulation_ops backend_name(emulation_ops) = {
    .init = floppy_init,
    .ribbon_is_reversed = floppy_ribbon_is_reversed,
    .insert = floppy_insert,
    .cancel = floppy_cancel,
    .handle = floppy_handle,
#ifdef DUAL_FDD
    .set_cyl = floppy_set_cyl,
#endif
    .get_track = floppy_get_track,
    .set_fintf_mode = floppy_set_fintf_mode,
    .set_max_cyl = floppy_set_max_cyl,
    .setup_exti = motor_chgrst_setup_exti,
    .exti_mask = &motor_chgrst_exti_mask,
    .bind_irqs = backend_bind_irqs
};
#endif
