idf_component_register(
    SRCS "albumart.c"
    INCLUDE_DIRS "include"
    REQUIRES ui esp_http_client esp-tls esp_new_jpeg
)
