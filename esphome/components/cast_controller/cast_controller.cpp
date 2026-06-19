#include "cast_controller.h"

#include "esphome/core/log.h"

// Shared Cast stack (components/cast/) — built into both targets.
extern "C" {
#include "cast_status.h"
}

namespace esphome {
namespace cast_controller {

static const char *const TAG = "cast_controller";

void CastController::setup() {
  // Smoke test: exercise a shared pure parser to prove components/cast/ is
  // compiled and linked into the ESPHome firmware (not just referenced).
  char type[32] = {0};
  bool ok = cast_parse_type("{\"type\":\"PING\"}", 15, type, sizeof(type));
  ESP_LOGCONFIG(TAG, "shared cast parser linked: ok=%d type=%s", ok, ok ? type : "?");
}

void CastController::dump_config() {
  ESP_LOGCONFIG(TAG, "Cast Controller (shared components/cast)");
}

// After Wi-Fi is up (Phase 3 will need the network for discovery/TLS).
float CastController::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

}  // namespace cast_controller
}  // namespace esphome
