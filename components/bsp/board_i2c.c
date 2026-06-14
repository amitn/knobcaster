#include "board_i2c.h"
#include "board_pins.h"

#include "esp_log.h"

static const char *TAG = "board_i2c";
static i2c_master_bus_handle_t s_bus;

i2c_master_bus_handle_t board_i2c_bus(void)
{
    if (s_bus) return s_bus;

    const i2c_master_bus_config_t cfg = {
        .i2c_port   = BOARD_TOUCH_I2C_PORT,
        .sda_io_num = BOARD_PIN_TOUCH_SDA,
        .scl_io_num = BOARD_PIN_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed");
        s_bus = NULL;
    }
    return s_bus;
}
