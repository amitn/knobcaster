#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace drv2605 {

// Minimal DRV2605 driver: internal-trigger ERM mode, plays one ROM waveform per
// play() (a detent "click"). Ports the register sequence from
// components/bsp/haptics.c (the vanilla build). I2C writes run on the main loop
// (a few sub-ms transactions), so play() is cheap to call from an automation.
class DRV2605 : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_effect(uint8_t effect) { effect_ = effect; }
  void play();  // fire one click (no-op if the chip didn't ACK)

 protected:
  uint8_t effect_{24};  // DRV2605 ROM effect 24 = "Sharp Tick" (tuned on hardware)
  bool ready_{false};
};

}  // namespace drv2605
}  // namespace esphome
