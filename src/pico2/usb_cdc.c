/*
 * pico2/usb_cdc.c
 *
 * Bare-metal USB CDC-ACM device for the RP2350, providing:
 *  - the debug console over the Pico 2's own USB connector (debug builds
 *    also keep mirroring to UART0 on GPIO 0/1); and
 *  - the Arduino "touch the port at 1200 baud" gesture: opening and then
 *    closing the port with the line rate set to 1200 reboots the board
 *    into BOOTSEL, so a UF2 can be dropped on it without unplugging.
 *
 * Endpoints:
 *   EP0    control, 64 bytes, buffer fixed at DPRAM 0x100
 *   EP1 IN interrupt, 8 bytes: CDC notifications (declared, never used)
 *   EP2    bulk IN/OUT, 64 bytes: the serial data pipe
 *
 * The whole device runs from USBCTRL_IRQ at USB_IRQ_PRI, which is below
 * every floppy-critical interrupt: flux generation is never delayed by
 * USB activity.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define EP_IN  0
#define EP_OUT 1

#define EP_NOTIFY 1
#define EP_DATA   2

#define EP0_MPS 64
#define EPX_MPS 64

/* Packet buffers, as offsets into usb_dpram->data[]. */
#define BUF_NOTIFY 0
#define BUF_OUT    64
#define BUF_IN     128

/* USB standard requests */
#define GET_STATUS        0
#define CLEAR_FEATURE     1
#define SET_FEATURE       3
#define SET_ADDRESS       5
#define GET_DESCRIPTOR    6
#define SET_DESCRIPTOR    7
#define GET_CONFIGURATION 8
#define SET_CONFIGURATION 9
#define GET_INTERFACE     10
#define SET_INTERFACE     11

/* CDC class requests */
#define SET_LINE_CODING        0x20
#define GET_LINE_CODING        0x21
#define SET_CONTROL_LINE_STATE 0x22
#define SEND_BREAK             0x23

#define DESC_DEVICE 1
#define DESC_CONFIG 2
#define DESC_STRING 3

/* Control-transfer phase. */
#define CTL_IDLE       0
#define CTL_IN_DATA    1 /* sending the data stage of a device->host xfer */
#define CTL_OUT_STATUS 2 /* awaiting the zero-length OUT status packet */
#define CTL_OUT_DATA   3 /* receiving the data stage of a host->device xfer */
#define CTL_IN_STATUS  4 /* sending the zero-length IN status packet */

static const uint8_t device_desc[] = {
    18, DESC_DEVICE,
    0x00, 0x02,             /* bcdUSB 2.00 */
    0x02, 0x00, 0x00,       /* Communications class device */
    EP0_MPS,                /* bMaxPacketSize0 */
    0x8a, 0x2e,             /* idVendor: Raspberry Pi */
    0x0a, 0x00,             /* idProduct: CDC UART (as the pico-sdk uses) */
    0x00, 0x01,             /* bcdDevice 1.00 */
    1, 2, 0,                /* iManufacturer, iProduct, iSerialNumber */
    1                       /* bNumConfigurations */
};

static const uint8_t config_desc[] = {
    /* Configuration: 2 interfaces, bus powered, 100mA */
    9, DESC_CONFIG, 67, 0, 2, 1, 0, 0x80, 50,

    /* Interface 0: CDC Communications, Abstract Control Model */
    9, 4, 0, 0, 1, 0x02, 0x02, 0x00, 0,
    /* CDC Header, version 1.10 */
    5, 0x24, 0x00, 0x10, 0x01,
    /* CDC Call Management: no call management, data interface 1 */
    5, 0x24, 0x01, 0x00, 0x01,
    /* CDC ACM: supports Set/Get_Line_Coding, Set_Control_Line_State */
    4, 0x24, 0x02, 0x02,
    /* CDC Union: control interface 0, subordinate interface 1 */
    5, 0x24, 0x06, 0x00, 0x01,
    /* EP1 IN, interrupt, 8 bytes, 16ms */
    7, 5, 0x80|EP_NOTIFY, 0x03, 8, 0, 16,

    /* Interface 1: CDC Data */
    9, 4, 1, 0, 2, 0x0a, 0x00, 0x00, 0,
    /* EP2 OUT, bulk, 64 bytes */
    7, 5, EP_DATA, 0x02, EPX_MPS, 0, 0,
    /* EP2 IN, bulk, 64 bytes */
    7, 5, 0x80|EP_DATA, 0x02, EPX_MPS, 0, 0
};

