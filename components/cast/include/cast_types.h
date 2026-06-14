// Shared Cast data types.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_netif_ip_addr.h"

#define CAST_MAX_DEVICES 16
#define CAST_NAME_LEN    64   // friendly name ("Kitchen speaker")
#define CAST_ID_LEN      40   // device UUID
#define CAST_MODEL_LEN   48   // model ("Google Nest Mini")

// A Google Cast device discovered on the LAN.
typedef struct {
    char           friendly_name[CAST_NAME_LEN];
    char           id[CAST_ID_LEN];
    char           model[CAST_MODEL_LEN];
    esp_ip4_addr_t ip;
    uint16_t       port;       // usually 8009
    bool           is_group;   // Cast group vs single device
} cast_device_t;
