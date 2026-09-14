/*
 * rp2350_regs.h
 *
 * RP2350 (Raspberry Pi Pico 2) peripheral register definitions.
 * Register offsets and field values cross-checked against the
 * RP2350 Datasheet and pico-sdk hardware_regs headers.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* The generic SPI helper API (inc/spi.h) passes opaque handles. */
struct spi;

/* Every APB/AHB peripheral register (not SIO) has atomic aliases:
 *  +0x1000: XOR on write
 *  +0x2000: bitmask SET on write
 *  +0x3000: bitmask CLEAR on write */
#define RP_XOR(reg_p) (*(volatile uint32_t *)((uint32_t)(reg_p) + 0x1000))
#define RP_SET(reg_p) (*(volatile uint32_t *)((uint32_t)(reg_p) + 0x2000))
#define RP_CLR(reg_p) (*(volatile uint32_t *)((uint32_t)(reg_p) + 0x3000))

/* Subsystem resets */
struct resets {
    uint32_t reset;      /* 00: Reset control */
    uint32_t wdsel;      /* 04: Watchdog select */
    uint32_t reset_done; /* 08: Reset done */
};

#define RST_ADC        (1u<< 0)
#define RST_BUSCTRL    (1u<< 1)
#define RST_DMA        (1u<< 2)
#define RST_HSTX       (1u<< 3)
#define RST_I2C0       (1u<< 4)
#define RST_I2C1       (1u<< 5)
#define RST_IO_BANK0   (1u<< 6)
#define RST_IO_QSPI    (1u<< 7)
#define RST_JTAG       (1u<< 8)
#define RST_PADS_BANK0 (1u<< 9)
#define RST_PADS_QSPI  (1u<<10)
#define RST_PIO0       (1u<<11)
#define RST_PIO1       (1u<<12)
#define RST_PIO2       (1u<<13)
#define RST_PLL_SYS    (1u<<14)
#define RST_PLL_USB    (1u<<15)
#define RST_PWM        (1u<<16)
#define RST_SHA256     (1u<<17)
#define RST_SPI0       (1u<<18)
#define RST_SPI1       (1u<<19)
#define RST_SYSCFG     (1u<<20)
#define RST_SYSINFO    (1u<<21)
#define RST_TBMAN      (1u<<22)
#define RST_TIMER0     (1u<<23)
#define RST_TIMER1     (1u<<24)
#define RST_TRNG       (1u<<25)
#define RST_UART0      (1u<<26)
#define RST_UART1      (1u<<27)
#define RST_USBCTRL    (1u<<28)

#define RESETS_BASE 0x40020000

/* Power-on state machine */
struct psm {
    uint32_t frce_on;    /* 00 */
    uint32_t frce_off;   /* 04 */
    uint32_t wdsel;      /* 08 */
    uint32_t done;       /* 0C */
};

#define PSM_BASE 0x40018000

/* Crystal oscillator */
struct xosc {
    uint32_t ctrl;       /* 00 */
    uint32_t status;     /* 04 */
    uint32_t dormant;    /* 08 */
    uint32_t startup;    /* 0C */
    uint32_t count;      /* 10 */
};

#define XOSC_CTRL_ENABLE         (0xfabu<<12)
#define XOSC_CTRL_DISABLE        (0xd1eu<<12)
#define XOSC_CTRL_FREQ_1_15MHZ   0xaa0u
#define XOSC_STATUS_STABLE       (1u<<31)

#define XOSC_BASE 0x40048000

/* PLL (sys/usb) */
struct pll {
    uint32_t cs;         /* 00: Control and status */
    uint32_t pwr;        /* 04: Power control */
    uint32_t fbdiv_int;  /* 08: Feedback divisor */
    uint32_t prim;       /* 0C: Post dividers */
};

#define PLL_CS_LOCK          (1u<<31)
#define PLL_CS_BYPASS        (1u<< 8)
#define PLL_CS_REFDIV(x)     ((x)<<0)
#define PLL_PWR_VCOPD        (1u<< 5)
#define PLL_PWR_POSTDIVPD    (1u<< 3)
#define PLL_PWR_DSMPD        (1u<< 2)
#define PLL_PWR_PD           (1u<< 0)
#define PLL_PRIM_POSTDIV1(x) ((x)<<16)
#define PLL_PRIM_POSTDIV2(x) ((x)<<12)

#define PLL_SYS_BASE 0x40050000
#define PLL_USB_BASE 0x40058000

/* Clock generators. Each generator: ctrl, div, selected. */
struct clk_gen {
    uint32_t ctrl;
    uint32_t div;
    uint32_t selected;
};

