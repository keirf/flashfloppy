/*
 * stm32f105_regs.h
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
};

#define PWR_CR_DBP           (1u<< 8)

#define PWR_BASE 0x40007000

/* Flash memory interface */
struct flash {
    uint32_t acr;      /* 00: Flash access control */
    uint32_t keyr;     /* 04: FPEC key */
    uint32_t optkeyr;  /* 08: Flash OPTKEY */
    uint32_t sr;       /* 0C: Flash status */
    uint32_t cr;       /* 10: Flash control */
    uint32_t ar;       /* 14: Flash address */
    uint32_t rsvd;     /* 18: - */
    uint32_t obr;      /* 1C: Option byte */
    uint32_t wrpr;     /* 20: Write protection */
};

#define FLASH_ACR_PRFTBS     (1u<< 5)
#define FLASH_ACR_PRFTBE     (1u<< 4)
#define FLASH_ACR_HLFCYA     (1u<< 3)
#define FLASH_ACR_LATENCY(w) ((w)<<0) /* wait states */

#define FLASH_SR_EOP         (1u<< 5)
#define FLASH_SR_WRPRTERR    (1u<< 4)
#define FLASH_SR_PGERR       (1u<< 2)
#define FLASH_SR_BSY         (1u<< 0)

#define FLASH_CR_EOPIE       (1u<<12)
#define FLASH_CR_ERRIE       (1u<<10)
#define FLASH_CR_OPTWRE      (1u<< 9)
#define FLASH_CR_LOCK        (1u<< 7)
#define FLASH_CR_STRT        (1u<< 6)
#define FLASH_CR_OPTER       (1u<< 5)
#define FLASH_CR_OPTPG       (1u<< 4)
#define FLASH_CR_MER         (1u<< 2)
#define FLASH_CR_PER         (1u<< 1)
#define FLASH_CR_PG          (1u<< 0)

#define FLASH_BASE 0x40022000

/* Reset and clock control */
struct rcc {
    uint32_t cr;       /* 00: Clock control */
    uint32_t cfgr;     /* 04: Clock configuration */
    uint32_t cir;      /* 08: Clock interrupt */
    uint32_t apb2rstr; /* 0C: APB2 peripheral reset */
    uint32_t apb1rstr; /* 10: APB1 peripheral reset */
    uint32_t ahbenr;   /* 14: AHB periphernal clock enable */
    uint32_t apb2enr;  /* 18: APB2 peripheral clock enable */
    uint32_t apb1enr;  /* 1C: APB1 peripheral clock enable */
    uint32_t bdcr;     /* 20: Backup domain control */
    uint32_t csr;      /* 24: Control/status */
    uint32_t ahbstr;   /* 28: AHB peripheral clock reset */
    uint32_t cfgr2;    /* 2C: Clock configuration 2 */
};

#define RCC_CR_PLL3RDY       (1u<<29)
#define RCC_CR_PLL3ON        (1u<<28)
#define RCC_CR_PLL2RDY       (1u<<27)
#define RCC_CR_PLL2ON        (1u<<26)
#define RCC_CR_PLLRDY        (1u<<25)
#define RCC_CR_PLLON         (1u<<24)
#define RCC_CR_CSSON         (1u<<19)
#define RCC_CR_HSEBYP        (1u<<18)
#define RCC_CR_HSERDY        (1u<<17)
#define RCC_CR_HSEON         (1u<<16)
#define RCC_CR_HSIRDY        (1u<<1)
#define RCC_CR_HSION         (1u<<0)

#define RCC_CFGR_PLLMUL(x)   (((x)-2)<<18)
#define RCC_CFGR_PLLXTPRE    (1u<<17)
#define RCC_CFGR_PLLSRC_HSI  (0u<<16)
#define RCC_CFGR_PLLSRC_PREDIV1 (1u<<16)
#define RCC_CFGR_ADCPRE_DIV8 (3u<<14)
#define RCC_CFGR_APB2PSC_2   (4u<<11)
#define RCC_CFGR_APB1PSC_2   (4u<< 8)
#define RCC_CFGR_SWS_HSI     (0u<<2)
#define RCC_CFGR_SWS_HSE     (1u<<2)
#define RCC_CFGR_SWS_PLL     (2u<<2)
#define RCC_CFGR_SWS_MASK    (3u<<2)
#define RCC_CFGR_SW_HSI      (0u<<0)
#define RCC_CFGR_SW_HSE      (1u<<0)
#define RCC_CFGR_SW_PLL      (2u<<0)
#define RCC_CFGR_SW_MASK     (3u<<0)

#define RCC_AHBENR_ETHMACRXEN (1u<<16)
#define RCC_AHBENR_ETHMACTXEN (1u<<15)
#define RCC_AHBENR_ETHMACEN  (1u<<14)
#define RCC_AHBENR_OTGFSEN   (1u<<12)
#define RCC_AHBENR_CRCEN     (1u<< 6)
#define RCC_AHBENR_FLITFEN   (1u<< 4)
#define RCC_AHBENR_SRAMEN    (1u<< 2)
#define RCC_AHBENR_DMA2EN    (1u<< 1)
#define RCC_AHBENR_DMA1EN    (1u<< 0)

