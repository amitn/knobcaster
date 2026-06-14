# "main" component for the PlatformIO/ESP-IDF build.
idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "." "../include"
    REQUIRES wifi nvs_flash esp_event esp_netif esp_hw_support
)