struct clocks {
    struct clk_gen gpout[4]; /* 00-2C */
    struct clk_gen ref;      /* 30: clk_ref */
    struct clk_gen sys;      /* 3C: clk_sys */
    struct clk_gen peri;     /* 48: clk_peri */
    struct clk_gen hstx;     /* 54: clk_hstx */
    struct clk_gen usb;      /* 60: clk_usb */
    struct clk_gen adc;      /* 6C: clk_adc */
    uint32_t dftclk_xosc_ctrl;    /* 78 */
    uint32_t dftclk_rosc_ctrl;    /* 7C */
    uint32_t dftclk_lposc_ctrl;   /* 80 */
    uint32_t clk_sys_resus_ctrl;  /* 84 */
    uint32_t clk_sys_resus_status;/* 88 */
};

#define CLK_REF_CTRL_SRC_ROSC     0u
#define CLK_REF_CTRL_SRC_AUX      1u
#define CLK_REF_CTRL_SRC_XOSC     2u
#define CLK_SYS_CTRL_SRC_REF      0u
#define CLK_SYS_CTRL_SRC_AUX      1u
#define CLK_SYS_CTRL_AUXSRC_PLL_SYS (0u<<5)
#define CLK_PERI_CTRL_ENABLE      (1u<<11)
#define CLK_PERI_CTRL_AUXSRC_SYS  (0u<<5)
#define CLK_USB_CTRL_ENABLE       (1u<<11)
#define CLK_USB_CTRL_AUXSRC_PLL_USB (0u<<5)
/* div register: integer part is [25:16] on RP2350 */
#define CLK_DIV_INT(x)            ((x)<<16)

#define CLOCKS_BASE 0x40010000

/* Tick generators (feed timers, watchdog, per-core systick) */
struct tick_gen {
    uint32_t ctrl;       /* +0: bit0 = enable, bit1 = running */
    uint32_t cycles;     /* +4: clk_ref cycles per tick */
    uint32_t count;      /* +8 */
};

struct ticks {
    struct tick_gen proc0;    /* 00 */
    struct tick_gen proc1;    /* 0C */
    struct tick_gen timer0;   /* 18 */
    struct tick_gen timer1;   /* 24 */
    struct tick_gen watchdog; /* 30 */
    struct tick_gen riscv;    /* 3C */
};

#define TICK_CTRL_ENABLE (1u<<0)

#define TICKS_BASE 0x40108000

/* Watchdog */
struct watchdog {
    uint32_t ctrl;       /* 00 */
    uint32_t load;       /* 04 */
    uint32_t reason;     /* 08 */
    uint32_t scratch[8]; /* 0C-28 */
};

#define WDOG_CTRL_TRIGGER (1u<<31)
#define WDOG_CTRL_ENABLE  (1u<<30)

#define WATCHDOG_BASE 0x400d8000

/* Single-cycle IO (per-core; GPIO fast access).
 * Note the RP2350 interleaved lo/hi register layout. */
struct sio {
    uint32_t cpuid;          /* 000 */
    uint32_t gpio_in;        /* 004: GPIO0-31 input */
    uint32_t gpio_hi_in;     /* 008 */
    uint32_t _0[1];
    uint32_t gpio_out;       /* 010 */
    uint32_t gpio_hi_out;    /* 014 */
    uint32_t gpio_out_set;   /* 018 */
    uint32_t gpio_hi_out_set;/* 01C */
    uint32_t gpio_out_clr;   /* 020 */
    uint32_t gpio_hi_out_clr;/* 024 */
    uint32_t gpio_out_xor;   /* 028 */
    uint32_t gpio_hi_out_xor;/* 02C */
    uint32_t gpio_oe;        /* 030 */
    uint32_t gpio_hi_oe;     /* 034 */
    uint32_t gpio_oe_set;    /* 038 */
    uint32_t gpio_hi_oe_set; /* 03C */
    uint32_t gpio_oe_clr;    /* 040 */
    uint32_t gpio_hi_oe_clr; /* 044 */
    uint32_t gpio_oe_xor;    /* 048 */
    uint32_t gpio_hi_oe_xor; /* 04C */
};

#define SIO_BASE 0xd0000000

/* IO bank 0: per-pin function select and interrupts */
struct io_gpio {
    uint32_t status;     /* +0 */
    uint32_t ctrl;       /* +4 */
};

struct io_bank0 {
    struct io_gpio gpio[48];      /* 000-17C */
    uint32_t _0[32];              /* 180-1FC */
    uint32_t irqsummary[12];      /* 200-22C */
    uint32_t intr[6];             /* 230-247: raw interrupts (W1C for edges) */
    uint32_t proc0_inte[6];       /* 248-25F */
    uint32_t proc0_intf[6];       /* 260-277 */
    uint32_t proc0_ints[6];       /* 278-28F */
    uint32_t proc1_inte[6];       /* 290 */
    uint32_t proc1_intf[6];       /* 2A8 */
    uint32_t proc1_ints[6];       /* 2C0 */
};