#define RCC_APB1ENR_DACEN    (1u<<29)
#define RCC_APB1ENR_PWREN    (1u<<28)
#define RCC_APB1ENR_BKPEN    (1u<<27)
#define RCC_APB1ENR_CAN2EN   (1u<<26)
#define RCC_APB1ENR_CAN1EN   (1u<<25)
#define RCC_APB1ENR_I2C2EN   (1u<<22)
#define RCC_APB1ENR_I2C1EN   (1u<<21)
#define RCC_APB1ENR_USART5EN (1u<<20)
#define RCC_APB1ENR_USART4EN (1u<<19)
#define RCC_APB1ENR_USART3EN (1u<<18)
#define RCC_APB1ENR_USART2EN (1u<<17)
#define RCC_APB1ENR_SPI3EN   (1u<<15)
#define RCC_APB1ENR_SPI2EN   (1u<<14)
#define RCC_APB1ENR_WWDGEN   (1u<<11)
#define RCC_APB1ENR_TIM7EN   (1u<< 5)
#define RCC_APB1ENR_TIM6EN   (1u<< 4)
#define RCC_APB1ENR_TIM5EN   (1u<< 3)
#define RCC_APB1ENR_TIM4EN   (1u<< 2)
#define RCC_APB1ENR_TIM3EN   (1u<< 1)
#define RCC_APB1ENR_TIM2EN   (1u<< 0)

#define RCC_APB2ENR_USART1EN (1u<<14)
#define RCC_APB2ENR_SPI1EN   (1u<<12)
#define RCC_APB2ENR_TIM1EN   (1u<<11)
#define RCC_APB2ENR_ADC2EN   (1u<<10)
#define RCC_APB2ENR_ADC1EN   (1u<< 9)
#define RCC_APB2ENR_IOPFEN   (1u<< 7)
#define RCC_APB2ENR_IOPEEN   (1u<< 6)
#define RCC_APB2ENR_IOPDEN   (1u<< 5)
#define RCC_APB2ENR_IOPCEN   (1u<< 4)
#define RCC_APB2ENR_IOPBEN   (1u<< 3)
#define RCC_APB2ENR_IOPAEN   (1u<< 2)
#define RCC_APB2ENR_AFIOEN   (1u<< 0)

#define RCC_BASE 0x40021000

/* General-purpose I/O */
struct gpio {
    uint32_t crl;  /* 00: Port configuration low */
    uint32_t crh;  /* 04: Port configuration high */
    uint32_t idr;  /* 08: Port input data */
    uint32_t odr;  /* 0C: Port output data */
    uint32_t bsrr; /* 10: Port bit set/reset */
    uint32_t brr;  /* 14: Port bit reset */
    uint32_t lckr; /* 18: Port configuration lock */
};

#define _GPI_pulled(level) (0x8u|((level)<<4))
#define GPI_analog    0x0u
#define GPI_floating  0x4u
#define GPI_pull_down _GPI_pulled(LOW)
#define GPI_pull_up   _GPI_pulled(HIGH)

#define GPO_pushpull(speed,level)  (0x0u|(speed)|((level)<<4))
#define GPO_opendrain(speed,level) (0x4u|(speed)|((level)<<4))
#define AFO_pushpull(speed)        (0x8u|(speed))
#define _AFO_pushpull(speed,level) (0x8u|(speed)|((level)<<4))
#define AFO_opendrain(speed)       (0xcu|(speed))
#define _2MHz  2
#define _10MHz 1
#define _50MHz 3
#define LOW  0
#define HIGH 1

#define GPIOA_BASE 0x40010800
#define GPIOB_BASE 0x40010c00
#define GPIOC_BASE 0x40011000
#define GPIOD_BASE 0x40011400
#define GPIOE_BASE 0x40011800
#define GPIOF_BASE 0x40011c00
#define GPIOG_BASE 0x40012000

/* Alternative-function I/O */
struct afio {
    uint32_t evcr;       /* 00: Event control */
    uint32_t mapr;       /* 04: AF remap and debug I/O configuration */
    uint32_t exticr[4];  /* 08-14: External interrupt configuration #1-4 */
    uint32_t rsvd;       /* 18: - */
    uint32_t mapr2;      /* 1C: AF remap and debug I/O configuration #2 */
};