static const char * const strings[] = {
    [1] = "FlashFloppy",
    [2] = "FlashFloppy Console"
};

/* Device state. */
static volatile bool_t configured;
static uint8_t pending_addr;

/* Control endpoint state. */
static uint8_t ctl_state;
static uint8_t ctl_req;      /* bRequest of the transfer in progress */
static const uint8_t *ep0_ptr;
static uint16_t ep0_len;     /* IN bytes still to send */
static bool_t ep0_zlp;       /* short-packet terminator still owed */
static uint8_t ep0_pid;
static uint8_t str_desc[64]; /* UTF-16LE string descriptor, built on demand */

/* Data endpoint state. */
static uint8_t tx_pid, rx_pid;
static volatile bool_t tx_busy;
static char txring[1024];
#define TXMASK(x) ((x)&(sizeof(txring)-1))
static volatile unsigned int txcons, txprod;

/* CDC line state. 115200 8N1 until the host says otherwise. */
static uint8_t line_coding[7] = { 0x00, 0xc2, 0x01, 0x00, 0, 0, 8 };
static bool_t dtr;
static bool_t reboot_pending;

static void usb_service(void);
static void tx_kick(void);
static void ep0_status_in(void);

/* Hand a buffer to the SIE. AVAIL must be set at least one clk_usb cycle
 * after the rest of the control word, or the SIE may sample a half-written
 * descriptor (RP2350 datasheet, "Concurrent access" -- same dance as the
 * pico-sdk). */
static void buf_start(volatile uint32_t *bc, unsigned int len,
                      bool_t in, unsigned int pid)
{
    uint32_t val = len | USB_BUF_CTRL_AVAIL;

    if (in)
        val |= USB_BUF_CTRL_FULL;
    if (pid)
        val |= USB_BUF_CTRL_DATA1_PID;

    *bc = val & ~USB_BUF_CTRL_AVAIL;
    asm volatile ("b 1f\n1: b 1f\n1: b 1f\n1:\n" ::: "memory");
    *bc = val;
}

/*
 * Control endpoint
 */

static void ep0_send_chunk(void)
{
    unsigned int n = min_t(unsigned int, ep0_len, EP0_MPS);

    memcpy((void *)usb_dpram->ep0_buf, ep0_ptr, n);
    ep0_ptr += n;
    ep0_len -= n;

    buf_start(&usb_dpram->ep_buf_ctrl[0][EP_IN], n, TRUE, ep0_pid);
    ep0_pid ^= 1;
}

/* Send @len bytes of @p as the data stage, truncated to what the host
 * asked for in wLength. */
static void ep0_send(const void *p, unsigned int len, unsigned int wlen)
{
    if (wlen == 0) {
        ep0_status_in();
        return;
    }

    if (len > wlen)
        len = wlen;

    ep0_ptr = p;
    ep0_len = len;
    /* A full final packet needs an explicit short packet to tell the host
     * that a shorter-than-requested transfer has ended. */
    ep0_zlp = (len < wlen) && (len != 0) && ((len % EP0_MPS) == 0);
    ep0_pid = 1;
    ctl_state = CTL_IN_DATA;

    ep0_send_chunk();
}

/* Zero-length IN packet: the status stage of a host->device transfer. */
static void ep0_status_in(void)
{
    ctl_state = CTL_IN_STATUS;
    buf_start(&usb_dpram->ep_buf_ctrl[0][EP_IN], 0, TRUE, 1);
}

