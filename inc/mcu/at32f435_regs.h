/*
 * at32f435_regs.h
 * 
 * Core and peripheral register definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Power control */
struct pwr {
    uint32_t cr;       /* 00: Power control */
    uint32_t csr;      /* 04: Power control/status */
    uint32_t rsvd[2];
    uint32_t ldoov;    /* 10: LDO output voltage */
};

#define PWR_LDOOV_1V3  (1u<<0)

#define PWR_BASE 0x40007000

/* Flash memory interface */
struct flash_bank {
    uint32_t sr;       /* +00: Flash status */
    uint32_t cr;       /* +04: Flash control */
    uint32_t ar;       /* +08: Flash address */
};
struct flash {
    uint32_t psr;      /* 00: Performance select */
    uint32_t unlock1;  /* 04: Bank 1 unlock */
    uint32_t opt_unlock;/* 08: Option bytes unlock */
    struct flash_bank bank1;
    uint32_t rsvd;     /* 18: - */
    uint32_t obr;      /* 1C: Option byte */
    uint32_t epps0;    /* 20: Erase/program protection */
    uint32_t rsvd2[2]; /* 24-28: - */
    uint32_t epps1;    /* 2C: Erase/program protection */
    uint32_t rsvd3[5]; /* 30-40: - */
    uint32_t unlock2;  /* 44: Bank 2 unlock */
    uint32_t rsvd4;    /* 48: - */
    struct flash_bank bank2;
    uint32_t contr;    /* 58: Continue read */
    uint32_t rsvd5;    /* 5C: - */
    uint32_t divr;     /* 60: Divider */
};

#define FLASH_SR_EOP         (1u<< 5)
#define FLASH_SR_WRPRTERR    (1u<< 4)
#define FLASH_SR_PGERR       (1u<< 2)
#define FLASH_SR_BSY         (1u<< 0)

#define FLASH_CR_EOPIE       (1u<<12)
#define FLASH_CR_ERRIE       (1u<<10)
#define FLASH_CR_OPTWRE      (1u<< 9)
#define FLASH_CR_LOCK        (1u<< 7)
#define FLASH_CR_ERASE_STRT  (1u<< 6)
#define FLASH_CR_OPTER       (1u<< 5)
#define FLASH_CR_OPTPG       (1u<< 4)
#define FLASH_CR_SEC_ER      (1u<< 3)
#define FLASH_CR_BANK_ER     (1u<< 2)
#define FLASH_CR_PG_ER       (1u<< 1)
#define FLASH_CR_PG          (1u<< 0)

#define FLASH_DIVR_DIV_2     (0u<< 0)
#define FLASH_DIVR_DIV_3     (1u<< 0)
#define FLASH_DIVR_DIV_4     (2u<< 0)

#define FLASH_BASE 0x40023c00

/* Reset and clock control */
struct rcc {
    uint32_t cr;       /* 00: Clock control */
    uint32_t pllcfgr;  /* 04: PLL configuration */
    uint32_t cfgr;     /* 08: Clock configuration */
    uint32_t cir;      /* 0C: Clock interrupt */
    uint32_t ahb1rstr; /* 10: AHB1 peripheral reset */
    uint32_t ahb2rstr; /* 14: AHB2 peripheral reset */
    uint32_t ahb3rstr; /* 18: AHB3 peripheral reset */
    uint32_t _unused0; /* 1C: - */
    uint32_t apb1rstr; /* 20: APB1 peripheral reset */
    uint32_t apb2rstr; /* 24: APB2 peripheral reset */
    uint32_t _unused1; /* 28: - */
    uint32_t _unused2; /* 2C: - */
    uint32_t ahb1enr;  /* 30: AHB1 peripheral clock enable */
    uint32_t ahb2enr;  /* 34: AHB2 peripheral clock enable */
    uint32_t ahb3enr;  /* 38: AHB3 peripheral clock enable */
    uint32_t _unused3; /* 3C: - */
    uint32_t apb1enr;  /* 40: APB1 peripheral clock enable */
    uint32_t apb2enr;  /* 44: APB2 peripheral clock enable */
    uint32_t _unused4; /* 48: - */
    uint32_t _unused5; /* 4C: - */
    uint32_t ahb1lpenr;/* 50: AHB1 peripheral clock enable (low-power mode) */
    uint32_t ahb2lpenr;/* 54: AHB2 peripheral clock enable (low-power mode) */
    uint32_t ahb3lpenr;/* 58: AHB3 peripheral clock enable (low-power mode) */
    uint32_t _unused6; /* 5C: - */
    uint32_t apb1lpenr;/* 60: APB1 peripheral clock enable (low-power mode) */
    uint32_t apb2lpenr;/* 64: APB2 peripheral clock enable (low-power mode) */
    uint32_t _unused7; /* 68: - */
    uint32_t _unused8; /* 6C: - */
    uint32_t bdcr;     /* 70: Backup domain control */
    uint32_t csr;      /* 74: Clock control & status */
    uint32_t _unused9[10]; /* 78-9C: - */
    uint32_t misc1;    /* A0: Misc 1 */
    uint32_t misc2;    /* A4: Misc 2 */
};

