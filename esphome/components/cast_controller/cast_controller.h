#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace esphome {
namespace cast_controller {

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
  void set_volume_sensor(sensor::Sensor *s) { volume_ = s; }

 protected:
  static void task_trampoline(void *arg);
  void task_main();

  sensor::Sensor *devices_found_{nullptr};
  text_sensor::TextSensor *now_playing_{nullptr};
  sensor::Sensor *volume_{nullptr};

  // Shared snapshot: task writes under lock_, loop() reads.
  SemaphoreHandle_t lock_{nullptr};
  int snap_count_{-1};
  int snap_volume_{-1};
  char snap_now_[160]{0};

  // Last value published to each entity (de-dupe).
  int pub_count_{-2};
  int pub_volume_{-2};
  char pub_now_[160]{0};
};

}  // namespace cast_controller
}  // namespace esphome
