/**
 * @file ns_usb.h
 * @brief Compatibility shim for SDK's patched cdc_device_ns.c
 *
 * The AmbiqSuite R5 SDK ships a patched TinyUSB CDC driver
 * (cdc_device_ns.c) that includes "ns_usb.h" and calls a small set of
 * functions to override internal FIFO buffers. This header provides
 * those declarations so the SDK source compiles against nsx-usb.
 */
#ifndef NS_USB_H
#define NS_USB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t *ns_usb_get_rx_buffer(void);
uint8_t *ns_usb_get_tx_buffer(void);
uint32_t ns_get_cdc_rx_bufferLength(void);
uint32_t ns_get_cdc_tx_bufferLength(void);

#ifdef __cplusplus
}
#endif

#endif /* NS_USB_H */
