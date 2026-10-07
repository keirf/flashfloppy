/*
 * hw.c
 *
 * Models of the MCU peripherals that the firmware's user interface depends
 * on. The firmware reads and writes register blocks in ordinary memory (see
 * regs.h); emu_hw_sync() looks at what it wrote and makes the registers
 * respond the way the hardware would.
 *
 * Modelled: GPIO inputs for the buttons and the rotary encoder, with their
 * EXTI interrupts; the one-shot timer behind timer.c; the I2C master with
 * its DMA channel, which carries the display traffic.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct stk emu_stk;
struct scb emu_scb;
struct nvic emu_nvic;
struct dbg emu_dbg;
struct flash emu_flash;
struct pwr emu_pwr;
struct bkp emu_bkp;
struct rcc emu_rcc;
struct gpio emu_gpio[7];
struct afio emu_afio;
struct exti emu_exti;
struct dma emu_dma[2];
struct tim emu_tim[7];
struct spi emu_spi[3];
struct i2c emu_i2c[2];
struct usart emu_usart[3];
struct usb_otg emu_usb_otg;

/* Interrupt handlers that the firmware half defines. */
void IRQ_14(void);
void IRQ_15(void);
void IRQ_16(void);
void IRQ_17(void);
void IRQ_23(void);
void IRQ_30(void);
void IRQ_31(void);
void IRQ_32(void);
void IRQ_33(void);
void IRQ_34(void);
void IRQ_40(void);

void emu_irq_vector(unsigned int nr)
{
    switch (nr) {
    case 14: IRQ_14(); break; /* DMA1 channel 4: I2C2 transmit */
    case 15: IRQ_15(); break; /* DMA1 channel 5: I2C2 receive */
    case 16: IRQ_16(); break; /* DMA1 channel 6: I2C1 transmit */
    case 17: IRQ_17(); break; /* DMA1 channel 7: I2C1 receive */
    case 23: IRQ_23(); break; /* EXTI9_5 */
    case 30: IRQ_30(); break; /* TIM4 */
    case 31: IRQ_31(); break; /* I2C1 event */
    case 32: IRQ_32(); break; /* I2C1 error */
    case 33: IRQ_33(); break; /* I2C2 event */
    case 34: IRQ_34(); break; /* I2C2 error */
    case 40: IRQ_40(); break; /* EXTI15_10 */
    default:
        printk("ffemu: no handler for IRQ %u\n", nr);
        break;
    }
}

static volatile int busy;

int emu_hw_busy(void)
{
    return busy;
}

uint32_t emu_stk_now(void)
{
    uint64_t ticks;
    emu_hw_sync();
    emu_irq_poll();
    /* SysTick counts down. */
    ticks = emu_time_ns() * STK_MHZ / 1000;
    return (uint32_t)(0 - ticks) & STK_MASK;
}

/*
 * GPIO: every input reads high (pulled up, as on the board) except those
 * that a pressed button or the encoder pulls low.
 */

#define PIN_SPEAKER 2  /* PA2 */
#define PIN_LEFT    3  /* PA3 */
#define PIN_RIGHT   4  /* PA4 */
#define PIN_SELECT  5  /* PA5 */
#define PIN_ROT_A   6  /* PA6: KC30 header, rotary bit 0 */
#define PIN_ROT_B  15  /* PA15: KC30 header, rotary bit 1 */

/* The encoder rests at 3 and outputs a Gray code. One detent is four
 * transitions: clockwise 3-2-0-1-3, anticlockwise 3-1-0-2-3. */
static const uint8_t rot_cw[] = { 2, 0, 1, 3 }, rot_ccw[] = { 1, 0, 2, 3 };
static uint8_t rot_state = 3;
static const uint8_t *rot_seq;
static unsigned int rot_pos;
static uint64_t rot_next_ns;
#define ROT_STEP_NS 1500000

static void gpio_sync(uint64_t now)
{
    unsigned int b = emu_in_buttons;
    uint32_t pa = 0xffff, changed, pr, bsrr;

    /* Outputs are driven through the set/reset register: a write that sets
     * the speaker pin is the start of a pulse. */
    bsrr = emu_gpio[0].bsrr;
    if (bsrr != 0) {
        emu_gpio[0].bsrr = 0;
        if (bsrr & m(PIN_SPEAKER))
            __sync_fetch_and_add(&emu_out_speaker, 1);
    }

    if ((rot_seq == NULL) && (emu_in_rotary != 0)) {
        bool_t cw = emu_in_rotary > 0;
        __sync_fetch_and_add(&emu_in_rotary, cw ? -1 : 1);
        rot_seq = cw ? rot_cw : rot_ccw;
        rot_pos = 0;
        rot_next_ns = now;
    }
    if ((rot_seq != NULL) && (now >= rot_next_ns)) {
        rot_state = rot_seq[rot_pos++];
        rot_next_ns = now + ROT_STEP_NS;
        if (rot_pos == sizeof(rot_cw))
            rot_seq = NULL;
    }

    if (b & EMU_B_LEFT)
        pa &= ~m(PIN_LEFT);
    if (b & EMU_B_RIGHT)
        pa &= ~m(PIN_RIGHT);
    if (b & EMU_B_SELECT)
        pa &= ~m(PIN_SELECT);
    if (!(rot_state & 1))
        pa &= ~m(PIN_ROT_A);
    if (!(rot_state & 2))
        pa &= ~m(PIN_ROT_B);

    changed = emu_gpio[0].idr ^ pa;
    emu_gpio[0].idr = pa;

    /* EXTI: both edges of the unmasked port-A lines raise an interrupt. */
    pr = changed & emu_exti.imr
        & ((pa & emu_exti.rtsr) | (~pa & emu_exti.ftsr));
    if (pr) {
        __sync_fetch_and_or(&emu_exti.pr, pr);
        if (pr & 0x03e0)
            emu_irqx_set_pending(23);
        if (pr & 0xfc00)
            emu_irqx_set_pending(40);
    }
}

