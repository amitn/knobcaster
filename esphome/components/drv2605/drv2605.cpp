#include "drv2605.h"

#include "esphome/core/log.h"

namespace esphome {
namespace drv2605 {

static const char *const TAG = "drv2605";

// DRV2605 registers (same as components/bsp/haptics.c).
static constexpr uint8_t REG_MODE = 0x01;
static constexpr uint8_t REG_LIBRARY = 0x03;
static constexpr uint8_t REG_WAVESEQ1 = 0x04;
static constexpr uint8_t REG_WAVESEQ2 = 0x05;
static constexpr uint8_t REG_GO = 0x0C;

static constexpr uint8_t MODE_INTTRIG = 0x00;   // internal trigger, out of standby
static constexpr uint8_t LIBRARY_ERM_A = 0x01;  // ERM waveform library A

void DRV2605::setup() {
  // Out of standby into internal-trigger mode + select the ERM library. If the
  // chip doesn't ACK (absent / wrong actuator wiring), disable quietly.
  if (this->write_byte(REG_MODE, MODE_INTTRIG) && this->write_byte(REG_LIBRARY, LIBRARY_ERM_A)) {
    ready_ = true;
  } else {
    ESP_LOGW(TAG, "DRV2605 not responding; haptics disabled");
  }
}

void DRV2605::play() {
  if (!ready_) return;
  this->write_byte(REG_WAVESEQ1, effect_);
  this->write_byte(REG_WAVESEQ2, 0x00);  // end of sequence
  this->write_byte(REG_GO, 0x01);        // play
}

void DRV2605::dump_config() {
  ESP_LOGCONFIG(TAG, "DRV2605 haptics (effect %u)", effect_);
  LOG_I2C_DEVICE(this);
}

}  // namespace drv2605
}  // namespace esphome
