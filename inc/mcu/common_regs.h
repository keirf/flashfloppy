/*
 * common_regs.h
 * 
 * Core and peripheral register definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* SysTick timer */
struct stk {
    uint32_t ctrl;     /* 00: Control and status */
    uint32_t load;     /* 04: Reload value */
    uint32_t val;      /* 08: Current value */
    uint32_t calib;    /* 0C: Calibration value */
};

#define STK_CTRL_COUNTFLAG  (1u<<16)
#define STK_CTRL_CLKSOURCE  (1u<< 2)
#define STK_CTRL_TICKINT    (1u<< 1)
#define STK_CTRL_ENABLE     (1u<< 0)

#define STK_MASK            ((1u<<24)-1)

#define STK_BASE 0xe000e010

/* System control block */
struct scb {
    uint32_t cpuid;    /* 00: CPUID base */
    uint32_t icsr;     /* 04: Interrupt control and state */
    uint32_t vtor;     /* 08: Vector table offset */
    uint32_t aircr;    /* 0C: Application interrupt and reset control */
    uint32_t scr;      /* 10: System control */
    uint32_t ccr;      /* 14: Configuration and control */
    uint32_t shpr1;    /* 18: System handler priority reg #1 */
    uint32_t shpr2;    /* 1C: system handler priority reg #2 */
    uint32_t shpr3;    /* 20: System handler priority reg #3 */
    uint32_t shcsr;    /* 24: System handler control and state */
    uint32_t cfsr;     /* 28: Configurable fault status */
    uint32_t hfsr;     /* 2C: Hard fault status */
    uint32_t _unused;  /* 30: - */
    uint32_t mmar;     /* 34: Memory management fault address */
    uint32_t bfar;     /* 38: Bus fault address */
};

#define SCB_CCR_STKALIGN       (1u<<9)
#define SCB_CCR_BFHFNMIGN      (1u<<8)
#define SCB_CCR_DIV_0_TRP      (1u<<4)
#define SCB_CCR_UNALIGN_TRP    (1u<<3)
#define SCB_CCR_USERSETMPEND   (1u<<1)
#define SCB_CCR_NONBASETHRDENA (1u<<0)

#define SCB_SHCSR_USGFAULTENA    (1u<<18)
#define SCB_SHCSR_BUSFAULTENA    (1u<<17)
#define SCB_SHCSR_MEMFAULTENA    (1u<<16)
#define SCB_SHCSR_SVCALLPENDED   (1u<<15)
#define SCB_SHCSR_BUSFAULTPENDED (1u<<14)
#define SCB_SHCSR_MEMFAULTPENDED (1u<<13)
#define SCB_SHCSR_USGFAULTPENDED (1u<<12)
#define SCB_SHCSR_SYSTICKACT     (1u<<11)
#define SCB_SHCSR_PENDSVACT      (1u<<10)
#define SCB_SHCSR_MONITORACT     (1u<< 8)
#define SCB_SHCSR_SVCALLACT      (1u<< 7)
#define SCB_SHCSR_USGFAULTACT    (1u<< 3)
#define SCB_SHCSR_BUSFAULTACT    (1u<< 1)
#define SCB_SHCSR_MEMFAULTACT    (1u<< 0)

#define SCB_CFSR_DIVBYZERO     (1u<<25)
#define SCB_CFSR_UNALIGNED     (1u<<24)
#define SCB_CFSR_NOCP          (1u<<19)
#define SCB_CFSR_INVPC         (1u<<18)
#define SCB_CFSR_INVSTATE      (1u<<17)
#define SCB_CFSR_UNDEFINSTR    (1u<<16)
#define SCB_CFSR_BFARVALID     (1u<<15)
#define SCB_CFSR_STKERR        (1u<<12)
#define SCB_CFSR_UNSTKERR      (1u<<11)
#define SCB_CFSR_IMPRECISERR   (1u<<10)
#define SCB_CFSR_PRECISERR     (1u<< 9)
#define SCB_CFSR_IBUSERR       (1u<< 8)
#define SCB_CFSR_MMARVALID     (1u<< 7)
#define SCB_CFSR_MSTKERR       (1u<< 4)
#define SCB_CFSR_MUNSTKERR     (1u<< 3)
#define SCB_CFSR_DACCVIOL      (1u<< 1)
#define SCB_CFSR_IACCVIOL      (1u<< 0)