/* Zero-length OUT packet: the status stage of a device->host transfer. */
static void ep0_status_out(void)
{
    ctl_state = CTL_OUT_STATUS;
    buf_start(&usb_dpram->ep_buf_ctrl[0][EP_OUT], 0, FALSE, 1);
}

/* Accept a data stage of up to @len bytes from the host. */
static void ep0_recv(unsigned int len)
{
    ctl_state = CTL_OUT_DATA;
    buf_start(&usb_dpram->ep_buf_ctrl[0][EP_OUT],
              min_t(unsigned int, len, EP0_MPS), FALSE, 1);
}

static void ep0_stall(void)
{
    ctl_state = CTL_IDLE;
    usb_dpram->ep_buf_ctrl[0][EP_IN] = USB_BUF_CTRL_STALL;
    usb_dpram->ep_buf_ctrl[0][EP_OUT] = USB_BUF_CTRL_STALL;
    RP_SET(&usb_hw->ep_stall_arm) =
        USB_EP_STALL_ARM_EP0_IN | USB_EP_STALL_ARM_EP0_OUT;
}

/* Build a UTF-16LE string descriptor in str_desc[], returning its length. */
static unsigned int make_string_desc(unsigned int idx)
{
    const char *s;
    unsigned int i, len;

    if (idx == 0) {
        /* Supported languages: US English only. */
        str_desc[0] = 4;
        str_desc[1] = DESC_STRING;
        str_desc[2] = 0x09;
        str_desc[3] = 0x04;
        return 4;
    }

    if ((idx >= ARRAY_SIZE(strings)) || ((s = strings[idx]) == NULL))
        return 0;

    len = strlen(s);
    if (len > (sizeof(str_desc)-2)/2)
        len = (sizeof(str_desc)-2)/2;

    str_desc[0] = 2 + len*2;
    str_desc[1] = DESC_STRING;
    for (i = 0; i < len; i++) {
        str_desc[2+i*2] = s[i];
        str_desc[3+i*2] = 0;
    }

    return str_desc[0];
}

static void handle_get_descriptor(unsigned int wval, unsigned int wlen)
{
    unsigned int type = wval >> 8, idx = wval & 0xff, len;

    switch (type) {
    case DESC_DEVICE:
        ep0_send(device_desc, sizeof(device_desc), wlen);
        break;
    case DESC_CONFIG:
        ep0_send(config_desc, sizeof(config_desc), wlen);
        break;
    case DESC_STRING:
        if ((len = make_string_desc(idx)) == 0)
            goto stall;
        ep0_send(str_desc, len, wlen);
        break;
    default:
    stall:
        ep0_stall();
        break;
    }
}

/* The 1200-baud gesture. The host has both selected a 1200-baud line rate
 * and dropped DTR (which is what closing the port does): reboot to the
 * bootrom's UF2 loader. */
static void maybe_arm_bootsel(void)
{
    uint32_t rate = ((uint32_t)line_coding[0] | ((uint32_t)line_coding[1] << 8)
                     | ((uint32_t)line_coding[2] << 16)
                     | ((uint32_t)line_coding[3] << 24));

    if (!dtr && (rate == 1200))
        reboot_pending = TRUE;
}

static void enter_bootsel(void)
{
    typedef int (*rom_reboot_fn)(uint32_t flags, uint32_t delay_ms,
                                 uint32_t p0, uint32_t p1);
    rom_reboot_fn reboot = rp2350_rom_func(ROM_TABLE_CODE('R', 'B'));

    /* Drop off the bus, so the host sees a clean disconnect and then
     * re-enumerates the bootrom's device. */
    RP_CLR(&usb_hw->sie_ctrl) = USB_SIE_CTRL_PULLUP_EN;

    if (reboot != NULL)
        (*reboot)(REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL
                  | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 10, 0, 0);

    /* Bootrom refused, or is not the one we expected: plain reset. */
    system_reset();
}