#define RCC_CR_PLLRDY        (1u<<25)
#define RCC_CR_PLLON         (1u<<24)
#define RCC_CR_CFDEN         (1u<<19)
#define RCC_CR_HSEBYP        (1u<<18)
#define RCC_CR_HSERDY        (1u<<17)
#define RCC_CR_HSEON         (1u<<16)
#define RCC_CR_HSIRDY        (1u<<1)
#define RCC_CR_HSION         (1u<<0)

#define RCC_PLLCFGR_PLLSRC_HSE (1<<22)
#define PLL_FR_2 1
#define RCC_PLLCFGR_PLL_FR(x) ((x)<<16)
#define RCC_PLLCFGR_PLL_NS(x) ((x)<< 6)
#define RCC_PLLCFGR_PLL_MS(x) ((x)<< 0)

#define RCC_CFGR_MCO2(x)     ((x)<<30)
#define RCC_CFGR_MCO2PRE(x)  ((x)<<27)
#define RCC_CFGR_USBPRE(x)   ((x)<<24)
#define RCC_CFGR_MCO1(x)     ((x)<<21)
#define RCC_CFGR_RTCPRE(x)   ((x)<<16)
#define RCC_CFGR_PPRE2(x)    ((x)<<13)
#define RCC_CFGR_PPRE1(x)    ((x)<<10)
#define RCC_CFGR_HPRE(x)     ((x)<< 4)
#define RCC_CFGR_SWS(x)      ((x)<< 2)
#define RCC_CFGR_SW(x)       ((x)<< 0)

#define RCC_AHB1ENR_OTGFS2EN (1u<<29)
#define RCC_AHB1ENR_DMA2EN   (1u<<24)
#define RCC_AHB1ENR_DMA1EN   (1u<<22)
#define RCC_AHB1ENR_EDMAEN   (1u<<21)
#define RCC_AHB1ENR_CRCEN    (1u<<12)
#define RCC_AHB1ENR_GPIOHEN  (1u<< 7)
#define RCC_AHB1ENR_GPIOGEN  (1u<< 6)
#define RCC_AHB1ENR_GPIOFEN  (1u<< 5)
#define RCC_AHB1ENR_GPIOEEN  (1u<< 4)
#define RCC_AHB1ENR_GPIODEN  (1u<< 3)
#define RCC_AHB1ENR_GPIOCEN  (1u<< 2)
#define RCC_AHB1ENR_GPIOBEN  (1u<< 1)
#define RCC_AHB1ENR_GPIOAEN  (1u<< 0)

#define RCC_AHB2ENR_OTGFS1EN (1u<< 7)
#define RCC_AHB2ENR_DVPEN    (1u<< 0)

#define RCC_AHB3ENR_QSPI2EN  (1u<<14)
#define RCC_AHB3ENR_QSPI1EN  (1u<< 1)
#define RCC_AHB3ENR_XMCEN    (1u<< 0)