#define SCB_AIRCR_VECTKEY     (0x05fau<<16)
#define SCB_AIRCR_SYSRESETREQ (1u<<2)

#define SCB_BASE 0xe000ed00

/* Nested vectored interrupt controller */
struct nvic {
    uint32_t iser[32]; /*  00: Interrupt set-enable */
    uint32_t icer[32]; /*  80: Interrupt clear-enable */
    uint32_t ispr[32]; /* 100: Interrupt set-pending */
    uint32_t icpr[32]; /* 180: Interrupt clear-pending */
    uint32_t iabr[64]; /* 200: Interrupt active */
    uint8_t ipr[100];  /* 300: Interrupt priority */
};

#define NVIC_BASE 0xe000e100

struct dbg {
    uint32_t mcu_idcode; /* 00: MCU ID code */
    uint32_t mcu_cr;     /* 04: Debug MCU configuration */
};

#define DBG_BASE 0xe0042000

/* Power control */
struct pwr {
    uint32_t cr;       /* 00: Power control */
    uint32_t csr;      /* 04: Power control/status */
};

#define PWR_CR_DBP           (1u<< 8)

#define PWR_BASE 0x40007000

/* Backup */
struct bkp {
    uint32_t _0[1];    /* 00: - */
    uint32_t dr1[10];  /* 04-28: Data block #1 */
    uint32_t rtccr;    /* 2C: RTC clock calibration */
    uint32_t cr;       /* 30: Control */
    uint32_t csr;      /* 34: Control/status */
    uint32_t _1[2];    /* 38-3C: - */
    uint32_t dr2[32];  /* 40-BC: Data block #2 */
};

#define BKP_BASE 0x40006c00

/* EXTI */
struct exti {
    uint32_t imr;        /* 00: Interrupt mask */
    uint32_t emr;        /* 04: Event mask */
    uint32_t rtsr;       /* 08: Rising trigger selection */
    uint32_t ftsr;       /* 0C: Falling trigger selection */
    uint32_t swier;      /* 10: Software interrupt event */
    uint32_t pr;         /* 14: Pending */
};

/* DMA */
struct dma_chn {
    uint32_t ccr;        /* +00: Configuration */
    uint32_t cndtr;      /* +04: Number of data */
    uint32_t cpar;       /* +08: Peripheral address */
    uint32_t cmar;       /* +0C: Memory address */
    uint32_t rsvd;       /* +10: - */
};
struct dma {
    uint32_t isr;        /* 00: Interrupt status */
    uint32_t ifcr;       /* 04: Interrupt flag clear */
    struct dma_chn ch[7];
};

/* n=1..7 */
#define DMA_ISR_TEIF(n)      (8u<<(((n)-1)*4))
#define DMA_ISR_HTIF(n)      (4u<<(((n)-1)*4))
#define DMA_ISR_TCIF(n)      (2u<<(((n)-1)*4))
#define DMA_ISR_GIF(n)       (1u<<(((n)-1)*4))

/* n=1..7 */
#define DMA_IFCR_CTEIF(n)    (8u<<(((n)-1)*4))
#define DMA_IFCR_CHTIF(n)    (4u<<(((n)-1)*4))
#define DMA_IFCR_CTCIF(n)    (2u<<(((n)-1)*4))
#define DMA_IFCR_CGIF(n)     (1u<<(((n)-1)*4))

