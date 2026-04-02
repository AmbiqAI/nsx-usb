/**
 * @file nsx_usb_descriptors.c
 * @brief USB descriptor definitions for TinyUSB CDC + Vendor composite.
 *
 * All device strings, VID/PID, and the WebUSB URL are read from the
 * nsx_usb_device_desc_t provided at init time.  If no override was
 * supplied (NULL), built-in defaults are used.
 *
 * Based on TinyUSB CDC + WebUSB example descriptors.
 *
 * MIT License — see TinyUSB project for full copyright notice.
 */

#include "nsx_usb.h"
#include "tusb.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Access to the active config set during nsx_usb_init.                */
/* ------------------------------------------------------------------ */

extern nsx_usb_config_t *g_usb_cfg;

/* ------------------------------------------------------------------ */
/* Defaults                                                            */
/* ------------------------------------------------------------------ */

#define DEFAULT_VID             0xCafe
#define DEFAULT_PID             0x4011   /* CDC + Vendor composite */
#define DEFAULT_MANUFACTURER    "Ambiq"
#define DEFAULT_PRODUCT         "NSX USB Device"
#define DEFAULT_SERIAL          "000001"
#define DEFAULT_CDC_INTERFACE   "NSX CDC"
#define DEFAULT_VENDOR_IFACE    "NSX Vendor"

static inline const nsx_usb_device_desc_t *desc_cfg(void) {
    return (g_usb_cfg != NULL) ? g_usb_cfg->device_desc : NULL;
}

static inline uint16_t cfg_vid(void) {
    const nsx_usb_device_desc_t *d = desc_cfg();
    return (d && d->vid) ? d->vid : DEFAULT_VID;
}
static inline uint16_t cfg_pid(void) {
    const nsx_usb_device_desc_t *d = desc_cfg();
    return (d && d->pid) ? d->pid : DEFAULT_PID;
}

/* ------------------------------------------------------------------ */
/* Vendor request codes for BOS                                        */
/* ------------------------------------------------------------------ */

#define VENDOR_REQUEST_WEBUSB    1
#define VENDOR_REQUEST_MICROSOFT 2

/* ------------------------------------------------------------------ */
/* Device Descriptor                                                   */
/* ------------------------------------------------------------------ */

/* Mutable so VID/PID can be patched at init time. */
static tusb_desc_device_t s_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0210,   /* USB 2.1 required for BOS / WebUSB */

    /* Use IAD for CDC */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = DEFAULT_VID,
    .idProduct          = DEFAULT_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    s_desc_device.idVendor  = cfg_vid();
    s_desc_device.idProduct = cfg_pid();
    return (uint8_t const *)&s_desc_device;
}

/* ------------------------------------------------------------------ */
/* Configuration Descriptor — CDC + Vendor                             */
/* ------------------------------------------------------------------ */

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_VENDOR,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_VENDOR_DESC_LEN)

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_VENDOR_OUT  0x03
#define EPNUM_VENDOR_IN   0x83

static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 5, EPNUM_VENDOR_OUT,
                          EPNUM_VENDOR_IN, 64),
};

#if TUD_OPT_HIGH_SPEED
static uint8_t const desc_hs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 512),
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 5, EPNUM_VENDOR_OUT,
                          EPNUM_VENDOR_IN, 512),
};
#endif

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
#if TUD_OPT_HIGH_SPEED
    return (tud_speed_get() == TUSB_SPEED_HIGH)
               ? desc_hs_configuration
               : desc_fs_configuration;
#else
    return desc_fs_configuration;
#endif
}

/* ------------------------------------------------------------------ */
/* BOS Descriptor (required for WebUSB + WinUSB on Windows)            */
/* ------------------------------------------------------------------ */

#define BOS_TOTAL_LEN      (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define MS_OS_20_DESC_LEN  0xB2

static uint8_t const desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),
    TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT),
};

uint8_t const *tud_descriptor_bos_cb(void) {
    return desc_bos;
}

/* ------------------------------------------------------------------ */
/* MS OS 2.0 Descriptor — WinUSB auto-install for Vendor interface     */
/* ------------------------------------------------------------------ */