#define RCC_APB1ENR_USART8EN (1u<<31)
#define RCC_APB1ENR_USART7EN (1u<<30)
#define RCC_APB1ENR_DACEN    (1u<<29)
#define RCC_APB1ENR_PWREN    (1u<<28)
#define RCC_APB1ENR_CAN1EN   (1u<<25)
#define RCC_APB1ENR_I2C3EN   (1u<<23)
#define RCC_APB1ENR_I2C2EN   (1u<<22)
#define RCC_APB1ENR_I2C1EN   (1u<<21)
#define RCC_APB1ENR_USART5EN (1u<<20)
#define RCC_APB1ENR_USART4EN (1u<<19)
#define RCC_APB1ENR_USART3EN (1u<<18)
#define RCC_APB1ENR_USART2EN (1u<<17)
#define RCC_APB1ENR_SPI3EN   (1u<<15)
#define RCC_APB1ENR_SPI2EN   (1u<<14)
#define RCC_APB1ENR_WWDGEN   (1u<<11)
#define RCC_APB1ENR_TIM14EN  (1u<< 8)
#define RCC_APB1ENR_TIM13EN  (1u<< 7)
#define RCC_APB1ENR_TIM12EN  (1u<< 6)
#define RCC_APB1ENR_TIM7EN   (1u<< 5)
#define RCC_APB1ENR_TIM6EN   (1u<< 4)
#define RCC_APB1ENR_TIM5EN   (1u<< 3)
#define RCC_APB1ENR_TIM4EN   (1u<< 2)
#define RCC_APB1ENR_TIM3EN   (1u<< 1)
#define RCC_APB1ENR_TIM2EN   (1u<< 0)

#define RCC_APB2ENR_ACCEN    (1u<<29)
#define RCC_APB2ENR_TIM20EN  (1u<<20)
#define RCC_APB2ENR_TIM11EN  (1u<<18)
#define RCC_APB2ENR_TIM10EN  (1u<<17)
#define RCC_APB2ENR_TIM9EN   (1u<<16)
#define RCC_APB2ENR_SYSCFGEN (1u<<14)
#define RCC_APB2ENR_SPI4EN   (1u<<13)
#define RCC_APB2ENR_SPI1EN   (1u<<12)
#define RCC_APB2ENR_ADC3EN   (1u<<10)
#define RCC_APB2ENR_ADC2EN   (1u<< 9)
#define RCC_APB2ENR_ADC1EN   (1u<< 8)
#define RCC_APB2ENR_USART6EN (1u<< 5)
#define RCC_APB2ENR_USART1EN (1u<< 4)
#define RCC_APB2ENR_TIM8EN   (1u<< 1)
#define RCC_APB2ENR_TIM1EN   (1u<< 0)

#define RCC_BDCR_BDRST       (1u<<16)
#define RCC_BDCR_RTCEN       (1u<<15)
#define RCC_BDCR_RTCSEL(x)   ((x)<<8)
#define RCC_BDCR_LSEDRV(x)   ((x)<<3)
#define RCC_BDCR_LSEBYP      (1u<< 2)
#define RCC_BDCR_LSERDY      (1u<< 1)
#define RCC_BDCR_LSEON       (1u<< 0)

#define RCC_CSR_LPWRRSTF     (1u<<31)
#define RCC_CSR_WWDGRSTF     (1u<<30)
#define RCC_CSR_IWDGRSTF     (1u<<29)
#define RCC_CSR_SFTRSTF      (1u<<28)
#define RCC_CSR_PORRSTF      (1u<<27)
#define RCC_CSR_PINRSTF      (1u<<26)
#define RCC_CSR_BORRSTF      (1u<<25)
#define RCC_CSR_RMVF         (1u<<24)
#define RCC_CSR_LSIRDY       (1u<< 1)
#define RCC_CSR_LSION        (1u<< 0)

#define RCC_MISC2_USBDIV(x)  ((x)<<12)
#define USBDIV_6 11
#define RCC_MISC2_AUTOSTEP   (3u<< 4)

#define RCC_BASE 0x40023800