/*
 * TIM4: timer.c programs it as a one-shot down to the next deadline.
 */

static bool_t tim_armed;
static uint64_t tim_deadline_ns;

static void tim_sync(uint64_t now)
{
    struct tim *t = &emu_tim[3];

    /* Writing EGR.UG marks every reprogramming. */
    if (t->egr & TIM_EGR_UG) {
        t->egr = 0;
        tim_armed = FALSE;
    }

    if (!(t->cr1 & TIM_CR1_CEN)) {
        tim_armed = FALSE;
        return;
    }

    if (!tim_armed) {
        uint64_t cycles = (uint64_t)(t->psc + 1) * (t->arr + 1);
        tim_deadline_ns = now + cycles * 1000 / SYSCLK_MHZ;
        tim_armed = TRUE;
    }

    if (now >= tim_deadline_ns) {
        tim_armed = FALSE;
        t->cr1 &= ~TIM_CR1_CEN; /* one-pulse mode */
        t->sr |= TIM_SR_UIF;
        if (t->dier & TIM_DIER_UIE)
            emu_irqx_set_pending(30);
    }
}

/*
 * I2C master, STM32F1 style, and the DMA channel that feeds it.
 *
 * The firmware's writes to the data register cannot be trapped, so the model
 * keeps a marker bit set in DR, above the data byte. A write by the firmware
 * clears the marker and is thereby noticed.
 */

#define DR_IDLE 0x100u
#define I2C_BYTE_NS 22500 /* 9 bit times at 400 kHz */

static struct i2c_model {
    struct i2c *r;
    struct dma_chn *tx;
    uint8_t tx_ch, irq_ev, irq_er, irq_dma_tx;
    enum { I2C_idle, I2C_addr, I2C_tx, I2C_rx } state;
    bool_t dma_active;
    uint64_t dma_done_ns;
} i2c_model[2] = {
    { .r = &emu_i2c[0], .tx = &emu_dma[0].ch[6-1], .tx_ch = 6,
      .irq_ev = 31, .irq_er = 32, .irq_dma_tx = 16 },
    { .r = &emu_i2c[1], .tx = &emu_dma[0].ch[4-1], .tx_ch = 4,
      .irq_ev = 33, .irq_er = 34, .irq_dma_tx = 14 }
};

/* DMA address registers hold only 32 bits of a firmware pointer. The rest is
 * the same as for any other object in the program image. */
static void *ptr32(uint32_t lo)
{
    uintptr_t ref = (uintptr_t)&emu_dma;
    uintptr_t p = (ref & ~(uintptr_t)0xffffffffu) | lo;
    if (sizeof(p) > 4) {
        const uintptr_t half = (uintptr_t)1 << 31;
        if ((p > ref) && ((p - ref) > half))
            p -= half << 1;
        else if ((p < ref) && ((ref - p) > half))
            p += half << 1;
    }
    return (void *)p;
}

