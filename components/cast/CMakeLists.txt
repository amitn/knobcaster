idf_component_register(
    SRCS
        "cast_discovery.c"
        "cast_proto.c"
        "cast_connection.c"
        "cast_session.c"
    INCLUDE_DIRS "include"
    REQUIRES mdns esp_netif esp-tls lwip cjson esp_timer
)
