/*
 * stm32f10x.h
 * 
 * Core and peripheral register definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit unlicense.org.
 */

#ifndef __STM32F10X_H__
#define __STM32F10X_H__

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
static volatile struct scb * const scb = (struct scb *)SCB_BASE;

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

#define RCC_BASE 0x40021000
static volatile struct rcc * const rcc = (struct rcc *)RCC_BASE;

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

#define GPIOA_BASE 0x40010800
#define GPIOB_BASE 0x40010c00
#define GPIOC_BASE 0x40011000
#define GPIOD_BASE 0x40011400
#define GPIOE_BASE 0x40011800
#define GPIOF_BASE 0x40011c00
#define GPIOG_BASE 0x40012000
static volatile struct gpio * const gpioa = (struct gpio *)GPIOA_BASE;
static volatile struct gpio * const gpiob = (struct gpio *)GPIOB_BASE;
static volatile struct gpio * const gpioc = (struct gpio *)GPIOC_BASE;
static volatile struct gpio * const gpiod = (struct gpio *)GPIOD_BASE;
static volatile struct gpio * const gpioe = (struct gpio *)GPIOE_BASE;
static volatile struct gpio * const gpiof = (struct gpio *)GPIOF_BASE;
static volatile struct gpio * const gpiog = (struct gpio *)GPIOG_BASE;

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

#define USART1_BASE 0x40013800
static volatile struct usart * const usart1 = (struct usart *)USART1_BASE;

#endif /* __STM32F10X_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