static void i2c_sync(struct i2c_model *m, uint64_t now)
{
    struct i2c *r = m->r;
    bool_t wrote;

    if (!(r->cr1 & I2C_CR1_PE) || (r->cr1 & I2C_CR1_SWRST)) {
        if (m->state != I2C_idle)
            emu_i2c_dev_stop();
        m->state = I2C_idle;
        m->dma_active = FALSE;
        r->sr1 = r->sr2 = 0;
        r->dr = DR_IDLE;
        return;
    }

    if (r->cr1 & I2C_CR1_STOP) {
        emu_i2c_dev_stop();
        __sync_fetch_and_and(&r->cr1, ~I2C_CR1_STOP);
        r->sr1 &= I2C_SR1_ERRORS;
        r->dr = DR_IDLE;
        m->state = I2C_idle;
        m->dma_active = FALSE;
    }

    /* START, or a repeated START in the middle of a transfer. */
    if ((r->cr1 & I2C_CR1_START) && (m->state != I2C_addr)) {
        if (m->state == I2C_rx) {
            /* The byte in flight completes first, and is NACKed. */
            r->dr = DR_IDLE | emu_i2c_dev_read();
            r->sr1 |= I2C_SR1_RXNE;
        } else {
            r->dr = DR_IDLE;
        }
        __sync_fetch_and_and(&r->cr1, ~I2C_CR1_START);
        r->sr1 = (r->sr1 & ~(I2C_SR1_ADDR | I2C_SR1_BTF | I2C_SR1_TXE))
            | I2C_SR1_SB;
        m->state = I2C_addr;
        m->dma_active = FALSE;
    }

    wrote = !(r->dr & DR_IDLE);

    switch (m->state) {

    case I2C_idle:
        break;

    case I2C_addr:
        if (wrote) {
            uint8_t a = r->dr;
            bool_t rd = a & 1;
            r->dr = DR_IDLE;
            r->sr1 &= ~(I2C_SR1_SB | I2C_SR1_RXNE);
            if (!emu_i2c_dev_start(a >> 1, rd)) {
                r->sr1 |= I2C_SR1_AF;
                m->state = I2C_idle;
            } else if (rd) {
                r->dr = DR_IDLE | emu_i2c_dev_read();
                r->sr1 |= I2C_SR1_ADDR | I2C_SR1_RXNE;
                m->state = I2C_rx;
            } else {
                r->sr1 |= I2C_SR1_ADDR | I2C_SR1_TXE;
                m->state = I2C_tx;
            }
        }
        break;

    case I2C_tx:
        if (m->dma_active) {
            if (now >= m->dma_done_ns) {
                uint8_t *p = ptr32(m->tx->cmar);
                unsigned int i, nr = m->tx->cndtr;
                for (i = 0; i < nr; i++)
                    emu_i2c_dev_write(p[i]);
                m->tx->cndtr = 0;
                m->dma_active = FALSE;
                emu_dma[0].isr |= DMA_ISR_TCIF(m->tx_ch)
                    | DMA_ISR_GIF(m->tx_ch);
                r->sr1 |= I2C_SR1_BTF | I2C_SR1_TXE;
                if (m->tx->ccr & DMA_CCR_TCIE)
                    emu_irqx_set_pending(m->irq_dma_tx);
            }
        } else if ((r->cr2 & I2C_CR2_DMAEN) && (m->tx->ccr & DMA_CCR_EN)
                   && (m->tx->cndtr != 0)) {
            r->sr1 &= ~(I2C_SR1_ADDR | I2C_SR1_BTF | I2C_SR1_TXE);
            m->dma_active = TRUE;
            m->dma_done_ns = now + (uint64_t)m->tx->cndtr * I2C_BYTE_NS;
        } else if (wrote) {
            emu_i2c_dev_write(r->dr);
            r->dr = DR_IDLE;
            r->sr1 = (r->sr1 & ~I2C_SR1_ADDR) | I2C_SR1_BTF | I2C_SR1_TXE;
        }
        break;

    case I2C_rx:
        break;

    }

    /* Interrupt requests are levels, held for as long as their cause is. */
    if ((r->cr2 & I2C_CR2_ITEVTEN)
        && (r->sr1 & (I2C_SR1_SB | I2C_SR1_ADDR | I2C_SR1_BTF)))
        emu_irqx_set_pending(m->irq_ev);
    if ((r->cr2 & I2C_CR2_ITERREN) && (r->sr1 & I2C_SR1_ERRORS))
        emu_irqx_set_pending(m->irq_er);
}

static void dma_sync(void)
{
    /* Writing 1s to IFCR clears the matching ISR flags. */
    uint32_t ifcr = emu_dma[0].ifcr;
    if (ifcr) {
        emu_dma[0].isr &= ~ifcr;
        emu_dma[0].ifcr = 0;
    }
}

void emu_hw_sync(void)
{
    uint64_t now;

    /* A tick may arrive while the firmware thread is in here. */
    if (__sync_lock_test_and_set(&busy, 1))
        return;

    now = emu_time_ns();
    gpio_sync(now);
    tim_sync(now);
    dma_sync();
    i2c_sync(&i2c_model[0], now);
    i2c_sync(&i2c_model[1], now);

    __sync_lock_release(&busy);
}

void emu_hw_init(void)
{
    unsigned int i;

    /* AT32F415 in QFN32: the SFRKC30.AT2 board. */
    emu_scb.cpuid = 0x410fc241;     /* Cortex-M4 */
    emu_dbg.mcu_idcode = 0x700301c6;

    for (i = 0; i < ARRAY_SIZE(emu_gpio); i++)
        emu_gpio[i].idr = 0xffff;
    for (i = 0; i < ARRAY_SIZE(emu_i2c); i++)
        emu_i2c[i].dr = DR_IDLE;
}

const char *emu_board_name(void)
{
    return "SFRKC30.AT2 (AT32F415, QFN32)";
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
