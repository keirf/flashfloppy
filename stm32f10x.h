/*
 * stm32f10x.h
 * 
 * Core and peripheral register definitions.
 */

#ifndef __STM32F10X_H__
#define __STM32F10X_H__

#include <stdint.h>

/* System control block */
struct scb {
    uint32_t cpuid;    /* CPUID base */
    uint32_t icsr;     /* Interrupt control and state */
    uint32_t vtor;     /* Vector table offset */
    uint32_t aircr;    /* Application interrupt and reset control */
    uint32_t scr;      /* System control */
    uint32_t ccr;      /* Configuration and control */
    uint32_t shpr1;    /* System handler priority reg #1 */
    uint32_t shpr2;    /* system handler priority reg #2 */
    uint32_t shpr3;    /* System handler priority reg #3 */
    uint32_t shcrs;    /* System handler control and state */
    uint32_t cfsr;     /* Configurable fault status */
    uint32_t hfsr;     /* Hard fault status */
    uint32_t _unused;
    uint32_t mmar;     /* Memory management fault address */
    uint32_t bfar;     /* Bus fault address */
};

#define SCB_AIRCR_VECTKEY     (0x05fau<<16)
#define SCB_AIRCR_SYSRESETREQ (1u<<2)

#define SCB_BASE 0xe000ed00
static volatile struct scb * const scb = (struct scb *)SCB_BASE;

/* Reset and clock control */
struct rcc {
    uint32_t cr;       /* Clock control */
    uint32_t cfgr;     /* Clock configuration */
    uint32_t cir;      /* Clock interrupt */
    uint32_t apb2rstr; /* APB2 peripheral reset */
    uint32_t apb1rstr; /* APB1 peripheral reset */
    uint32_t ahbenr;   /* AHB periphernal clock enable */
    uint32_t apb2enr;  /* APB2 peripheral clock enable */
    uint32_t apb1enr;  /* APB1 peripheral clock enable */
    uint32_t bdcr;     /* Backup domain control */
    uint32_t csr;      /* Control/status */
    uint32_t ahbstr;   /* AHB peripheral clock reset */
    uint32_t cfgr2;    /* Clock configuration 2 */
};

#define RCC_BASE 0x40021000
static volatile struct rcc * const rcc = (struct rcc *)RCC_BASE;

/* General-purpose I/O */
struct gpio {
    uint32_t crl;  /* Port configuration low */
    uint32_t crh;  /* Port configuration high */
    uint32_t idr;  /* Port input data */
    uint32_t odr;  /* Port output data */
    uint32_t bsrr; /* Port bit set/reset */
    uint32_t brr;  /* Port bit reset */
    uint32_t lckr; /* Port configuration lock */
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
    uint32_t sr;   /* Status */
    uint32_t dr;   /* Data */
    uint32_t brr;  /* Baud rate */
    uint32_t cr1;  /* Control 1 */
    uint32_t cr2;  /* Control 2 */
    uint32_t cr3;  /* Control 3 */
    uint32_t gtpr; /* Guard time and prescaler */
};

#define USART1_BASE 0x40013800
static volatile struct usart * const usart1 = (struct usart *)USART1_BASE;

#endif /* __STM32F10X_H__ */
