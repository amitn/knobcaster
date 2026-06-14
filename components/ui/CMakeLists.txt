idf_component_register(
    SRCS "ui.c" "fonts/font_dejavu_heb.c"
    INCLUDE_DIRS "include" "fonts"
    REQUIRES lvgl esp_lvgl_port
)