/* GPIOx_CTRL fields */
#define IO_CTRL_FUNCSEL_MASK  0x1fu
#define IO_CTRL_OUTOVER(x)    ((x)<<12)
#define IO_CTRL_OEOVER(x)     ((x)<<14)
#define IO_CTRL_INOVER(x)     ((x)<<16)
#define IO_CTRL_IRQOVER(x)    ((x)<<28)

/* Function selects (only ones we use) */
#define GPIO_FUNC_HSTX  0u
#define GPIO_FUNC_SPI   1u
#define GPIO_FUNC_UART  2u
#define GPIO_FUNC_I2C   3u
#define GPIO_FUNC_PWM   4u
#define GPIO_FUNC_SIO   5u
#define GPIO_FUNC_PIO0  6u
#define GPIO_FUNC_PIO1  7u
#define GPIO_FUNC_PIO2  8u
#define GPIO_FUNC_USB   10u
#define GPIO_FUNC_UART_AUX 11u
#define GPIO_FUNC_NULL  0x1fu

/* Per-GPIO interrupt event bits, 4 bits per GPIO, 8 GPIOs per register.
 * Register index = gpio/8; field lsb = (gpio%8)*4. */
#define GPIO_IRQ_LEVEL_LOW  1u
#define GPIO_IRQ_LEVEL_HIGH 2u
#define GPIO_IRQ_EDGE_LOW   4u
#define GPIO_IRQ_EDGE_HIGH  8u

#define IO_BANK0_BASE 0x40028000

/* Pad control, bank 0 */
struct pads_bank0 {
    uint32_t voltage_select; /* 00 */
    uint32_t gpio[48];       /* 04-C4 */
    uint32_t swclk;          /* C8 */
    uint32_t swd;            /* CC */
};

#define PAD_SLEWFAST (1u<<0)
#define PAD_SCHMITT  (1u<<1)
#define PAD_PDE      (1u<<2)
#define PAD_PUE      (1u<<3)
#define PAD_DRIVE_2MA  (0u<<4)
#define PAD_DRIVE_4MA  (1u<<4)
#define PAD_DRIVE_8MA  (2u<<4)
#define PAD_DRIVE_12MA (3u<<4)
#define PAD_IE       (1u<<6)
#define PAD_OD       (1u<<7)
#define PAD_ISO      (1u<<8)

#define PADS_BANK0_BASE 0x40038000

/* DMA controller: 16 channels, 0x40 bytes apart */
struct dma_chan {
    uint32_t read_addr;      /* +00 */
    uint32_t write_addr;     /* +04 */
    uint32_t trans_count;    /* +08 */
    uint32_t ctrl_trig;      /* +0C: writing triggers channel */
    uint32_t al1_ctrl;       /* +10: ctrl alias, no trigger */
    uint32_t al1_read_addr;  /* +14 */
    uint32_t al1_write_addr; /* +18 */
    uint32_t al1_trans_count_trig; /* +1C */
    uint32_t al2_ctrl;       /* +20 */
    uint32_t al2_trans_count;/* +24 */
    uint32_t al2_read_addr;  /* +28 */
    uint32_t al2_write_addr_trig; /* +2C */
    uint32_t al3_ctrl;       /* +30 */
    uint32_t al3_write_addr; /* +34 */
    uint32_t al3_trans_count;/* +38 */
    uint32_t al3_read_addr_trig; /* +3C */
};

struct dma {
    struct dma_chan ch[16];  /* 000-3FC */
    uint32_t intr;           /* 400: raw interrupt status (W1C) */
    uint32_t inte0;          /* 404 */
    uint32_t intf0;          /* 408 */
    uint32_t ints0;          /* 40C: masked status for IRQ0 (W1C) */
    uint32_t intr1;          /* 410 */
    uint32_t inte1;          /* 414 */
    uint32_t intf1;          /* 418 */
    uint32_t ints1;          /* 41C */
    uint32_t intr2;          /* 420 */
    uint32_t inte2;          /* 424 */
    uint32_t intf2;          /* 428 */
    uint32_t ints2;          /* 42C */
    uint32_t intr3;          /* 430 */
    uint32_t inte3;          /* 434 */
    uint32_t intf3;          /* 438 */
    uint32_t ints3;          /* 43C */
    uint32_t timer[4];       /* 440-44C */
    uint32_t multi_chan_trigger; /* 450 */
    uint32_t sniff_ctrl;     /* 454 */
    uint32_t sniff_data;     /* 458 */
    uint32_t _0[1];          /* 45C */
    uint32_t fifo_levels;    /* 460 */
    uint32_t chan_abort;     /* 464 */
    uint32_t n_channels;     /* 468 */
};

