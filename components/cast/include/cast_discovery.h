// Cast discovery — browse `_googlecast._tcp` via mDNS.
#pragma once

#include "cast_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize mDNS. Call once after the network is up.
void cast_discovery_init(void);

// Run a one-shot PTR query for Cast devices, filling `out` (up to max_devices).
// Blocks for up to timeout_ms while collecting responses. Returns the number of
// devices found.
int cast_discovery_scan(cast_device_t *out, int max_devices, int timeout_ms);

#ifdef __cplusplus
}
#endif
