// CST816 capacitive touch (I2C) -> LVGL pointer input device.
#include "display.h"
#include "board_pins.h"

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "touch";

bool touch_init(lv_display_t *disp)
{
    ESP_LOGI(TAG, "init CST816 touch (I2C addr 0x%02X)", BOARD_TOUCH_ADDR);

    // New I2C master bus (IDF v5.2+/6.0 driver).
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_TOUCH_I2C_PORT,
        .sda_io_num = BOARD_PIN_TOUCH_SDA,
        .scl_io_num = BOARD_PIN_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus = NULL;
    if (i2c_new_master_bus(&bus_cfg, &i2c_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed");
        return false;
    }

    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    if (esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io) != ESP_OK) {
        ESP_LOGE(TAG, "touch panel IO failed");
        return false;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_PIN_TOUCH_RST,
        .int_gpio_num = BOARD_PIN_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags  = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    esp_lcd_touch_handle_t tp = NULL;
    if (esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &tp) != ESP_OK) {
        ESP_LOGE(TAG, "cst816s init failed");
        return false;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };
    if (lvgl_port_add_touch(&touch_cfg) == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        return false;
    }
    ESP_LOGI(TAG, "touch ready");
    return true;
}