/* CTRL_TRIG fields (RP2350 layout) */
#define DMA_CTRL_EN            (1u<< 0)
#define DMA_CTRL_HIGH_PRIO     (1u<< 1)
#define DMA_CTRL_SIZE_BYTE     (0u<< 2)
#define DMA_CTRL_SIZE_HALF     (1u<< 2)
#define DMA_CTRL_SIZE_WORD     (2u<< 2)
#define DMA_CTRL_INCR_READ     (1u<< 4)
#define DMA_CTRL_INCR_READ_REV (1u<< 5)
#define DMA_CTRL_INCR_WRITE    (1u<< 6)
#define DMA_CTRL_INCR_WRITE_REV (1u<<7)
#define DMA_CTRL_RING_SIZE(x)  ((x)<< 8) /* log2 bytes */
#define DMA_CTRL_RING_SEL_WR   (1u<<12)  /* ring applies to write addr */
#define DMA_CTRL_CHAIN_TO(x)   ((x)<<13)
#define DMA_CTRL_TREQ_SEL(x)   ((x)<<17)
#define DMA_CTRL_IRQ_QUIET     (1u<<23)
#define DMA_CTRL_BSWAP         (1u<<24)
#define DMA_CTRL_SNIFF_EN      (1u<<25)
#define DMA_CTRL_BUSY          (1u<<26)

/* TRANS_COUNT mode field */
#define DMA_TRANS_MODE_NORMAL       (0x0u<<28)
#define DMA_TRANS_MODE_TRIGGER_SELF (0x1u<<28)
#define DMA_TRANS_MODE_ENDLESS      (0xfu<<28)

/* DREQ numbers */
#define DREQ_PIO0_TX0  0
#define DREQ_PIO0_TX1  1
#define DREQ_PIO0_TX2  2
#define DREQ_PIO0_TX3  3
#define DREQ_PIO0_RX0  4
#define DREQ_PIO0_RX1  5
#define DREQ_PIO0_RX2  6
#define DREQ_PIO0_RX3  7
#define DREQ_SPI0_TX   24
#define DREQ_SPI0_RX   25
#define DREQ_SPI1_TX   26
#define DREQ_SPI1_RX   27
#define DREQ_UART0_TX  28
#define DREQ_UART0_RX  29
#define DREQ_UART1_TX  30
#define DREQ_UART1_RX  31
#define DREQ_I2C0_TX   44
#define DREQ_I2C0_RX   45
#define DREQ_I2C1_TX   46
#define DREQ_I2C1_RX   47

#define DMA_BASE 0x50000000

/* PIO block (3 instances, 4 state machines each) */
struct pio_sm {
    uint32_t clkdiv;     /* +00: INT[31:16], FRAC[15:8] */
    uint32_t execctrl;   /* +04 */
    uint32_t shiftctrl;  /* +08 */
    uint32_t addr;       /* +0C: RO current PC */
    uint32_t instr;      /* +10: write to execute an instruction */
    uint32_t pinctrl;    /* +14 */
};

struct pio {
    uint32_t ctrl;           /* 000 */
    uint32_t fstat;          /* 004 */
    uint32_t fdebug;         /* 008 */
    uint32_t flevel;         /* 00C */
    uint32_t txf[4];         /* 010-01C */
    uint32_t rxf[4];         /* 020-02C */
    uint32_t irq;            /* 030 */
    uint32_t irq_force;      /* 034 */
    uint32_t input_sync_bypass; /* 038 */
    uint32_t dbg_padout;     /* 03C */
    uint32_t dbg_padoe;      /* 040 */
    uint32_t dbg_cfginfo;    /* 044 */
    uint32_t instr_mem[32];  /* 048-0C4 */
    struct pio_sm sm[4];     /* 0C8-124 */
    uint32_t rxf_putget[4][4]; /* 128-164 */
    uint32_t gpiobase;       /* 168 */
    uint32_t intr;           /* 16C */
    uint32_t irq0_inte;      /* 170 */
    uint32_t irq0_intf;      /* 174 */
    uint32_t irq0_ints;      /* 178 */
    uint32_t irq1_inte;      /* 17C */
    uint32_t irq1_intf;      /* 180 */
    uint32_t irq1_ints;      /* 184 */
};

#define PIO_CTRL_SM_ENABLE(x)     (1u<<(x))
#define PIO_CTRL_SM_RESTART(x)    (1u<<(4+(x)))
#define PIO_CTRL_CLKDIV_RESTART(x) (1u<<(8+(x)))

#define PIO_EXECCTRL_WRAP_BOTTOM(x) ((x)<< 7)
#define PIO_EXECCTRL_WRAP_TOP(x)    ((x)<<12)
#define PIO_EXECCTRL_JMP_PIN(x)     ((x)<<24)
#define PIO_EXECCTRL_SIDE_PINDIR    (1u<<29)
#define PIO_EXECCTRL_SIDE_EN        (1u<<30)