#define DMA_CCR_MEM2MEM      (1u<<14)
#define DMA_CCR_PL_LOW       (0u<<12)
#define DMA_CCR_PL_MEDIUM    (1u<<12)
#define DMA_CCR_PL_HIGH      (2u<<12)
#define DMA_CCR_PL_V_HIGH    (3u<<12)
#define DMA_CCR_MSIZE_8BIT   (0u<<10)
#define DMA_CCR_MSIZE_16BIT  (1u<<10)
#define DMA_CCR_MSIZE_32BIT  (2u<<10)
#define DMA_CCR_PSIZE_8BIT   (0u<< 8)
#define DMA_CCR_PSIZE_16BIT  (1u<< 8)
#define DMA_CCR_PSIZE_32BIT  (2u<< 8)
#define DMA_CCR_MINC         (1u<< 7)
#define DMA_CCR_PINC         (1u<< 6)
#define DMA_CCR_CIRC         (1u<< 5)
#define DMA_CCR_DIR_P2M      (0u<< 4)
#define DMA_CCR_DIR_M2P      (1u<< 4)
#define DMA_CCR_TEIE         (1u<< 3)
#define DMA_CCR_HTIE         (1u<< 2)
#define DMA_CCR_TCIE         (1u<< 1)
#define DMA_CCR_EN           (1u<< 0)

/* Timer */
struct tim {
    uint32_t cr1;   /* 00: Control 1 */
    uint32_t cr2;   /* 04: Control 2 */
    uint32_t smcr;  /* 08: Slave mode control */
    uint32_t dier;  /* 0C: DMA/interrupt enable */
    uint32_t sr;    /* 10: Status */
    uint32_t egr;   /* 14: Event generation */
    uint32_t ccmr1; /* 18: Capture/compare mode 1 */
    uint32_t ccmr2; /* 1C: Capture/compare mode 2 */
    uint32_t ccer;  /* 20: Capture/compare enable */
    uint32_t cnt;   /* 24: Counter */
    uint32_t psc;   /* 28: Prescaler */
    uint32_t arr;   /* 2C: Auto-reload */
    uint32_t rcr;   /* 30: Repetition counter */
    uint32_t ccr1;  /* 34: Capture/compare 1 */
    uint32_t ccr2;  /* 38: Capture/compare 2 */
    uint32_t ccr3;  /* 3C: Capture/compare 3 */
    uint32_t ccr4;  /* 40: Capture/compare 4 */
    uint32_t bdtr;  /* 44: Break and dead-time */
    uint32_t dcr;   /* 48: DMA control */
    uint32_t dmar;  /* 4C: DMA address for full transfer */
};

#define TIM_CR1_ARPE         (1u<<7)
#define TIM_CR1_DIR          (1u<<4)
#define TIM_CR1_OPM          (1u<<3)
#define TIM_CR1_URS          (1u<<2)
#define TIM_CR1_UDIS         (1u<<1)
#define TIM_CR1_CEN          (1u<<0)

#define TIM_CR2_TI1S         (1u<<7)
#define TIM_CR2_CCDS         (1u<<3)

#define TIM_DIER_TDE         (1u<<14)
#define TIM_DIER_CC4DE       (1u<<12)
#define TIM_DIER_CC3DE       (1u<<11)
#define TIM_DIER_CC2DE       (1u<<10)
#define TIM_DIER_CC1DE       (1u<<9)
#define TIM_DIER_UDE         (1u<<8)
#define TIM_DIER_TIE         (1u<<6)
#define TIM_DIER_CC4IE       (1u<<4)
#define TIM_DIER_CC3IE       (1u<<3)
#define TIM_DIER_CC2IE       (1u<<2)
#define TIM_DIER_CC1IE       (1u<<1)
#define TIM_DIER_UIE         (1u<<0)

#define TIM_SR_CC4OF         (1u<<12)
#define TIM_SR_CC3OF         (1u<<11)
#define TIM_SR_CC2OF         (1u<<10)
#define TIM_SR_CC1OF         (1u<<9)
#define TIM_SR_TIF           (1u<<6)
#define TIM_SR_CC4IF         (1u<<4)
#define TIM_SR_CC3IF         (1u<<3)
#define TIM_SR_CC2IF         (1u<<2)
#define TIM_SR_CC1IF         (1u<<1)
#define TIM_SR_UIF           (1u<<0)

#define TIM_EGR_TG           (1u<<6)
#define TIM_EGR_CC4G         (1u<<4)
#define TIM_EGR_CC3G         (1u<<3)
#define TIM_EGR_CC2G         (1u<<2)
#define TIM_EGR_CC1G         (1u<<1)
#define TIM_EGR_UG           (1u<<0)

