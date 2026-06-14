// Shared I2C master bus (I2C_NUM_0): the touch controller and the DRV2605 haptic
// driver sit on the same bus, and IDF only allows one master bus per port — so
// both go through this lazily-created shared handle.
#pragma once

#include "driver/i2c_master.h"

// Returns the shared I2C_NUM_0 master bus, creating it on first call. NULL on
// failure. Safe to call during single-threaded startup.
i2c_master_bus_handle_t board_i2c_bus(void);
