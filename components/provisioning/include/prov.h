// Wi-Fi web provisioning: serves an HTML form on the SoftAP and captures the
// home-network credentials the user submits. The caller (app) brings the SoftAP
// up first (wifi_start_ap), then runs this server.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start/stop the provisioning HTTP server (device at 192.168.4.1).
void prov_start(void);
void prov_stop(void);

// Non-blocking: returns true once the user has submitted credentials, copying
// them into the caller's buffers.
bool prov_take_creds(char *ssid, int ssid_len, char *pass, int pass_len);

#ifdef __cplusplus
}
#endif
