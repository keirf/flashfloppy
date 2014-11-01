/*
 * usb_protocol.h
 * 
 * Standard packet formats and field types from the USB Specification Rev 2.0.
 * Most of this is verbatim from Chapter 9 "USB Device Framework".
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct usb_device_request {
    uint8_t bmRequestType; /* USB_{DIR_*,TYPE_*,RX_*} */
    uint8_t bRequest;      /* USB_REQ_* */
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

/* bmRequestType definitions (Table 9-2): */
/* b7: Data transfer direction */
#define USB_DIR_OUT      (0u<<7) /* Host-to-Device */
#define USB_DIR_IN       (1u<<7) /* Device-to-Host */
/* b6..5: Type */
#define USB_TYPE_STD     (0u<<5)
#define USB_TYPE_CLASS   (1u<<5)
#define USB_TYPE_VENDOR  (2u<<5)
/* b4..0: Recipient */
#define USB_RX_DEVICE    (0u<<0)
#define USB_RX_INTERFACE (1u<<0)
#define USB_RX_ENDPOINT  (2u<<0)

/* Standard request codes (bRequest) (Table 9-4). */
#define USB_REQ_GET_STATUS          0
#define USB_REQ_CLEAR_FEATURE       1
#define USB_REQ_SET_FEATURE         3
#define USB_REQ_SET_ADDRESS         5
#define USB_REQ_GET_DESCRIPTOR      6
#define USB_REQ_SET_DESCRIPTOR      7
#define USB_REQ_GET_CONFIGURATION   8
#define USB_REQ_SET_CONFIGURATION   9
#define USB_REQ_GET_INTERFACE      10
#define USB_REQ_SET_INTERFACE      11
#define USB_REQ_SYNCH_FRAME        12

/* Standard descriptor types (Table 9-5). */
#define USB_DESC_DEVICE             1
#define USB_DESC_CONFIGURATION      2
#define USB_DESC_STRING             3
#define USB_DESC_INTERFACE          4
#define USB_DESC_ENDPOINT           5
#define USB_DESC_DEVICE_QUALIFIER   6
#define USB_DESC_OTHER_SPEED_CONFIGURATION 7
#define USB_DESC_INTERFACE_POWER    8

/* Standard feature selectors (Table 9-6). */
#define USB_FEAT_ENDPOINT_HALT      0
#define USB_FEAT_DEVICE_REMOTE_WAKEUP 1
#define USB_FEAT_TEST_MODE          2

/* Table 9-8, etc. */
struct usb_descriptor_header {
    uint8_t bLength;         /* Size of entire descriptor, in bytes */
    uint8_t bDescriptorType; /* USB_DESC_* */
};

/* Table 9-8 */
struct usb_device_descriptor {
    struct usb_descriptor_header h;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0; /* MPS for ep0: 8, 16, 32, or 64 bytes */
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
};

/* Table 9-9 */
struct usb_device_qualifier_descriptor {
    struct usb_descriptor_header h;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint8_t bNumConfigurations;
    uint8_t bReserved;
};

/* Table 9-10 */
struct usb_configuration_descriptor {
    struct usb_descriptor_header h;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
};    

/* Table 9-12 */
struct usb_interface_descriptor {
    struct usb_descriptor_header h;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
};

/* Table 9-13 */
struct usb_endpoint_descriptor {
    struct usb_descriptor_header h;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
};

/* Table 9-15 */
struct usb_string0_descriptor {
    struct usb_descriptor_header h;
    uint16_t wLANGID[];
};

/* Table 9-16 */
struct usb_string_descriptor {
    struct usb_descriptor_header h;
    uint8_t bString[];
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
