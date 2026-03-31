cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "captive_portal.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_http_server
        esp_netif
        log
)
