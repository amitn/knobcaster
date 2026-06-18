// GitHub Releases OTA. See ota.h for the contract.
#include "ota.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "ota";

// Repository to pull releases from. GitHub's stable "latest" endpoints avoid the
// (UA-required, large-JSON) REST API: /releases/latest 302-redirects to the tag
// page, and /releases/latest/download/<asset> 302-redirects to the binary.
#define OTA_REPO   "amitn/knobcaster"
#define LATEST_URL "https://github.com/" OTA_REPO "/releases/latest"
#define ASSET_URL  "https://github.com/" OTA_REPO "/releases/latest/download/firmware.bin"
#define OTA_UA     "knobcaster-ota"

static TaskHandle_t       s_task;
static volatile ota_state_t s_state = OTA_IDLE;
static volatile int       s_pct;
static char               s_latest_tag[40];   // filled by the redirect header cb

// --- version compare (leading vMAJOR.MINOR.PATCH; ignores any -gSHA suffix) ---
static bool semver(const char *s, int v[3])
{
    while (*s == 'v' || *s == 'V') s++;
    v[0] = v[1] = v[2] = 0;
    return sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]) >= 1;
}

static bool semver_newer(const int a[3], const int b[3])   // a > b ?
{
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

// Capture the `Location` of the 302 from /releases/latest -> /releases/tag/<tag>.
static esp_err_t latest_header_cb(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER &&
        strcasecmp(e->header_key, "Location") == 0) {
        const char *p = strstr(e->header_value, "/tag/");
        if (p) strlcpy(s_latest_tag, p + 5, sizeof(s_latest_tag));
    }
    return ESP_OK;
}

// Resolve the latest release tag via the redirect Location header. Returns false
// if there is no release or the request fails.
static bool fetch_latest_tag(char *out, size_t cap)
{
    s_latest_tag[0] = '\0';
    esp_http_client_config_t cfg = {
        .url                   = LATEST_URL,
        .crt_bundle_attach     = esp_crt_bundle_attach,
        .event_handler         = latest_header_cb,
        .disable_auto_redirect = true,     // we want to read the 302 Location
        .timeout_ms            = 10000,
        .user_agent            = OTA_UA,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "latest check failed: %s", esp_err_to_name(err));
        return false;
    }
    if (!s_latest_tag[0]) {
        ESP_LOGW(TAG, "no release tag (HTTP %d)", status);   // 404 = no releases
        return false;
    }
    strlcpy(out, s_latest_tag, cap);
    return true;
}

// Stream the latest firmware.bin into the inactive slot and set it as boot.
static bool download_and_install(void)
{
    esp_http_client_config_t http = {
        .url               = ASSET_URL,    // 302 -> signed asset URL (followed)
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 20000,
        .keep_alive_enable = true,
        .user_agent        = OTA_UA,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http };

    esp_https_ota_handle_t h = NULL;
    if (esp_https_ota_begin(&ota_cfg, &h) != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin failed");
        return false;
    }

    int total = esp_https_ota_get_image_size(h);
    esp_err_t r;
    while ((r = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int got = esp_https_ota_get_image_len_read(h);
        s_pct = (total > 0) ? (got * 100 / total) : 0;
    }

    bool ok = (r == ESP_OK) && esp_https_ota_is_complete_data_received(h);
    if (esp_https_ota_finish(h) != ESP_OK) ok = false;   // also validates the image
    if (ok) s_pct = 100;
    else    ESP_LOGE(TAG, "ota failed: perform=%s", esp_err_to_name(r));
    return ok;
}

static void check_and_update(void)
{
    s_state = OTA_CHECKING;
    const char *running = esp_app_get_description()->version;

    char latest[40];
    if (!fetch_latest_tag(latest, sizeof(latest))) { s_state = OTA_FAILED; return; }

    // Update when the release parses and is strictly newer. An unparseable
    // running version (a dev build with no git tag / version.txt) is treated as
    // out-of-date, so a bare board pulls the latest release.
    int rv[3], lv[3];
    bool rok = semver(running, rv), lok = semver(latest, lv);
    ESP_LOGI(TAG, "running %s, latest %s", running, latest);
    if (!lok || (rok && !semver_newer(lv, rv))) {
        ESP_LOGI(TAG, "up to date");
        s_state = OTA_IDLE;
        return;
    }

    ESP_LOGW(TAG, "updating %s -> %s", running, latest);
    s_state = OTA_UPDATING;
    s_pct = 0;
    if (download_and_install()) {
        ESP_LOGW(TAG, "update installed; rebooting into %s", latest);
        vTaskDelay(pdMS_TO_TICKS(500));   // let the log flush
        esp_restart();
    }
    s_state = OTA_FAILED;
}

static void ota_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // wait for ota_check_now()
        check_and_update();
    }
}

void ota_start(void)
{
    if (s_task) return;
    // Low priority, generous stack: TLS + OTA writes need headroom.
    xTaskCreate(ota_task, "ota", 8192, NULL, 2, &s_task);
    ESP_LOGI(TAG, "OTA ready (repo %s, running %s)",
             OTA_REPO, esp_app_get_description()->version);
}

void ota_check_now(void)
{
    if (!s_task) return;
    if (s_state == OTA_CHECKING || s_state == OTA_UPDATING) return;   // coalesce
    xTaskNotifyGive(s_task);
}

void ota_mark_valid(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            ESP_LOGI(TAG, "running image marked valid (rollback cancelled)");
    }
}

ota_state_t ota_state(void)      { return s_state; }
int         ota_progress_pct(void) { return s_pct; }