/* General-purpose I/O */
struct gpio {
    uint32_t moder;   /* 00: Port mode */
    uint32_t otyper;  /* 04: Port output type */
    uint32_t odrvr;   /* 08: Drive capability */
    uint32_t pupdr;   /* 0C: Port pull-up/pull-down */
    uint32_t idr;     /* 10: Port input data */
    uint32_t odr;     /* 14: Port output data */
    uint32_t bsrr;    /* 18: Port bit set/reset */
    uint32_t lckr;    /* 1C: Port configuration lock */
    uint32_t afrl;    /* 20: Alternate function low */
    uint32_t afrh;    /* 24: Alternate function high */
    uint32_t brr;     /* 28: Port bit reset */
    uint32_t rsvd[4]; /* 2C-38 */
    uint32_t hdrv;    /* 3C: Huge current control */
};

/* 0-1: MODE, 2: OTYPE, 3-4:ODRV, 5-6:PUPD, 7:OUTPUT_LEVEL */
#define GPI_analog    0x3u
#define GPI(pupd)     (0x0u|((pupd)<<5))
#define PUPD_none     0
#define PUPD_up       1
#define PUPD_down     2
#define GPI_floating  GPI(PUPD_none)
#define GPI_pull_down GPI(PUPD_down)
#define GPI_pull_up   GPI(PUPD_up)

#define GPO_pushpull(speed,level)  (0x1u|((speed)<<3)|((level)<<7))
#define GPO_opendrain(speed,level) (0x5u|((speed)<<3)|((level)<<7))
#define AFI(pupd)                  (0x2u|((pupd)<<5))
#define AFO_pushpull(speed)        (0x2u|((speed)<<3))
#define AFO_opendrain(speed)       (0x6u|((speed)<<3))
#define _2MHz 0
#define _10MHz 0
#define _50MHz 0
#define LOW  0
#define HIGH 1

#define GPIOA_BASE 0x40020000
#define GPIOB_BASE 0x40020400
#define GPIOC_BASE 0x40020800
#define GPIOD_BASE 0x40020C00
#define GPIOE_BASE 0x40021000
#define GPIOF_BASE 0x40021400
#define GPIOG_BASE 0x40021800
#define GPIOH_BASE 0x40021C00

/* System configuration controller */
struct syscfg {
    uint32_t cfg1;       /* 00: Configuration 1 */
    uint32_t cfg2;       /* 04: Configuration 2 */
    uint32_t exticr[4];  /* 08-14: External interrupt configuration #1-4 */
    uint32_t _pad[5];
    uint32_t uhdrv;      /* 2C: Ultra high source/sink strength */
};

#define SYSCFG_BASE 0x40013800

/* EXTI */
#define EXTI_BASE 0x40013c00

/* DMA */
#define DMA1_CH1_IRQ 56
#define DMA1_CH2_IRQ 57
#define DMA1_CH3_IRQ 58
#define DMA1_CH4_IRQ 59
#define DMA1_CH5_IRQ 60
#define DMA1_CH6_IRQ 68
#define DMA1_CH7_IRQ 69

#define DMA1_BASE 0x40026400
#define DMA2_BASE 0x40026600

/* DMAMUX */
struct dmamux {
    uint32_t sel;        /* 00: Selection */
    uint32_t cctrl[7];   /* 04-1C: Channel control */
    uint32_t gctrl[4];   /* 20-2C: Generator control */
    uint32_t sync_sts;   /* 30: Channel synchronisation status */
    uint32_t sync_clr;   /* 34: Channel synchronisation clear */
    uint32_t g_sts;      /* 38: Generator interrupt status */
    uint32_t g_clr;      /* 3C: Generator interrupt clear */
};

#define DMAMUX_SEL_TBL_SEL   (1u<< 0)

#define DMAMUX_CCTRL_REQSEL(x) ((x)<<0)

#define DMAMUX_REQ_I2C2_RX  18
#define DMAMUX_REQ_I2C2_TX  19
#define DMAMUX_REQ_TIM1_CH1 42
#define DMAMUX_REQ_TIM3_OVF 65

#define DMAMUX1_BASE (DMA1_BASE + 0x100)
#define DMAMUX2_BASE (DMA2_BASE + 0x100)

/* Timer */
#define TIM_CR1_PMEN (1u<<10)