static void handle_setup(uint32_t lo, uint32_t hi)
{
    uint8_t bmreq = lo & 0xff, breq = (lo >> 8) & 0xff;
    uint16_t wval = lo >> 16, wlen = hi >> 16;

    /* Any transfer in progress is abandoned by a new SETUP. */
    usb_dpram->ep_buf_ctrl[0][EP_IN] = 0;
    usb_dpram->ep_buf_ctrl[0][EP_OUT] = 0;
    ctl_state = CTL_IDLE;
    ctl_req = breq;
    ep0_len = 0;
    ep0_zlp = FALSE;

    if ((bmreq & 0x60) == 0x20) {
        /* Class request, CDC. */
        switch (breq) {
        case SET_LINE_CODING:
            if (wlen != sizeof(line_coding))
                goto stall;
            ep0_recv(wlen);
            break;
        case GET_LINE_CODING:
            ep0_send(line_coding, sizeof(line_coding), wlen);
            break;
        case SET_CONTROL_LINE_STATE:
            dtr = wval & 1;
            maybe_arm_bootsel();
            ep0_status_in();
            break;
        case SEND_BREAK:
            ep0_status_in();
            break;
        default:
            goto stall;
        }
        return;
    }

    if ((bmreq & 0x60) != 0x00)
        goto stall; /* vendor request: none supported */

    switch (breq) {
    case GET_STATUS: {
        /* Bus powered, no remote wakeup; endpoints never halted. */
        static const uint8_t zero[2] = { 0, 0 };
        ep0_send(zero, sizeof(zero), wlen);
        break;
    }
    case CLEAR_FEATURE:
    case SET_FEATURE:
        ep0_status_in();
        break;
    case SET_ADDRESS:
        /* The address takes effect only once the status stage is done. */
        pending_addr = wval & 0x7f;
        ep0_status_in();
        break;
    case GET_DESCRIPTOR:
        handle_get_descriptor(wval, wlen);
        break;
    case GET_CONFIGURATION:
        /* Static: ep0_send() holds the pointer past this function. */
        str_desc[0] = configured ? 1 : 0;
        ep0_send(str_desc, 1, wlen);
        break;
    case SET_CONFIGURATION:
        configured = (wval != 0);
        if (configured) {
            /* Arm the OUT pipe and start draining anything the console
             * has queued while we were unconfigured. */
            tx_pid = rx_pid = 0;
            tx_busy = FALSE;
            buf_start(&usb_dpram->ep_buf_ctrl[EP_DATA][EP_OUT],
                      EPX_MPS, FALSE, rx_pid);
            rx_pid ^= 1;
        }
        ep0_status_in();
        if (configured)
            tx_kick();
        break;
    case GET_INTERFACE: {
        static const uint8_t alt = 0;
        ep0_send(&alt, 1, wlen);
        break;
    }
    case SET_INTERFACE:
        ep0_status_in();
        break;
    default:
        goto stall;
    }
    return;

stall:
    ep0_stall();
}

static void ep0_in_done(void)
{
    switch (ctl_state) {
    case CTL_IN_DATA:
        if (ep0_len != 0) {
            ep0_send_chunk();
        } else if (ep0_zlp) {
            ep0_zlp = FALSE;
            ep0_send_chunk(); /* ep0_len is zero: sends the ZLP */
        } else {
            ep0_status_out();
        }
        break;
    case CTL_IN_STATUS:
        ctl_state = CTL_IDLE;
        if (pending_addr != 0) {
            usb_hw->dev_addr_ctrl = pending_addr;
            pending_addr = 0;
        }
        if (reboot_pending)
            enter_bootsel();
        break;
    }
}

static void ep0_out_done(void)
{
    unsigned int len;

    switch (ctl_state) {
    case CTL_OUT_DATA:
        len = usb_dpram->ep_buf_ctrl[0][EP_OUT] & USB_BUF_CTRL_LEN_MASK;
        if ((ctl_req == SET_LINE_CODING) && (len == sizeof(line_coding))) {
            memcpy(line_coding, (void *)usb_dpram->ep0_buf, len);
            maybe_arm_bootsel();
        }
        ep0_status_in();
        break;
    case CTL_OUT_STATUS:
        ctl_state = CTL_IDLE;
        break;
    }
}