#define TIM_CCMR1_OC2CE      (1u <<15)
#define TIM_CCMR1_OC2M(x)    ((x)<<12)
#define TIM_CCMR1_OC2PE      (1u <<11)
#define TIM_CCMR1_OC2FE      (1u <<10)
#define TIM_CCMR1_CC2S(x)    ((x)<< 8)
#define TIM_CCMR1_OC1CE      (1u << 7)
#define TIM_CCMR1_OC1M(x)    ((x)<< 4)
#define TIM_CCMR1_OC1PE      (1u << 3)
#define TIM_CCMR1_OC1FE      (1u << 2)
#define TIM_CCMR1_CC1S(x)    ((x)<< 0)

#define TIM_CCMR1_IC2F(x)    ((x)<<12)
#define TIM_CCMR1_IC2PSC(x)  ((x)<<10)
#define TIM_CCMR1_IC1F(x)    ((x)<< 4)
#define TIM_CCMR1_IC1PSC(x)  ((x)<< 2)

#define TIM_CCMR2_OC4CE      (1u <<15)
#define TIM_CCMR2_OC4M(x)    ((x)<<12)
#define TIM_CCMR2_OC4PE      (1u <<11)
#define TIM_CCMR2_OC4FE      (1u <<10)
#define TIM_CCMR2_CC4S(x)    ((x)<< 8)
#define TIM_CCMR2_OC3CE      (1u << 7)
#define TIM_CCMR2_OC3M(x)    ((x)<< 4)
#define TIM_CCMR2_OC3PE      (1u << 3)
#define TIM_CCMR2_OC3FE      (1u << 2)
#define TIM_CCMR2_CC3S(x)    ((x)<< 0)

#define TIM_CCMR2_IC4F(x)    ((x)<<12)
#define TIM_CCMR2_IC4PSC(x)  ((x)<<10)
#define TIM_CCMR2_IC3F(x)    ((x)<< 4)
#define TIM_CCMR2_IC3PSC(x)  ((x)<< 2)

#define TIM_OCM_FROZEN       (0u)
#define TIM_OCM_SET_HIGH     (1u)
#define TIM_OCM_SET_LOW      (2u)
#define TIM_OCM_TOGGLE       (3u)
#define TIM_OCM_FORCE_LOW    (4u)
#define TIM_OCM_FORCE_HIGH   (5u)
#define TIM_OCM_PWM1         (6u)
#define TIM_OCM_PWM2         (7u)
#define TIM_OCM_MASK         (7u)

#define TIM_CCS_OUTPUT       (0u)
#define TIM_CCS_INPUT_TI1    (1u)
#define TIM_CCS_INPUT_TI2    (2u)
#define TIM_CCS_INPUT_TRC    (3u)
#define TIM_CCS_MASK         (3u)

#define TIM_CCER_CC4P        (1u<<13)
#define TIM_CCER_CC4E        (1u<<12)
#define TIM_CCER_CC3P        (1u<< 9)
#define TIM_CCER_CC3E        (1u<< 8)
#define TIM_CCER_CC2P        (1u<< 5)
#define TIM_CCER_CC2E        (1u<< 4)
#define TIM_CCER_CC1P        (1u<< 1)
#define TIM_CCER_CC1E        (1u<< 0)

#define TIM_BDTR_MOE         (1u<<15)
#define TIM_BDTR_AOE         (1u<<14)
#define TIM_BDTR_BKP         (1u<<13)
#define TIM_BDTR_BKE         (1u<<12)
#define TIM_BDTR_OSSR        (1u<<11)
#define TIM_BDTR_OSSI        (1u<<10)
#define TIM_BDTR_LOCK(x)     ((x)<<8)
#define TIM_BDTR_DTG(x)      ((x)<<0)

/* SPI/I2S */
struct spi {
    uint32_t cr1;     /* 00: Control 1 */
    uint32_t cr2;     /* 04: Control 2 */
    uint32_t sr;      /* 08: Status */
    uint32_t dr;      /* 0C: Data */
    uint32_t crcpr;   /* 10: CRC polynomial */
    uint32_t rxcrcr;  /* 14: RX CRC */
    uint32_t txcrcr;  /* 18: TX CRC */
    uint32_t i2scfgr; /* 1C: I2S configuration */
    uint32_t i2spr;   /* 20: I2S prescaler */
};

