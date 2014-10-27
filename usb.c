/*
 * usb.c
 * 
 * USB-On-The-Go in Host Mode.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define USB_IRQ 67
void IRQ_67(void) __attribute__((alias("IRQ_usb")));

#define LOW_SPEED 1

void usb_init(void)
{
    delay_ms(250); /* Get serial client up */

    /* Enable the USB clock. */
    rcc->ahbenr |= RCC_AHBENR_OTGFSEN;

    /* Force host mode. */
    usb_otg->gusbcfg = (OTG_GUSBCFG_FHMOD |
                        OTG_GUSBCFG_TRDT(9) |
                        OTG_GUSBCFG_PHYSEL |
                        OTG_GUSBCFG_TOCAL(0));

    printk(" - Waiting for host mode... ");
    while (!(usb_otg->gintsts & OTG_GINT_CMOD))
        cpu_relax();
    printk("done\n");

    /* FIFOs. */
    usb_otg->grxfsiz = 128;                /* Rx FIFO: 512 bytes */
    usb_otg->hnptxfsiz = (96 << 16) | 128; /* Tx NP FIFO: 384 bytes */
    usb_otg->hptxfsiz = (96 << 16) | 224;  /* TX P FIFO: 384 bytes */

    /* Interrupt config. */
    usb_otg->gahbcfg = OTG_GAHBCFG_GINTMSK;
    usb_otg->gintmsk = (OTG_GINT_HPRTINT | /* Host port */
                        OTG_GINT_HCINT |   /* Host channels */
#if 0
/* Set these as and when we have data to transmit */
                        OTG_GINT_PTXFE |   /* Periodic Tx empty */
                        OTG_GINT_NPTXFE |  /* NP Tx empty */
#endif
                        OTG_GINT_RXFLVL |  /* Rx non-empty */
                        OTG_GINT_MMIS);    /* Mode mismatch */

    /* NVIC setup. */
    IRQx_set_prio(USB_IRQ, 14); /* low-ish */
    IRQx_enable(USB_IRQ);

    /* Turn on full-speed PHY. */
#ifndef LOW_SPEED
    usb_otg->hcfg = OTG_HCFG_FSLSPCS_48;
    usb_otg->hfir = 48000;
#else
    usb_otg->hcfg = 2;
    usb_otg->hfir = 6000;
#endif
    usb_otg->hprt = (usb_otg->hprt & ~OTG_HPRT_INTS) | OTG_HPRT_PPWR;
    usb_otg->gccfg = OTG_GCCFG_PWRDWN;
}

static void enable_host_channel(void)
{
    volatile uint32_t *fifo;
    int i;

#define CHN 0
    usb_otg->hc[CHN].intsts = ~0u;
    usb_otg->hc[CHN].intmsk = ~0u; /* XXX */
    usb_otg->haintmsk = (1u<<CHN);
    printk("Enabled %08x %08x\n",
           usb_otg->hc[CHN].intsts,
           usb_otg->hc[CHN].intmsk);

    usb_otg->hc[CHN].charac = (OTG_HCCHAR_DAD(0x00) |
                               OTG_HCCHAR_ETYP_CTRL |
#ifdef LOW_SPEED
                               OTG_HCCHAR_LSDEV |
#endif
                               OTG_HCCHAR_EPDIR_OUT |
                               OTG_HCCHAR_EPNUM(0x0) |
                               OTG_HCCHAR_MPSIZ(64));
    usb_otg->hc[CHN].tsiz = (OTG_HCTSIZ_DPID_SETUP |
                             OTG_HCTSIZ_PKTCNT(3) |
                             OTG_HCTSIZ_XFRSIZ(3*64));
    usb_otg->hc[CHN].charac |= OTG_HCCHAR_CHENA;

    fifo = (volatile uint32_t *)((char *)usb_otg + 0x1000);
    for (i = 0; i < 3*64/4; i++)
        *fifo = 0xaaaaaaaau;
}

static void IRQ_usb(void)
{
    uint32_t gintsts = usb_otg->gintsts;

    if (gintsts & OTG_GINT_HPRTINT) {

        /* Interrupt via HPRT: this port mixes set-to-clear IRQs with other 
         * status and r/w control bits. Clear the IRQs via writeback, then 
         * separate IRQs from everything else for further processing. */
        uint32_t hprt_int, hprt = usb_otg->hprt;
        usb_otg->hprt = hprt; /* clears the lines */
        hprt_int = hprt & OTG_HPRT_INTS;
        hprt ^= hprt_int;

        {
            static uint32_t xx; uint32_t yy = stk->val;
            printk("HPRT=%08x HCFG=%08x GRSTCTL=%08x GINTSTS=%08x "
                   "GCCFG=%08x +%u us\n",
                   hprt|hprt_int, usb_otg->hcfg,
                   usb_otg->grstctl, usb_otg->gintsts,
                   usb_otg->gccfg, (xx-yy)/9);
            xx = yy;
        }

        if ((hprt_int & OTG_HPRT_POCCHNG) && (hprt & OTG_HPRT_POCA)) {
            /* Shouldn't happen, the core isn't managing V_BUS. */
            printk("USB port over-current condition detected!\n");
        }

        if (hprt_int & OTG_HPRT_PENCHNG) {
            if (hprt & OTG_HPRT_PENA) {
                printk("USB port enabled: %s-speed device attached.\n",
                       (hprt & OTG_HPRT_PSPD_MASK) != OTG_HPRT_PSPD_FULL
                       ? "Low" : "Full");
#ifndef LOW_SPEED
                if ((hprt & OTG_HPRT_PSPD_MASK) == OTG_HPRT_PSPD_FULL)
#endif
                    enable_host_channel();
            } else {
                printk("USB port disabled.\n");
            }
        }

        if ((hprt & (OTG_HPRT_PENA|OTG_HPRT_PCSTS)) == OTG_HPRT_PCSTS) {
            printk("USB RST\n");
            usb_otg->hprt = hprt | OTG_HPRT_PRST;
            delay_ms(10);
            usb_otg->hprt = hprt;
        }
    }

    if (gintsts & OTG_GINT_HCINT) {
        uint32_t hcint = usb_otg->hc[CHN].intsts;
        usb_otg->hc[CHN].intsts = hcint;
        printk("HCINT %08x\n", hcint);
        ASSERT(0);
    }

#if 0
    if (gintsts & OTG_GINT_PTXFE) {
        printk("Periodic Tx FIFO empty.\n");
    }

    if (gintsts & OTG_GINT_NPTXFE) {
        printk("Non-Periodic Tx FIFO empty.\n");
    }
#endif

    if (gintsts & OTG_GINT_RXFLVL) {
        printk("Rx FIFO non-empty.\n");
    }

    if (gintsts & OTG_GINT_MMIS) {
        printk("USB Mode Mismatch\n");
        ASSERT(0);
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
