/**
 * @file nsx_usb.c
 * @brief USB CDC + Vendor implementation for Ambiq SoCs using TinyUSB.
 *
 * Migrated from legacy ns-usb with the following fixes:
 *   - All ns_lp_printf removed from driver code
 *   - All arbitrary ns_delay_us replaced with bounded polling
 *   - Proper error codes returned from send/receive
 *   - Fixed function name: receive not recieve
 *   - Configurable poll interval and timeout
 *   - Minimum CDC RX buffer size enforced
 *   - Vendor (WebUSB) send/read API added
 */

#include "nsx_usb.h"
#include "ns_core.h"
#include "ns_timer.h"
#include "tusb.h"

/* ------------------------------------------------------------------ */
/* Internal state                                                      */
/* ------------------------------------------------------------------ */

nsx_usb_config_t *g_usb_cfg = NULL;

/* ------------------------------------------------------------------ */
/* Timer callback — polls TinyUSB from ISR context                     */
/* ------------------------------------------------------------------ */

static void usb_timer_callback(ns_timer_config_t *tc) {
    tud_task();
    if (g_usb_cfg != NULL) {
        if (g_usb_cfg->_rx_ready == 0 && tud_cdc_available()) {
            g_usb_cfg->_rx_ready = 1;
            if (g_usb_cfg->rx_cb != NULL) {
                g_usb_cfg->rx_cb(g_usb_cfg);
            }
        }
        if (g_usb_cfg->_vendor_rx_ready == 0 && tud_vendor_available()) {
            g_usb_cfg->_vendor_rx_ready = 1;
            if (g_usb_cfg->vendor_rx_cb != NULL) {
                g_usb_cfg->vendor_rx_cb(g_usb_cfg);
            }
        }
    }
}

static ns_timer_config_t g_usb_timer = {
    .api                  = &ns_timer_V1_0_0,
    .timer                = NS_TIMER_USB,
    .enableInterrupt      = true,
    .periodInMicroseconds = NSX_USB_DEFAULT_POLL_US,
    .callback             = usb_timer_callback,
};

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

