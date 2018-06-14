
/*
 * Circular DMA Restart Behaviour:
 * 
 * DMA channel can be disabled/enabled and buffer-ring will continue where
 * it left off *unless* CNDTR is re-initialised. In that case buffer-ring
 * cursor will reset to base address. In no case does CPAR/CMAR need to be
 * re-initialised.
 * 
 * HC and TC interrupts always occur at correct place within ring buffer
 * regardless of restarts.
 */

static void check(uint8_t *p, int s, int e)
{
    int i;
    uint8_t hc = (dma1->isr >> 10) & 1;
    uint8_t tc = (dma1->isr >>  9) & 1;
    printk("%u-%u(%u): [", s, e, 8-dma1->ch3.cndtr);
    for (i = 0; i < 7; i++)
        printk(" %c,", p[i] ? p[i]+'0'-1 : '-');
    printk(" %c ] %c%c\n", p[i] ? p[i]+'0'-1 : '-', hc ? 'H' : ' ',
           tc ? 'T' : ' ');
    memset(p, 0, 8);
    dma1->ifcr = 0xf << 8;
}

static void dma_test(void)
{
    uint8_t src[8], dst[8];
    int i;
    uint32_t ccr = (DMA_CCR_PL_HIGH |
                    DMA_CCR_MSIZE_8BIT |
                    DMA_CCR_PSIZE_8BIT |
                    DMA_CCR_MINC |
                    DMA_CCR_PINC |
                    DMA_CCR_CIRC |
                    DMA_CCR_DIR_P2M |
                    DMA_CCR_HTIE |
                    DMA_CCR_TCIE |
                    DMA_CCR_EN);

    for (i = 0; i < ARRAY_SIZE(src); i++)
        src[i] = i+1;
    memset(dst, 0, sizeof(dst));

    dma1->ifcr = 0xf << 8;
    dma1->ch3.cpar = (uint32_t)(unsigned long)src;
    dma1->ch3.cmar = (uint32_t)(unsigned long)dst;
    dma1->ch3.cndtr = ARRAY_SIZE(src);
    dma1->ch3.ccr = ccr;

    /* 1ms */
    tim3->psc = SYSCLK_MHZ - 1;
    tim3->arr = 1000;
    tim3->cr2 = 0;
    tim3->dier = TIM_DIER_UDE;

    printk("Timer On then Off:\n");
    tim3->cr1 = TIM_CR1_CEN;
    while (dst[3] == 0)
        cpu_relax();
    tim3->cr1 = 0;
    check(dst, 0, 3);

    printk("Timer Disable/Enable\n");
    tim3->cr1 = TIM_CR1_CEN;
    while (dst[5] == 0)
        cpu_relax();
    tim3->cr1 = 0;
    dma1->ch3.ccr = 0;
    check(dst, 4, 5);

    printk("... + DMA:\n");
    dma1->ch3.ccr = ccr;
    tim3->cr1 = TIM_CR1_CEN;
    while (dst[1] == 0)
        cpu_relax();
    tim3->cr1 = 0;
    dma1->ch3.ccr = 0;
    check(dst, 6, 1);

    printk("... + CNDTR Reset:\n");
    dma1->ch3.cndtr = ARRAY_SIZE(src);
    dma1->ch3.ccr = ccr;
    tim3->cr1 = TIM_CR1_CEN;
    while (dst[4] == 0)
        cpu_relax();
    tim3->cr1 = 0;
    dma1->ch3.ccr = 0;
    check(dst, 2, 4);

    printk("... + CPAR/CMAR Reset:\n");
    dma1->ch3.cpar = (uint32_t)(unsigned long)src;
    dma1->ch3.cmar = (uint32_t)(unsigned long)dst;
    dma1->ch3.ccr = ccr;
    tim3->cr1 = TIM_CR1_CEN;
    while (dst[1] == 0)
        cpu_relax();
    tim3->cr1 = 0;
    dma1->ch3.ccr = 0;
    check(dst, 5, 1);

    printk("All done\n");
    for (;;);
}