#define SPI_CR1_BIDIMODE  (1u<<15)
#define SPI_CR1_BIDIOE    (1u<<14)
#define SPI_CR1_CRCEN     (1u<<13)
#define SPI_CR1_CRCNEXT   (1u<<12)
#define SPI_CR1_DFF       (1u<<11)
#define SPI_CR1_RXONLY    (1u<<10)
#define SPI_CR1_SSM       (1u<< 9)
#define SPI_CR1_SSI       (1u<< 8)
#define SPI_CR1_LSBFIRST  (1u<< 7)
#define SPI_CR1_SPE       (1u<< 6)
#define SPI_CR1_BR_DIV2   (0u<< 3)
#define SPI_CR1_BR_DIV4   (1u<< 3)
#define SPI_CR1_BR_DIV8   (2u<< 3)
#define SPI_CR1_BR_DIV16  (3u<< 3)
#define SPI_CR1_BR_DIV32  (4u<< 3)
#define SPI_CR1_BR_DIV64  (5u<< 3)
#define SPI_CR1_BR_DIV128 (6u<< 3)
#define SPI_CR1_BR_DIV256 (7u<< 3)
#define SPI_CR1_BR_MASK   (7u<< 3)
#define SPI_CR1_MSTR      (1u<< 2)
#define SPI_CR1_CPOL      (1u<< 1)
#define SPI_CR1_CPHA      (1u<< 0)

#define SPI_CR2_TXEIE     (1u<< 7)
#define SPI_CR2_RXNEIE    (1u<< 6)
#define SPI_CR2_ERRIE     (1u<< 5)
#define SPI_CR2_SSOE      (1u<< 2)
#define SPI_CR2_TXDMAEN   (1u<< 1)
#define SPI_CR2_RXDMAEN   (1u<< 0)

#define SPI_SR_BSY        (1u<< 7)
#define SPI_SR_OVR        (1u<< 6)
#define SPI_SR_MODF       (1u<< 5)
#define SPI_SR_CRCERR     (1u<< 4)
#define SPI_SR_USR        (1u<< 3)
#define SPI_SR_CHSIDE     (1u<< 2)
#define SPI_SR_TXE        (1u<< 1)
#define SPI_SR_RXNE       (1u<< 0)

#define SPI1_BASE 0x40013000
#define SPI2_BASE 0x40003800
#define SPI3_BASE 0x40003C00

/* USART */
struct usart {
    uint32_t sr;   /* 00: Status */
    uint32_t dr;   /* 04: Data */
    uint32_t brr;  /* 08: Baud rate */
    uint32_t cr1;  /* 0C: Control 1 */
    uint32_t cr2;  /* 10: Control 2 */
    uint32_t cr3;  /* 14: Control 3 */
    uint32_t gtpr; /* 18: Guard time and prescaler */
};

#define USART_SR_CTS         (1u<<9)
#define USART_SR_LBD         (1u<<8)
#define USART_SR_TXE         (1u<<7)
#define USART_SR_TC          (1u<<6)
#define USART_SR_RXNE        (1u<<5)
#define USART_SR_IDLE        (1u<<4)
#define USART_SR_ORE         (1u<<3)
#define USART_SR_NE          (1u<<2)
#define USART_SR_FE          (1u<<1)
#define USART_SR_PE          (1u<<0)

#define USART_CR1_UE         (1u<<13)
#define USART_CR1_M          (1u<<12)
#define USART_CR1_WAKE       (1u<<11)
#define USART_CR1_PCE        (1u<<10)
#define USART_CR1_PS         (1u<< 9)
#define USART_CR1_PEIE       (1u<< 8)
#define USART_CR1_TXEIE      (1u<< 7)
#define USART_CR1_TCIE       (1u<< 6)
#define USART_CR1_RXNEIE     (1u<< 5)
#define USART_CR1_IDLEIE     (1u<< 4) 
#define USART_CR1_TE         (1u<< 3)
#define USART_CR1_RE         (1u<< 2)
#define USART_CR1_RWU        (1u<< 1)
#define USART_CR1_SBK        (1u<< 0)