#define PIO_SHIFTCTRL_AUTOPUSH      (1u<<16)
#define PIO_SHIFTCTRL_AUTOPULL      (1u<<17)
#define PIO_SHIFTCTRL_IN_SHIFTDIR_R (1u<<18)
#define PIO_SHIFTCTRL_OUT_SHIFTDIR_R (1u<<19)
#define PIO_SHIFTCTRL_PUSH_THRESH(x) ((x)<<20) /* 0 => 32 */
#define PIO_SHIFTCTRL_PULL_THRESH(x) ((x)<<25) /* 0 => 32 */
#define PIO_SHIFTCTRL_FJOIN_TX      (1u<<30)
#define PIO_SHIFTCTRL_FJOIN_RX      (1u<<31)

#define PIO_PINCTRL_OUT_BASE(x)     ((x)<< 0)
#define PIO_PINCTRL_SET_BASE(x)     ((x)<< 5)
#define PIO_PINCTRL_SIDESET_BASE(x) ((x)<<10)
#define PIO_PINCTRL_IN_BASE(x)      ((x)<<15)
#define PIO_PINCTRL_OUT_COUNT(x)    ((x)<<20)
#define PIO_PINCTRL_SET_COUNT(x)    ((x)<<26)
#define PIO_PINCTRL_SIDESET_COUNT(x) ((x)<<29)

#define PIO0_BASE 0x50200000
#define PIO1_BASE 0x50300000
#define PIO2_BASE 0x50400000

/* 64-bit microsecond timer with 4 alarms */
struct rp_timer {
    uint32_t timehw;     /* 00 */
    uint32_t timelw;     /* 04 */
    uint32_t timehr;     /* 08: latched high (read timelr first) */
    uint32_t timelr;     /* 0C */
    uint32_t alarm[4];   /* 10-1C: fires when timelr == alarm */
    uint32_t armed;      /* 20: W1C to disarm */
    uint32_t timerawh;   /* 24 */
    uint32_t timerawl;   /* 28 */
    uint32_t dbgpause;   /* 2C */
    uint32_t pause;      /* 30 */
    uint32_t locked;     /* 34 */
    uint32_t source;     /* 38 */
    uint32_t intr;       /* 3C: W1C */
    uint32_t inte;       /* 40 */
    uint32_t intf;       /* 44 */
    uint32_t ints;       /* 48 */
};

#define TIMER0_BASE 0x400b0000
#define TIMER1_BASE 0x400b8000

/* PL011 UART */
struct rp_uart {
    uint32_t dr;         /* 00 */
    uint32_t rsr;        /* 04 */
    uint32_t _0[4];
    uint32_t fr;         /* 18 */
    uint32_t _1[1];
    uint32_t ilpr;       /* 20 */
    uint32_t ibrd;       /* 24 */
    uint32_t fbrd;       /* 28 */
    uint32_t lcr_h;      /* 2C */
    uint32_t cr;         /* 30 */
    uint32_t ifls;       /* 34 */
    uint32_t imsc;       /* 38 */
    uint32_t ris;        /* 3C */
    uint32_t mis;        /* 40 */
    uint32_t icr;        /* 44 */
    uint32_t dmacr;      /* 48 */
};

#define UART_FR_TXFE  (1u<<7)
#define UART_FR_RXFF  (1u<<6)
#define UART_FR_TXFF  (1u<<5)
#define UART_FR_RXFE  (1u<<4)
#define UART_FR_BUSY  (1u<<3)

#define UART_LCR_H_WLEN8 (3u<<5)
#define UART_LCR_H_FEN   (1u<<4)

#define UART_CR_RXE    (1u<<9)
#define UART_CR_TXE    (1u<<8)
#define UART_CR_UARTEN (1u<<0)

#define UART_INT_TX    (1u<<5)
#define UART_INT_RX    (1u<<4)

#define UART0_BASE 0x40070000
#define UART1_BASE 0x40078000

/* PL022 SPI */
struct rp_spi {
    uint32_t cr0;        /* 00 */
    uint32_t cr1;        /* 04 */
    uint32_t dr;         /* 08 */
    uint32_t sr;         /* 0C */
    uint32_t cpsr;       /* 10 */
    uint32_t imsc;       /* 14 */
    uint32_t ris;        /* 18 */
    uint32_t mis;        /* 1C */
    uint32_t icr;        /* 20 */
    uint32_t dmacr;      /* 24 */
};

#define SPI_CR0_DSS(x)   ((x)-1)     /* data size select: 4-16 bits */
#define SPI_CR0_SPO      (1u<<6)
#define SPI_CR0_SPH      (1u<<7)
#define SPI_CR0_SCR(x)   ((x)<<8)

#define SPI_CR1_SSE      (1u<<1)

