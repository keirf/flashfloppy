OBJS += usb_core.o
OBJS += usb_hcd.o
OBJS += usb_hcd_int.o
OBJS += usbh_core.o
OBJS += usbh_hcs.o
OBJS += usbh_ioreq.o
OBJS += usbh_msc_bot.o
OBJS += usbh_msc_core.o
OBJS += usbh_msc_scsi.o
OBJS += usbh_stdreq.o

CFLAGS += -I$(SRCDIR)/inc/ -include usbh_conf.h

# GCC >= 12 raises false uninitialized-use warnings on ST's union-punned
# register accesses in this vendor file.
usb_hcd_int.o: CFLAGS += -Wno-uninitialized -Wno-maybe-uninitialized
