#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

namespace esphome {
namespace cast_controller {

// Transport command kinds (HA/UI -> session task, via cmd_q_).
enum CastCmdKind : uint8_t {
  CMD_VOLUME = 0,  // absolute, arg = 0..1
  CMD_VOLUME_STEP, // relative, arg = delta (-1..1)
  CMD_MUTE,
  CMD_PLAYPAUSE,
  CMD_NEXT,
  CMD_PREV,
};

// ESPHome wrapper over the shared components/cast/ stack. Discovery + Cast
// sessions run on a background FreeRTOS task (like the IDF app's net loop) so
// they never block ESPHome's cooperative main loop. The task writes snapshot
// state under a mutex; loop() reads it and publishes to ESPHome/HA entities
// (publish_state must run on the main-loop thread).
class CastController : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_devices_found_sensor(sensor::Sensor *s) { devices_found_ = s; }
  void set_now_playing_sensor(text_sensor::TextSensor *s) { now_playing_ = s; }
  void set_current_device_sensor(text_sensor::TextSensor *s) { current_device_ = s; }
  void set_art_url_sensor(text_sensor::TextSensor *s) { art_url_ = s; }
  void set_volume_sensor(sensor::Sensor *s) { volume_ = s; }

  // Control entry points (call from ESPHome lambdas/main loop). They enqueue a
  // command for the session task to apply — the session is single-threaded.
  void request_set_volume(float pct);     // absolute, 0..100
  void request_step_volume(float pct);    // relative, e.g. +3 / -3 (knob detent)
  void request_mute();
  void request_play_pause();
  void request_next();
  void request_prev();

 protected:
  static void task_trampoline(void *arg);
  void task_main();
  void enqueue(uint8_t kind, float arg);

  sensor::Sensor *devices_found_{nullptr};
  text_sensor::TextSensor *now_playing_{nullptr};
  text_sensor::TextSensor *current_device_{nullptr};
  text_sensor::TextSensor *art_url_{nullptr};
  sensor::Sensor *volume_{nullptr};

  QueueHandle_t cmd_q_{nullptr};   // HA/UI commands -> session task

  // Shared snapshot: task writes under lock_, loop() reads.
  SemaphoreHandle_t lock_{nullptr};
  int snap_count_{-1};
  int snap_volume_{-1};
  char snap_now_[160]{0};
  char snap_device_[64]{0};
  char snap_art_[512]{0};

  // Last value published to each entity (de-dupe).
  int pub_count_{-2};
  int pub_volume_{-2};
  char pub_now_[160]{0};
  char pub_device_[64]{0};
  char pub_art_[512]{0};
};

}  // namespace cast_controller
}  // namespace esphome