uint8_t const desc_ms_os_20[] = {
    /* Set header: length, type, windows version, total length */
    U16_TO_U8S_LE(0x000A),
    U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000),
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    /* Configuration subset header */
    U16_TO_U8S_LE(0x0008),
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

    /* Function subset header — applies to VENDOR interface */
    U16_TO_U8S_LE(0x0008),
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_NUM_VENDOR, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

    /* Compatible ID: WINUSB */
    U16_TO_U8S_LE(0x0014),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Registry property: DeviceInterfaceGUIDs */
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007),     /* REG_MULTI_SZ */
    U16_TO_U8S_LE(0x002A),     /* wPropertyNameLength */
    /* "DeviceInterfaceGUIDs\0" in UTF-16 */
    'D',0,'e',0,'v',0,'i',0,'c',0,'e',0,'I',0,'n',0,'t',0,'e',0,
    'r',0,'f',0,'a',0,'c',0,'e',0,'G',0,'U',0,'I',0,'D',0,'s',0,
    0x00, 0x00,
    U16_TO_U8S_LE(0x0050),     /* wPropertyDataLength */
    /* GUID: {975F44D9-0D08-43FD-8B3E-127CA8AFFF9D} */
    '{',0,'9',0,'7',0,'5',0,'F',0,'4',0,'4',0,'D',0,'9',0,
    '-',0,'0',0,'D',0,'0',0,'8',0,'-',0,'4',0,'3',0,'F',0,'D',0,
    '-',0,'8',0,'B',0,'3',0,'E',0,'-',0,'1',0,'2',0,'7',0,'C',0,
    'A',0,'8',0,'A',0,'F',0,'F',0,'F',0,'9',0,'D',0,
    '}',0, 0x00, 0x00, 0x00, 0x00,
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

/* ------------------------------------------------------------------ */
/* WebUSB URL descriptor — built dynamically from config               */
/* ------------------------------------------------------------------ */

/* Max URL length for the descriptor (TinyUSB struct layout). */
#define MAX_WEBUSB_URL_LEN 127

static uint8_t s_webusb_url_desc[3 + MAX_WEBUSB_URL_LEN];
static uint8_t s_webusb_url_len = 0;

/**
 * Build the WebUSB URL descriptor from nsx_usb_device_desc_t::webusb_url.
 * Called once during nsx_usb_init().  Returns pointer to descriptor or
 * NULL if no URL was configured.
 */
const uint8_t *nsx_usb_get_webusb_url_desc(uint8_t *out_len) {
    const nsx_usb_device_desc_t *d = desc_cfg();
    if (d == NULL || d->webusb_url == NULL || d->webusb_url[0] == '\0') {
        if (out_len) *out_len = 0;
        return NULL;
    }
    size_t url_len = strlen(d->webusb_url);
    if (url_len > MAX_WEBUSB_URL_LEN) {
        url_len = MAX_WEBUSB_URL_LEN;
    }
    s_webusb_url_desc[0] = (uint8_t)(3 + url_len);   /* bLength */
    s_webusb_url_desc[1] = 3;                         /* WEBUSB URL descriptor type */
    s_webusb_url_desc[2] = 1;                         /* URL scheme: https:// */
    memcpy(&s_webusb_url_desc[3], d->webusb_url, url_len);
    s_webusb_url_len = (uint8_t)(3 + url_len);
    if (out_len) *out_len = s_webusb_url_len;
    return s_webusb_url_desc;
}

/* ------------------------------------------------------------------ */
/* String Descriptors                                                  */
/* ------------------------------------------------------------------ */

static const char *get_string(uint8_t index) {
    const nsx_usb_device_desc_t *d = desc_cfg();
    switch (index) {
    case 1: return (d && d->manufacturer)     ? d->manufacturer     : DEFAULT_MANUFACTURER;
    case 2: return (d && d->product)          ? d->product          : DEFAULT_PRODUCT;
    case 3: return (d && d->serial)           ? d->serial           : DEFAULT_SERIAL;
    case 4: return (d && d->cdc_interface)    ? d->cdc_interface    : DEFAULT_CDC_INTERFACE;
    case 5: return (d && d->vendor_interface) ? d->vendor_interface : DEFAULT_VENDOR_IFACE;
    default: return NULL;
    }
}

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t count;

    if (index == 0) {
        _desc_str[1] = 0x0409;  /* English */
        count = 1;
    } else {
        const char *str = get_string(index);
        if (str == NULL) {
            return NULL;
        }
        count = 0;
        for (; str[count] && count < 31; count++) {
            _desc_str[1 + count] = str[count];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return _desc_str;
}
