#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace cast_controller {

// ESPHome wrapper over the shared components/cast/ stack. Discovery + (later)
// Cast sessions run on a background FreeRTOS task — like the IDF app's net loop —
// so they never block ESPHome's cooperative main loop. The task publishes state
// that loop() reads and surfaces to ESPHome/HA.
class CastController : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

 protected:
  static void task_trampoline(void *arg);
  void task_main();

  volatile int device_count_{-1};  // written by the task, read by loop()
  int last_logged_{-2};
};

}  // namespace cast_controller
}  // namespace esphome