#define SPI_SR_TFE       (1u<<0)
#define SPI_SR_TNF       (1u<<1)
#define SPI_SR_RNE       (1u<<2)
#define SPI_SR_RFF       (1u<<3)
#define SPI_SR_BSY       (1u<<4)

#define SPI0_BASE 0x40080000
#define SPI1_BASE 0x40088000

/* DesignWare I2C */
struct rp_i2c {
    uint32_t con;            /* 00 */
    uint32_t tar;            /* 04 */
    uint32_t sar;            /* 08 */
    uint32_t _0[1];
    uint32_t data_cmd;       /* 10 */
    uint32_t ss_scl_hcnt;    /* 14 */
    uint32_t ss_scl_lcnt;    /* 18 */
    uint32_t fs_scl_hcnt;    /* 1C */
    uint32_t fs_scl_lcnt;    /* 20 */
    uint32_t _1[2];
    uint32_t intr_stat;      /* 2C */
    uint32_t intr_mask;      /* 30 */
    uint32_t raw_intr_stat;  /* 34 */
    uint32_t rx_tl;          /* 38 */
    uint32_t tx_tl;          /* 3C */
    uint32_t clr_intr;       /* 40 */
    uint32_t clr_rx_under;   /* 44 */
    uint32_t clr_rx_over;    /* 48 */
    uint32_t clr_tx_over;    /* 4C */
    uint32_t clr_rd_req;     /* 50 */
    uint32_t clr_tx_abrt;    /* 54 */
    uint32_t clr_rx_done;    /* 58 */
    uint32_t clr_activity;   /* 5C */
    uint32_t clr_stop_det;   /* 60 */
    uint32_t clr_start_det;  /* 64 */
    uint32_t clr_gen_call;   /* 68 */
    uint32_t enable;         /* 6C */
    uint32_t status;         /* 70 */
    uint32_t txflr;          /* 74 */
    uint32_t rxflr;          /* 78 */
    uint32_t sda_hold;       /* 7C */
    uint32_t tx_abrt_source; /* 80 */
    uint32_t slv_data_nack_only; /* 84 */
    uint32_t dma_cr;         /* 88 */
    uint32_t dma_tdlr;       /* 8C */
    uint32_t dma_rdlr;       /* 90 */
    uint32_t sda_setup;      /* 94 */
    uint32_t ack_general_call; /* 98 */
    uint32_t enable_status;  /* 9C */
    uint32_t fs_spklen;      /* A0 */
};

#define I2C_CON_MASTER        (1u<<0)
#define I2C_CON_SPEED_STD     (1u<<1)
#define I2C_CON_SPEED_FAST    (2u<<1)
#define I2C_CON_RESTART_EN    (1u<<5)
#define I2C_CON_SLAVE_DISABLE (1u<<6)
#define I2C_CON_TX_EMPTY_CTRL (1u<<8)

#define I2C_DATA_CMD_READ     (1u<<8)
#define I2C_DATA_CMD_STOP     (1u<<9)
#define I2C_DATA_CMD_RESTART  (1u<<10)

#define I2C_INT_RX_FULL       (1u<<2)
#define I2C_INT_TX_EMPTY      (1u<<4)
#define I2C_INT_TX_ABRT       (1u<<6)
#define I2C_INT_STOP_DET      (1u<<9)

#define I2C_DMA_CR_TDMAE      (1u<<1)

#define I2C0_BASE 0x40090000
#define I2C1_BASE 0x40098000

/* USB device controller.
 *
 * Two blocks: 4kB of dual-port RAM (packet buffers and per-endpoint
 * control words) at USBCTRL_DPRAM_BASE, and the controller registers at
 * USBCTRL_REGS_BASE. Only the DPRAM has no atomic aliases: it is plain
 * memory and must be read-modify-written by hand. */

struct usb_dpram {
    uint32_t setup_low;          /* 000: bmRequestType, bRequest, wValue */
    uint32_t setup_high;         /* 004: wIndex, wLength */
    /* [ep-1][0] = IN, [ep-1][1] = OUT. EP0 has no control word: its
     * buffer is fixed at DPRAM offset 0x100. */
    uint32_t ep_ctrl[15][2];     /* 008-07C */
    /* [ep][0] = IN, [ep][1] = OUT. */
    uint32_t ep_buf_ctrl[16][2]; /* 080-0FC */
    uint8_t ep0_buf[64];         /* 100-13F */
    uint8_t ep0_buf1[64];        /* 140-17F: 2nd EP0 buffer, unused */
    uint8_t data[3712];          /* 180-FFF: endpoint packet buffers */
};

#define USB_DPRAM_DATA_OFFSET 0x180

