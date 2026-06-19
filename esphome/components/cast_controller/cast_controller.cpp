#include "cast_controller.h"

#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Shared Cast stack (components/cast/) — built into both targets.
extern "C" {
#include "cast_discovery.h"
#include "cast_types.h"
}

namespace esphome {
namespace cast_controller {

static const char *const TAG = "cast_controller";

void CastController::setup() {
  ESP_LOGCONFIG(TAG, "starting Cast discovery task (shared components/cast)");
  // Generous stack: mDNS + (later) TLS. Mirrors the IDF app's net task.
  xTaskCreate(task_trampoline, "cast", 6144, this, 3, nullptr);
}

void CastController::task_trampoline(void *arg) {
  static_cast<CastController *>(arg)->task_main();
}

void CastController::task_main() {
  cast_discovery_init();
  static cast_device_t scan[CAST_MAX_DEVICES];
  for (;;) {
    int n = cast_discovery_scan(scan, CAST_MAX_DEVICES, 3000);
    for (int i = 0; i < n; i++) {
      ESP_LOGD(TAG, "  [%d] %s (%s)%s", i, scan[i].friendly_name, scan[i].model,
               scan[i].is_group ? " (group)" : "");
    }
    device_count_ = n;
    vTaskDelay(pdMS_TO_TICKS(30000));  // periodic rescan, like the IDF net loop
  }
}

void CastController::loop() {
  int n = device_count_;
  if (n != last_logged_) {
    last_logged_ = n;
    if (n >= 0) ESP_LOGI(TAG, "discovered %d Cast device(s)", n);
  }
}

void CastController::dump_config() {
  ESP_LOGCONFIG(TAG, "Cast Controller (shared components/cast): discovery active");
}

float CastController::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

}  // namespace cast_controller
}  // namespace esphome
