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

/* Offsets within the input_pins bitmap. */
#define inp_dir     0
#define inp_step    3
#define inp_sel0    4
#define inp_sel1    5
#define inp_wgate   6
#define inp_side    7

/* Outputs are buffered, thus do *not* need to be 5v tolerant. */
#define gpio_out gpiob
#define pin_dskchg  3
static uint8_t pin_index; /* differs across board revisions */
#define pin_trk0    5
#define pin_wrprot 11
#define pin_rdy    12

#define gpio_timer gpiob
#define pin_wdata   6 /* must be 5v tolerant */
#define dma_wdata   (dma1->ch1)
#define tim_wdata   (tim4)
#define pin_rdata   7
#define dma_rdata   (dma1->ch7)
#define tim_rdata   (tim4)

#define NR_DRIVES 2

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
        pin_index = 4;
        input_init_default();
        break;
    case BRDREV_MM150:
        pin_index = 2;
        input_init_default();
        break;
    case BRDREV_TB160:
        pin_index = 1;
        input_init_tb160();
        break;
    }
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