#define USART_CR3_CTSIE      (1u<<10)
#define USART_CR3_CTSE       (1u<< 9)
#define USART_CR3_RTSE       (1u<< 8)
#define USART_CR3_DMAT       (1u<< 7)
#define USART_CR3_DMAR       (1u<< 6)
#define USART_CR3_SCEN       (1u<< 5)
#define USART_CR3_NACK       (1u<< 4)
#define USART_CR3_HDSEL      (1u<< 3)
#define USART_CR3_IRLP       (1u<< 2)
#define USART_CR3_IREN       (1u<< 1)
#define USART_CR3_EIE        (1u<< 0)

/* USB On-The-Go Full Speed interface */
struct usb_otg {
    uint32_t gotctl;   /* 00: Control and status */
    uint32_t gotgint;  /* 04: Interrupt */
    uint32_t gahbcfg;  /* 08: AHB configuration */
    uint32_t gusbcfg;  /* 0C: USB configuration */
    uint32_t grstctl;  /* 10: Reset */
    uint32_t gintsts;  /* 14: Core interrupt */
    uint32_t gintmsk;  /* 18: Interrupt mask */
    uint32_t grxstsr;  /* 1C: Receive status debug read */
    uint32_t grxstsp;  /* 20: Receive status debug pop */
    uint32_t grxfsiz;  /* 24: Receive FIFO size */
    union {
        uint32_t hnptxfsiz;  /* 28: Host non-periodic transmit FIFO size */
        uint32_t dieptxf0;   /* 28: Endpoint 0 transmit FIFO size */
    };
    uint32_t hnptxsts; /* 2C: Non-periodic transmit FIFO/queue status */
    uint32_t _0[2];
    uint32_t gccfg;    /* 38: General core configuration */
    uint32_t cid;      /* 3C: Core ID */
    uint32_t _1[48];
    uint32_t hptxfsiz; /* 100: Host periodic transmit FIFO size */
    uint32_t dieptxf1; /* 104: Device IN endpoint transmit FIFO #1 size */
    uint32_t dieptxf2; /* 108: Device IN endpoint transmit FIFO #2 size */
    uint32_t dieptxf3; /* 10C: Device IN endpoint transmit FIFO #3 size */
    uint32_t _2[188];
    uint32_t hcfg;     /* 400: Host configuration */
    uint32_t hfir;     /* 404: Host frame interval */
    uint32_t hfnum;    /* 408: Host frame number / frame time remaining */
    uint32_t _3[1];    /* 40C: */
    uint32_t hptxsts;  /* 410: Host periodic transmit FIFO / queue status */
    uint32_t haint;    /* 414: Host all channels interrupt status */
    uint32_t haintmsk; /* 418: Host all channels interrupt mask */
    uint32_t _4[9];
    uint32_t hprt;     /* 440: Host port control and status */
    uint32_t _5[47];
    struct {
        uint32_t charac; /* +00: Host channel-x characteristics */
        uint32_t _0[1];
        uint32_t intsts; /* +08: Host channel-x interrupt status */
        uint32_t intmsk; /* +0C: Host channel-x interrupt mask */
        uint32_t tsiz;   /* +10: Host channel x transfer size */
        uint32_t _1[3];
    } hc[8];           /* 500..5E0: */
    uint32_t _6[128];

