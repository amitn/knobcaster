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

struct CastCmd {
  uint8_t kind;
  float arg;
};

void CastController::setup() {
  lock_ = xSemaphoreCreateMutex();
  cmd_q_ = xQueueCreate(8, sizeof(CastCmd));
  ESP_LOGCONFIG(TAG, "starting Cast task (shared components/cast)");
  // Bigger stack than discovery-only: a session does TLS + JSON parsing.
  xTaskCreate(task_trampoline, "cast", 8192, this, 3, nullptr);
}

void CastController::task_trampoline(void *arg) {
  static_cast<CastController *>(arg)->task_main();
}

// Apply a queued HA/UI command to the active session (runs on the task thread).
static void apply_cmd(cast_session_t *sess, const CastCmd &c) {
  if (!sess) return;
  switch (c.kind) {
    case CMD_VOLUME: cast_session_set_volume(sess, c.arg); break;
    case CMD_VOLUME_STEP: cast_session_step_volume(sess, c.arg); break;
    case CMD_MUTE: {
      cast_volume_status_t v;
      cast_session_get_volume(sess, &v);
      cast_session_set_muted(sess, !v.muted);
      break;
    }
    case CMD_PLAYPAUSE: cast_session_toggle_pause(sess); break;
    case CMD_NEXT: cast_session_next(sess); break;
    case CMD_PREV: cast_session_prev(sess); break;
  }
}

void CastController::task_main() {
  cast_discovery_init();
  static cast_device_t scan[CAST_MAX_DEVICES];
  cast_session_t *sess = nullptr;

  for (;;) {
    // Periodic discovery; keep following the first device (selection: TODO).
    int n = cast_discovery_scan(scan, CAST_MAX_DEVICES, 3000);
    xSemaphoreTake(lock_, portMAX_DELAY);
    snap_count_ = n;
    xSemaphoreGive(lock_);

    if (n > 0 && !sess) {
      sess = cast_session_open(&scan[0]);
      if (sess) {
        ESP_LOGI(TAG, "session open: %s", scan[0].friendly_name);
        xSemaphoreTake(lock_, portMAX_DELAY);
        std::strncpy(snap_device_, scan[0].friendly_name, sizeof(snap_device_) - 1);
        snap_device_[sizeof(snap_device_) - 1] = '\0';
        xSemaphoreGive(lock_);
      }
    }

    // Service the session for a while: drain commands, poll, publish snapshots.
    for (int i = 0; i < 60 && sess; i++) {
      CastCmd c;
      while (xQueueReceive(cmd_q_, &c, 0) == pdTRUE) apply_cmd(sess, c);

      if (!cast_session_poll(sess, 500)) {  // died -> reopen next discovery cycle
        cast_session_close(sess);
        sess = nullptr;
        break;
      }
      cast_media_status_t m;
      cast_session_get_media(sess, &m);
      cast_volume_status_t v;
      cast_session_get_volume(sess, &v);
      xSemaphoreTake(lock_, portMAX_DELAY);
      snprintf(snap_now_, sizeof(snap_now_), "%s%s%s", m.title,
               m.subtitle[0] ? " - " : "", m.subtitle);
      snap_volume_ = (int) (v.level * 100 + 0.5f);
      std::strncpy(snap_art_, m.art_url, sizeof(snap_art_) - 1);
      snap_art_[sizeof(snap_art_) - 1] = '\0';
      xSemaphoreGive(lock_);
    }
    if (!sess) vTaskDelay(pdMS_TO_TICKS(2000));  // back off before re-discovering
  }
}

void CastController::enqueue(uint8_t kind, float arg) {
  if (!cmd_q_) return;
  CastCmd c{kind, arg};
  xQueueSend(cmd_q_, &c, 0);
}
void CastController::request_set_volume(float pct) { enqueue(CMD_VOLUME, pct / 100.0f); }
void CastController::request_step_volume(float pct) { enqueue(CMD_VOLUME_STEP, pct / 100.0f); }
void CastController::request_mute() { enqueue(CMD_MUTE, 0); }
void CastController::request_play_pause() { enqueue(CMD_PLAYPAUSE, 0); }
void CastController::request_next() { enqueue(CMD_NEXT, 0); }
void CastController::request_prev() { enqueue(CMD_PREV, 0); }

void CastController::loop() {
  // Snapshot under lock, then publish (publish_state must run on this thread).
  int count, volume;
  char now[sizeof(snap_now_)], device[sizeof(snap_device_)], art[sizeof(snap_art_)];
  xSemaphoreTake(lock_, portMAX_DELAY);
  count = snap_count_;
  volume = snap_volume_;
  std::strncpy(now, snap_now_, sizeof(now));
  std::strncpy(device, snap_device_, sizeof(device));
  std::strncpy(art, snap_art_, sizeof(art));
  xSemaphoreGive(lock_);
  now[sizeof(now) - 1] = '\0';
  device[sizeof(device) - 1] = '\0';
  art[sizeof(art) - 1] = '\0';

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
  if (current_device_ && std::strcmp(device, pub_device_) != 0) {
    std::strncpy(pub_device_, device, sizeof(pub_device_));
    current_device_->publish_state(device);
  }
  if (art_url_ && std::strcmp(art, pub_art_) != 0) {
    std::strncpy(pub_art_, art, sizeof(pub_art_));
    art_url_->publish_state(art);
  }
}

void CastController::dump_config() {
  ESP_LOGCONFIG(TAG, "Cast Controller (shared components/cast)");
  LOG_SENSOR("  ", "Devices found", devices_found_);
  LOG_SENSOR("  ", "Volume", volume_);
  LOG_TEXT_SENSOR("  ", "Now playing", now_playing_);
  LOG_TEXT_SENSOR("  ", "Current device", current_device_);
  LOG_TEXT_SENSOR("  ", "Art URL", art_url_);
}

float CastController::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

}  // namespace cast_controller
}  // namespace esphome
