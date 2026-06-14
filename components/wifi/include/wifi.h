// Wi-Fi station: init, connect, scan, auto-reconnect, and NVS-stored creds.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    bool    secure;   // not an open network
} wifi_ap_t;

// Initialize NETIF station + Wi-Fi driver and start it (no connection yet).
// Call after nvs_flash_init(), esp_netif_init() and esp_event_loop_create_default().
void wifi_init(void);

// (Re)connect to (ssid, pass). Reconnects automatically on drop. Non-blocking.
void wifi_connect_to(const char *ssid, const char *pass);

// True once an IP address has been acquired.
bool wifi_is_connected(void);

// Block until connected or timeout_ms elapses. Returns true if connected.
bool wifi_wait_connected(int timeout_ms);

// Last acquired IPv4 address (0.0.0.0 if never connected).
esp_ip4_addr_t wifi_get_ip(void);

// Start/stop an open SoftAP for web provisioning (device is reachable at
// 192.168.4.1). Uses APSTA mode so the STA can connect once creds are entered.
void wifi_start_ap(const char *ssid);
void wifi_stop_ap(void);

// Scan for access points (blocking). Fills out[0..max-1] (deduped by SSID,
// hidden SSIDs skipped). Returns the number found.
int wifi_scan(wifi_ap_t *out, int max, int timeout_ms);

// Load/save the last-used credentials in NVS (namespace "wifi").
bool wifi_creds_load(char *ssid, int ssid_len, char *pass, int pass_len);
void wifi_creds_save(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
