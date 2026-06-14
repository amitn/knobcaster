// Wi-Fi station: connect + auto-reconnect with backoff.
//
// Minimal, event-driven station manager. Credentials are passed in (from
// include/secrets.h for MVP; later from NVS / provisioning). The rest of the
// firmware only needs wifi_start() + wifi_is_connected().
#pragma once

#include <stdbool.h>
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize NETIF station + Wi-Fi driver and begin connecting to (ssid, pass).
// Must be called after nvs_flash_init(), esp_netif_init() and
// esp_event_loop_create_default(). Non-blocking: returns immediately; connection
// happens asynchronously. Reconnects automatically on drop.
void wifi_start(const char *ssid, const char *pass);

// True once an IP address has been acquired.
bool wifi_is_connected(void);

// Block until connected or until timeout_ms elapses. Pass portMAX_DELAY-style
// large value to wait indefinitely. Returns true if connected.
bool wifi_wait_connected(int timeout_ms);

// Last acquired IPv4 address (0.0.0.0 if never connected).
esp_ip4_addr_t wifi_get_ip(void);

#ifdef __cplusplus
}
#endif
