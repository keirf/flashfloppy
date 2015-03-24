/*
 * usb_hcd.c
 * 
 * STM32F105xx/STM32F107xx USB OTG Host Controller Driver.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define USB_IRQ 67
void IRQ_67(void) __attribute__((alias("IRQ_usb")));

struct usb_dev {
    enum { USBSPD_low, USBSPD_full } speed;
    int stage;
    int errcnt;
};

static struct usb_dev root_dev;

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
    usb_otg->hcfg = OTG_HCFG_FSLSPCS_48;
    usb_otg->hfir = 48000;
    usb_otg->hprt = (usb_otg->hprt & ~OTG_HPRT_INTS) | OTG_HPRT_PPWR;
    usb_otg->gccfg = OTG_GCCFG_PWRDWN;
}

static void write_host_channel(uint16_t chn, void *dat, uint16_t sz,
                               uint32_t pid)
{
    volatile uint32_t *fifo = (volatile uint32_t *)(
        (char *)usb_otg + ((chn+1)<<12));
    uint16_t i, mps = 8, nr_packets;
    uint32_t charac, *p = (uint32_t *)dat;

    usb_otg->hc[chn].intsts = ~0u;
    usb_otg->hc[chn].intmsk = ~0u; /* XXX */
    usb_otg->haintmsk = (1u<<chn);
    printk("Enabled %08x %08x\n",
           usb_otg->hc[chn].intsts,
           usb_otg->hc[chn].intmsk);

    nr_packets = (sz + mps - 1) / mps ?: 1;

    charac = (OTG_HCCHAR_DAD(0x00) |
              OTG_HCCHAR_ETYP_CTRL |
              OTG_HCCHAR_EPDIR_OUT |
              OTG_HCCHAR_EPNUM(0x0) |
              OTG_HCCHAR_MPSIZ(mps));
    if (root_dev.speed == USBSPD_low)
        charac |= OTG_HCCHAR_LSDEV;

    usb_otg->hc[chn].charac = charac;
    usb_otg->hc[chn].tsiz = (pid |
                             OTG_HCTSIZ_PKTCNT(nr_packets) |
                             OTG_HCTSIZ_XFRSIZ(sz));
    usb_otg->hc[chn].charac |= OTG_HCCHAR_CHENA;

    for (i = 0; i < (sz+3)/4; i++)
        *fifo = *p++;
}

static void usbdev_get_mps_ep0(void)
{
    struct usb_device_request req = {
        .bmRequestType = USB_DIR_IN | USB_TYPE_STD | USB_RX_DEVICE,
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DESC_DEVICE << 8) | 0,
        .wLength = 8
    };

    write_host_channel(0, &req, sizeof(req), OTG_HCTSIZ_DPID_SETUP);
}

static void usbdev_rx_mps_ep0(uint16_t chn)
{
    uint16_t mps = 8, nr_packets, sz;
    uint32_t charac;

    nr_packets = 1;
    sz = 8;

    charac = (OTG_HCCHAR_DAD(0x00) |
              OTG_HCCHAR_ETYP_CTRL |
              OTG_HCCHAR_EPDIR_IN |
              OTG_HCCHAR_EPNUM(0x0) |
              OTG_HCCHAR_MPSIZ(mps));
    if (root_dev.speed == USBSPD_low)
        charac |= OTG_HCCHAR_LSDEV;

    usb_otg->hc[chn].charac = charac;
    usb_otg->hc[chn].tsiz = (OTG_HCTSIZ_DPID_DATA1 |
                             OTG_HCTSIZ_PKTCNT(nr_packets) |
                             OTG_HCTSIZ_XFRSIZ(sz));
    usb_otg->hc[chn].charac |= OTG_HCCHAR_CHENA;
}

static void usbdev_send_status(uint16_t chn)
{
    write_host_channel(chn, NULL, 0, OTG_HCTSIZ_DPID_DATA1);
}

static void chn_halt(uint16_t chn)
{
    usb_otg->hc[chn].charac |= OTG_HCCHAR_CHDIS | OTG_HCCHAR_CHENA;
}

static void port_reset(void)
{
    uint32_t hprt = usb_otg->hprt & ~OTG_HPRT_INTS;
    printk("USB RST\n");
    usb_otg->hprt = hprt | OTG_HPRT_PRST;
    delay_ms(50); /* USB Spec: TDRSTR (Root-port reset time) */
    usb_otg->hprt = hprt;
    delay_ms(10); /* USB Spec: TRSTRCY (Post-reset recovery) */
}

static void next_state(uint16_t chn)
{
    if (root_dev.errcnt == 3) {
        root_dev.errcnt = 0;
        root_dev.stage = 0;
        port_reset();
    }
    printk("STATE %d\n", root_dev.stage);
    switch (root_dev.stage++) {
    case 0:
        usbdev_get_mps_ep0();
        break;
    case 1:
        usbdev_rx_mps_ep0(chn);
        break;
    case 2:
        usbdev_send_status(chn);
        break;
    default:
        break;
    }
}