/* ep_ctrl[] fields */
#define USB_EP_CTRL_ENABLE        (1u<<31)
#define USB_EP_CTRL_DOUBLE_BUF    (1u<<30)
#define USB_EP_CTRL_INT_PER_BUF   (1u<<29)
#define USB_EP_CTRL_INT_PER_2BUF  (1u<<28)
#define USB_EP_CTRL_TYPE(x)       ((x)<<26)
#define USB_EP_CTRL_INT_ON_STALL  (1u<<17)
#define USB_EP_CTRL_INT_ON_NAK    (1u<<16)
#define USB_EP_TYPE_CONTROL   0u
#define USB_EP_TYPE_ISO       1u
#define USB_EP_TYPE_BULK      2u
#define USB_EP_TYPE_INTERRUPT 3u

/* ep_buf_ctrl[] fields (buffer 0; buffer 1 is the same, 16 bits up) */
#define USB_BUF_CTRL_FULL      (1u<<15)
#define USB_BUF_CTRL_LAST      (1u<<14)
#define USB_BUF_CTRL_DATA1_PID (1u<<13)
#define USB_BUF_CTRL_RESET     (1u<<12)
#define USB_BUF_CTRL_STALL     (1u<<11)
#define USB_BUF_CTRL_AVAIL     (1u<<10)
#define USB_BUF_CTRL_LEN_MASK  0x3ffu

struct rp_usb {
    uint32_t dev_addr_ctrl;      /* 00 */
    uint32_t int_ep_addr[15];    /* 04-3C */
    uint32_t main_ctrl;          /* 40 */
    uint32_t sof_wr;             /* 44 */
    uint32_t sof_rd;             /* 48 */
    uint32_t sie_ctrl;           /* 4C */
    uint32_t sie_status;         /* 50 */
    uint32_t int_ep_ctrl;        /* 54 */
    uint32_t buf_status;         /* 58 */
    uint32_t buf_cpu_should_handle; /* 5C */
    uint32_t ep_abort;           /* 60 */
    uint32_t ep_abort_done;      /* 64 */
    uint32_t ep_stall_arm;       /* 68 */
    uint32_t nak_poll;           /* 6C */
    uint32_t ep_status_stall_nak;/* 70 */
    uint32_t muxing;             /* 74 */
    uint32_t pwr;                /* 78 */
    uint32_t phy_direct;         /* 7C */
    uint32_t phy_direct_override;/* 80 */
    uint32_t phy_trim;           /* 84 */
    uint32_t linestate_tuning;   /* 88 */
    uint32_t intr;               /* 8C: raw interrupt status */
    uint32_t inte;               /* 90 */
    uint32_t intf;               /* 94 */
    uint32_t ints;               /* 98: masked interrupt status */
};

#define USB_MAIN_CTRL_CONTROLLER_EN (1u<<0)
#define USB_MAIN_CTRL_HOST_NDEVICE  (1u<<1)
#define USB_MAIN_CTRL_PHY_ISO       (1u<<2)

#define USB_SIE_CTRL_EP0_INT_STALL  (1u<<31)
#define USB_SIE_CTRL_EP0_DOUBLE_BUF (1u<<30)
#define USB_SIE_CTRL_EP0_INT_1BUF   (1u<<29)
#define USB_SIE_CTRL_EP0_INT_2BUF   (1u<<28)
#define USB_SIE_CTRL_EP0_INT_NAK    (1u<<27)
#define USB_SIE_CTRL_TRANSCEIVER_PD (1u<<18)
#define USB_SIE_CTRL_PULLUP_EN      (1u<<16)
#define USB_SIE_CTRL_PULLDOWN_EN    (1u<<15)
#define USB_SIE_CTRL_RESET_BUS      (1u<<13)

#define USB_SIE_STATUS_DATA_SEQ_ERROR (1u<<31)
#define USB_SIE_STATUS_ACK_REC        (1u<<30)
#define USB_SIE_STATUS_STALL_REC      (1u<<29)
#define USB_SIE_STATUS_NAK_REC        (1u<<28)
#define USB_SIE_STATUS_RX_TIMEOUT     (1u<<27)
#define USB_SIE_STATUS_RX_OVERFLOW    (1u<<26)
#define USB_SIE_STATUS_BIT_STUFF_ERROR (1u<<25)
#define USB_SIE_STATUS_CRC_ERROR      (1u<<24)
#define USB_SIE_STATUS_ENDPOINT_ERROR (1u<<23)
#define USB_SIE_STATUS_BUS_RESET      (1u<<19)
#define USB_SIE_STATUS_TRANS_COMPLETE (1u<<18)
#define USB_SIE_STATUS_SETUP_REC      (1u<<17)
#define USB_SIE_STATUS_CONNECTED      (1u<<16)
#define USB_SIE_STATUS_RESUME         (1u<<11)
#define USB_SIE_STATUS_SUSPENDED      (1u<< 4)

