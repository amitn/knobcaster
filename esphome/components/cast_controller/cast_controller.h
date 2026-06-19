#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace cast_controller {

// Thin ESPHome wrapper over the shared components/cast/ stack. Phase 3 will grow
// this to expose now-playing state + volume/transport actions to ESPHome/HA.
class CastController : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;
};

}  // namespace cast_controller
}  // namespace esphome
