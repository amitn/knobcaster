idf_component_register(
    SRCS "display.c" "touch.c" "knob.c" "fbdump.c" "board_i2c.c" "haptics.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c esp_driver_pcnt esp_driver_usb_serial_jtag esp_timer
             esp_lcd lvgl esp_lvgl_port esp_lcd_sh8601 esp_lcd_touch esp_lcd_touch_cst816s
)
