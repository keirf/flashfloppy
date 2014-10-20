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
