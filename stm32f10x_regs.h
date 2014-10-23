/*
 * stm32f10x_regs.h
 * 
 * Core and peripheral register definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#ifndef __STM32F10X_REGS_H__
#define __STM32F10X_REGS_H__

#include <stdint.h>

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
    uint32_t shcrs;    /* 24: System handler control and state */
    uint32_t cfsr;     /* 28: Configurable fault status */
    uint32_t hfsr;     /* 2C: Hard fault status */
    uint32_t _unused;  /* 30: - */
    uint32_t mmar;     /* 34: Memory management fault address */
    uint32_t bfar;     /* 38: Bus fault address */
};

#define SCB_AIRCR_VECTKEY     (0x05fau<<16)
#define SCB_AIRCR_SYSRESETREQ (1u<<2)

#define SCB_BASE 0xe000ed00

/* Power control */
struct pwr {
    uint32_t cr;       /* 00: Power control */
    uint32_t csr;      /* 04: Power control/status */
};

#define PWR_BASE 0x40007000

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
#define RCC_CFGR_PPRE1_DIV2  (4u<<8)
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
#define RCC_APB1ENR_UART5EN  (1u<<20)
#define RCC_APB1ENR_UART4EN  (1u<<19)
#define RCC_APB1ENR_UART3EN  (1u<<18)
#define RCC_APB1ENR_UART2EN  (1u<<17)
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
#define RCC_APB2ENR_SP1EN    (1u<<12)
#define RCC_APB2ENR_TIM1EN   (1u<<11)
#define RCC_APB2ENR_ADC2EN   (1u<<10)
#define RCC_APB2ENR_ADC1EN   (1u<< 9)
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

#define GPI_analog    0x0u
#define GPI_floating  0x4u
#define GPI_pulled    0x8u
/* NB. Outputs have slew rate set up for 10MHz. */
#define GPO_pushpull  0x1u
#define GPO_opendrain 0x5u
#define AFO_pushpull  0x9u
#define AFO_opendrain 0xdu

#define GPIOA_BASE 0x40010800
#define GPIOB_BASE 0x40010c00
#define GPIOC_BASE 0x40011000
#define GPIOD_BASE 0x40011400
#define GPIOE_BASE 0x40011800
#define GPIOF_BASE 0x40011c00
#define GPIOG_BASE 0x40012000

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
    struct dma_chn ch1;  /* 08: Channel 1 */
    struct dma_chn ch2;  /* 1C: Channel 2 */
    struct dma_chn ch3;  /* 30: Channel 3 */
    struct dma_chn ch4;  /* 44: Channel 4 */
    struct dma_chn ch5;  /* 58: Channel 5 */
    struct dma_chn ch6;  /* 6C: Channel 6 */
    struct dma_chn ch7;  /* 80: Channel 7 */
};

#define DMA_ISR_TEIF7        (1u<<27)
#define DMA_ISR_HTIF7        (1u<<26)
#define DMA_ISR_TCIF7        (1u<<25)
#define DMA_ISR_GIF7         (1u<<24)
#define DMA_ISR_TEIF6        (1u<<23)
#define DMA_ISR_HTIF6        (1u<<22)
#define DMA_ISR_TCIF6        (1u<<21)
#define DMA_ISR_GIF6         (1u<<20)
#define DMA_ISR_TEIF5        (1u<<19)
#define DMA_ISR_HTIF5        (1u<<18)
#define DMA_ISR_TCIF5        (1u<<17)
#define DMA_ISR_GIF5         (1u<<16)
#define DMA_ISR_TEIF4        (1u<<15)
#define DMA_ISR_HTIF4        (1u<<14)
#define DMA_ISR_TCIF4        (1u<<13)
#define DMA_ISR_GIF4         (1u<<12)
#define DMA_ISR_TEIF3        (1u<<11)
#define DMA_ISR_HTIF3        (1u<<10)
#define DMA_ISR_TCIF3        (1u<< 9)
#define DMA_ISR_GIF3         (1u<< 8)
#define DMA_ISR_TEIF2        (1u<< 7)
#define DMA_ISR_HTIF2        (1u<< 6)
#define DMA_ISR_TCIF2        (1u<< 5)
#define DMA_ISR_GIF2         (1u<< 4)
#define DMA_ISR_TEIF1        (1u<< 3)
#define DMA_ISR_HTIF1        (1u<< 2)
#define DMA_ISR_TCIF1        (1u<< 1)
#define DMA_ISR_GIF1         (1u<< 0)

#define DMA_IFCR_CTEIF7      (1u<<27)
#define DMA_IFCR_CHTIF7      (1u<<26)
#define DMA_IFCR_CTCIF7      (1u<<25)
#define DMA_IFCR_CGIF7       (1u<<24)
#define DMA_IFCR_CTEIF6      (1u<<23)
#define DMA_IFCR_CHTIF6      (1u<<22)
#define DMA_IFCR_CTCIF6      (1u<<21)
#define DMA_IFCR_CGIF6       (1u<<20)
#define DMA_IFCR_CTEIF5      (1u<<19)
#define DMA_IFCR_CHTIF5      (1u<<18)
#define DMA_IFCR_CTCIF5      (1u<<17)
#define DMA_IFCR_CGIF5       (1u<<16)
#define DMA_IFCR_CTEIF4      (1u<<15)
#define DMA_IFCR_CHTIF4      (1u<<14)
#define DMA_IFCR_CTCIF4      (1u<<13)
#define DMA_IFCR_CGIF4       (1u<<12)
#define DMA_IFCR_CTEIF3      (1u<<11)
#define DMA_IFCR_CHTIF3      (1u<<10)
#define DMA_IFCR_CTCIF3      (1u<< 9)
#define DMA_IFCR_CGIF3       (1u<< 8)
#define DMA_IFCR_CTEIF2      (1u<< 7)
#define DMA_IFCR_CHTIF2      (1u<< 6)
#define DMA_IFCR_CTCIF2      (1u<< 5)
#define DMA_IFCR_CGIF2       (1u<< 4)
#define DMA_IFCR_CTEIF1      (1u<< 3)
#define DMA_IFCR_CHTIF1      (1u<< 2)
#define DMA_IFCR_CTCIF1      (1u<< 1)
#define DMA_IFCR_CGIF1       (1u<< 0)

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

#define DMA1_BASE 0x40020000
#define DMA2_BASE 0x40020400

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

#define TIM1_BASE 0x40012c00
#define TIM2_BASE 0x40000000
#define TIM3_BASE 0x40000400
#define TIM4_BASE 0x40000800
#define TIM5_BASE 0x40000c00
#define TIM6_BASE 0x40001000
#define TIM7_BASE 0x40001400

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

#define USART1_BASE 0x40013800

#endif /* __STM32F10X_REGS_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