#define TIM1_BASE 0x40010000
#define TIM2_BASE 0x40000000
#define TIM3_BASE 0x40000400
#define TIM4_BASE 0x40000800
#define TIM5_BASE 0x40000c00
#define TIM6_BASE 0x40001000
#define TIM7_BASE 0x40001400
#define TIM8_BASE 0x40010400
#define TIM9_BASE 0x40014000
#define TIM10_BASE 0x40014400
#define TIM11_BASE 0x40014800
#define TIM12_BASE 0x40001800
#define TIM13_BASE 0x40001c00
#define TIM14_BASE 0x40002000

/* I2C */
struct i2c {
    uint32_t cr1;     /* 00: Control 1 */
    uint32_t cr2;     /* 04: Control 2 */
    uint32_t oar1;    /* 08: Own address 1 */
    uint32_t oar2;    /* 0C: Own address 2 */
    uint32_t timingr; /* 10: Timing */
    uint32_t timeoutr;/* 14: Timeout */
    uint32_t isr;     /* 18: Interrupt status */
    uint32_t icr;     /* 1C: Interrupt clear */
    uint32_t pecr;    /* 20: PEC */
    uint32_t rxdr;    /* 24: Receive data */
    uint32_t txdr;    /* 28: Transmit data */
};

#define I2C_CR1_PECEN     (1u<<23)
#define I2C_CR1_ALERTEN   (1u<<22)
#define I2C_CR1_SMBDEN    (1u<<21)
#define I2C_CR1_SMBHEN    (1u<<20)
#define I2C_CR1_GCEN      (1u<<19)
#define I2C_CR1_NOSTRETCH (1u<<17)
#define I2C_CR1_SBC       (1u<<16)
#define I2C_CR1_RXDMAEN   (1u<<15)
#define I2C_CR1_TXDMAEN   (1u<<14)
#define I2C_CR1_ANFOFF    (1u<<12)
#define I2C_CR1_DNF(x)    ((x)<<8)
#define I2C_CR1_ERRIE     (1u<< 7)
#define I2C_CR1_TCIE      (1u<< 6)
#define I2C_CR1_STOPIE    (1u<< 5)
#define I2C_CR1_NACKIE    (1u<< 4)
#define I2C_CR1_ADDRIE    (1u<< 3)
#define I2C_CR1_RXIE      (1u<< 2)
#define I2C_CR1_TXIE      (1u<< 1)
#define I2C_CR1_PE        (1u<< 0)

#define I2C_CR2_PECBYTE   (1u<<26)
#define I2C_CR2_AUTOEND   (1u<<25)
#define I2C_CR2_RELOAD    (1u<<24)
#define I2C_CR2_NBYTES(x) ((x)<<16)
#define I2C_CR2_NACK      (1u<<15)
#define I2C_CR2_STOP      (1u<<14)
#define I2C_CR2_START     (1u<<13)
#define I2C_CR2_HEAD10R   (1u<<12)
#define I2C_CR2_ADD10     (1u<<11)
#define I2C_CR2_RD_WRN    (1u<<10)
#define I2C_CR2_SADD(x)   ((x)<<0)

/* Based on 144MHz peripheral clock */
#define I2C_TIMING_100k 0x80504C4E
#define I2C_TIMING_400k 0x40301B28

#define I2C_SR_ERRORS     0x1f10
#define I2C_SR_BUSY       (1u<<15)
#define I2C_SR_ALERT      (1u<<13)
#define I2C_SR_TIMEOUT    (1u<<12)
#define I2C_SR_PECERR     (1u<<11)
#define I2C_SR_OVR        (1u<<10)
#define I2C_SR_ARLO       (1u<< 9)
#define I2C_SR_BERR       (1u<< 8)
#define I2C_SR_TCR        (1u<< 7)
#define I2C_SR_TC         (1u<< 6)
#define I2C_SR_STOPF      (1u<< 5)
#define I2C_SR_NACKF      (1u<< 4)
#define I2C_SR_ADDR       (1u<< 3)
#define I2C_SR_RXNE       (1u<< 2)
#define I2C_SR_TXIS       (1u<< 1)
#define I2C_SR_TXE        (1u<< 0)

#define I2C1_BASE 0x40005400
#define I2C2_BASE 0x40005800

/* USART */
#define USART1_BASE 0x40011000
#define USART2_BASE 0x40004400
#define USART3_BASE 0x40004800

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