#define USB_MUXING_TO_PHY           (1u<<0)
#define USB_MUXING_TO_EXTPHY        (1u<<1)
#define USB_MUXING_TO_DIGITAL_PAD   (1u<<2)
#define USB_MUXING_SOFTCON          (1u<<3)

#define USB_PWR_VBUS_EN             (1u<<0)
#define USB_PWR_VBUS_EN_OVERRIDE_EN (1u<<1)
#define USB_PWR_VBUS_DETECT         (1u<<2)
#define USB_PWR_VBUS_DETECT_OVERRIDE_EN (1u<<3)

/* buf_status: two bits per endpoint, IN then OUT. */
#define USB_BUFF_STATUS_IN(ep)  (1u<<((ep)*2))
#define USB_BUFF_STATUS_OUT(ep) (2u<<((ep)*2))

#define USB_EP_STALL_ARM_EP0_IN  (1u<<0)
#define USB_EP_STALL_ARM_EP0_OUT (1u<<1)

/* intr/inte/ints */
#define USB_INT_TRANS_COMPLETE   (1u<< 3)
#define USB_INT_BUFF_STATUS      (1u<< 4)
#define USB_INT_ERROR_DATA_SEQ   (1u<< 5)
#define USB_INT_STALL            (1u<<10)
#define USB_INT_VBUS_DETECT      (1u<<11)
#define USB_INT_BUS_RESET        (1u<<12)
#define USB_INT_DEV_CONN_DIS     (1u<<13)
#define USB_INT_DEV_SUSPEND      (1u<<14)
#define USB_INT_DEV_RESUME       (1u<<15)
#define USB_INT_SETUP_REQ        (1u<<16)
#define USB_INT_DEV_SOF          (1u<<17)

#define USBCTRL_DPRAM_BASE 0x50100000
#define USBCTRL_REGS_BASE  0x50110000

/* Bootrom reboot() (ROM_TABLE_CODE('R','B')) flags. */
#define REBOOT2_FLAG_REBOOT_TYPE_NORMAL   0x000u
#define REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL  0x002u
#define REBOOT2_FLAG_NO_RETURN_ON_SUCCESS 0x100u

/* QMI (QSPI memory interface) -- only for optional XIP timing tweaks */
struct qmi {
    uint32_t direct_csr;  /* 00 */
    uint32_t direct_tx;   /* 04 */
    uint32_t direct_rx;   /* 08 */
    uint32_t m0_timing;   /* 0C */
    uint32_t m0_rfmt;     /* 10 */
    uint32_t m0_rcmd;     /* 14 */
    uint32_t m0_wfmt;     /* 18 */
    uint32_t m0_wcmd;     /* 1C */
    uint32_t m1_timing;   /* 20 */
    uint32_t m1_rfmt;     /* 24 */
    uint32_t m1_rcmd;     /* 28 */
    uint32_t m1_wfmt;     /* 2C */
    uint32_t m1_wcmd;     /* 30 */
    uint32_t atrans[8];   /* 34-50 */
};

#define QMI_BASE 0x400d0000

/* Interrupt numbers */
#define TIMER0_IRQ_0   0
#define TIMER0_IRQ_1   1
#define TIMER0_IRQ_2   2
#define TIMER0_IRQ_3   3
#define TIMER1_IRQ_0   4
#define PWM_IRQ_WRAP_0 8
#define DMA_IRQ_0      10
#define DMA_IRQ_1      11
#define DMA_IRQ_2      12
#define DMA_IRQ_3      13
#define USBCTRL_IRQ    14
#define PIO0_IRQ_0     15
#define PIO0_IRQ_1     16
#define PIO1_IRQ_0     17
#define PIO2_IRQ_0     19
#define IO_IRQ_BANK0   21
#define SIO_IRQ_FIFO   25
#define CLOCKS_IRQ     30
#define SPI0_IRQ       31
#define SPI1_IRQ       32
#define UART0_IRQ      33
#define UART1_IRQ      34
#define ADC_IRQ_FIFO   35
#define I2C0_IRQ       36
#define I2C1_IRQ       37
#define SPARE_IRQ_0    46
#define SPARE_IRQ_1    47
#define SPARE_IRQ_2    48
#define SPARE_IRQ_3    49
#define SPARE_IRQ_4    50
#define SPARE_IRQ_5    51

#define RP2350_NR_IRQS 52

/* Generic pin levels */
#define LOW  0
#define HIGH 1

/* XIP flash mapping */
#define XIP_BASE      0x10000000
#define XIP_NOCACHE_NOALLOC_BASE 0x14000000

/* Bootrom function-table lookup (Arm, secure) */
#define BOOTROM_TABLE_LOOKUP_OFFSET 0x16
#define ROM_TABLE_CODE(c1,c2) ((c1) | ((c2)<<8))
#define RT_FLAG_FUNC_ARM_SEC  0x0004

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
