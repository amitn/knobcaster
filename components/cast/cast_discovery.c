#include "cast_discovery.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mdns.h"
#include "esp_log.h"

static const char *TAG = "cast.disc";

void cast_discovery_init(void)
{
    ESP_ERROR_CHECK(mdns_init());
}

// Case-insensitive substring search (strcasestr isn't portable in newlib).
static bool contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return false;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0) return true;
    }
    return false;
}

// Find a TXT value by key (case-insensitive). Returns NULL if absent.
static const char *txt_lookup(const mdns_result_t *r, const char *key)
{
    for (size_t i = 0; i < r->txt_count; i++) {
        if (r->txt[i].key && strcasecmp(r->txt[i].key, key) == 0) {
            return r->txt[i].value;
        }
    }
    return NULL;
}

int cast_discovery_scan(cast_device_t *out, int max_devices, int timeout_ms)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_googlecast", "_tcp", timeout_ms,
                                   max_devices, &results);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_query_ptr failed: %s", esp_err_to_name(err));
        return 0;
    }

    int n = 0;
    for (mdns_result_t *r = results; r != NULL && n < max_devices; r = r->next) {
        cast_device_t *d = &out[n];
        memset(d, 0, sizeof(*d));

        const char *fn = txt_lookup(r, "fn");
        const char *md = txt_lookup(r, "md");
        const char *id = txt_lookup(r, "id");
        const char *ca = txt_lookup(r, "ca");   // capabilities bitmask

        strlcpy(d->friendly_name,
                fn ? fn : (r->instance_name ? r->instance_name : "?"),
                sizeof(d->friendly_name));
        if (md) strlcpy(d->model, md, sizeof(d->model));
        if (id) strlcpy(d->id, id, sizeof(d->id));
        d->port = r->port;

        // First IPv4 address from the record's address list.
        for (mdns_ip_addr_t *a = r->addr; a != NULL; a = a->next) {
            if (a->addr.type == ESP_IPADDR_TYPE_V4) {
                d->ip.addr = a->addr.u_addr.ip4.addr;
                break;
            }
        }

        int ca_bits = ca ? atoi(ca) : 0;

        // Hide stereo-pair member speakers: when two speakers are bonded into a
        // stereo pair, each advertises capability bit 0x100 (the pair's group
        // endpoint does NOT). Skip the members so only the pair's group shows —
        // controlling the individual halves of a stereo pair isn't wanted.
        // (Verified against two pairs on a real network; see docs/03-cast-protocol.)
        if (ca_bits & 0x100) {
            ESP_LOGD(TAG, "hiding stereo-pair member: %s", d->friendly_name);
            continue;   // don't add to out[]; reuse this slot next iteration
        }

        // Group = model says "group", or capabilities bit 0x20 (multizone group).
        d->is_group = contains_ci(md, "group") || (ca_bits & 0x20);

        n++;
    }

    mdns_query_results_free(results);
    return n;
}