static void HCINT_xfrc(uint16_t chn)
{
    printk("XFRC %d\n", chn);
    root_dev.errcnt = 0;
    chn_halt(chn);
}

static void HCINT_chh(uint16_t chn)
{
    printk("CHH %d\n", chn);
    next_state(chn);
}

static void HCINT_ack(uint16_t chn)
{
    printk("ACK %d\n", chn);
}

static void HCINT_nak(uint16_t chn)
{
    printk("NAK %d\n", chn);
    root_dev.errcnt = 0;
    root_dev.stage--;
    chn_halt(chn);
}

static void HCINT_txerr(uint16_t chn)
{
    printk("TXERR %d\n", chn);
    root_dev.errcnt++;
    root_dev.stage = 0;
    chn_halt(chn);
}

static void IRQ_usb_channel(uint16_t chn)
{
    uint32_t hcint = usb_otg->hc[chn].intsts & usb_otg->hc[chn].intmsk;
    uint16_t i;
    void (*hnd[])(uint16_t) = {
        [0] = HCINT_xfrc,
        [1] = HCINT_chh,
        [4] = HCINT_nak,
        [5] = HCINT_ack,
        [7] = HCINT_txerr
    };

    usb_otg->hc[chn].intsts = hcint;

    for (i = 0; hcint; i++) {
        if (hcint & 1) {
            if ((i >= ARRAY_SIZE(hnd)) || !hnd[i])
                printk("Bad HCINT %u:%u\n", chn, i);
            else
                (*hnd[i])(chn);
        }
        hcint >>= 1;
    }
}

static void IRQ_usb(void)
{
    uint32_t gintsts = usb_otg->gintsts;

    printk("---\n");

    if (gintsts & OTG_GINT_HPRTINT) {

        /* Interrupt via HPRT: this port mixes set-to-clear IRQs with other 
         * status and r/w control bits. Clear the IRQs via writeback, then 
         * separate IRQs from everything else for further processing. */
        uint32_t hprt_int, hprt = usb_otg->hprt;
        usb_otg->hprt = hprt & ~OTG_HPRT_PENA; /* clears the lines */
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
            if (hprt_int & OTG_HPRT_PENA) {
                uint32_t hcfg = usb_otg->hcfg;
                root_dev.speed = ((hprt & OTG_HPRT_PSPD_MASK)
                                  == OTG_HPRT_PSPD_FULL)
                    ? USBSPD_full : USBSPD_low;
                printk("USB port enabled: %s-speed device attached.\n",
                       (root_dev.speed == USBSPD_low) ? "Low" : "Full");
                if (root_dev.speed == USBSPD_full) {
                    if ((hcfg & OTG_HCFG_FSLSPCS) != OTG_HCFG_FSLSPCS_48) {
                        usb_otg->hcfg = OTG_HCFG_FSLSPCS_48;
                        usb_otg->hfir = 48000;
                        hprt_int &= ~OTG_HPRT_PENA;
                    }
                } else {
                    if ((hcfg & OTG_HCFG_FSLSPCS) != OTG_HCFG_FSLSPCS_6) {
                        usb_otg->hcfg = OTG_HCFG_FSLSPCS_6;
                        usb_otg->hfir = 6000;
                        hprt_int &= ~OTG_HPRT_PENA;
                    }
                }
                if (hprt_int & OTG_HPRT_PENA) {
                    root_dev.stage = 0;
                    next_state(0);
                }
            } else {
                printk("USB port disabled.\n");
            }
        }

        if (!(hprt_int & OTG_HPRT_PENA)) {
            if (hprt & OTG_HPRT_PCSTS) {
                delay_ms(100); /* USB Spec: TATTDB (Debounce interval) */
                port_reset();
            }
        }
    }

    if (gintsts & OTG_GINT_HCINT) {
        uint16_t chn, haint = usb_otg->haint & usb_otg->haintmsk;
        for (chn = 0; haint; chn++) {
            if (haint & 1)
                IRQ_usb_channel(chn);
            haint >>= 1;
        }
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
        uint32_t rxsts = usb_otg->grxstsp;
        printk("Rx FIFO non-empty %08x.\n", rxsts);
        if (OTG_RXSTS_PKTSTS(rxsts) == OTG_RXSTS_PKTSTS_IN) {
            volatile uint32_t *fifo = (volatile uint32_t *)(
                (char *)usb_otg + (1<<12));
            uint16_t i, sz = OTG_RXSTS_BCNT(rxsts);
            for (i = 0; i < (sz+3)/4; i++)
                printk("%08x ", *fifo);
            printk("\n");
        }
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
