#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include <string>

namespace esphome {
namespace sh8601 {
class SH8601;
}
namespace drv2605 {
class DRV2605;
}
namespace cast_controller {

// Transport command kinds (HA/UI -> session task, via cmd_q_).
enum CastCmdKind : uint8_t {
  CMD_VOLUME = 0,  // absolute, arg = 0..1
  CMD_VOLUME_STEP, // relative, arg = delta (-1..1)
  CMD_MUTE,
  CMD_PLAYPAUSE,
  CMD_NEXT,
  CMD_PREV,
  CMD_NEXT_DEVICE,  // switch the active speaker (+1 / -1, by arg sign)
  CMD_SELECT_DEVICE,  // switch to an absolute device index (arg = index)
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
  void set_display(sh8601::SH8601 *d) { display_ = d; }  // for the 'S' screenshot key
  void set_haptics(drv2605::DRV2605 *h) { haptics_ = h; }  // click per physical detent/press

  // Control entry points (call from ESPHome lambdas/main loop). They enqueue a
  // command for the session task to apply — the session is single-threaded.
  void request_set_volume(float pct);     // absolute, 0..100
  void request_step_volume(float pct);    // relative, e.g. +3 / -3 (knob detent)
  void request_mute();
  void request_play_pause();
  void request_next();
  void request_prev();
  void request_next_device();   // switch to the next discovered speaker
  void request_prev_device();
  void request_select_device(int index);  // switch to an absolute speaker index

  // Newline-joined list of discovered speaker names, for the LVGL roller on the
  // speaker-list page (call from a lambda; takes the snapshot lock briefly).
  std::string device_list_str();
  int device_count();
  int current_index();   // index of the active speaker (for per-speaker UI color)

  // Speaker-list mode: while the list page is shown, the encoder scrolls the
  // roller instead of changing volume. cast_controller stays LVGL-free — it just
  // buffers the scroll; an LVGL interval in the YAML applies take_list_scroll()
  // to the roller.
  void set_list_mode(bool on) { list_mode_ = on; list_scroll_ = 0; }
  int take_list_scroll() { int s = list_scroll_; list_scroll_ = 0; return s; }

  // Screen-sleep helpers. The encoder lives outside LVGL, so the YAML resets
  // LVGL's inactivity (and wakes) when the knob moves; while asleep the detent is
  // swallowed so waking by turning doesn't also change volume.
  void set_asleep(bool s) { asleep_ = s; }
  bool is_asleep() { return asleep_; }
  bool take_encoder_activity() { bool a = enc_activity_; enc_activity_ = false; return a; }

 protected:
  static void task_trampoline(void *arg);
  void task_main();
  void enqueue(uint8_t kind, float arg);

  // Physical knob: PCNT encoder + GPIO button, decoded by the SAME algorithm as
  // the vanilla components/bsp/knob.c (this board pulses PCNT to +/-1 per detent
  // and returns to 0 — ESPHome's stock rotary_encoder can't decode that). State
  // is updated by a poll task + ISR (file-scope statics in the .cpp) and consumed
  // on the main loop, where request_*/haptics can run safely.
  void knob_setup();
  void knob_poll();   // consume decoded detents/presses (call from loop())

  sensor::Sensor *devices_found_{nullptr};
  text_sensor::TextSensor *now_playing_{nullptr};
  text_sensor::TextSensor *current_device_{nullptr};
  text_sensor::TextSensor *art_url_{nullptr};
  sensor::Sensor *volume_{nullptr};

  QueueHandle_t cmd_q_{nullptr};   // HA/UI commands -> session task
  int sel_index_{0};               // active device index (task-owned)
  sh8601::SH8601 *display_{nullptr};
  drv2605::DRV2605 *haptics_{nullptr};
  bool list_mode_{false};   // encoder scrolls the speaker list instead of volume
  int list_scroll_{0};      // buffered detents while in list mode
  bool asleep_{false};      // screen is in sleep (panel + backlight off)
  bool enc_activity_{false};// an encoder detent occurred since last poll (for wake)

  // Shared snapshot: task writes under lock_, loop() reads.
  SemaphoreHandle_t lock_{nullptr};
  int snap_count_{-1};
  int snap_volume_{-1};
  int snap_index_{0};         // active speaker index
  char snap_now_[160]{0};
  char snap_device_[64]{0};
  char snap_art_[512]{0};
  char snap_list_[1024]{0};   // newline-joined discovered speaker names

  // Last value published to each entity (de-dupe).
  int pub_count_{-2};
  int pub_volume_{-2};
  char pub_now_[160]{0};
  char pub_device_[64]{0};
  char pub_art_[512]{0};
};

}  // namespace cast_controller
}  // namespace esphome