/*
 * Data endpoints
 */

static void tx_kick(void)
{
    volatile uint8_t *buf = &usb_dpram->data[BUF_IN];
    unsigned int c = txcons, p = txprod, n = 0;

    if (!configured || tx_busy || (c == p))
        return;

    while ((c != p) && (n < EPX_MPS))
        buf[n++] = txring[TXMASK(c++)];
    txcons = c;

    tx_busy = TRUE;
    buf_start(&usb_dpram->ep_buf_ctrl[EP_DATA][EP_IN], n, TRUE, tx_pid);
    tx_pid ^= 1;
}

/* Host->device data. Nothing consumes console input, so it is discarded;
 * the endpoint is re-armed immediately so the host never stalls. */
static void rx_done(void)
{
    buf_start(&usb_dpram->ep_buf_ctrl[EP_DATA][EP_OUT],
              EPX_MPS, FALSE, rx_pid);
    rx_pid ^= 1;
}

static void bus_reset(void)
{
    usb_hw->dev_addr_ctrl = 0;
    pending_addr = 0;
    configured = FALSE;
    ctl_state = CTL_IDLE;
    ep0_len = 0;
    ep0_zlp = FALSE;
    tx_busy = FALSE;
    tx_pid = rx_pid = 0;
    usb_dpram->ep_buf_ctrl[0][EP_IN] = 0;
    usb_dpram->ep_buf_ctrl[0][EP_OUT] = 0;
    usb_dpram->ep_buf_ctrl[EP_DATA][EP_IN] = 0;
    usb_dpram->ep_buf_ctrl[EP_DATA][EP_OUT] = 0;
    usb_dpram->ep_buf_ctrl[EP_NOTIFY][EP_IN] = 0;
}

static void handle_buff_status(void)
{
    uint32_t bs = usb_hw->buf_status;

    if (bs & USB_BUFF_STATUS_IN(0)) {
        RP_CLR(&usb_hw->buf_status) = USB_BUFF_STATUS_IN(0);
        ep0_in_done();
    }

    if (bs & USB_BUFF_STATUS_OUT(0)) {
        RP_CLR(&usb_hw->buf_status) = USB_BUFF_STATUS_OUT(0);
        ep0_out_done();
    }

    if (bs & USB_BUFF_STATUS_IN(EP_DATA)) {
        RP_CLR(&usb_hw->buf_status) = USB_BUFF_STATUS_IN(EP_DATA);
        tx_busy = FALSE;
        tx_kick();
    }

    if (bs & USB_BUFF_STATUS_OUT(EP_DATA)) {
        RP_CLR(&usb_hw->buf_status) = USB_BUFF_STATUS_OUT(EP_DATA);
        rx_done();
    }

    /* We never send notifications, but acknowledge them defensively. */
    if (bs & USB_BUFF_STATUS_IN(EP_NOTIFY))
        RP_CLR(&usb_hw->buf_status) = USB_BUFF_STATUS_IN(EP_NOTIFY);
}

static void usb_service(void)
{
    uint32_t status = usb_hw->ints;

    if (status & USB_INT_BUS_RESET) {
        RP_CLR(&usb_hw->sie_status) = USB_SIE_STATUS_BUS_RESET;
        bus_reset();
    }

    if (status & USB_INT_BUFF_STATUS)
        handle_buff_status();

    if (status & USB_INT_SETUP_REQ) {
        /* Latch the packet out of DPRAM before releasing the SIE to
         * receive another one. */
        uint32_t lo = usb_dpram->setup_low, hi = usb_dpram->setup_high;
        RP_CLR(&usb_hw->sie_status) = USB_SIE_STATUS_SETUP_REC;
        handle_setup(lo, hi);
    }
}

