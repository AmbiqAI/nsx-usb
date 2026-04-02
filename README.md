# nsx-usb

USB CDC serial driver for Ambiq SoCs, backed by TinyUSB.

This module is the NSX successor to the legacy `ns-usb` module with
significant cleanup.

## Key changes from legacy `ns-usb`

- **No printf in driver code** — all 15 active `ns_lp_printf` calls removed.
  Errors are returned as status codes.
- **No arbitrary delays** — the 8 `ns_delay_us` sites (including a 1-second
  brute-force in the error handler) are replaced with bounded polling loops
  that return timeout errors instead of blocking forever.
- **Function name fix** — `ns_usb_recieve_data` → `nsx_usb_receive`.
- **Proper error returns** — send and receive return status codes; the caller
  gets an out-parameter byte count.
- **CDC-only** — WebUSB vendor class is dropped for a clean start.
- **Configurable poll interval** — the TinyUSB timer period is a config field
  (default 1 ms).
- **Builds TinyUSB from nsx-ambiqsuite-r5** — creates a proper CMake
  target for the TinyUSB sources that were previously Make-only.

## Supported SoCs

Apollo4P, Apollo5B, Apollo510, Apollo510B (any SoC with USB peripheral).

## Public API

```c
#include "nsx_usb.h"

uint32_t nsx_usb_init(nsx_usb_config_t *cfg);
uint32_t nsx_usb_send(nsx_usb_config_t *cfg, const void *data, uint32_t len, uint32_t *bytes_sent);
uint32_t nsx_usb_receive(nsx_usb_config_t *cfg, void *data, uint32_t len, uint32_t *bytes_received);
bool     nsx_usb_connected(nsx_usb_config_t *cfg);
```
