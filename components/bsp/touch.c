// CST816 capacitive touch (I2C) -> LVGL pointer input device.
#include "display.h"
#include "board_pins.h"
#include "board_i2c.h"

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

    // Shared I2C master bus (also used by the DRV2605 haptics).
    i2c_master_bus_handle_t i2c_bus = board_i2c_bus();
    if (!i2c_bus) {
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