#define AFIO_MAPR_SWJ_CFG_ENABLED      (2u<<24)
#define AFIO_MAPR_SWJ_CFG_DISABLED     (4u<<24)
#define AFIO_MAPR_TIM4_REMAP_FULL      (1u<<12)
#define AFIO_MAPR_TIM3_REMAP_FULL      (3u<<10)
#define AFIO_MAPR_TIM3_REMAP_PARTIAL   (2u<<10)
#define AFIO_MAPR_TIM2_REMAP_FULL      (3u<< 8)
#define AFIO_MAPR_TIM2_REMAP_PARTIAL_1 (1u<< 8)
#define AFIO_MAPR_TIM2_REMAP_PARTIAL_2 (2u<< 8)
#define AFIO_MAPR_TIM1_REMAP_FULL      (3u<< 6)
#define AFIO_MAPR_TIM1_REMAP_PARTIAL   (1u<< 6)
#define AFIO_MAPR_USART3_REMAP_FULL    (3u<< 4)
#define AFIO_MAPR_USART3_REMAP_PARTIAL (1u<< 4)

#define AFIO_BASE 0x40010000

/* EXTI */
#define EXTI_BASE 0x40010400

/* DMA */
#define DMA1_CH1_IRQ 11
#define DMA1_CH2_IRQ 12
#define DMA1_CH3_IRQ 13
#define DMA1_CH4_IRQ 14
#define DMA1_CH5_IRQ 15
#define DMA1_CH6_IRQ 16
#define DMA1_CH7_IRQ 17

#define DMA1_BASE 0x40020000
#define DMA2_BASE 0x40020400

/* Timer */
#define TIM1_BASE 0x40012c00
#define TIM2_BASE 0x40000000
#define TIM3_BASE 0x40000400
#define TIM4_BASE 0x40000800
#define TIM5_BASE 0x40000c00
#define TIM6_BASE 0x40001000
#define TIM7_BASE 0x40001400

/* I2C */
struct i2c {
    uint32_t cr1;     /* 00: Control 1 */
    uint32_t cr2;     /* 04: Control 2 */
    uint32_t oar1;    /* 08: Own address 1 */
    uint32_t oar2;    /* 0C: Own address 2 */
    uint32_t dr;      /* 10: Data */
    uint32_t sr1;     /* 14: Status 1 */
    uint32_t sr2;     /* 18: Status 2 */
    uint32_t ccr;     /* 1C: Clock control */
    uint32_t trise;   /* 20: Rise time */
};

#define I2C_CR1_SWRST     (1u<<15)
#define I2C_CR1_ALERT     (1u<<13)
#define I2C_CR1_PEC       (1u<<12)
#define I2C_CR1_POS       (1u<<11)
#define I2C_CR1_ACK       (1u<<10)
#define I2C_CR1_STOP      (1u<< 9)
#define I2C_CR1_START     (1u<< 8)
#define I2C_CR1_NOSTRETCH (1u<< 7)
#define I2C_CR1_ENGC      (1u<< 6)
#define I2C_CR1_ENPEC     (1u<< 5)
#define I2C_CR1_ENARP     (1u<< 4)
#define I2C_CR1_SMBTYPE   (1u<< 3)
#define I2C_CR1_SMBUS     (1u<< 1)
#define I2C_CR1_PE        (1u<< 0)

#define I2C_CR2_LAST      (1u<<12)
#define I2C_CR2_DMAEN     (1u<<11)
#define I2C_CR2_ITBUFEN   (1u<<10)
#define I2C_CR2_ITEVTEN   (1u<< 9)
#define I2C_CR2_ITERREN   (1u<< 8)
#define I2C_CR2_FREQ(x)   (x)

#define I2C_SR1_SMBALERT  (1u<<15)
#define I2C_SR1_TIMEOUT   (1u<<14)
#define I2C_SR1_PECERR    (1u<<12)
#define I2C_SR1_OVR       (1u<<11)
#define I2C_SR1_AF        (1u<<10)
#define I2C_SR1_ARLO      (1u<< 9)
#define I2C_SR1_BERR      (1u<< 8)
#define I2C_SR1_ERRORS    0xdf00
#define I2C_SR1_TXE       (1u<< 7)
#define I2C_SR1_RXNE      (1u<< 6)
#define I2C_SR1_STOPF     (1u<< 4)
#define I2C_SR1_ADD10     (1u<< 3)
#define I2C_SR1_BTF       (1u<< 2)
#define I2C_SR1_ADDR      (1u<< 1)
#define I2C_SR1_SB        (1u<< 0)
#define I2C_SR1_EVENTS    0x001f

#define I2C_SR2_PEC(x)    ((x)<<15)
#define I2C_SR2_DUALF     (1u<< 7)
#define I2C_SR2_SMBHOST   (1u<< 6)
#define I2C_SR2_SMBDEFAULT (1u<< 5)
#define I2C_SR2_GENCALL   (1u<< 4)
#define I2C_SR2_TRA       (1u<< 2)
#define I2C_SR2_BUSY      (1u<< 1)
#define I2C_SR2_MSL       (1u<< 0)

#define I2C_CCR_FS        (1u<<15)
#define I2C_CCR_DUTY      (1u<<14)
#define I2C_CCR_CCR(x)    (x)

#define I2C1_BASE 0x40005400
#define I2C2_BASE 0x40005800

/* USART */
#define USART1_BASE 0x40013800
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
