/*
 * at32f415_regs.h
 * 
 * Extra registers and features of the AT32F415 that we use, over and above
 * the baseline features of the STM32F105.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define RCC_CFGR_PLLMUL_18 ((uint32_t)0x20040000)
#define RCC_CFGR_USBPSC_3  ((uint32_t)0x08400000)

#define RCC_PLL (&rcc->cfgr2)
#define RCC_PLL_PLLCFGEN  (1u<<31)
#define RCC_PLL_FREF_MASK (7u<<24)
#define RCC_PLL_FREF_8M   (2u<<24)

static volatile uint32_t * const RCC_MISC2 = (uint32_t *)(RCC_BASE + 0x54);
#define RCC_MISC2_AUTOSTEP_EN (3u<< 4)

#define TIM_CR1_PMEN (1u<<10)

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