uint32_t nsx_usb_init(nsx_usb_config_t *cfg) {
    if (cfg == NULL) {
        return NS_STATUS_INVALID_HANDLE;
    }
    if (cfg->tx_buffer == NULL || cfg->rx_buffer == NULL) {
        return NS_STATUS_INVALID_CONFIG;
    }
    if (cfg->tx_buffer_len == 0 || cfg->rx_buffer_len == 0) {
        return NS_STATUS_INVALID_CONFIG;
    }
    /* Enforce minimum CDC RX buffer size.  If the application buffer is
     * smaller than CFG_TUD_CDC_EP_BUFSIZE (1024) the TinyUSB OUT endpoint
     * will never be primed and no data will ever be received. */
    if (cfg->rx_buffer_len < NSX_USB_MIN_CDC_RX_BUFSIZE) {
        return NS_STATUS_INVALID_CONFIG;
    }

    g_usb_cfg = cfg;
    cfg->_rx_ready          = 0;
    cfg->_vendor_rx_ready   = 0;
    cfg->_vendor_connected  = 0;
    cfg->_initialized       = 0;

    tusb_init();

    /* Set up the polling timer. */
    uint32_t poll_us = cfg->poll_interval_us;
    if (poll_us == 0) {
        poll_us = NSX_USB_DEFAULT_POLL_US;
    }
    g_usb_timer.periodInMicroseconds = poll_us;

    uint32_t rc = ns_timer_init(&g_usb_timer);
    if (rc != NS_STATUS_SUCCESS) {
        return NS_STATUS_INIT_FAILED;
    }

    cfg->_initialized = 1;
    return NS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Send                                                                */
/* ------------------------------------------------------------------ */

uint32_t nsx_usb_send(nsx_usb_config_t *cfg, const void *data, uint32_t len,
                       uint32_t *bytes_sent) {
    if (cfg == NULL || !cfg->_initialized) {
        return NS_STATUS_INVALID_HANDLE;
    }
    if (data == NULL || len == 0) {
        if (bytes_sent) *bytes_sent = 0;
        return NS_STATUS_SUCCESS;
    }

    uint32_t timeout_ms = cfg->timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = NSX_USB_DEFAULT_TIMEOUT_MS;
    }

    const uint8_t *src = (const uint8_t *)data;
    uint32_t remaining = len;
    uint32_t sent = 0;
    uint32_t elapsed_ms = 0;

    /* Wait until TinyUSB has room for at least our payload, bounded by
     * timeout.  Comparing against tx_buffer_len (the app buffer) was
     * wrong — TinyUSB's internal FIFO is ~512 B and that condition could
     * never be satisfied, burning the full timeout on every send. */
    while (tud_cdc_write_available() < len && elapsed_ms < timeout_ms) {
        ns_interrupt_master_disable();
        tud_cdc_write_flush();
        tud_task();
        ns_interrupt_master_enable();
        am_util_delay_ms(1);
        elapsed_ms++;
    }

    /* Write in chunks. */
    while (remaining > 0 && elapsed_ms < timeout_ms) {
        ns_interrupt_master_disable();
        uint32_t chunk = tud_cdc_write(src + sent, remaining);
        tud_cdc_write_flush();
        tud_task();
        ns_interrupt_master_enable();

        if (chunk > 0) {
            sent += chunk;
            remaining -= chunk;
        } else {
            /* No progress — back off 1 ms and retry. */
            am_util_delay_ms(1);
            elapsed_ms++;
        }
    }

    /* Final flush for parts that need it. */
#if defined(AM_PART_APOLLO5B) || defined(AM_PART_APOLLO510L) || defined(AM_PART_APOLLO330P)
    tud_cdc_write_flush();
#endif

    if (bytes_sent) {
        *bytes_sent = sent;
    }
    if (remaining > 0) {
        return NSX_USB_STATUS_TIMEOUT;
    }
    return NS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Receive                                                             */
/* ------------------------------------------------------------------ */

uint32_t nsx_usb_receive(nsx_usb_config_t *cfg, void *data, uint32_t len,
                          uint32_t *bytes_received) {
    if (cfg == NULL || !cfg->_initialized) {
        return NS_STATUS_INVALID_HANDLE;
    }
    if (data == NULL || len == 0) {
        if (bytes_received) *bytes_received = 0;
        return NS_STATUS_SUCCESS;
    }

    uint32_t timeout_ms = cfg->timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = NSX_USB_DEFAULT_TIMEOUT_MS;
    }

    uint32_t total_rx = 0;
    uint32_t elapsed_ms = 0;

    /* Poll until we have enough data or timeout. */
    while (total_rx < len && elapsed_ms < timeout_ms) {
        uint32_t avail = tud_cdc_available();
        if (avail > 0) {
            uint32_t want = len - total_rx;
            if (want > avail) want = avail;

            ns_interrupt_master_disable();
            tud_task();
            uint32_t got = tud_cdc_read((uint8_t *)data + total_rx, want);
            ns_interrupt_master_enable();

            total_rx += got;
            cfg->_rx_ready = 0;
        } else {
            am_util_delay_ms(1);
            elapsed_ms++;
        }
    }

    if (bytes_received) {
        *bytes_received = total_rx;
    }
    if (total_rx == 0 && elapsed_ms >= timeout_ms) {
        return NSX_USB_STATUS_TIMEOUT;
    }
    if (total_rx < len) {
        return NSX_USB_STATUS_PARTIAL;
    }
    return NS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Utilities                                                           */
/* ------------------------------------------------------------------ */

bool nsx_usb_connected(nsx_usb_config_t *cfg) {
    (void)cfg;
    return tud_cdc_connected();
}

bool nsx_usb_data_available(nsx_usb_config_t *cfg) {
    if (cfg == NULL) return false;
    return (cfg->_rx_ready != 0) || (tud_cdc_available() > 0);
}

void nsx_usb_flush_rx(nsx_usb_config_t *cfg) {
    if (cfg == NULL) return;
    tud_cdc_read_flush();
    cfg->_rx_ready = 0;
}

uint32_t nsx_usb_read_nb(nsx_usb_config_t *cfg, void *data, uint32_t max_len,
                          uint32_t *bytes_read) {
    if (bytes_read) *bytes_read = 0;
    if (cfg == NULL || !cfg->_initialized || data == NULL || max_len == 0) {
        return NS_STATUS_INVALID_HANDLE;
    }
    /* Protect against the USB timer ISR calling tud_task() while we
     * read the RX FIFO — TinyUSB is not re-entrant.
     * Pump tud_task() first so any pending OUT completions are
     * processed into the CDC RX FIFO before we check available(). */
    ns_interrupt_master_disable();
    tud_task();
    uint32_t avail = tud_cdc_available();
    uint32_t got = 0;
    if (avail > 0) {
        uint32_t want = (avail < max_len) ? avail : max_len;
        got = tud_cdc_read(data, want);
    }
    ns_interrupt_master_enable();
    if (bytes_read) *bytes_read = got;
    cfg->_rx_ready = 0;
    return NS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Compatibility shims for SDK's patched cdc_device_ns.c               */
/* These functions are called from the TinyUSB CDC driver to override  */
/* internal FIFO buffers with app-provided buffers.                    */
/* ------------------------------------------------------------------ */

uint8_t *ns_usb_get_rx_buffer(void) {
    return (g_usb_cfg != NULL) ? g_usb_cfg->rx_buffer : NULL;
}

uint8_t *ns_usb_get_tx_buffer(void) {
    return (g_usb_cfg != NULL) ? g_usb_cfg->tx_buffer : NULL;
}

uint32_t ns_get_cdc_rx_bufferLength(void) {
    return (g_usb_cfg != NULL) ? g_usb_cfg->rx_buffer_len : 0;
}

uint32_t ns_get_cdc_tx_bufferLength(void) {
    return (g_usb_cfg != NULL) ? g_usb_cfg->tx_buffer_len : 0;
}

/* ================================================================== */
/* Vendor (WebUSB) API                                                 */
/* ================================================================== */

uint32_t nsx_usb_vendor_send(nsx_usb_config_t *cfg, const void *data,
                              uint32_t len, uint32_t *bytes_sent) {
    if (cfg == NULL || !cfg->_initialized) {
        return NS_STATUS_INVALID_HANDLE;
    }
    if (data == NULL || len == 0) {
        if (bytes_sent) *bytes_sent = 0;
        return NS_STATUS_SUCCESS;
    }

    uint32_t timeout_ms = cfg->timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = NSX_USB_DEFAULT_TIMEOUT_MS;
    }

    const uint8_t *src = (const uint8_t *)data;
    uint32_t remaining = len;
    uint32_t sent = 0;
    uint32_t elapsed_ms = 0;

    while (remaining > 0 && elapsed_ms < timeout_ms) {
        ns_interrupt_master_disable();
        uint32_t avail = tud_vendor_write_available();
        uint32_t chunk = 0;
        if (avail > 0) {
            uint32_t want = (remaining < avail) ? remaining : avail;
            chunk = tud_vendor_write(src + sent, want);
            tud_vendor_flush();
        }
        tud_task();
        ns_interrupt_master_enable();

        if (chunk > 0) {
            sent += chunk;
            remaining -= chunk;
        } else {
            am_util_delay_ms(1);
            elapsed_ms++;
        }
    }

    if (bytes_sent) *bytes_sent = sent;
    return (remaining > 0) ? NSX_USB_STATUS_TIMEOUT : NS_STATUS_SUCCESS;
}

uint32_t nsx_usb_vendor_read_nb(nsx_usb_config_t *cfg, void *data,
                                 uint32_t max_len, uint32_t *bytes_read) {
    if (bytes_read) *bytes_read = 0;
    if (cfg == NULL || !cfg->_initialized || data == NULL || max_len == 0) {
        return NS_STATUS_INVALID_HANDLE;
    }
    ns_interrupt_master_disable();
    tud_task();
    uint32_t avail = tud_vendor_available();
    uint32_t got = 0;
    if (avail > 0) {
        uint32_t want = (avail < max_len) ? avail : max_len;
        got = tud_vendor_read(data, want);
    }
    ns_interrupt_master_enable();
    if (bytes_read) *bytes_read = got;
    cfg->_vendor_rx_ready = 0;
    return NS_STATUS_SUCCESS;
}

bool nsx_usb_vendor_connected(nsx_usb_config_t *cfg) {
    if (cfg == NULL) return false;
    return cfg->_vendor_connected != 0;
}

bool nsx_usb_vendor_data_available(nsx_usb_config_t *cfg) {
    if (cfg == NULL) return false;
    return (cfg->_vendor_rx_ready != 0) || (tud_vendor_available() > 0);
}
