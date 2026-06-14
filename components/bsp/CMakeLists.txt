idf_component_register(
    SRCS "display.c" "touch.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd lvgl esp_lvgl_port esp_lcd_sh8601
             esp_lcd_touch esp_lcd_touch_cst816s
)
