#include "cast_controller.h"

#include "esphome/core/log.h"
#include "freertos/task.h"

#include <cstring>
#include <cstdio>

// Shared Cast stack (components/cast/) — built into both targets.
extern "C" {
#include "cast_discovery.h"
#include "cast_session.h"
#include "cast_types.h"
}

namespace esphome {
namespace cast_controller {

static const char *const TAG = "cast_controller";

void CastController::setup() {
  lock_ = xSemaphoreCreateMutex();
  ESP_LOGCONFIG(TAG, "starting Cast task (shared components/cast)");
  // Bigger stack than discovery-only: a session does TLS + JSON parsing.
  xTaskCreate(task_trampoline, "cast", 8192, this, 3, nullptr);
}

void CastController::task_trampoline(void *arg) {
  static_cast<CastController *>(arg)->task_main();
}

void CastController::task_main() {
  cast_discovery_init();
  static cast_device_t scan[CAST_MAX_DEVICES];
  for (;;) {
    int n = cast_discovery_scan(scan, CAST_MAX_DEVICES, 3000);
    xSemaphoreTake(lock_, portMAX_DELAY);
    snap_count_ = n;
    xSemaphoreGive(lock_);

    // PHASE-3 stub: follow the first discovered device's now-playing for a while.
    // (Device selection + commands + a warm pool come later — see docs/08.)
    if (n > 0) {
      cast_session_t *sess = cast_session_open(&scan[0]);
      if (sess) {
        ESP_LOGI(TAG, "session open: %s", scan[0].friendly_name);
        for (int i = 0; i < 40 && cast_session_poll(sess, 500); i++) {
          cast_media_status_t m;
          cast_session_get_media(sess, &m);
          cast_volume_status_t v;
          cast_session_get_volume(sess, &v);
          xSemaphoreTake(lock_, portMAX_DELAY);
          snprintf(snap_now_, sizeof(snap_now_), "%s%s%s", m.title,
                   m.subtitle[0] ? " - " : "", m.subtitle);
          snap_volume_ = (int) (v.level * 100 + 0.5f);
          xSemaphoreGive(lock_);
        }
        cast_session_close(sess);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void CastController::loop() {
  // Snapshot under lock, then publish (publish_state must run on this thread).
  int count, volume;
  char now[sizeof(snap_now_)];
  xSemaphoreTake(lock_, portMAX_DELAY);
  count = snap_count_;
  volume = snap_volume_;
  std::strncpy(now, snap_now_, sizeof(now));
  xSemaphoreGive(lock_);
  now[sizeof(now) - 1] = '\0';

  if (devices_found_ && count != pub_count_) {
    pub_count_ = count;
    if (count >= 0) devices_found_->publish_state(count);
  }
  if (volume_ && volume != pub_volume_) {
    pub_volume_ = volume;
    if (volume >= 0) volume_->publish_state(volume);
  }
  if (now_playing_ && std::strcmp(now, pub_now_) != 0) {
    std::strncpy(pub_now_, now, sizeof(pub_now_));
    now_playing_->publish_state(now);
  }
}

void CastController::dump_config() {
  ESP_LOGCONFIG(TAG, "Cast Controller (shared components/cast)");
  LOG_SENSOR("  ", "Devices found", devices_found_);
  LOG_SENSOR("  ", "Volume", volume_);
  LOG_TEXT_SENSOR("  ", "Now playing", now_playing_);
}

float CastController::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

}  // namespace cast_controller
}  // namespace esphome
