/**
 * @file nsx_usb.h
 * @brief USB CDC + Vendor (WebUSB) driver for Ambiq SoCs.
 *
 * Provides a CDC serial interface and an optional Vendor class interface
 * (for WebUSB / WinUSB) backed by TinyUSB.  All device strings, VID/PID,
 * and the WebUSB landing-page URL are configurable at init time — no
 * values are hardcoded into the driver.
 *
 * ## DTR (Data Terminal Ready)
 * The CDC `nsx_usb_connected()` check relies on the host asserting DTR.
 * Most terminal emulators (minicom, screen, PuTTY) assert DTR on open,
 * but **pyserial does NOT by default**.  When opening a port with pyserial
 * you must explicitly set `ser.dtr = True` (or pass `dsrdtr=True`) before
 * the device will report as connected.  Without DTR, `tud_cdc_connected()`
 * returns false and send/receive will return NSX_USB_STATUS_NOT_CONNECTED.
 */
#ifndef NSX_USB_H
#define NSX_USB_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Status codes                                                        */
/* ------------------------------------------------------------------ */

/** USB status codes (in addition to generic NS_STATUS_*). */
#define NSX_USB_STATUS_TIMEOUT       0x100  /**< Operation timed out */
#define NSX_USB_STATUS_NOT_CONNECTED 0x101  /**< Host not connected / DTR not asserted */
#define NSX_USB_STATUS_PARTIAL       0x102  /**< Partial transfer completed */

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

/** Default TinyUSB polling interval in microseconds. */
#define NSX_USB_DEFAULT_POLL_US  1000

/** Default send/receive timeout in milliseconds. */
#define NSX_USB_DEFAULT_TIMEOUT_MS  5000

/**
 * Minimum CDC RX buffer size.  Must be >= CFG_TUD_CDC_EP_BUFSIZE (1024)
 * or the TinyUSB OUT endpoint will never be primed and no data will be
 * received.  nsx_usb_init() rejects smaller buffers with
 * NS_STATUS_INVALID_CONFIG.
 */
#define NSX_USB_MIN_CDC_RX_BUFSIZE  1024

/* ------------------------------------------------------------------ */
/* Device descriptor overrides (optional)                              */
/* ------------------------------------------------------------------ */

/**
 * Optional USB device descriptor overrides.
 *
 * Pass a pointer to this struct in nsx_usb_config_t::device_desc to
 * customise VID, PID, strings, and the WebUSB landing-page URL.
 * Any field left as 0 / NULL uses the built-in default.
 *
 * Defaults:
 *   vid              = 0xCafe
 *   pid              = 0x4011 (CDC + Vendor composite)
 *   manufacturer     = "Ambiq"
 *   product          = "NSX USB Device"
 *   serial           = "000001"
 *   cdc_interface    = "NSX CDC"
 *   vendor_interface = "NSX Vendor"
 *   webusb_url       = NULL (WebUSB landing page disabled)
 */
typedef struct {
    uint16_t    vid;                /**< USB Vendor ID  (0 = default 0xCafe) */
    uint16_t    pid;                /**< USB Product ID (0 = default 0x4011) */
    const char *manufacturer;       /**< iManufacturer string */
    const char *product;            /**< iProduct string */
    const char *serial;             /**< iSerialNumber string */
    const char *cdc_interface;      /**< CDC interface string */
    const char *vendor_interface;   /**< Vendor interface string */
    const char *webusb_url;         /**< WebUSB landing-page URL (no scheme prefix) */
} nsx_usb_device_desc_t;

/* ------------------------------------------------------------------ */
/* Forward declarations & callbacks                                    */
/* ------------------------------------------------------------------ */

/** Forward declaration. */
typedef struct nsx_usb_config nsx_usb_config_t;

/** Optional callback when CDC data arrives (invoked from timer ISR context). */
typedef void (*nsx_usb_rx_cb_t)(nsx_usb_config_t *cfg);

/** Optional callback when Vendor data arrives (invoked from ISR context). */
typedef void (*nsx_usb_vendor_rx_cb_t)(nsx_usb_config_t *cfg);

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

/** USB CDC + Vendor configuration. */
struct nsx_usb_config {
    /* ---- CDC buffers (caller-provided) ---- */

    /** TX buffer for CDC — must be caller-allocated. */
    uint8_t            *tx_buffer;
    uint32_t            tx_buffer_len;

    /** RX buffer for CDC — must be caller-allocated.
     *  Must be >= NSX_USB_MIN_CDC_RX_BUFSIZE (1024) or init will fail. */
    uint8_t            *rx_buffer;
    uint32_t            rx_buffer_len;

