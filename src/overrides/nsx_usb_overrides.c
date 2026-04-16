/**
 * @file nsx_usb_overrides.c
 * @brief TinyUSB weak-function overrides for CDC + Vendor callbacks.
 *
 * These override TinyUSB weak callbacks to hook CDC and Vendor RX/TX
 * events into the nsx_usb driver state.  The Vendor control-transfer
 * handler responds to WebUSB and MS OS 2.0 requests.
 */

#include "nsx_usb.h"
#include "tusb.h"
#include <string.h>

/* Access the active config set during nsx_usb_init. */
extern nsx_usb_config_t *g_usb_cfg;

/* Descriptor helpers defined in nsx_usb_descriptors.c */
extern uint8_t const desc_ms_os_20[];
const uint8_t *nsx_usb_get_webusb_url_desc(uint8_t *out_len);

/* Vendor request codes — must match BOS descriptor values. */
#define VENDOR_REQUEST_WEBUSB    1
#define VENDOR_REQUEST_MICROSOFT 2
#define DESC_MS_OS_20            7
#define WEBUSB_REQUEST_SET_CONTROL_LINE_STATE 0x22

/* ------------------------------------------------------------------ */
/* CDC callbacks                                                       */
/* ------------------------------------------------------------------ */

void tud_cdc_rx_cb(uint8_t itf) {
    (void)itf;
    if (g_usb_cfg != NULL) {
        g_usb_cfg->_rx_ready = 1;
        if (g_usb_cfg->rx_cb != NULL) {
            g_usb_cfg->rx_cb(g_usb_cfg);
        }
    }
}

void tud_cdc_tx_complete_cb(uint8_t itf) {
    (void)itf;
}

/* ------------------------------------------------------------------ */
/* Lifecycle callbacks                                                 */
/* ------------------------------------------------------------------ */

void tud_mount_cb(void) {
    if (g_usb_cfg) g_usb_cfg->_vendor_connected = 0;
}

#ifndef TUSB_ADDED_FUNCTIONS
void tud_umount_cb(void) {
    if (g_usb_cfg) g_usb_cfg->_vendor_connected = 0;
}
#endif

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    if (g_usb_cfg) g_usb_cfg->_vendor_connected = 0;
}

void tud_resume_cb(void) {}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    (void)dtr;
    (void)rts;
}

/* ------------------------------------------------------------------ */
/* Vendor (WebUSB) callbacks                                           */
/* ------------------------------------------------------------------ */

void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize) {
    (void)itf;
    (void)buffer;
    (void)bufsize;
    if (g_usb_cfg != NULL) {
        g_usb_cfg->_vendor_rx_ready = 1;
        if (g_usb_cfg->vendor_rx_cb != NULL) {
            g_usb_cfg->vendor_rx_cb(g_usb_cfg);
        }
    }
}

/**
 * Handle Vendor control transfers:
 *   - VENDOR_REQUEST_WEBUSB  → return WebUSB URL descriptor
 *   - VENDOR_REQUEST_MICROSOFT → return MS OS 2.0 descriptor
 *   - SET_CONTROL_LINE_STATE → track WebUSB connection state
 */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                 tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    switch (request->bmRequestType_bit.type) {
    case TUSB_REQ_TYPE_VENDOR:
        switch (request->bRequest) {
        case VENDOR_REQUEST_WEBUSB: {
            uint8_t url_len = 0;
            const uint8_t *url_desc = nsx_usb_get_webusb_url_desc(&url_len);
            if (url_desc == NULL || url_len == 0) {
                return false;   /* no URL configured — stall */
            }
            return tud_control_xfer(rhport, request, (void *)(uintptr_t)url_desc, url_len);
        }
        case VENDOR_REQUEST_MICROSOFT:
            if (request->wIndex == DESC_MS_OS_20) {
                uint16_t total_len;
                memcpy(&total_len, desc_ms_os_20 + 8, 2);
                return tud_control_xfer(rhport, request,
                                        (void *)(uintptr_t)desc_ms_os_20, total_len);
            }
            return false;
        default:
            break;
        }
        break;

    case TUSB_REQ_TYPE_CLASS:
        if (request->bRequest == WEBUSB_REQUEST_SET_CONTROL_LINE_STATE) {
            if (g_usb_cfg != NULL) {
                g_usb_cfg->_vendor_connected = (request->wValue != 0) ? 1 : 0;
            }
            return tud_control_status(rhport, request);
        }
        break;

    default:
        break;
    }

    return false;   /* stall unknown requests */
}
