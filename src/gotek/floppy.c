/*
 * gotek/floppy.c
 * 
 * Gotek-specific floppy-interface setup.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define O_FALSE 1
#define O_TRUE  0

/* Offsets within the input_pins bitmap. */
#define inp_dir     0
#define inp_step    2
#define inp_sel0    1
#define inp_wgate   7
#define inp_side    4

/* Outputs. */
#define gpio_out gpiob
#define pin_dskchg  7
#define pin_index   8
#define pin_trk0    6
#define pin_wrprot  5
#define pin_rdy     3

#define gpio_timer gpioa
#define pin_wdata   8
#define pin_rdata   7
#define dma_rdata   (dma1->ch3)
#define tim_rdata   (tim3)

#define NR_DRIVES 1

/* Input pins:
 * DIR = PB0, STEP=PA1, SELA=PA0, WGATE=PB9, SIDE=PB4
 */
static uint8_t input_update(void)
{
    uint16_t pr, in_a, in_b;

    pr = exti->pr;
    exti->pr = pr;

    in_a = gpioa->idr;
    in_b = gpiob->idr;
    input_pins = ((in_a << 1) & 0x06) | ((in_b >> 2) & 0x80) | (in_b & 0x11);

    return ((pr << 1) & 0x06) | ((pr >> 2) & 0x80) | (pr & 0x10);
}

static void board_floppy_init(void)
{
    gpio_configure_pin(gpiob, 0, GPI_bus);
    gpio_configure_pin(gpioa, 1, GPI_bus);
    gpio_configure_pin(gpioa, 0, GPI_bus);
    gpio_configure_pin(gpiob, 9, GPI_bus);
    gpio_configure_pin(gpiob, 4, GPI_bus);

    /* PB[15:2] -> EXT[15:2], PA[1:0] -> EXT[1:0] */
    afio->exticr2 = afio->exticr3 = afio->exticr4 = 0x1111;
    afio->exticr1 = 0x1100;

    exti->imr = exti->rtsr = exti->ftsr = m(9) | m(4) | m(1) | m(0);
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