    uint32_t dcfg;     /* 800: Device configuration */
    uint32_t dctl;     /* 804: Device control */
    uint32_t dsts;     /* 808: Device status */
    uint32_t _7[1];
    uint32_t diepmsk;  /* 810: Device IN endpoint common interrupt mask */
    uint32_t doepmsk;  /* 814: Device OUT endpoint common interrupt mask */
    uint32_t daint;    /* 818: Device all endpoints interrupt status */
    uint32_t daintmsk; /* 81C: Device all endpoints interrupt mask */
    uint32_t _8[2];
    uint32_t dvbusdis; /* 828: Device VBUS discharge time */
    uint32_t dvbuspulse; /* 82C: Device VBUS pulsing time */
    uint32_t _9[1];
    uint32_t diepempmsk; /* 834: Device IN endpoint FIFO empty int. mask */
    uint32_t _10[50];
    struct {
        uint32_t ctl;    /* +00: Device IN endpoint-x control */
        uint32_t _0[1];
        uint32_t intsts; /* +08: Device IN endpoint-x interrupt status */
        uint32_t _1[3];
        uint32_t txfsts; /* +18: Device IN endpoint-x transmit FIFO status */
        uint32_t _2[1];
    } diep[4];         /* 900..960: */
    uint32_t _11[96];
    struct {
        uint32_t ctl;    /* +00: Device OUT endpoint-x control */
        uint32_t _0[1];
        uint32_t intsts; /* +08: Device OUT endpoint-x interrupt status */
        uint32_t _1[1];
        uint32_t tsiz;   /* +10: Device OUT endpoint-x transmit FIFO status */
        uint32_t _2[3];
    } doep[4];         /* B00..B60: */
    uint32_t _12[160];

    uint32_t pcgcctl;  /* E00: Power and clock gating control */
};

#define OTG_GAHBCFG_PTXFELVL (1u<< 8)
#define OTG_GAHBCFG_TXFELVL  (1u<< 7)
#define OTG_GAHBCFG_GINTMSK  (1u<< 0)

#define OTG_GUSBCFG_CTXPKT   (1u<<31)
#define OTG_GUSBCFG_FDMOD    (1u<<30)
#define OTG_GUSBCFG_FHMOD    (1u<<29)
#define OTG_GUSBCFG_TRDT(x)  ((x)<<10)
#define OTG_GUSBCFG_HNPCAP   (1u<< 9)
#define OTG_GUSBCFG_SRPCAP   (1u<< 8)
#define OTG_GUSBCFG_PHYSEL   (1u<< 6)
#define OTG_GUSBCFG_TOCAL(x) ((x)<< 0)

/* GINTSTS and GINTMSK */
#define OTG_GINT_WKUPINT     (1u<<31) /* Host + Device */
#define OTG_GINT_SRQINT      (1u<<30) /* H + D */
#define OTG_GINT_DISCINT     (1u<<29) /* H */
#define OTG_GINT_CIDSCHG     (1u<<28) /* H + D */
#define OTG_GINT_PTXFE       (1u<<26) /* H */
#define OTG_GINT_HCINT       (1u<<25) /* H */
#define OTG_GINT_HPRTINT     (1u<<24) /* H */
#define OTG_GINT_IPXFR       (1u<<21) /* H */
#define OTG_GINT_IISOIXFR    (1u<<20) /* D */
#define OTG_GINT_OEPINT      (1u<<19) /* D */
#define OTG_GINT_IEPINT      (1u<<18) /* D */
#define OTG_GINT_EOPF        (1u<<15) /* D */
#define OTG_GINT_ISOODRP     (1u<<14) /* D */
#define OTG_GINT_ENUMDNE     (1u<<13) /* D */
#define OTG_GINT_USBRST      (1u<<12) /* D */
#define OTG_GINT_USBSUSP     (1u<<11) /* D */
#define OTG_GINT_ESUSP       (1u<<10) /* D */
#define OTG_GINT_GONAKEFF    (1u<< 7) /* D */
#define OTG_GINT_GINAKEFF    (1u<< 6) /* D */
#define OTG_GINT_NPTXFE      (1u<< 5) /* H */
#define OTG_GINT_RXFLVL      (1u<< 4) /* H + D */
#define OTG_GINT_SOF         (1u<< 3) /* H + D */
#define OTG_GINT_OTGINT      (1u<< 2) /* H + D */
#define OTG_GINT_MMIS        (1u<< 1) /* H + D */
#define OTG_GINT_CMOD        (1u<< 0) /* H + D */

#define OTG_RXSTS_PKTSTS_IN  (2u)
#define OTG_RXSTS_PKTSTS(r)  (((r)>>17)&0xf)
#define OTG_RXSTS_BCNT(r)    (((r)>>4)&0x7ff)
#define OTG_RXSTS_CHNUM(r)   ((r)&0xf)