    /* ---- Timing ---- */

    /** TinyUSB polling interval in microseconds (0 = use default 1000). */
    uint32_t            poll_interval_us;

    /** Send/receive timeout in milliseconds (0 = use default 5000). */
    uint32_t            timeout_ms;

    /* ---- Callbacks ---- */

    /** Optional CDC RX callback — called from ISR context when data arrives.
     *  May be NULL if the application prefers polling via nsx_usb_receive. */
    nsx_usb_rx_cb_t     rx_cb;

    /** Optional Vendor RX callback — called from ISR context when vendor
     *  data arrives.  May be NULL; use nsx_usb_vendor_read_nb() to read. */
    nsx_usb_vendor_rx_cb_t vendor_rx_cb;

    /* ---- Optional device-descriptor overrides ---- */

    /** Device descriptor overrides (VID, PID, strings, WebUSB URL).
     *  NULL = all defaults.  The pointed-to struct must remain valid for
     *  the lifetime of the USB session. */
    const nsx_usb_device_desc_t *device_desc;

    /** Opaque user context. */
    void               *user_ctx;

    /* --- internal (set by nsx_usb_init, do not modify) --- */
    uint8_t             _initialized;
    volatile uint8_t    _rx_ready;
    volatile uint8_t    _vendor_rx_ready;
    volatile uint8_t    _vendor_connected;
};

/* ================================================================== */
/* CDC API                                                             */
/* ================================================================== */

/**
 * Initialize USB (CDC + Vendor).
 *
 * Sets up TinyUSB, configures a timer for USB polling, and prepares
 * both the CDC and Vendor interfaces.  Does NOT block waiting for host
 * enumeration.
 *
 * @return 0 on success, NS_STATUS_INVALID_CONFIG if rx_buffer_len <
 *         NSX_USB_MIN_CDC_RX_BUFSIZE, other NS_STATUS error on failure.
 */
uint32_t nsx_usb_init(nsx_usb_config_t *cfg);

/**
 * Send data over USB CDC.  Blocks until all bytes sent or timeout.
 */
uint32_t nsx_usb_send(nsx_usb_config_t *cfg, const void *data, uint32_t len,
                       uint32_t *bytes_sent);

/**
 * Receive data over USB CDC.  Blocks until @p len bytes received or timeout.
 */
uint32_t nsx_usb_receive(nsx_usb_config_t *cfg, void *data, uint32_t len,
                          uint32_t *bytes_received);

/**
 * Check if the USB host is connected (CDC DTR asserted).
 *
 * @note pyserial users must set `ser.dtr = True` explicitly.
 *       See file-level documentation for details.
 */
bool nsx_usb_connected(nsx_usb_config_t *cfg);

/** Check if CDC RX data is available without blocking. */
bool nsx_usb_data_available(nsx_usb_config_t *cfg);

/**
 * Non-blocking CDC read — returns however many bytes are immediately
 * available (up to max_len) without waiting.
 */
uint32_t nsx_usb_read_nb(nsx_usb_config_t *cfg, void *data, uint32_t max_len,
                          uint32_t *bytes_read);

/** Flush the CDC RX FIFO, discarding any buffered data. */
void nsx_usb_flush_rx(nsx_usb_config_t *cfg);

/* ================================================================== */
/* Vendor (WebUSB) API                                                 */
/* ================================================================== */

/**
 * Send data over the USB Vendor (WebUSB) interface.
 * Blocks until all bytes sent or timeout.
 *
 * No framing is applied — raw bytes are written to the vendor bulk
 * endpoint.  The application is responsible for any higher-level
 * protocol framing.
 */
uint32_t nsx_usb_vendor_send(nsx_usb_config_t *cfg, const void *data,
                              uint32_t len, uint32_t *bytes_sent);

/**
 * Non-blocking Vendor read — returns however many bytes are immediately
 * available (up to max_len).  No framing is stripped.
 */
uint32_t nsx_usb_vendor_read_nb(nsx_usb_config_t *cfg, void *data,
                                 uint32_t max_len, uint32_t *bytes_read);

/**
 * Check if the WebUSB host has connected to the Vendor interface.
 *
 * Connection is tracked via the SET_CONTROL_LINE_STATE class request
 * that WebUSB-capable browsers send on open.
 */
bool nsx_usb_vendor_connected(nsx_usb_config_t *cfg);

/** Check if Vendor RX data is available without blocking. */
bool nsx_usb_vendor_data_available(nsx_usb_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* NSX_USB_H */