static void IRQ_usbctrl(void)
{
    usb_service();
}
DEFINE_IRQ(USBCTRL_IRQ, "IRQ_usbctrl");

/*
 * Public interface
 */

/* Queue one character for the host. Dropped if the host is not draining
 * the pipe: the console must never block the emulator. */
void usb_cdc_putc(char c)
{
    unsigned int p = txprod;

    if ((p - txcons) >= (sizeof(txring) - 1))
        return;

    txring[TXMASK(p)] = c;
    barrier();
    txprod = p + 1;
}

void usb_cdc_kick(void)
{
    uint32_t oldpri = IRQ_save(USB_IRQ_PRI);
    tx_kick();
    IRQ_restore(oldpri);
}

/* Drain the transmit ring with interrupts disabled, for crash dumps.
 * Bounded, so a vanished host cannot wedge us here. */
void usb_cdc_flush_sync(void)
{
    static bool_t gave_up;
    time_t t = time_now();

    if (gave_up)
        return;

    while (configured && ((txcons != txprod) || tx_busy)) {
        if (time_since(t) > time_ms(100)) {
            gave_up = TRUE;
            break;
        }
        usb_service();
        tx_kick();
    }
}

void usb_cdc_init(void)
{
    /* The DPRAM struct must exactly cover the 4kB window. */
    BUILD_BUG_ON(sizeof(*usb_dpram) != 4096);
    BUILD_BUG_ON(sizeof(config_desc) != 67);

    /* Take the controller out of reset. clk_usb is already running. */
    RP_SET(&resets->reset) = RST_USBCTRL;
    RP_CLR(&resets->reset) = RST_USBCTRL;
    while (!(resets->reset_done & RST_USBCTRL))
        cpu_relax();

    memset((void *)usb_dpram, 0, sizeof(*usb_dpram));

    /* Route the SIE to the on-chip PHY. The controller has no VBUS-sense
     * input on this board, so tell it VBUS is always present. */
    usb_hw->muxing = USB_MUXING_TO_PHY | USB_MUXING_SOFTCON;
    usb_hw->pwr = USB_PWR_VBUS_DETECT | USB_PWR_VBUS_DETECT_OVERRIDE_EN;

    /* Device mode. Writing the whole register also clears PHY_ISO, which
     * is set out of reset. */
    usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN;

    /* Interrupt on each EP0 buffer completion. */
    usb_hw->sie_ctrl = USB_SIE_CTRL_EP0_INT_1BUF;

    usb_hw->inte = (USB_INT_BUS_RESET | USB_INT_SETUP_REQ
                    | USB_INT_BUFF_STATUS);

    usb_dpram->ep_ctrl[EP_NOTIFY-1][EP_IN] =
        USB_EP_CTRL_ENABLE | USB_EP_CTRL_INT_PER_BUF
        | USB_EP_CTRL_TYPE(USB_EP_TYPE_INTERRUPT)
        | (USB_DPRAM_DATA_OFFSET + BUF_NOTIFY);
    usb_dpram->ep_ctrl[EP_DATA-1][EP_IN] =
        USB_EP_CTRL_ENABLE | USB_EP_CTRL_INT_PER_BUF
        | USB_EP_CTRL_TYPE(USB_EP_TYPE_BULK)
        | (USB_DPRAM_DATA_OFFSET + BUF_IN);
    usb_dpram->ep_ctrl[EP_DATA-1][EP_OUT] =
        USB_EP_CTRL_ENABLE | USB_EP_CTRL_INT_PER_BUF
        | USB_EP_CTRL_TYPE(USB_EP_TYPE_BULK)
        | (USB_DPRAM_DATA_OFFSET + BUF_OUT);

    IRQx_set_prio(USBCTRL_IRQ, USB_IRQ_PRI);
    IRQx_enable(USBCTRL_IRQ);

    /* Assert the D+ pull-up: we are now visible to any attached host. */
    RP_SET(&usb_hw->sie_ctrl) = USB_SIE_CTRL_PULLUP_EN;
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