#define OTG_GCCFG_SOFOUTEN   (1u<<20)
#define OTG_GCCFG_VBUSBSEN   (1u<<19)
#define OTG_GCCFG_VBUSASEN   (1u<<18)
#define OTG_GCCFG_PWRDWN     (1u<<16)

#define OTG_HCFG_FSLSS       (1u<<2)
#define OTG_HCFG_FSLSPCS     (3u<<0)
#define OTG_HCFG_FSLSPCS_48  (1u<<0)
#define OTG_HCFG_FSLSPCS_6   (2u<<0)

#define OTG_HPRT_PSPD_FULL   (1u<<17)
#define OTG_HPRT_PSPD_LOW    (2u<<17)
#define OTG_HPRT_PSPD_MASK   (1u<<17) /* read-only */
#define OTG_HPRT_PPWR        (1u<<12)
#define OTG_HPRT_PRST        (1u<< 8)
#define OTG_HPRT_PSUSP       (1u<< 7)
#define OTG_HPRT_PRES        (1u<< 6)
#define OTG_HPRT_POCCHNG     (1u<< 5) /* raises HPRTINT */
#define OTG_HPRT_POCA        (1u<< 4)
#define OTG_HPRT_PENCHNG     (1u<< 3) /* raises HPRTINT */
#define OTG_HPRT_PENA        (1u<< 2)
#define OTG_HPRT_PCDET       (1u<< 1) /* raises HPRTINT */
#define OTG_HPRT_PCSTS       (1u<< 0)
#define OTG_HPRT_INTS (OTG_HPRT_POCCHNG|OTG_HPRT_PENCHNG|OTG_HPRT_PCDET| \
                       OTG_HPRT_PENA) /* PENA is also set-to-clear  */

/* HCINTSTS and HCINTMSK */
#define OTG_HCINT_DTERR      (1u<<10)
#define OTG_HCINT_FRMOR      (1u<< 9)
#define OTG_HCINT_BBERR      (1u<< 8)
#define OTG_HCINT_TXERR      (1u<< 7)
#define OTG_HCINT_NYET       (1u<< 6) /* high-speed only; not STM32F10x */
#define OTG_HCINT_ACK        (1u<< 5)
#define OTG_HCINT_NAK        (1u<< 4)
#define OTG_HCINT_STALL      (1u<< 3)
#define OTG_HCINT_CHH        (1u<< 1)
#define OTG_HCINT_XFRC       (1u<< 0)

#define OTG_HCCHAR_CHENA     (1u<<31)
#define OTG_HCCHAR_CHDIS     (1u<<30)
#define OTG_HCCHAR_ODDFRM    (1u<<29)
#define OTG_HCCHAR_DAD(x)    ((x)<<22)
#define OTG_HCCHAR_MCNT(x)   ((x)<<20)
#define OTG_HCCHAR_ETYP_CTRL (0u<<18)
#define OTG_HCCHAR_ETYP_ISO  (1u<<18)
#define OTG_HCCHAR_ETYP_BULK (2u<<18)
#define OTG_HCCHAR_ETYP_INT  (3u<<18)
#define OTG_HCCHAR_LSDEV     (1u<<17)
#define OTG_HCCHAR_EPDIR_OUT (0u<<15)
#define OTG_HCCHAR_EPDIR_IN  (1u<<15)
#define OTG_HCCHAR_EPNUM(x)  ((x)<<11)
#define OTG_HCCHAR_MPSIZ(x)  ((x)<< 0)

#define OTG_HCTSIZ_DPID_DATA0 (0u<<29)
#define OTG_HCTSIZ_DPID_DATA2 (1u<<29)
#define OTG_HCTSIZ_DPID_DATA1 (2u<<29)
#define OTG_HCTSIZ_DPID_MDATA (3u<<29)
#define OTG_HCTSIZ_DPID_SETUP (3u<<29)
#define OTG_HCTSIZ_PKTCNT(x)  ((x)<<19)
#define OTG_HCTSIZ_XFRSIZ(x)  ((x)<< 0)

#define USB_OTG_BASE 0x50000000

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
