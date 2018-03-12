/*
 * touch/floppy.c
 * 
 * Touch-specific floppy-interface setup.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define O_FALSE 0
#define O_TRUE  1

/* NB. All input pins must be 5v tolerant. */
/* Bitmap of current states of input pins. */
static uint8_t input_pins;

/* Offsets within the input_pins bitmap. */
#define inp_dir     0
#define inp_step    3
#define inp_sel0    4
#define inp_sel1    5
#define inp_wgate   6
#define inp_side    7

/* Subset of output pins which are active (O_TRUE). */
static uint16_t gpio_out_active;

/* Outputs are buffered, thus do *not* need to be 5v tolerant. */
#define gpio_out gpiob
#define pin_02      3
static uint8_t pin_08; /* differs across board revisions */
#define pin_26      5
#define pin_28     11
#define pin_34     12

#define gpio_data gpiob

#define pin_wdata   6 /* must be 5v tolerant */
#define tim_wdata   (tim4)
#define dma_wdata   (dma1->ch1)
#define dma_wdata_ch 1
#define dma_wdata_irq 11
void IRQ_11(void) __attribute__((alias("IRQ_wdata_dma")));

#define pin_rdata   7
#define tim_rdata   (tim4)
#define dma_rdata   (dma1->ch7)
#define dma_rdata_ch 7
#define dma_rdata_irq 17
void IRQ_17(void) __attribute__((alias("IRQ_rdata_dma")));

/* Bind all EXTI IRQs */
void IRQ_6(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI0 */
void IRQ_7(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI1 */
void IRQ_8(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI2 */
void IRQ_9(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI3 */
void IRQ_10(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI4 */
void IRQ_23(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI9_5 */
void IRQ_40(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI15_10 */
static const struct exti_irq exti_irqs[] = {
    {  6, FLOPPY_IRQ_HI_PRI, 0 }, 
    {  7, FLOPPY_IRQ_HI_PRI, 0 }, 
    {  8, FLOPPY_IRQ_HI_PRI, 0 }, 
    {  9, FLOPPY_IRQ_HI_PRI, 0 }, 
    { 10, FLOPPY_IRQ_HI_PRI, 0 }, 
    { 23, FLOPPY_IRQ_HI_PRI, 0 }, 
    { 40, FLOPPY_IRQ_HI_PRI, 0 }
};

/* Updates the board-agnostic input_pins bitmask with current states of 
 * input pins, and returns mask of pins which have changed state. */
static uint8_t (*input_update)(void);

/* Default input pins:
 * DIR = PA8, STEP=PA11, SELA=PA12, SELB=PA13, WGATE=PA14, SIDE=PA15
 */
static uint8_t input_update_default(void)
{
    uint16_t pr;

    pr = exti->pr;
    exti->pr = pr;

    input_pins = (gpioa->idr >> 8) & 0xf9;

    return (pr >> 8) & 0xf8;
}

static void input_init_default(void)
{
    gpio_configure_pin(gpioa, 8+inp_sel0,  GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_sel1,  GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_dir,   GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_step,  GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_wgate, GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_side,  GPI_bus);

    /* PA[15:0] -> EXT[15:0] */
    afio->exticr1 = afio->exticr2 = afio->exticr3 = afio->exticr4 = 0x0000;

    exti->imr = exti->rtsr = exti->ftsr =
        m(8+inp_step) | m(8+inp_sel0) | m(8+inp_sel1)
        | m(8+inp_wgate) | m(8+inp_side);

    input_update = input_update_default;
}

/* TB160 input pins as default except:
 * SELB = PB8, WGATE = PB9.
 */
static uint8_t input_update_tb160(void)
{
    uint16_t pr;

    pr = exti->pr;
    exti->pr = pr;

    input_pins = ((gpioa->idr >> 8) & 0x99) | ((gpiob->idr >> 3) & 0x60);

    return ((pr >> 8) & 0x98) | ((pr >> 3) & 0x60);
}

static void input_init_tb160(void)
{
    gpio_configure_pin(gpioa, 8+inp_sel0,  GPI_bus);
    gpio_configure_pin(gpiob, 3+inp_sel1,  GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_dir,   GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_step,  GPI_bus);
    gpio_configure_pin(gpiob, 3+inp_wgate, GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_side,  GPI_bus);

    /* PA[15:10,7:0] -> EXT[15:10,7:0], PB[9:8] -> EXT[9:8]. */
    afio->exticr1 = afio->exticr2 = afio->exticr4 = 0x0000;
    afio->exticr3 = 0x0011;

    exti->imr = exti->rtsr = exti->ftsr =
        m(8+inp_step) | m(8+inp_sel0) | m(3+inp_sel1)
        | m(3+inp_wgate) | m(8+inp_side);

    input_update = input_update_tb160;
}

static void board_floppy_init(void)
{
    switch (board_id) {
    case BRDREV_LC150:
        pin_08 = 4;
        input_init_default();
        break;
    case BRDREV_MM150:
        pin_08 = 2;
        input_init_default();
        break;
    case BRDREV_TB160:
        pin_08 = 1;
        input_init_tb160();
        break;
    }
}

static void IRQ_input_changed(void)
{
    uint8_t inp, changed, sel;
    struct drive *drv = &drive;

    changed = input_update();
    inp = input_pins;
    sel = !(inp & m(inp_sel0));

    /* DSKCHG asserts on any falling edge of STEP. We deassert on any edge. */
    if ((changed & m(inp_step)) && sel && (dma_rd != NULL))
        drive_change_output(drv, outp_dskchg, FALSE);
    /* Handle step request. */
    if ((changed & inp & m(inp_step)) /* Rising edge on STEP */
        && sel                        /* Drive is selected */
        && !(drv->step.state & STEP_active)) { /* Not already mid-step */
        /* Latch the step direction and check bounds (0 <= cyl <= 255). */
        drv->step.inward = !(inp & m(inp_dir));
        if (drv->cyl != (drv->step.inward ? 255 : 0)) {
            /* Valid step request for this drive: start the step operation. */
            drv->step.start = time_now();
            drv->step.state = STEP_started;
            drive_change_output(drv, outp_trk0, FALSE);
            if (dma_rd != NULL)
                rdata_stop();
            IRQx_set_pending(FLOPPY_SOFTIRQ);
        }
    }

    /* Handle side change. */
    if (changed & m(inp_side)) {
        drv->head = !(inp & m(inp_side));
        if (dma_rd != NULL) {
            rdata_stop();
        }
    }

    /* Handle write gate. */
    if ((changed & m(inp_wgate)) && (dma_wr != NULL)
        && sel && drv->image->handler->write_track) {
        if (inp & m(inp_wgate)) {
            wdata_stop();
        } else {
            rdata_stop();
            wdata_start();
        }
    }

    drv->sel = sel;
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
